"""Kookong IRDB provider — offline database from the OnePlus/OPPO Consumer IR app.

Source data: ``kkoffline.db`` (SQLite, ~11k remotes / ~330k keys) shipped with the
Kookong (a.k.a. hzy/tvmao) SDK.  The database stores encrypted tokens and
encrypted pulse blobs, so decoding requires the *native host library*
``libkksdk_host.so`` built from the reverse-engineered Kookong sources
(``1-plus/kksdklib-codex-new``).  The OEM ``libkksdk.so`` from the APK cannot be
used here: it is an Android/bionic JNI library and does not load on Linux.

This provider emits IR2MQTT ``raw`` buttons, i.e.::

    {"protocol": "raw", "payload": {"timings": [+mark_us, -space_us, ...], "frequency": 36000}}

Configuration (env var, or ``/data/options.json`` key):

===========================================  ==========================================
``KKOOKONG_DB`` / ``kookong_db``             path to ``kkoffline.db``
``KKOOKONG_LIB`` / ``kookong_lib``           path to ``libkksdk_host.so``
``KKOOKONG_BRANDS`` / ``kookong_brands``     comma list of brand names, ``*`` = all
``KKOOKONG_MAX_REMOTES``                     global cap (default 1200)
``KKOOKONG_MAX_REMOTES_PER_BRAND``           per brand cap (default 20)
``KKOOKONG_MAX_KEYS_PER_REMOTE``             per remote cap (default 64)
``KKOOKONG_MIN_KEYS``                        skip remotes with fewer keys (default 4)
``KKOOKONG_INCLUDE_AC``                      include air-conditioner remotes (default 0)
``KKOOKONG_AC_REMOTES``                      AC remotes to expand (default 0)
``KKOOKONG_AC_MODES`` / ``KKOOKONG_AC_TEMPS``  AC grid (default ``0,1,2,3,4`` / ``16..30``)
``KKOOKONG_AC_INCLUDE_OFF``                  add a "Power Off" button per AC remote
===========================================  ==========================================
"""

from __future__ import annotations

import json
import logging
import os
import sqlite3
import time
from pathlib import Path
from typing import Any

from ..ir_base import IrRepoProvider, standardize_ir_key

logger = logging.getLogger(__name__)

DEFAULT_DB_PATHS = (
    "/data/kkoffline.db",
    "/share/kkoffline.db",
    "/usr/local/share/kookong/kkoffline.db",
)
DEFAULT_LIB_PATHS = (
    "/usr/lib/libkksdk_host.so",
    "/usr/local/lib/libkksdk_host.so",
    "/data/libkksdk_host.so",
)

# Best-effort labels for RcDeviceTypeBrandMap.device_type_id.  Unknown ids fall
# back to "type<N>".  These are display-only; encoding does not depend on them.
DEVICE_TYPE_LABELS = {
    1: "TV",
    2: "Set-top Box",
    3: "Projector",
    4: "Audio",
    5: "Air Conditioner",
    6: "Fan",
    7: "Light",
    8: "Camera",
    9: "DVD/Blu-ray",
    10: "Air Purifier",
    11: "Heater",
    12: "Water Heater",
    13: "Robot Vacuum",
    14: "Amplifier",
    15: "Sweeper",
    16: "Humidifier",
    17: "Kettle",
    18: "Curtain",
    20: "Soundbar",
    21: "Speaker",
}

# Default AC state grid.  mode 0..4 == auto/cool/heat/fan/dry in the Kookong app.
DEFAULT_AC_MODES = [0, 1, 2, 3, 4]
DEFAULT_AC_TEMPS = list(range(16, 31))


def _options() -> dict[str, Any]:
    """HA add-on options (``/data/options.json``) if present."""
    path = "/data/options.json"
    try:
        if os.path.exists(path):
            with open(path, encoding="utf-8") as fh:
                return json.load(fh) or {}
    except Exception:  # pragma: no cover - defensive
        logger.debug("could not read %s", path, exc_info=True)
    return {}


def _cfg(*names: str, default: Any = None) -> Any:
    """First non-empty env var, then ``/data/options.json`` key, else default."""
    for name in names:
        if name in os.environ and os.environ[name] != "":
            return os.environ[name]
    opts = _options()
    for name in names:
        key = name.lower()
        if key in opts and opts[key] not in (None, ""):
            return opts[key]
    return default


def _cfg_int(names: tuple[str, ...], default: int) -> int:
    raw = _cfg(*names)
    try:
        return int(raw)
    except (TypeError, ValueError):
        return default


