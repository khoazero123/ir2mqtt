"""Kookong-style "match my remote" service.

Mirrors the OnePlus/OPPO Consumer IR phone app flow: pick a device category,
pick a brand, then step through candidate remotes while sending a test
transmission until the user confirms the device reacted.

Two things keep this faithful to the original app:

* **Candidate order** comes straight from the source database
  (``RcBrandRemoteMap.rank``), i.e. the same popularity ranking the app uses.
* **Test codes** are produced by the same native kksdk encoder the app calls
  (``KkHost.encode_ac`` for air conditioners, ``KkHost.encode_pulse`` for the
  rest) — not from whatever happened to be imported into the IR database.
  *Exception:* Daikin AC remotes.  Those units ignore the Kookong Daikin
  variant and only react to the ESPHome ``platform: daikin`` frame, so their
  codes come from :mod:`backend.kookong.ac_native` instead (see that module).

Everything is read on demand from ``kkoffline.db``, so matching works for the
whole database regardless of which remotes were imported.
"""

from __future__ import annotations

import logging
import os
import sqlite3
from typing import Any

from ..ir_base import standardize_ir_key
from ..providers.kookong import (
    DEVICE_TYPE_LABELS,
    KookongProvider,
    _payload_to_timings,
    _signed_timings,
)
from .ac_native import daikin_code

logger = logging.getLogger(__name__)

# Home Assistant country (ISO code, e.g. "VN") fetched once per process through
# the Supervisor proxy.  ``_ha_country_loaded`` is only set after a successful
# read so a transient failure is retried on the next request.
_ha_country: str | None = None
_ha_country_loaded = False

def ha_country() -> str | None:
    """Country configured in Home Assistant core, or None if unavailable."""
    global _ha_country, _ha_country_loaded
    if _ha_country_loaded:
        return _ha_country
    token = os.environ.get("SUPERVISOR_TOKEN")
    if not token:
        return None
    try:
        import httpx

        with httpx.Client(timeout=5.0) as client:
            resp = client.get(
                "http://supervisor/core/api/config",
                headers={"Authorization": f"Bearer {token}"},
            )
        if resp.status_code == 200:
            value = (resp.json() or {}).get("country")
            if isinstance(value, str) and value.strip():
                _ha_country = value.strip().upper()
                _ha_country_loaded = True
    except Exception as exc:  # pragma: no cover - depends on HA Supervisor
        logger.info("Could not read HA country from the supervisor: %s", exc)
    return _ha_country

# Default test state for air conditioners.  The app's match wizard sends a
# "turn on" frame; cool/25 °C is the least surprising choice for a test.
DEFAULT_AC_POWER = 1
DEFAULT_AC_MODE = 1  # 1 = cool
DEFAULT_AC_TEMPERATURE = 25

# function_id values used by the Kookong AC key model
AC_POWER_FUNCTION = 1
AC_MODE_FUNCTION = 2
AC_TEMP_UP_FUNCTION = 3
AC_TEMP_DOWN_FUNCTION = 4
AC_WIND_FUNCTION = 5
AC_SWING_ON_FUNCTION = 6
AC_SWING_OFF_FUNCTION = 7

_MODE_LABELS = {0: "Auto", 1: "Cool", 2: "Heat", 3: "Fan", 4: "Dry"}
_WIND_LABELS = {0: "Auto", 1: "Low", 2: "Medium", 3: "High"}

# Kookong AC key model -> native Daikin encoder arguments.  Mode ids are the
# app's 0..4 (auto/cool/heat/fan/dry) and wind speeds 0..3 (auto/low/medium/
# high); the Daikin encoder expects ESPHome's names (see ``ac_native``).
_KK_MODE_TO_DAIKIN = {0: "auto", 1: "cool", 2: "heat", 3: "fan", 4: "dry"}
_KK_WIND_TO_DAIKIN = {0: "auto", 1: "low", 2: "medium", 3: "high"}

# Brands whose remotes need the native Daikin frame instead of the Kookong
# bytes.  Some Daikin units ignore the Kookong variant (mode byte 0x40/0x48,
# fan 0xaf, different fixed frame) but accept the ESPHome ``platform: daikin``
# frame — verified on real hardware.
_DAIKIN_BRAND = "daikin"

def is_daikin_name(name: str | None) -> bool:
    """True for a decrypted brand name of a Daikin remote (case-insensitive)."""
    if not name:
        return False
    key = " ".join(str(name).split()).lower()
    return key == _DAIKIN_BRAND or key.startswith(f"{_DAIKIN_BRAND} ")

