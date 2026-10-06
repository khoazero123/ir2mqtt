"""Lazy (on-demand) IRDB index over the Kookong offline database.

The old provider imported **the whole** ``kkoffline.db`` into the add-on's own
SQLite database (10k+ remotes / 300k+ buttons).  That import is what made the
add-on balloon to ~300 MB and get OOM-killed on small hosts.  This module keeps
the same browse/search experience without the copy:

* ``list_path("kookong")`` and below are answered straight from
  ``kkoffline.db`` (device type -> brand -> remote), no app-DB rows involved.
* ``search()`` matches brand/remote names in the offline DB.
* keys are decoded **only** when the user opens a remote
  (:meth:`KookongIrdbIndex.parse`), through the same native encoder the bulk
  importer used.

Paths keep the exact layout the importer produced, so anything that was already
imported (and every frontend breadcrumb) still resolves::

    kookong/<slug(type_label)>/<slug(brand_name)>/<slug(brand_name)>_<remote_dec>

Reading is done through :class:`KookongMatchService`, which already opens
``kkoffline.db`` + ``libkksdk_host.so`` on demand and exposes the low-level
queries (categories/brands/countries/buttons_for).  SQLite is used
synchronously from the event-loop thread, exactly like the match service.
"""

from __future__ import annotations

import logging
import threading
from typing import Any

from ..providers.kookong import DEVICE_TYPE_LABELS, _slug
from .match_service import KookongMatchService, get_match_service, ha_country

logger = logging.getLogger("ir2mqtt")

PROVIDER_ID = "kookong"

# Sort key for brands/remotes that have no row in the recommended region; it
# keeps them after every ranked entry without needing a second ordering pass.
_NO_RANK = 1_000_000

def _recommended_country(available: list[str]) -> str | None:
    """Which region's ranking to use — same rule as ``KookongMatchService.countries``.

    The Home Assistant country when it has data here, otherwise CN, otherwise the
    first available region.
    """
    ha = ha_country()
    if ha and ha in available:
        return ha
    if "CN" in available:
        return "CN"
    return available[0] if available else None