def _cfg_bool(names: tuple[str, ...], default: bool) -> bool:
    raw = _cfg(*names)
    if raw is None:
        return default
    return str(raw).strip().lower() in ("1", "true", "yes", "on")


def _cfg_int_list(names: tuple[str, ...], default: list[int]) -> list[int]:
    raw = _cfg(*names)
    if raw is None:
        return list(default)
    if isinstance(raw, (list, tuple)):
        out: list[int] = []
        for item in raw:
            try:
                out.append(int(item))
            except (TypeError, ValueError):
                continue
        return out or list(default)
    out = []
    for part in str(raw).replace(";", ",").split(","):
        part = part.strip()
        if not part:
            continue
        if ".." in part:
            lo, _, hi = part.partition("..")
            try:
                out.extend(range(int(lo), int(hi) + 1))
                continue
            except ValueError:
                pass
        try:
            out.append(int(part))
        except ValueError:
            continue
    return out or list(default)


def _limit(value: int) -> int:
    """A cap of 0 (or negative) means "no limit"."""
    return value if value > 0 else 10**9


def _first_existing(candidates: tuple[str, ...], extra: str | None) -> Path | None:
    if extra:
        path = Path(extra)
        if path.exists():
            return path
    for candidate in candidates:
        path = Path(candidate)
        if path.exists():
            return path
    return None


def _signed_timings(pulse_us: list[int]) -> list[int]:
    """Kookong yields alternating mark/space durations, all positive.

    IR2MQTT/Flipper convention is signed: positive = mark, negative = space.
    """
    return [value if index % 2 == 0 else -value for index, value in enumerate(pulse_us)]