def _daikin_swing(lr_wind_mode: int, ud_wind_mode: int) -> str:
    """Kookong swing flags -> encoder swing (ud = vertical, lr = horizontal)."""
    ud = bool(ud_wind_mode)
    lr = bool(lr_wind_mode)
    if ud and lr:
        return "both"
    if ud:
        return "vertical"
    if lr:
        return "horizontal"
    return "off"

def _daikin_code_from_state(state: dict) -> dict:
    """Native Daikin code for a Kookong AC state dict (match wizard model)."""
    return daikin_code(
        power=bool(int(state.get("power", 1))),
        mode=_KK_MODE_TO_DAIKIN.get(int(state.get("mode", 0)), "auto"),
        temp_c=int(state.get("temperature", 25)),
        fan=_KK_WIND_TO_DAIKIN.get(int(state.get("wind_speed", 0)), "auto"),
        swing=_daikin_swing(int(state.get("lr_wind_mode", 0)), int(state.get("ud_wind_mode", 0))),
    )


def _ac_label(state: dict, function_id: int) -> str:
    """Human label for a test transmission (shown in the UI / device name)."""
    mode = _MODE_LABELS.get(state["mode"], state["mode"])
    wind = _WIND_LABELS.get(state["wind_speed"], state["wind_speed"])
    if function_id == AC_POWER_FUNCTION:
        # Nói rõ frame này BẬT hay TẮT máy, kèm trạng thái đi kèm
        return f"Power {'On' if state['power'] else 'Off'} · {mode} {state['temperature']}C · Fan {wind}"
    key = {
        AC_TEMP_UP_FUNCTION: "Temp Up",
        AC_TEMP_DOWN_FUNCTION: "Temp Down",
        AC_MODE_FUNCTION: "Mode",
        AC_WIND_FUNCTION: "Fan Speed",
        AC_SWING_ON_FUNCTION: "Swing On",
        AC_SWING_OFF_FUNCTION: "Swing Off",
    }.get(function_id, "Test")
    return f"{key} · {mode} {state['temperature']}C · Fan {wind}"