class KookongIrdbIndex:
    """Read-only browse/search/decode view over ``kkoffline.db``.

    Every layer (brand names, brands per device type, remotes per brand, the
    search index) is built at most once and cached in RAM, guarded by a lock.
    """

    def __init__(self, service: KookongMatchService | None = None) -> None:
        self._service = service or get_match_service()
        self._lock = threading.RLock()

        self._categories: list[dict] | None = None
        self._cats_by_id: dict[int, dict] = {}
        self._cat_by_slug: dict[str, int] = {}

        self._brand_names: dict[str, str] | None = None
        self._brands_by_cat: dict[int, list[dict]] = {}
        # (category_id, brand slug) -> brand ids (a slug can collide)
        self._brand_ids_by_slug: dict[tuple[int, str], list[str]] = {}

        self._remotes_by_cb: dict[tuple[int, str], list[dict]] = {}
        self._remotes_by_path: dict[str, tuple[int, str, dict]] = {}

        self._search_index: list[tuple[str, str]] | None = None
        self._distinct_remotes: int | None = None

    # ------------------------------------------------------------------ plumbing

    def available(self) -> bool:
        """True when ``kkoffline.db`` + the native host library can be loaded."""
        try:
            return self._service.available()
        except Exception:  # pragma: no cover - defensive, service already guards
            logger.warning("Kookong lazy index unavailable.", exc_info=True)
            return False

    def _ensure(self):
        return self._service._ensure()

    def reset_cache(self) -> None:
        """Drop every cached layer (used by tests / after the DB changes)."""
        with self._lock:
            self._categories = None
            self._cats_by_id = {}
            self._cat_by_slug = {}
            self._brand_names = None
            self._brands_by_cat = {}
            self._brand_ids_by_slug = {}
            self._remotes_by_cb = {}
            self._remotes_by_path = {}
            self._search_index = None
            self._distinct_remotes = None

    # ------------------------------------------------------------------ categories

    def categories(self) -> list[dict]:
        """Device types with a remote count, ordered by type id."""
        with self._lock:
            if self._categories is None:
                out: list[dict] = []
                by_id: dict[int, dict] = {}
                by_slug: dict[str, int] = {}
                for row in self._service.categories():
                    dt = int(row["id"])
                    label = row["name"]
                    item = {
                        "id": dt,
                        "name": label,
                        "slug": _slug(label),
                        "count": int(row["count"]),
                    }
                    out.append(item)
                    by_id[dt] = item
                    by_slug.setdefault(item["slug"], dt)
                self._categories = out
                self._cats_by_id = by_id
                self._cat_by_slug = by_slug
            return self._categories

    def _cat_id(self, type_slug: str) -> int | None:
        self.categories()
        cat_id = self._cat_by_slug.get(type_slug)
        if cat_id is None:
            # The DB may have changed under us; rebuild once before giving up.
            self.reset_cache()
            self.categories()
            cat_id = self._cat_by_slug.get(type_slug)
        return cat_id

    def _type_label(self, category_id: int) -> str:
        cat = self._cats_by_id.get(category_id)
        if cat is not None:
            return cat["name"]
        return DEVICE_TYPE_LABELS.get(category_id, f"type{category_id}")

    def _brand_map(self, con, host) -> dict[str, str]:
        with self._lock:
            if self._brand_names is None:
                self._brand_names = self._service._brand_map(con, host)
            return self._brand_names

    def _category_country(self, con, category_id: int) -> str | None:
        rows = con.execute(
            "SELECT DISTINCT country FROM RcBrandRemoteMap "
            "WHERE device_type_id = ? AND country IS NOT NULL AND country <> ''",
            (category_id,),
        ).fetchall()
        return _recommended_country([row["country"] for row in rows])

    # ------------------------------------------------------------------ browsing

    def list_types(self) -> list[dict]:
        """Device-type directories (``kookong/<slug>``)."""
        return [
            {
                "name": c["name"],
                "type": "dir",
                "path": f"{PROVIDER_ID}/{c['slug']}",
                "count": c["count"],
            }
            for c in self.categories()
        ]

    def list_brands(self, type_slug: str) -> list[dict]:
        """Brand directories for a device type, most popular first."""
        cat_id = self._cat_id(type_slug)
        if cat_id is None:
            return []
        with self._lock:
            brands = self._brands_by_cat.get(cat_id)
            if brands is None:
                brands = self._build_brands(cat_id, type_slug)
            return [
                {"name": b["name"], "type": "dir", "path": b["path"], "count": b["count"]}
                for b in brands
            ]

    def _build_brands(self, category_id: int, type_slug: str) -> list[dict]:
        con, host = self._ensure()
        names = self._brand_map(con, host)
        rec = self._category_country(con, category_id)

        rows = con.execute(
            """
            SELECT brand_id,
                   MIN(CASE WHEN country = ? THEN rank ELSE ? END) AS rec_rank,
                   MIN(rank)                                       AS best_rank,
                   COUNT(DISTINCT remote_id)                       AS remotes
            FROM RcBrandRemoteMap
            WHERE device_type_id = ?
            GROUP BY brand_id
            """,
            (rec, _NO_RANK, category_id),
        ).fetchall()

        items: list[dict] = []
        for row in rows:
            name = names.get(row["brand_id"])
            if not name:
                continue
            slug = _slug(name)
            items.append(
                {
                    "id": row["brand_id"],
                    "name": name,
                    "slug": slug,
                    "count": int(row["remotes"]),
                    "rec_rank": int(row["rec_rank"] or _NO_RANK),
                    "best_rank": int(row["best_rank"] or _NO_RANK),
                    "path": f"{PROVIDER_ID}/{type_slug}/{slug}",
                }
            )

        # Recommended-region popularity first, then global rank, then name.
        items.sort(key=lambda b: (b["rec_rank"], b["best_rank"], b["name"].lower()))

        # Two brands can slugify to the same directory; keep the best-ranked one
        # for the listing, but remember every brand id so a remote path that
        # belongs to the dropped twin still resolves.
        for item in items:
            self._brand_ids_by_slug.setdefault((category_id, item["slug"]), []).append(item["id"])

        deduped: list[dict] = []
        seen_paths: set[str] = set()
        for item in items:
            if item["path"] in seen_paths:
                continue
            seen_paths.add(item["path"])
            deduped.append(item)

        self._brands_by_cat[category_id] = deduped
        return deduped

    def _resolve_brand(self, category_id: int, type_slug: str, brand_slug: str) -> str | None:
        with self._lock:
            ids = self._brand_ids_by_slug.get((category_id, brand_slug))
            if ids is None:
                self._brands_by_cat.pop(category_id, None)
                self._brand_ids_by_slug = {
                    key: value
                    for key, value in self._brand_ids_by_slug.items()
                    if key[0] != category_id
                }
                self._build_brands(category_id, type_slug)
                ids = self._brand_ids_by_slug.get((category_id, brand_slug))
            return ids[0] if ids else None

    def list_remotes(self, type_slug: str, brand_slug: str) -> list[dict]:
        """Remote files inside a brand directory."""
        cat_id = self._cat_id(type_slug)
        if cat_id is None:
            return []
        brand_id = self._resolve_brand(cat_id, type_slug, brand_slug)
        if brand_id is None:
            return []
        return [
            {"name": r["name"], "type": "file", "path": r["path"]}
            for r in self._remotes_for(cat_id, brand_id)
        ]

    def _remotes_for(self, category_id: int, brand_id: str) -> list[dict]:
        key = (category_id, brand_id)
        with self._lock:
            cached = self._remotes_by_cb.get(key)
            if cached is not None:
                return cached

            con, host = self._ensure()
            # Region ranking comes from the same helper the match wizard uses.
            rec = self._service.countries(category_id, brand_id).get("recommended")
            rows = con.execute(
                """
                SELECT m.remote_id                                  AS remote_id,
                       MIN(CASE WHEN m.country = ? THEN m.rank ELSE ? END) AS rec_rank,
                       MIN(m.rank)                                  AS best_rank,
                       r.frequency                                  AS frequency,
                       r.type                                       AS type
                FROM RcBrandRemoteMap m
                JOIN RcRemoteController r ON r.remote_id = m.remote_id
                WHERE m.device_type_id = ? AND m.brand_id = ?
                GROUP BY m.remote_id
                ORDER BY rec_rank, best_rank, m.remote_id
                """,
                (rec, _NO_RANK, category_id, brand_id),
            ).fetchall()

            label = self._type_label(category_id)
            brand_name = self._brand_map(con, host).get(brand_id) or str(brand_id)
            brand_slug = _slug(brand_name)
            type_slug = _slug(label)

            remotes: list[dict] = []
            for row in rows:
                remote_enc = row["remote_id"]
                try:
                    remote_dec = host.decrypt_token(remote_enc)
                except Exception:
                    remote_dec = remote_enc
                remotes.append(
                    {
                        "remote_enc": remote_enc,
                        "remote_dec": remote_dec,
                        "category_id": category_id,
                        "brand_id": brand_id,
                        "rank": int(row["rec_rank"] or _NO_RANK),
                        "is_ac": int(row["type"] or 0) == 2,
                        "name": f"{label} {brand_name} {remote_dec}".strip(),
                        "path": (
                            f"{PROVIDER_ID}/{type_slug}/{brand_slug}/{brand_slug}_{remote_dec}"
                        ),
                    }
                )

            self._remotes_by_cb[key] = remotes
            for remote in remotes:
                self._remotes_by_path.setdefault(
                    remote["path"], (category_id, brand_id, remote)
                )
            return remotes

    # ------------------------------------------------------------------ search

    def search(self, query: str, limit: int = 100) -> list[dict]:
        """Brand/remote matches from the offline DB, ranked like the app-DB one."""
        if not query:
            return []
        tokens = query.lower().split()
        if not tokens:
            return []

        results: list[dict] = []
        for name, path in self._search_entries():
            haystack = f"{name.lower()} {path.lower()}"
            if all(token in haystack for token in tokens):
                results.append({"path": path, "name": name, "provider": PROVIDER_ID})

        lowered = query.lower()
        results.sort(
            key=lambda x: (
                0 if x["name"].lower() == lowered
                else (1 if x["name"].lower().startswith(lowered) else 2),
                x["name"].lower(),
            )
        )
        return results[:limit]

    def _search_entries(self) -> list[tuple[str, str]]:
        with self._lock:
            if self._search_index is not None:
                return self._search_index

            con, host = self._ensure()
            names = self._brand_map(con, host)
            labels = {c["id"]: c["name"] for c in self.categories()}

            rows = con.execute(
                """
                SELECT m.device_type_id AS device_type_id,
                       m.brand_id       AS brand_id,
                       m.remote_id      AS remote_id
                FROM RcBrandRemoteMap m
                JOIN RcRemoteController r ON r.remote_id = m.remote_id
                GROUP BY m.device_type_id, m.brand_id, m.remote_id
                """
            ).fetchall()

            entries: list[tuple[str, str]] = []
            seen_paths: set[str] = set()
            for row in rows:
                brand = names.get(row["brand_id"])
                if not brand:
                    continue
                dt = int(row["device_type_id"])
                label = labels.get(dt) or DEVICE_TYPE_LABELS.get(dt, f"type{dt}")
                try:
                    remote_dec = host.decrypt_token(row["remote_id"])
                except Exception:
                    remote_dec = row["remote_id"]
                brand_slug = _slug(brand)
                path = f"{PROVIDER_ID}/{_slug(label)}/{brand_slug}/{brand_slug}_{remote_dec}"
                if path in seen_paths:
                    continue
                seen_paths.add(path)
                entries.append((f"{label} {brand} {remote_dec}".strip(), path))

            self._search_index = entries
            return entries

    # ------------------------------------------------------------------ decoding

    def parse(self, path: str) -> list[dict] | None:
        """Decode one remote's buttons on demand.

        Returns ``None`` when the lazy index itself is unavailable (so the
        caller can fall back to whatever the app DB still holds) and ``[]`` when
        the path is not part of the offline database.
        """
        parts = path.strip("/").split("/")
        if len(parts) != 4 or parts[0] != PROVIDER_ID:
            return None
        if not self.available():
            return None

        _, type_slug, brand_slug, filename = parts

        with self._lock:
            # Fast path: the remote was already listed (browse / search).
            hit = self._remotes_by_path.get(path)
            if hit is not None:
                cat_id, brand_id, remote = hit
                buttons = self._service.buttons_for(cat_id, brand_id, remote["remote_enc"])
                return buttons or []

        cat_id = self._cat_id(type_slug)
        if cat_id is None:
            return []

        with self._lock:
            brand_ids = list(self._brand_ids_by_slug.get((cat_id, brand_slug)) or [])
            if not brand_ids:
                resolved = self._resolve_brand(cat_id, type_slug, brand_slug)
                brand_ids = [resolved] if resolved else []
            for brand_id in brand_ids:
                for remote in self._remotes_for(cat_id, brand_id):
                    if remote["path"] == path or remote["path"].rsplit("/", 1)[-1] == filename:
                        buttons = self._service.buttons_for(cat_id, brand_id, remote["remote_enc"])
                        return buttons or []
        return []

    # ------------------------------------------------------------------ stats

    def stats(self) -> dict:
        """Small status block for ``/api/irdb/status``."""
        if not self.available():
            return {
                "lazy": True,
                "available": False,
                "total_remotes": 0,
                "total_categories": 0,
            }
        try:
            with self._lock:
                if self._distinct_remotes is None:
                    con, _ = self._ensure()
                    row = con.execute(
                        "SELECT COUNT(*) AS n FROM (SELECT DISTINCT remote_id FROM RcBrandRemoteMap)"
                    ).fetchone()
                    self._distinct_remotes = int(row["n"] if row is not None else 0)
                total = self._distinct_remotes
            categories = len(self.categories())
        except Exception:
            logger.warning("Could not read Kookong lazy stats.", exc_info=True)
            return {
                "lazy": True,
                "available": False,
                "total_remotes": 0,
                "total_categories": 0,
            }
        return {
            "lazy": True,
            "available": True,
            "total_remotes": total,
            "total_categories": categories,
        }

_index: KookongIrdbIndex | None = None

def get_irdb_index() -> KookongIrdbIndex:
    """Process-wide lazy index (shares the match service + its open DB)."""
    global _index
    if _index is None:
        _index = KookongIrdbIndex()
    return _index