class KookongProvider(IrRepoProvider):
    """Decode ``kkoffline.db`` into IR2MQTT ``raw`` buttons via ``libkksdk_host.so``."""

    def __init__(self) -> None:
        super().__init__(
            id="kookong",
            name="Kookong IRDB (OnePlus/OPPO)",
            url="",
        )
        self._host = None
        self._db_path: Path | None = None
        self._lib_path: Path | None = None

        self._brands_raw = str(_cfg("KKOOKONG_BRANDS", "kookong_brands", default="") or "")
        self._max_remotes = _limit(_cfg_int(("KKOOKONG_MAX_REMOTES", "kookong_max_remotes"), 1200))
        self._max_per_brand = _limit(
            _cfg_int(("KKOOKONG_MAX_REMOTES_PER_BRAND", "kookong_max_remotes_per_brand"), 20)
        )
        self._max_keys = _limit(
            _cfg_int(("KKOOKONG_MAX_KEYS_PER_REMOTE", "kookong_max_keys_per_remote"), 64)
        )
        self._min_keys = _cfg_int(("KKOOKONG_MIN_KEYS", "kookong_min_keys"), 4)
        self._include_ac = _cfg_bool(("KKOOKONG_INCLUDE_AC", "kookong_include_ac"), False)
        self._ac_remotes = _limit(
            _cfg_int(("KKOOKONG_AC_REMOTES", "kookong_ac_remotes"), 0)
        )
        self._ac_modes = _cfg_int_list(("KKOOKONG_AC_MODES", "kookong_ac_modes"), DEFAULT_AC_MODES)
        self._ac_temps = _cfg_int_list(("KKOOKONG_AC_TEMPS", "kookong_ac_temps"), DEFAULT_AC_TEMPS)
        self._ac_include_off = _cfg_bool(("KKOOKONG_AC_INCLUDE_OFF", "kookong_ac_include_off"), True)

        self._skip_counts: dict[str, int] = {}

    # ------------------------------------------------------------------ helpers

    def _reset_skip_counts(self) -> None:
        self._skip_counts = {
            "missing_db": 0,
            "missing_lib": 0,
            "unknown_brand": 0,
            "too_few_keys": 0,
            "no_pulse": 0,
            "encode_failed": 0,
            "ac_skipped": 0,
            "duplicate_remote": 0,
            "limit_reached": 0,
        }

    def _brand_filter(self) -> set[str] | None:
        raw = self._brands_raw.strip()
        if not raw or raw == "*":
            return None
        return {part.strip().lower() for part in raw.split(",") if part.strip()}

    def _resolve_paths(self) -> None:
        if self._db_path is None:
            self._db_path = _first_existing(
                DEFAULT_DB_PATHS, _cfg("KKOOKONG_DB", "kookong_db")
            )
        if self._lib_path is None:
            self._lib_path = _first_existing(
                DEFAULT_LIB_PATHS, _cfg("KKOOKONG_LIB", "kookong_lib")
            )

    def _load_host(self):
        if self._host is None:
            from ..kookong.kksdk_db_common import KkHost

            self._resolve_paths()
            if self._lib_path is None:
                raise RuntimeError(
                    "libkksdk_host.so not found (set KKOOKONG_LIB or bundle it in the image)"
                )
            self._host = KkHost(str(self._lib_path))
            logger.info("[%s] loaded native host lib: %s", self.name, self._lib_path)
        return self._host

    # ------------------------------------------------------- provider contract

    async def download_and_convert(self, broadcast_func=None):
        """Local-only import: no network download, the DB ships with the add-on.

        Returns a *generator* of remote dicts, so a full-database import
        (10k+ remotes / 250k+ buttons) is never materialised in memory.
        """
        from ..kookong.kksdk_db_common import connect_db

        self._reset_skip_counts()
        self._resolve_paths()

        async def emit(status: str, message: str, percent: int) -> None:
            if broadcast_func:
                await broadcast_func(
                    {
                        "type": "irdb_progress",
                        "status": status,
                        "db": self.id,
                        "message": message,
                        "percent": percent,
                    }
                )

        if self._db_path is None:
            self._skip_counts["missing_db"] += 1
            message = (
                "kkoffline.db not found — set KKOOKONG_DB or bundle it at "
                "/data/kkoffline.db. Skipping Kookong import."
            )
            logger.warning("[%s] %s", self.name, message)
            self.last_convert_stats = {
                "total_rows": 0,
                "imported": 0,
                "skipped": 1,
                "skip_reasons": dict(self._skip_counts),
            }
            await emit("error", message, 0)
            return []

        # Fail fast (before handing back a generator) so a missing/incompatible
        # native library cannot abort the whole multi-provider sync mid-way.
        try:
            host = self._load_host()
            con = connect_db(Path(self._db_path))
        except Exception as exc:
            logger.error("[%s] import failed: %s", self.name, exc, exc_info=True)
            self._skip_counts["encode_failed"] += 1
            self._finalize_stats()
            await emit("error", f"Error: {exc}", 0)
            return []

        await emit("downloading", "Loading Kookong offline database...", 0)
        return self._iter_remotes(con, host)

    def convert(self, raw_root: Path) -> list[dict]:
        """Materialise the import as a list (convenience/testing helper)."""
        from ..kookong.kksdk_db_common import connect_db

        self._reset_skip_counts()
        self._resolve_paths()
        host = self._load_host()
        db_path = self._db_path or (Path(raw_root) / "kkoffline.db")
        con = connect_db(Path(db_path))
        return list(self._iter_remotes(con, host))

    def _finalize_stats(self) -> None:
        self.last_convert_stats = {
            "total_rows": getattr(self, "_total_keys_seen", 0),
            "imported": getattr(self, "_buttons_yielded", 0),
            "skipped": sum(self._skip_counts.values()),
            "skip_reasons": dict(self._skip_counts),
        }

    def _iter_remotes(self, con: sqlite3.Connection, host):
        """Yield remote dicts one at a time (streaming decode)."""
        started = time.time()
        if not self._skip_counts:
            self._reset_skip_counts()
        self._total_keys_seen = 0
        self._buttons_yielded = 0
        self._remotes_yielded = 0
        try:
            brand_filter = self._brand_filter()
            brands = self._load_brands(con, host, brand_filter)
            if brand_filter:
                missing = brand_filter - {name.lower() for name in brands.values()}
                for name in missing:
                    logger.warning("[%s] brand not found in DB: %s", self.name, name)
                    self._skip_counts["unknown_brand"] += 1

            per_brand: dict[tuple[int, str], int] = {}
            ac_budget = self._ac_remotes
            # A remote is commonly mapped to several brands/device types
            # (white-label), so the same remote_id shows up under multiple
            # folders.  Import each remote once — otherwise the database
            # balloons with identical codes.
            seen_remotes: set[str] = set()

            # Single ordered pass: device type ascending, then brand popularity
            # rank, then remote id.  This is what makes the picker read
            # "type -> brand -> remote".
            for row in self._ordered_remote_rows(con):
                if self._remotes_yielded >= self._max_remotes:
                    self._skip_counts["limit_reached"] += 1
                    break

                brand_id = row["brand_id"]
                brand_name = brands.get(brand_id)
                if brand_name is None:
                    # filtered out, or the brand name could not be decoded
                    continue

                device_type_id = int(row["device_type_id"] or 0)
                key = (device_type_id, brand_id)
                if per_brand.get(key, 0) >= self._max_per_brand:
                    self._skip_counts["limit_reached"] += 1
                    continue

                remote_enc = row["remote_id"]
                if remote_enc in seen_remotes:
                    self._skip_counts["duplicate_remote"] += 1
                    continue
                seen_remotes.add(remote_enc)

                is_ac = int(row["type"] or 0) == 2
                if is_ac:
                    if not self._include_ac or ac_budget <= 0:
                        self._skip_counts["ac_skipped"] += 1
                        continue
                    ac_budget -= 1

                type_label = DEVICE_TYPE_LABELS.get(device_type_id, f"type{device_type_id}")
                remote = self._build_remote(con, host, row, brand_name, type_label, is_ac)
                if remote is None:
                    continue

                per_brand[key] = per_brand.get(key, 0) + 1
                self._remotes_yielded += 1
                self._buttons_yielded += len(remote["buttons"])
                yield remote
        finally:
            try:
                con.close()
            except Exception:
                pass
            self._finalize_stats()
            logger.info(
                "[%s] import done: %d remotes / %d buttons in %.1fs (skipped=%d)",
                self.name,
                getattr(self, "_remotes_yielded", 0),
                getattr(self, "_buttons_yielded", 0),
                time.time() - started,
                sum(self._skip_counts.values()),
            )

    # ------------------------------------------------------------------ loading

    def _load_brands(
        self, con: sqlite3.Connection, host, brand_filter: set[str] | None
    ) -> dict[str, str]:
        """brand_id -> decoded brand name (English), optionally filtered."""
        out: dict[str, str] = {}
        seen: set[str] = set()
        for row in con.execute(
            "SELECT brand_id, name FROM RcCountryBrand WHERE lang_code = 'en'"
        ):
            brand_id = row["brand_id"]
            if brand_id in seen:
                continue
            seen.add(brand_id)
            try:
                name = host.decrypt_token(row["name"])
            except Exception:
                continue
            if not name:
                continue
            if brand_filter and name.lower() not in brand_filter:
                continue
            out[brand_id] = name
        return out

    def _ordered_remote_rows(self, con: sqlite3.Connection) -> list[sqlite3.Row]:
        """Every (device type, brand, remote) mapping in browse order.

        One query instead of one per (device type, brand) pair — with ~21 device
        types and thousands of brands the per-pair variant issues ~15k queries
        and dominates import time.
        """
        return con.execute(
            """
            SELECT m.device_type_id AS device_type_id,
                   m.brand_id       AS brand_id,
                   r.remote_id      AS remote_id,
                   r.frequency      AS frequency,
                   r.type           AS type,
                   r.param          AS param,
                   MIN(m.rank)      AS rank
            FROM RcBrandRemoteMap m
            JOIN RcRemoteController r ON r.remote_id = m.remote_id
            GROUP BY m.device_type_id, m.brand_id, r.remote_id
            ORDER BY m.device_type_id, rank, m.brand_id, r.remote_id
            """
        ).fetchall()

    # ------------------------------------------------------------------ building

    def _build_remote(
        self,
        con: sqlite3.Connection,
        host,
        row: sqlite3.Row,
        brand_name: str,
        type_label: str,
        is_ac: bool,
    ) -> dict | None:
        remote_enc = row["remote_id"]
        try:
            remote_dec = host.decrypt_token(remote_enc)
        except Exception:
            remote_dec = remote_enc

        frequency = int(row["frequency"] or 38000)

        if is_ac:  # noqa: SIM108 - keep the two branches explicit
            buttons = self._build_ac_buttons(con, host, remote_enc, remote_dec, row, frequency)
        else:
            buttons = self._build_non_ac_buttons(con, host, remote_enc, frequency, row)

        if len(buttons) < self._min_keys:
            self._skip_counts["too_few_keys"] += 1
            return None

        remote_name = f"{type_label} {brand_name} {remote_dec}".strip()
        # Device-type-first layout so the picker starts with AC / Fan / TV / ...
        # and only then asks for the brand.
        path = (
            f"{self.id}/{_slug(type_label)}/{_slug(brand_name)}"
            f"/{_slug(brand_name)}_{remote_dec}"
        )
        return {
            "path": path,
            "name": remote_name,
            "provider": self.id,
            "source_file": "kkoffline.db",
            "buttons": buttons,
        }

    def _build_non_ac_buttons(
        self, con: sqlite3.Connection, host, remote_enc: str, frequency: int, row: sqlite3.Row
    ) -> list[dict]:
        from ..kookong.kksdk_db_common import ticks_to_us

        remote_param = bytes(row["param"] or b"")
        keys = con.execute(
            """
            SELECT remote_key_id, function_id, pulse_data
            FROM RcRemoteKey
            WHERE remote_id = ? AND pulse_data IS NOT NULL
            ORDER BY function_id, remote_key_id
            LIMIT ?
            """,
            (remote_enc, self._max_keys),
        ).fetchall()

        names = _function_names(con)
        buttons: list[dict] = []
        used_names: set[str] = set()

        for key in keys:
            self._total_keys_seen += 1
            fid = int(key["function_id"] or 0)
            raw_name = names.get(fid, f"f{fid}")

            try:
                payload = host.decrypt_bytes(bytes(key["pulse_data"]))
            except Exception:
                self._skip_counts["no_pulse"] += 1
                continue

            timings = _payload_to_timings(payload, remote_param, host, frequency, ticks_to_us)
            if not timings:
                self._skip_counts["encode_failed"] += 1
                continue

            std = standardize_ir_key(raw_name)
            name = std["name"]
            if name in used_names:
                name = f"{name} ({fid})"
            used_names.add(name)

            payload_dict: dict[str, Any] = {"timings": timings}
            if frequency and frequency != 38000:
                payload_dict["frequency"] = frequency

            buttons.append(
                {"name": name, "icon": std["icon"], "code": {"protocol": "raw", "payload": payload_dict}}
            )

        return buttons

    def _ac_state_button(
        self,
        con: sqlite3.Connection,
        host,
        remote_enc: str,
        remote_dec: str,
        row: sqlite3.Row,
        frequency: int,
        power: int,
        mode: int,
        temperature: int,
        wind_speed: int = 0,
        lr_wind_mode: int = 0,
        ud_wind_mode: int = 0,
        function_id: int = 1,
        label: str | None = None,
    ) -> dict | None:
        """Encode ONE air-conditioner state into a raw button (or None).

        Used by the on-demand match flow so a test transmission is byte-for-byte
        what the bulk importer would have stored for that state.  ``function_id``
        selects which key's wave configuration drives the frame (1 power,
        2 mode, 3 temp up, 4 temp down, 5 wind, 6/7 swing) — exactly how the
        phone app sends individual AC keys.
        """
        from ..kookong.kksdk_db_common import (
            render_ac_duration_pulses,
            select_ac_wave_format_id,
            ticks_to_us,
        )

        try:
            remote_id_int = int(remote_dec)
        except (TypeError, ValueError):
            return None

        lines = _controller_lines(con, host, remote_enc)
        if not lines:
            return None

        remote_param = bytes(row["param"] or b"")
        explicit_label = label is not None
        if not explicit_label:
            label = (
                "Power Off"
                if power == 0
                else f"On {_MODE_LABELS.get(mode, mode)} {temperature}C"
            )
        try:
            selected = select_ac_wave_format_id(
                lines, function_id, power, mode, temperature, wind_speed, ud_wind_mode, None
            )
            selected_key_lines = _remote_key_lines(con, host, remote_enc, selected)
            frames = host.encode_ac(
                remote_id_int,
                lines,
                power=power,
                mode=mode,
                temperature=temperature,
                wind_speed=wind_speed,
                lr_wind_mode=lr_wind_mode,
                ud_wind_mode=ud_wind_mode,
                function_id=function_id,
                ext_string=None,
            )
            waves, _source = render_ac_duration_pulses(
                lines,
                frames,
                function_id,
                power,
                mode,
                temperature,
                wind_speed,
                ud_wind_mode,
                None,
                selected_key_lines=selected_key_lines,
            )
            timings: list[int] = []
            for index, frame in enumerate(frames):
                wave = waves[index] if index < len(waves) else []
                if wave:
                    timings.extend(_signed_timings([int(v) for v in wave]))
                else:
                    pulse_ticks = host.encode_pulse(remote_param, frame)
                    timings.extend(_signed_timings(ticks_to_us(pulse_ticks, frequency)))
            if not timings:
                return None
        except Exception:
            return None

        payload_dict: dict[str, Any] = {"timings": timings}
        if frequency and frequency != 38000:
            payload_dict["frequency"] = frequency
        if explicit_label:
            # Match-flow labels carry the full state ("Temp Up · Cool 26C · Fan Auto")
            # and must not be run through the button-name standardiser, which
            # truncates anything longer than 20 characters.
            name, icon = label, "air-conditioner"
        else:
            std = standardize_ir_key(label)
            name, icon = std["name"], std["icon"]
        return {
            "name": name,
            "icon": icon,
            "code": {"protocol": "raw", "payload": payload_dict},
        }

    def _build_ac_buttons(
        self,
        con: sqlite3.Connection,
        host,
        remote_enc: str,
        remote_dec: str,
        row: sqlite3.Row,
        frequency: int,
    ) -> list[dict]:
        try:
            int(remote_dec)
        except (TypeError, ValueError):
            self._skip_counts["encode_failed"] += 1
            return []

        if not _controller_lines(con, host, remote_enc):
            self._skip_counts["encode_failed"] += 1
            return []

        buttons: list[dict] = []
        used_names: set[str] = set()

        states: list[tuple[int, int, int]] = []
        if self._ac_include_off:
            states.append((0, 0, 24))
        for mode in self._ac_modes:
            for temp in self._ac_temps:
                states.append((1, mode, temp))

        for power, mode, temperature in states:
            self._total_keys_seen += 1
            button = self._ac_state_button(
                con, host, remote_enc, remote_dec, row, frequency, power, mode, temperature
            )
            if button is None:
                self._skip_counts["encode_failed"] += 1
                continue
            if button["name"] in used_names:
                continue
            used_names.add(button["name"])
            buttons.append(button)

        return buttons


