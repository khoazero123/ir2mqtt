import importlib
import inspect
import logging
import pkgutil
from pathlib import Path

from sqlalchemy import delete, func, insert, select

from .db.models import IrDbButton, IrDbRemote
from .db.session import get_session_maker
from .ir_base import IrRepoProvider

logger = logging.getLogger("ir2mqtt")

# Large imports (e.g. the Kookong offline DB with 10k+ remotes / 250k+ buttons)
# must be written in chunks: a single bulk insert blows past SQLite's bound
# parameter limit and spikes memory.  Keep every write batch bounded.
#
# Measured on the Kookong import (10,981 remotes / 386,181 buttons, max 143
# buttons/remote, ~12 KB of timings per button): holding 1000 remotes in the
# write buffer peaked at 500 MB RSS on top of a 56 MB baseline.  A 4 GB host
# that is already swapping loses the whole add-on to the kernel OOM killer.
# 200 keeps the same peak at roughly a fifth of that, at the cost of a few
# more (still batched) SQLite statements.
DB_INSERT_CHUNK = 200


class IrDbManager:
    def __init__(self):
        self.providers: list[IrRepoProvider] = []
        self._load_providers()
        self._last_updated: int | None = None

    def _load_providers(self):
        providers_pkg = "backend.providers"
        providers_path = Path(__file__).parent / "providers"

        if not providers_path.exists():
            return

        for _, name, _ in pkgutil.iter_modules([str(providers_path)]):
            try:
                module = importlib.import_module(f"{providers_pkg}.{name}")
                for _, obj in inspect.getmembers(module):
                    if inspect.isclass(obj) and issubclass(obj, IrRepoProvider) and obj is not IrRepoProvider:
                        self.providers.append(obj())
            except Exception as e:
                logger.error("Failed to load provider %s: %s", name, e)

    def _session(self):
        return get_session_maker()()

    async def exists(self) -> bool:
        async with self._session() as session:
            count = await session.scalar(select(func.count(IrDbRemote.id)))
            return (count or 0) > 0

    async def get_stats(self) -> dict:
        async with self._session() as session:
            total_remotes = await session.scalar(select(func.count(IrDbRemote.id))) or 0
            total_codes = await session.scalar(select(func.count(IrDbButton.id))) or 0
            proto_rows = await session.execute(select(IrDbButton.protocol, func.count(IrDbButton.id)).group_by(IrDbButton.protocol))
            protocols = {row[0]: row[1] for row in proto_rows if row[0]}
        return {
            "total_remotes": total_remotes,
            "total_codes": total_codes,
            "protocols": protocols,
            "last_updated": self._last_updated,
        }

    async def delete_db(self):
        logger.info("Deleting IR database...")
        async with self._session() as session:
            async with session.begin():
                await session.execute(delete(IrDbRemote))
        self._last_updated = None
        logger.info("IR database deleted.")

    async def _write_provider(self, provider, remotes) -> tuple[int, int]:
        """Write a provider's remotes and buttons in bounded chunks.

        ``remotes`` may be a list or a generator.  Nothing larger than one chunk
        is ever held in memory, so very large imports (the Kookong offline DB
        has 10k+ remotes / 250k+ buttons) stay flat in RAM, and no single
        SQLite statement exceeds the bound-parameter limit.
        """
        logger.info("[%s] Inserting remotes into database...", provider.name)
        remote_count = 0
        button_count = 0
        buf: list[dict] = []

        async with self._session() as session:
            async with session.begin():
                # Remove existing entries for this provider.  A bulk Core DELETE
                # bypasses the ORM cascade, so the child buttons must be removed
                # explicitly — otherwise they are orphaned and keep accumulating
                # in irdb_buttons on every re-sync.
                provider_remote_ids = select(IrDbRemote.id).where(
                    IrDbRemote.provider == provider.id
                )
                await session.execute(
                    delete(IrDbButton).where(IrDbButton.remote_id.in_(provider_remote_ids))
                )
                await session.execute(
                    delete(IrDbRemote).where(IrDbRemote.provider == provider.id)
                )

                async def flush() -> None:
                    nonlocal buf, remote_count, button_count
                    if not buf:
                        return

                    await session.execute(
                        insert(IrDbRemote),
                        [
                            {
                                "provider": r["provider"],
                                "path": r["path"],
                                "name": r["name"],
                                "source_file": r.get("source_file"),
                            }
                            for r in buf
                        ],
                    )

                    paths = [r["path"] for r in buf]
                    result = await session.execute(
                        select(IrDbRemote.id, IrDbRemote.path).where(IrDbRemote.path.in_(paths))
                    )
                    path_to_id = {row.path: row.id for row in result}

                    rows: list[dict] = []
                    for remote in buf:
                        remote_id = path_to_id.get(remote["path"])
                        if remote_id is None:
                            continue
                        for btn in remote.get("buttons", []):
                            code = btn.get("code", {})
                            rows.append(
                                {
                                    "remote_id": remote_id,
                                    "name": btn["name"],
                                    "icon": btn.get("icon"),
                                    "protocol": code.get("protocol"),
                                    "payload": code.get("payload", {}),
                                }
                            )
                            if len(rows) >= DB_INSERT_CHUNK:
                                await session.execute(insert(IrDbButton), rows)
                                button_count += len(rows)
                                rows = []
                    if rows:
                        await session.execute(insert(IrDbButton), rows)
                        button_count += len(rows)

                    remote_count += len(buf)
                    buf = []

                for remote in remotes:
                    buf.append(remote)
                    if len(buf) >= DB_INSERT_CHUNK:
                        await flush()
                await flush()

        logger.info(
            "[%s] Stored %d remotes / %d buttons.", provider.name, remote_count, button_count
        )
        return remote_count, button_count

    async def update(self, flipper: bool = True, probono: bool = True, kookong: bool = False):
        from .websockets import broadcast_ws

        def get_provider(pid):
            return next((p for p in self.providers if p.id == pid), None)

        providers_to_update = []
        if flipper:
            p = get_provider("flipper")
            if p:
                providers_to_update.append(p)
        if probono:
            p = get_provider("probono")
            if p:
                providers_to_update.append(p)
        if kookong:
            p = get_provider("kookong")
            if p:
                providers_to_update.append(p)
            else:
                logger.warning("Kookong provider requested but not loaded.")

        total_providers = len(providers_to_update)

        for i, p in enumerate(providers_to_update):

            async def wrapper(msg, idx=i):
                if msg.get("status") == "done":
                    return
                if "percent" in msg and msg["percent"] is not None:
                    msg["percent"] = int(((idx * 100) + msg["percent"]) / total_providers)
                await broadcast_ws(msg)

            remotes = await p.download_and_convert(wrapper)
            await self._write_provider(p, remotes)
            logger.info("[%s] Database insert complete.", p.name)

        import time

        self._last_updated = int(time.time() * 1000)
        db_stats = await self.get_stats()

        convert_stats = {}
        for p in providers_to_update:
            if p.last_convert_stats:
                convert_stats[p.id] = p.last_convert_stats

        total_skipped = sum(s.get("skipped", 0) for s in convert_stats.values())

        await broadcast_ws(
            {
                "type": "irdb_progress",
                "status": "done",
                "message": "Update complete.",
                "stats": {
                    "total_remotes": db_stats["total_remotes"],
                    "total_codes": db_stats["total_codes"],
                    "total_skipped": total_skipped,
                    "providers": convert_stats,
                },
            }
        )

    async def search(self, query: str) -> list[dict]:
        if not query:
            return []

        tokens = query.lower().split()
        if not tokens:
            return []

        async with self._session() as session:
            stmt = select(IrDbRemote.path, IrDbRemote.name, IrDbRemote.provider)
            for token in tokens:
                pattern = f"%{token}%"
                stmt = stmt.where(func.lower(IrDbRemote.name).like(pattern) | func.lower(IrDbRemote.path).like(pattern))
            stmt = stmt.limit(100)
            rows = await session.execute(stmt)
            results = [{"path": r.path, "name": r.name, "provider": r.provider} for r in rows]

        q_lower = query.lower()
        results.sort(key=lambda x: 0 if x["name"].lower() == q_lower else (1 if x["name"].lower().startswith(q_lower) else 2))
        return results

    async def list_path(self, subpath: str = "") -> list[dict]:
        async with self._session() as session:
            if not subpath:
                rows = await session.execute(select(IrDbRemote.provider).distinct())
                providers_in_db = {r[0] for r in rows}
                return [{"name": p.name, "type": "dir", "path": p.id} for p in self.providers if p.id in providers_in_db]

            prefix = subpath.rstrip("/") + "/"
            rows = await session.execute(select(IrDbRemote.path, IrDbRemote.name).where(IrDbRemote.path.like(f"{prefix}%")))

            seen_dirs: set[str] = set()
            items: list[dict] = []
            for path, name in rows:
                rest = path[len(prefix) :]
                slash = rest.find("/")
                if slash == -1:
                    items.append({"name": name, "type": "file", "path": path})
                else:
                    dir_name = rest[:slash]
                    dir_path = prefix + dir_name
                    if dir_path not in seen_dirs:
                        seen_dirs.add(dir_path)
                        items.append({"name": dir_name, "type": "dir", "path": dir_path})

        items.sort(key=lambda x: (x["type"] != "dir", x["name"].lower()))
        return items

    async def parse_file(self, subpath: str) -> list[dict]:
        async with self._session() as session:
            remote_id = await session.scalar(select(IrDbRemote.id).where(IrDbRemote.path == subpath))
            if remote_id is None:
                return []

            rows = await session.execute(select(IrDbButton).where(IrDbButton.remote_id == remote_id))
            buttons = []
            for (btn,) in rows:
                code = {"protocol": btn.protocol, "payload": btn.payload or {}}
                buttons.append({"name": btn.name, "icon": btn.icon, "code": code})
        return buttons