class KookongMatchService:
    """Read-only view over ``kkoffline.db`` for the match wizard."""

    def __init__(self) -> None:
        self._provider = KookongProvider()
        self._con: sqlite3.Connection | None = None
        self._host = None

    # ------------------------------------------------------------------ plumbing

    def _ensure(self) -> tuple[sqlite3.Connection, Any]:
        if self._con is None or self._host is None:
            from .kksdk_db_common import connect_db

            provider = self._provider
            provider._resolve_paths()
            if provider._db_path is None:
                raise RuntimeError(
                    "kkoffline.db not found — the Kookong match wizard needs it bundled"
                )
            self._host = provider._load_host()
            self._con = connect_db(provider._db_path)
            provider._reset_skip_counts()
        return self._con, self._host

    def available(self) -> bool:
        try:
            self._ensure()
            return True
        except Exception as exc:  # pragma: no cover - depends on bundled files
            logger.warning("Kookong match service unavailable: %s", exc)
            return False

    def _brand_map(self, con: sqlite3.Connection, host) -> dict[str, str]:
        out: dict[str, str] = {}
        for row in con.execute(
            "SELECT brand_id, name FROM RcCountryBrand WHERE lang_code = 'en'"
        ):
            brand_id = row["brand_id"]
            if brand_id in out:
                continue
            try:
                name = host.decrypt_token(row["name"])
            except Exception:
                continue
            if name:
                out[brand_id] = name
        return out

    def _brand_name(self, con: sqlite3.Connection, host, brand_id: str) -> str | None:
        """Decrypted English brand name for one brand id (None when unknown)."""
        row = con.execute(
            "SELECT name FROM RcCountryBrand WHERE brand_id = ? AND lang_code = 'en' LIMIT 1",
            (brand_id,),
        ).fetchone()
        if row is None:
            row = con.execute(
                "SELECT name FROM RcCountryBrand WHERE brand_id = ? LIMIT 1",
                (brand_id,),
            ).fetchone()
        if row is None:
            return None
        try:
            return host.decrypt_token(row["name"])
        except Exception:
            return None

    def is_daikin_brand(self, category_id: int, brand_id: str) -> bool:
        """True when this brand needs the native (ESPHome-style) Daikin frame."""
        con, host = self._ensure()
        return is_daikin_name(self._brand_name(con, host, brand_id))

    # ------------------------------------------------------------------ browsing

    def categories(self) -> list[dict]:
        """Device categories that have remotes, with counts."""
        con, _ = self._ensure()
        rows = con.execute(
            """
            SELECT device_type_id AS dt, COUNT(DISTINCT remote_id) AS remotes
            FROM RcBrandRemoteMap
            WHERE device_type_id IS NOT NULL
            GROUP BY device_type_id
            ORDER BY device_type_id
            """
        ).fetchall()
        out = []
        for row in rows:
            dt = int(row["dt"])
            label = DEVICE_TYPE_LABELS.get(dt, f"type{dt}")
            out.append({"id": dt, "name": label, "count": int(row["remotes"])})
        return out

    def brands(self, category_id: int) -> list[dict]:
        """Brands inside a category, most popular first."""
        con, host = self._ensure()
        names = self._brand_map(con, host)
        rows = con.execute(
            """
            SELECT brand_id, MIN(rank) AS best_rank, COUNT(DISTINCT remote_id) AS remotes
            FROM RcBrandRemoteMap
            WHERE device_type_id = ?
            GROUP BY brand_id
            ORDER BY best_rank, brand_id
            """,
            (category_id,),
        ).fetchall()
        out = []
        for row in rows:
            name = names.get(row["brand_id"])
            if not name:
                continue
            out.append(
                {
                    "id": row["brand_id"],
                    "name": name,
                    "count": int(row["remotes"]),
                }
            )
        return out

    def _available_countries(
        self, con: sqlite3.Connection, category_id: int, brand_id: str
    ) -> list[str]:
        """Country codes that actually have rows for this (device_type, brand),
        ordered by their best (lowest) rank — i.e. the same popularity order the
        phone app uses within a region."""
        rows = con.execute(
            """
            SELECT country, MIN(rank) AS best
            FROM RcBrandRemoteMap
            WHERE device_type_id = ? AND brand_id = ?
              AND country IS NOT NULL AND country <> ''
            GROUP BY country
            ORDER BY best, country
            """,
            (category_id, brand_id),
        ).fetchall()
        return [row["country"] for row in rows]

    def countries(self, category_id: int, brand_id: str) -> dict:
        """Countries with data for this brand + the one to preselect.

        ``recommended`` is the Home Assistant country when it has data here,
        otherwise the global default (``CN``), otherwise the first one.
        """
        con, _ = self._ensure()
        available = self._available_countries(con, category_id, brand_id)
        ha = ha_country()
        if ha and ha in available:
            recommended = ha
        elif "CN" in available:
            recommended = "CN"
        else:
            recommended = available[0] if available else None
        return {"countries": available, "recommended": recommended}

    def candidates(
        self,
        category_id: int,
        brand_id: str,
        country: str | None = None,
        limit: int = 300,
    ) -> list[dict]:
        """Candidate remotes for a (category, brand), best rank first.

        Ranking is per country, exactly like the phone app: when ``country``
        has data we filter to it, otherwise we fall back to ``CN`` and finally
        to the whole (country-merged) set for backwards compatibility.
        """
        con, host = self._ensure()
        available = self._available_countries(con, category_id, brand_id)
        used: str | None = None
        if country:
            wanted = country.strip().upper()
            if wanted in available:
                used = wanted
        if used is None and "CN" in available:
            used = "CN"

        if used is not None:
            rows = con.execute(
                """
                SELECT m.remote_id, m.rank, r.frequency, r.type
                FROM RcBrandRemoteMap m
                JOIN RcRemoteController r ON r.remote_id = m.remote_id
                WHERE m.device_type_id = ? AND m.brand_id = ? AND m.country = ?
                GROUP BY m.remote_id
                ORDER BY MIN(m.rank), m.remote_id
                LIMIT ?
                """,
                (category_id, brand_id, used, limit),
            ).fetchall()
        else:
            rows = con.execute(
                """
                SELECT m.remote_id, m.rank, r.frequency, r.type
                FROM RcBrandRemoteMap m
                JOIN RcRemoteController r ON r.remote_id = m.remote_id
                WHERE m.device_type_id = ? AND m.brand_id = ?
                GROUP BY m.remote_id
                ORDER BY MIN(m.rank), m.remote_id
                LIMIT ?
                """,
                (category_id, brand_id, limit),
            ).fetchall()

        out = []
        for row in rows:
            remote_enc = row["remote_id"]
            try:
                remote_dec = host.decrypt_token(remote_enc)
            except Exception:
                remote_dec = remote_enc
            is_ac = int(row["type"] or 0) == 2
            out.append(
                {
                    "remote_id": remote_enc,
                    "remote_key": remote_dec,
                    "frequency": int(row["frequency"] or 38000),
                    "is_ac": is_ac,
                    "rank": int(row["rank"] or 0),
                    "country": used,
                    "label": f"{'AC' if is_ac else 'Remote'} {remote_dec}",
                }
            )
        return out

    # ------------------------------------------------------------------ encoding

    def _remote_row(self, con: sqlite3.Connection, category_id: int, brand_id: str, remote_enc: str):
        return con.execute(
            """
            SELECT r.remote_id, r.frequency, r.type, r.param
            FROM RcBrandRemoteMap m
            JOIN RcRemoteController r ON r.remote_id = m.remote_id
            WHERE m.device_type_id = ? AND m.brand_id = ? AND m.remote_id = ?
            LIMIT 1
            """,
            (category_id, brand_id, remote_enc),
        ).fetchone()

    def test_code(
        self,
        category_id: int,
        brand_id: str,
        remote_enc: str,
        power: int | None = None,
        mode: int | None = None,
        temperature: int | None = None,
        wind_speed: int | None = None,
        lr_wind_mode: int | None = None,
        ud_wind_mode: int | None = None,
        function_id: int | None = None,
    ) -> dict | None:
        """Build the test transmission for one candidate remote.

        Air conditioners are stateful, so the caller passes the whole state plus
        the key being pressed (``function_id``: 1 power, 2 mode, 3 temp up,
        4 temp down, 5 wind, 6/7 swing) — the same model the phone app uses for
        its virtual remote during matching.
        """
        con, host = self._ensure()
        row = self._remote_row(con, category_id, brand_id, remote_enc)
        if row is None:
            return None

        frequency = int(row["frequency"] or 38000)
        try:
            remote_dec = host.decrypt_token(remote_enc)
        except Exception:
            remote_dec = remote_enc

        if int(row["type"] or 0) == 2:
            state = {
                "power": DEFAULT_AC_POWER if power is None else int(power),
                "mode": DEFAULT_AC_MODE if mode is None else int(mode),
                "temperature": (
                    DEFAULT_AC_TEMPERATURE if temperature is None else int(temperature)
                ),
                "wind_speed": 0 if wind_speed is None else int(wind_speed),
                "lr_wind_mode": 0 if lr_wind_mode is None else int(lr_wind_mode),
                "ud_wind_mode": 0 if ud_wind_mode is None else int(ud_wind_mode),
            }
            fid = AC_POWER_FUNCTION if function_id is None else int(function_id)
            label = _ac_label(state, fid)
            if self.is_daikin_brand(category_id, brand_id):
                # Daikin units ignore the Kookong Daikin variant (mode byte
                # 0x40/0x48, fan 0xaf, different fixed frame) but accept the
                # ESPHome `platform: daikin` frame — verified on real hardware.
                try:
                    code = _daikin_code_from_state(state)
                except Exception:
                    logger.warning(
                        "Native Daikin encode failed for remote %s", remote_enc, exc_info=True
                    )
                    return None
                return {
                    "button_name": label,
                    "code": code,
                    "is_ac": True,
                    "state": state,
                    "function_id": fid,
                }
            # Kookong DB lưu token trạng thái power NGƯỢC: token 0 = bật, 1 = tắt.
            # Bằng chứng: frame thật của remote ARC480A33 (bắt qua IR receiver S11) cho
            # cool + power ON -> byte5 = 0x31 (nibble thấp = 1). Nếu truyền thẳng power=1
            # thì encoder trả byte5 = 0x30 (= tắt) -> máy nháy đèn rồi ở trạng thái tắt.
            enc_power = 1 - int(state["power"])
            button = self._provider._ac_state_button(
                con,
                host,
                remote_enc,
                remote_dec,
                row,
                frequency,
                enc_power,
                state["mode"],
                state["temperature"],
                wind_speed=state["wind_speed"],
                lr_wind_mode=state["lr_wind_mode"],
                ud_wind_mode=state["ud_wind_mode"],
                function_id=fid,
                label=label,
            )
            if button is None:
                return None
            return {
                "button_name": button["name"],
                "code": button["code"],
                "is_ac": True,
                "state": state,
                "function_id": fid,
            }

        # Non-AC: use function_id 1 ("power"), else the first available key.
        key = con.execute(
            "SELECT pulse_data FROM RcRemoteKey WHERE remote_id = ? AND function_id = 1 "
            "AND pulse_data IS NOT NULL LIMIT 1",
            (remote_enc,),
        ).fetchone()
        if key is None:
            key = con.execute(
                "SELECT pulse_data FROM RcRemoteKey WHERE remote_id = ? "
                "AND pulse_data IS NOT NULL LIMIT 1",
                (remote_enc,),
            ).fetchone()
        if key is None:
            return None

        remote_param = bytes(row["param"] or b"")
        try:
            payload = host.decrypt_bytes(bytes(key["pulse_data"]))
        except Exception:
            return None

        from .kksdk_db_common import ticks_to_us

        timings = _payload_to_timings(payload, remote_param, host, frequency, ticks_to_us)
        if not timings:
            return None

        payload_dict: dict[str, Any] = {"timings": timings}
        if frequency and frequency != 38000:
            payload_dict["frequency"] = frequency
        return {
            "button_name": "Power",
            "code": {"protocol": "raw", "payload": payload_dict},
            "is_ac": False,
        }

    def is_ac_remote(self, category_id: int, brand_id: str, remote_enc: str) -> bool:
        """True when the remote is an air conditioner (type 2 in the source DB)."""
        con, _ = self._ensure()
        row = self._remote_row(con, category_id, brand_id, remote_enc)
        return bool(row is not None and int(row["type"] or 0) == 2)

    def buttons_for(self, category_id: int, brand_id: str, remote_enc: str) -> list[dict]:
        """Full button set for a matched remote (used when saving it)."""
        con, host = self._ensure()
        row = self._remote_row(con, category_id, brand_id, remote_enc)
        if row is None:
            return []

        frequency = int(row["frequency"] or 38000)
        try:
            remote_dec = host.decrypt_token(remote_enc)
        except Exception:
            remote_dec = remote_enc

        provider = self._provider
        provider._reset_skip_counts()
        # The importer's caps do not apply when saving a single matched remote:
        # keep every key the remote has.
        provider._total_keys_seen = 0
        provider._buttons_yielded = 0
        provider._remotes_yielded = 0
        saved_max_keys = provider._max_keys
        provider._max_keys = 10**9
        try:
            if int(row["type"] or 0) == 2:
                if self.is_daikin_brand(category_id, brand_id):
                    return self._daikin_ac_buttons(provider)
                return provider._build_ac_buttons(
                    con, host, remote_enc, remote_dec, row, frequency
                )
            return provider._build_non_ac_buttons(con, host, remote_enc, frequency, row)
        finally:
            provider._max_keys = saved_max_keys

    def _daikin_ac_buttons(self, provider: KookongProvider) -> list[dict]:
        """State grid for a Daikin AC remote, built by the native encoder.

        Same states and *the same button names* as
        ``KookongProvider._build_ac_buttons`` (labels run through the same
        ``standardize_ir_key`` call, so an already-saved device keeps matching),
        only the frames come from ``ac_native``.  Fan auto + swing off are the
        defaults for a grid whose labels carry mode/temperature only.
        """
        states: list[tuple[int, int, int]] = []
        if provider._ac_include_off:
            states.append((0, 0, 24))
        for mode in provider._ac_modes:
            for temp in provider._ac_temps:
                states.append((1, mode, temp))

        buttons: list[dict] = []
        used_names: set[str] = set()
        for power, mode, temperature in states:
            provider._total_keys_seen += 1
            label = (
                "Power Off"
                if not power
                else f"On {_MODE_LABELS.get(mode, mode)} {temperature}C"
            )
            try:
                code = daikin_code(
                    power=bool(power),
                    mode=_KK_MODE_TO_DAIKIN.get(mode, "auto"),
                    temp_c=temperature,
                    fan="auto",
                    swing="off",
                )
            except Exception:
                provider._skip_counts["encode_failed"] = (
                    provider._skip_counts.get("encode_failed", 0) + 1
                )
                continue
            std = standardize_ir_key(label)
            if std["name"] in used_names:
                continue
            used_names.add(std["name"])
            buttons.append({"name": std["name"], "icon": std["icon"], "code": code})
        return buttons


_service: KookongMatchService | None = None


def get_match_service() -> KookongMatchService:
    global _service
    if _service is None:
        _service = KookongMatchService()
    return _service