# --------------------------------------------------------------------- helpers

_MODE_LABELS = {0: "Auto", 1: "Cool", 2: "Heat", 3: "Fan", 4: "Dry"}


def _slug(value: str) -> str:
    keep = [ch if ch.isalnum() or ch in "-_" else "_" for ch in value.strip()]
    slug = "".join(keep).strip("_")
    while "__" in slug:
        slug = slug.replace("__", "_")
    return slug or "unknown"


def _function_names(con: sqlite3.Connection) -> dict[int, str]:
    try:
        return {
            int(row["function_id"]): row["function_name"]
            for row in con.execute("SELECT function_id, function_name FROM RcFunctions")
        }
    except Exception:
        return {}


def _controller_lines(con: sqlite3.Connection, host, remote_enc: str) -> list[str]:
    rows = con.execute(
        "SELECT tag, value FROM RcRemoteControllerExt WHERE remote_id = ? ORDER BY tag",
        (remote_enc,),
    ).fetchall()
    lines = []
    for row in rows:
        try:
            lines.append(f"{int(row['tag'])}|{host.decrypt_token(row['value'])}")
        except Exception:
            continue
    return lines


def _remote_key_lines(con: sqlite3.Connection, host, remote_enc: str, function_id: int) -> list[str]:
    key = con.execute(
        "SELECT remote_key_id FROM RcRemoteKey WHERE remote_id = ? AND function_id = ? "
        "ORDER BY remote_key_id LIMIT 1",
        (remote_enc, function_id),
    ).fetchone()
    if not key:
        return []
    rows = con.execute(
        "SELECT tag, value FROM RcRemoteKeyExt WHERE remote_key_id = ? ORDER BY tag",
        (key["remote_key_id"],),
    ).fetchall()
    out = []
    for row in rows:
        try:
            out.append(f"{int(row['tag'])}|{host.decrypt_token(row['value'])}")
        except Exception:
            continue
    return out


def _payload_to_timings(
    payload: bytes, remote_param: bytes, host, frequency: int, ticks_to_us
) -> list[int]:
    """Decoded key payload -> signed microsecond timings (IR2MQTT raw format).

    Some keys carry a ready-made duration CSV (``9000,4500,...``); the rest are
    encoded through the native pulse encoder.
    """
    if not payload:
        return []

    text = payload.decode("ascii", "ignore").strip()
    segment = text.split("&", 1)[0].strip()
    if segment and "," in segment:
        try:
            values = [int(part.strip()) for part in segment.split(",") if part.strip()]
        except ValueError:
            values = []
        if values:
            return _signed_timings(values)

    command = payload
    if (
        segment
        and len(segment) % 2 == 0
        and all(ch in "0123456789abcdefABCDEF" for ch in segment)
    ):
        try:
            command = bytes.fromhex(segment)
        except ValueError:
            command = payload

    try:
        pulse_ticks = host.encode_pulse(remote_param, command)
    except Exception:
        return []
    if not pulse_ticks:
        return []
    return _signed_timings(ticks_to_us(pulse_ticks, frequency))
