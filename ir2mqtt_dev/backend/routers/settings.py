import asyncio
import json
import os
import re
from collections import Counter
from datetime import datetime, timedelta, timezone
from logging import Logger
from pathlib import Path
from typing import TYPE_CHECKING, Literal

from fastapi import APIRouter, File, Form, HTTPException, UploadFile
from fastapi.responses import Response
from pydantic import BaseModel

from .. import config
from ..dependencies import (
    AutomationManagerDep,
    DatabaseDep,
    IrDbManagerDep,
    LoggerDep,
    MQTTManagerDep,
    SettingsDep,
    StateManagerDep,
)
from ..ha_automation_discovery import send_ha_discovery_for_all_automations
from ..integrations import get_integration
from ..models import (
    AppModeResponse,
    ImportConfigResponse,
    IRAutomation,
    IRDevice,
    LogLevelResponse,
    MqttSettings,
    MqttTestResponse,
    StatusOk,
)
from ..utils import update_options_file
from ..websockets import broadcast_ws

if TYPE_CHECKING:
    pass

router = APIRouter(prefix="/api", tags=["settings"])


class AppModePayload(BaseModel):
    mode: str
    topic_style: Literal["name", "id"] | None = "name"
    migrate: bool = False
    echo_suppression_ms: int | None = None


class LogLevelPayload(BaseModel):
    log_level: str

# --- BACKUP / RESTORE ---
BACKUP_VERSION = 1
BACKUP_MAX_FILES = 20
BACKUP_TS_FORMAT = "%Y%m%d-%H%M%S"
BACKUP_ID_RE = re.compile(r"^backup-\d{8}-\d{6}\.json$")

class BackupMetadata(BaseModel):
    id: str
    filename: str
    created_at: str
    size: int
    device_count: int
    automation_count: int

class BackupListResponse(BaseModel):
    status: str = "ok"
    backups: list[BackupMetadata]

class BackupRestoreResponse(BaseModel):
    status: str = "ok"
    mode: str
    detail: str
    auto_backup: BackupMetadata | None = None

def _now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()

def _get_backup_dir(settings: "config.Settings") -> Path:
    """Backups live on the add-on data volume (default /data/backups)."""
    custom = os.environ.get("IR2MQTT_BACKUP_DIR")
    if custom:
        return Path(custom)
    return Path(settings.options_file).parent / "backups"

def _build_config_payload(state: "StateManager", automation_manager: "AutomationManager") -> dict:
    """Normalised export/backup payload with metadata."""
    return {
        "version": BACKUP_VERSION,
        "exported_at": _now_iso(),
        "devices": [d.model_dump() for d in state.devices],
        "automations": [a.model_dump() for a in automation_manager.automations],
    }

def _backup_metadata(path: Path) -> BackupMetadata:
    stat = path.stat()
    created_at = datetime.fromtimestamp(stat.st_mtime, tz=timezone.utc).isoformat()
    device_count = 0
    automation_count = 0
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        device_count = len(data.get("devices", []) or [])
        automation_count = len(data.get("automations", []) or [])
        if data.get("exported_at"):
            created_at = str(data["exported_at"])
    except (OSError, ValueError):
        pass
    return BackupMetadata(
        id=path.name,
        filename=path.name,
        created_at=created_at,
        size=stat.st_size,
        device_count=device_count,
        automation_count=automation_count,
    )

def _prune_backups(backup_dir: Path, logger: Logger) -> None:
    """Keep only the newest BACKUP_MAX_FILES backups."""
    backups = sorted(p for p in backup_dir.iterdir() if p.is_file() and BACKUP_ID_RE.match(p.name))
    for old in backups[: max(0, len(backups) - BACKUP_MAX_FILES)]:
        try:
            old.unlink()
            logger.info("Pruned old backup: %s", old.name)
        except OSError as e:
            logger.warning("Failed to prune backup %s: %s", old.name, e)

def _create_backup(
    state: "StateManager",
    automation_manager: "AutomationManager",
    settings: "config.Settings",
    logger: Logger,
) -> BackupMetadata:
    backup_dir = _get_backup_dir(settings)
    backup_dir.mkdir(parents=True, exist_ok=True)
    payload = _build_config_payload(state, automation_manager)

    base = datetime.now()
    path: Path | None = None
    for offset in range(120):
        candidate = backup_dir / f"backup-{(base + timedelta(seconds=offset)).strftime(BACKUP_TS_FORMAT)}.json"
        if not candidate.exists():
            path = candidate
            break
    if path is None:
        raise HTTPException(status_code=500, detail="Could not allocate a backup filename.")

    path.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    _prune_backups(backup_dir, logger)
    logger.info(
        "Created backup %s (%s devices, %s automations).",
        path.name,
        len(payload["devices"]),
        len(payload["automations"]),
    )
    return _backup_metadata(path)

def _resolve_backup_file(settings: "config.Settings", backup_id: str) -> Path:
    """Reject any id that could escape the backup directory (path traversal)."""
    if not BACKUP_ID_RE.match(backup_id):
        raise HTTPException(status_code=400, detail="Invalid backup id.")
    backup_dir = _get_backup_dir(settings)
    path = backup_dir / backup_id
    if path.parent.resolve() != backup_dir.resolve():
        raise HTTPException(status_code=400, detail="Invalid backup id.")
    return path

def _auto_backup(
    state: "StateManager",
    automation_manager: "AutomationManager",
    settings: "config.Settings",
    logger: Logger,
) -> BackupMetadata:
    """Create a safety snapshot before any destructive operation (abort on failure)."""
    try:
        backup = _create_backup(state, automation_manager, settings, logger)
    except HTTPException:
        raise
    except Exception as e:
        logger.error("Automatic pre-operation backup failed: %s", e, exc_info=True)
        raise HTTPException(status_code=500, detail="Automatic backup failed; operation aborted.") from e
    logger.info("Automatic safety backup created before destructive operation: %s", backup.filename)
    return backup

def _dedupe_names(items: list[dict], used: set[str], default_name: str) -> None:
    """Append _1, _2... to names that collide (case-insensitive)."""
    for item in items:
        original_name = str(item.get("name") or default_name).strip() or default_name
        name = original_name
        counter = 1
        while name.lower() in used:
            name = f"{original_name}_{counter}"
            counter += 1
        item["name"] = name
        used.add(name.lower())

async def _apply_config(
    data: dict,
    mode: str,
    *,
    state: "StateManager",
    mqtt: "MQTTManager",
    automation_manager: "AutomationManager",
    logger: Logger,
    db: DatabaseDep,
) -> tuple[int, int]:
    """Shared import/restore logic. mode=replace wipes, mode=merge appends."""
    if mode not in ("replace", "merge"):
        raise HTTPException(status_code=400, detail="Invalid import mode.")

    devices_data = list(data.get("devices", []) or [])
    automations_data = list(data.get("automations", []) or [])
    for entry in devices_data + automations_data:
        if not isinstance(entry, dict):
            raise HTTPException(status_code=400, detail="Invalid config format. Expected objects in devices/automations.")

    if mode == "merge":
        existing_dev_ids = {d.id for d in state.devices}
        existing_auto_ids = {a.id for a in automation_manager.automations if a.id}
        # Never overwrite existing ids; only add genuinely new items.
        devices_data = [d for d in devices_data if d.get("id") not in existing_dev_ids]
        automations_data = [
            a for a in automations_data if not (a.get("id") and a.get("id") in existing_auto_ids)
        ]
        used_dev_names = {d.name.lower().strip() for d in state.devices}
        used_auto_names = {a.name.lower().strip() for a in automation_manager.automations}
    else:
        used_dev_names = set()
        used_auto_names = set()

    # Deduplicate names (against existing data in merge mode).
    _dedupe_names(devices_data, used_dev_names, "Unknown")
    for dev in devices_data:
        _dedupe_names(list(dev.get("buttons", []) or []), set(), "Button")
    _dedupe_names(automations_data, used_auto_names, "Automation")

    if mode == "replace":
        logger.info("Clearing existing integration entities before import.")
        await mqtt.integration.clear_all(mqtt)
        logger.info("Clearing database...")
        await db.delete_all_devices()
        await db.delete_all_automations()
        await db.commit()
        state.devices = []
        automation_manager.automations = []

    for d_data in devices_data:
        try:
            dev = IRDevice.model_validate(d_data)
            await db.save_device(dev)
            state.devices.append(dev)
        except Exception as e:
            logger.error("Skipping invalid device during import: %s", e)
    await db.commit()

    for a_data in automations_data:
        try:
            auto = IRAutomation.model_validate(a_data)
            await automation_manager.add_automation(auto)
        except Exception as e:
            logger.error("Skipping invalid automation during import: %s", e)

    logger.info("Re-initializing integration with new data.")
    await mqtt.integration.on_mqtt_connect(mqtt)
    await broadcast_ws({"type": "devices_updated"})
    await broadcast_ws({"type": "automations_updated"})
    return len(state.devices), len(automation_manager.automations)


@router.get("/settings/app", response_model=AppModeResponse)
async def get_app_mode(settings: SettingsDep):
    version = os.environ.get("APP_VERSION", "unknown")

    return AppModeResponse(
        mode=settings.app_mode,
        topic_style=settings.topic_style,
        locked="SUPERVISOR_TOKEN" in os.environ,
        log_level=config.get_log_level_name(),
        echo_suppression_ms=settings.echo_suppression_ms,
        version=version,
    )


@router.put("/settings/log_level", response_model=LogLevelResponse)
async def set_log_level_endpoint(payload: LogLevelPayload, logger: LoggerDep):
    logger.info("Request to set log level to %s", payload.log_level)
    config.set_log_level(payload.log_level)
    return LogLevelResponse(log_level=payload.log_level)


@router.put("/settings/app", response_model=AppModeResponse)
async def set_app_mode(
    payload: AppModePayload,
    state: StateManagerDep,
    mqtt: MQTTManagerDep,
    automation_manager: AutomationManagerDep,
    logger: LoggerDep,
    db: DatabaseDep,
    settings: SettingsDep,
):
    logger.info(
        "Request to change app mode to '%s' (topic style: %s, migrate: %s)",
        payload.mode,
        payload.topic_style,
        payload.migrate,
    )
    if payload.mode not in ["home_assistant", "standalone"]:
        logger.error("Invalid app mode requested: '%s'", payload.mode)
        raise HTTPException(400, "Invalid mode")

    # Check for duplicates if we are switching to (or are in) standalone mode with name style
    if payload.mode == "standalone" and payload.topic_style == "name":
        logger.info("Checking for duplicate names for standalone/name mode.")
        # Check Devices
        device_names = [d.name.lower().strip() for d in state.devices]
        dev_counts = Counter(device_names)
        dup_devs = [n for n, c in dev_counts.items() if c > 1]

        # Check Buttons (per device)
        dup_btns = []
        for dev in state.devices:
            btn_names = [b.name.lower().strip() for b in dev.buttons]
            btn_counts = Counter(btn_names)
            if any(c > 1 for c in btn_counts.values()):
                dup_btns.append(dev.name)

        # Check Automations
        auto_names = [a.name.lower().strip() for a in automation_manager.automations]
        auto_counts = Counter(auto_names)
        dup_autos = [n for n, c in auto_counts.items() if c > 1]

        if dup_devs or dup_btns or dup_autos:
            if not payload.migrate:
                detail = []
                if dup_devs:
                    detail.append(f"Duplicate Devices: {', '.join(dup_devs)}")
                if dup_btns:
                    detail.append(f"Duplicate Buttons in: {', '.join(dup_btns)}")
                if dup_autos:
                    detail.append(f"Duplicate Automations: {', '.join(dup_autos)}")
                logger.warning(
                    "Duplicate names found and migration not requested. Details: %s",
                    "; ".join(detail),
                )
                raise HTTPException(409, "; ".join(detail))
            else:
                logger.info("Migrating duplicate names...")
                # Migrate: Rename duplicates
                # Devices
                used_dev_names = set()
                for dev in state.devices:
                    original_name = dev.name.strip()
                    name = original_name
                    counter = 1
                    while name.lower() in used_dev_names:
                        name = f"{original_name}_{counter}"
                        counter += 1
                    if name != original_name:
                        logger.info("Renaming device '%s' to '%s'", original_name, name)
                        dev.name = name
                    used_dev_names.add(name.lower())

                # Save renamed devices to DB
                for dev in state.devices:
                    await db.save_device(dev)
                await db.commit()

                # Buttons
                for dev in state.devices:
                    used_btn_names = set()
                    for btn in dev.buttons:
                        original_name = btn.name.strip()
                        name = original_name
                        counter = 1
                        while name.lower() in used_btn_names:
                            name = f"{original_name}_{counter}"
                            counter += 1
                        if name != original_name:
                            logger.info(
                                "Renaming button '%s' to '%s' on device '%s'",
                                original_name,
                                name,
                                dev.name,
                            )
                            btn.name = name
                        used_btn_names.add(name.lower())
                    # Save device again if buttons changed (optimized: could be done once above)
                    await db.save_device(dev)
                await db.commit()

                # Automations
                used_auto_names = set()
                for auto in automation_manager.automations:
                    original_name = auto.name.strip()
                    name = original_name
                    counter = 1
                    while name.lower() in used_auto_names:
                        name = f"{original_name}_{counter}"
                        counter += 1
                    if name != original_name:
                        logger.info("Renaming automation '%s' to '%s'", original_name, name)
                        auto.name = name
                    used_auto_names.add(name.lower())

                # Save automations to DB
                for auto in automation_manager.automations:
                    await automation_manager.update_automation(auto)

                automation_manager.save()
                await broadcast_ws({"type": "devices_updated"})
                await broadcast_ws({"type": "automations_updated"})
                logger.info("Duplicate name migration complete.")

    settings.app_mode = payload.mode
    settings.topic_style = payload.topic_style if payload.topic_style is not None else "name"

    if payload.echo_suppression_ms is not None:
        settings.echo_suppression_ms = payload.echo_suppression_ms

    # Save to options.yaml
    logger.info("Saving new app mode settings to options.yaml.")
    update_options_file(
        settings.options_file,
        {
            "app_mode": settings.app_mode,
            "topic_style": settings.topic_style,
            "echo_suppression_ms": settings.echo_suppression_ms,
        },
    )

    # Switch integration
    logger.info("Switching to '%s' integration.", settings.app_mode)
    old_integration = mqtt.integration
    if old_integration:
        logger.info("Clearing entities from old integration.")
        await old_integration.clear_all(mqtt)

    new_integration = get_integration(settings.app_mode, state, settings)
    mqtt.set_integration(new_integration)

    if mqtt.connected:
        logger.info("Re-subscribing to topics and running on_connect for new integration.")
        for topic in new_integration.get_subscribe_topics():
            mqtt.subscribe(topic)
        await new_integration.on_mqtt_connect(mqtt)
        if settings.app_mode != "standalone":
            send_ha_discovery_for_all_automations(automation_manager.automations, mqtt)

    logger.info("App mode successfully switched.")
    return AppModeResponse(
        mode=settings.app_mode,
        topic_style=settings.topic_style,
        locked="SUPERVISOR_TOKEN" in os.environ,
        log_level=config.get_log_level_name(),
        echo_suppression_ms=settings.echo_suppression_ms,
    )


@router.get("/settings/mqtt", response_model=MqttSettings)
async def get_mqtt_settings(
    logger: LoggerDep,
    settings: SettingsDep,
):
    logger.debug("Request for MQTT settings.")
    return MqttSettings(
        broker=settings.mqtt_broker,
        port=settings.mqtt_port,
        user=settings.mqtt_user,
        password=settings.mqtt_pass,
    )


@router.put("/settings/mqtt", response_model=StatusOk)
async def save_mqtt_settings(
    mqtt_settings: MqttSettings,
    mqtt: MQTTManagerDep,
    logger: LoggerDep,
    app_settings: SettingsDep,
):
    logger.info("Request to save MQTT settings.")

    # Prevent changes in supervised (HA App) environments where settings are from env vars
    if "SUPERVISOR_TOKEN" in os.environ:
        logger.warning("Attempted to save MQTT settings in a supervised environment. Ignoring.")
        raise HTTPException(403, "MQTT settings are managed by the supervisor.")
    update_options_file(
        app_settings.options_file,
        {
            "mqtt_broker": mqtt_settings.broker,
            "mqtt_port": mqtt_settings.port,
            "mqtt_user": mqtt_settings.user,
            "mqtt_pass": mqtt_settings.password,
        },
    )

    # Update the in-memory settings object so changes are reflected without a restart.
    app_settings.mqtt_broker = mqtt_settings.broker
    app_settings.mqtt_port = mqtt_settings.port
    app_settings.mqtt_user = mqtt_settings.user
    app_settings.mqtt_pass = mqtt_settings.password

    logger.info("MQTT settings saved. Triggering MQTT client reload.")
    task = asyncio.create_task(mqtt.reload())
    task.add_done_callback(lambda t: t.exception() and logger.error("MQTT reload failed: %s", t.exception(), exc_info=t.exception()))
    return StatusOk()


@router.post("/settings/mqtt/test", response_model=MqttTestResponse)
async def test_mqtt_settings(
    settings: MqttSettings,
    mqtt: MQTTManagerDep,
    logger: LoggerDep,
):
    logger.info("Request to test MQTT connection to %s:%s", settings.broker, settings.port)
    result = await mqtt.test_connection(settings.model_dump())
    logger.info("MQTT connection test result: %s", result)
    return MqttTestResponse(**result)


@router.post("/reset", response_model=StatusOk)
async def factory_reset(
    state: StateManagerDep,
    mqtt: MQTTManagerDep,
    automation_manager: AutomationManagerDep,
    logger: LoggerDep,
    db: DatabaseDep,
    irdb_manager: IrDbManagerDep,
    settings: SettingsDep,
    keep_irdb: bool = False,
):
    logger.warning("Initiating factory reset...")

    # Safety net: snapshot the current state before wiping everything.
    _auto_backup(state, automation_manager, settings, logger)

    logger.info("Clearing all integration entities (e.g., Home Assistant).")
    await mqtt.integration.clear_all(mqtt)

    # 3. Clear memory
    logger.info("Clearing in-memory data (devices, automations).")
    state.devices = []
    automation_manager.automations = []
    await db.delete_all_devices()
    await db.delete_all_automations()
    await db.commit()

    # 4. Remove files
    files_to_remove = [
        settings.options_file,
    ]
    for f_path in files_to_remove:
        if os.path.exists(f_path):
            logger.info("Removing configuration file: %s", f_path)
            os.remove(f_path)

    # Delete IRDB (skip when caller wants to preserve it, e.g. during test runs)
    if not keep_irdb:
        await irdb_manager.delete_db()

    # Reset log level to default
    config.set_log_level("INFO")

    # Clear the settings cache to force re-read on next request
    config.get_settings.cache_clear()
    logger.info("Settings cache cleared.")

    # Reset in-memory settings object
    default_settings = config.Settings()
    settings.app_mode = default_settings.app_mode
    settings.topic_style = default_settings.topic_style
    settings.echo_suppression_ms = default_settings.echo_suppression_ms
    settings.bridge_settings = default_settings.bridge_settings
    settings.log_level = default_settings.log_level
    settings.mqtt_broker = default_settings.mqtt_broker
    settings.mqtt_port = default_settings.mqtt_port
    settings.mqtt_user = default_settings.mqtt_user
    settings.mqtt_pass = default_settings.mqtt_pass
    logger.info("In-memory settings reset to defaults.")

    # 5. Broadcast updates
    await broadcast_ws({"type": "devices_updated"})
    await broadcast_ws(
        {
            "type": "bridges_updated",
            "bridges": mqtt._get_bridges_list_for_broadcast(),
        }
    )
    await broadcast_ws({"type": "irdb_updated"})

    logger.warning("Factory reset complete.")
    return StatusOk()


@router.get("/config/export")
async def export_config(
    state: StateManagerDep,
    automation_manager: AutomationManagerDep,
    logger: LoggerDep,
):
    """Provides the configuration file for download."""
    logger.info("Configuration export requested.")
    # Export from memory to ensure it works even if file doesn't exist yet
    export_data = _build_config_payload(state, automation_manager)
    json_str = json.dumps(export_data, indent=2)
    return Response(
        content=json_str,
        media_type="application/json",
        headers={"Content-Disposition": "attachment; filename=ir2mqtt_config.json"},
    )


@router.post("/config/import", response_model=ImportConfigResponse)
async def import_config(
    state: StateManagerDep,
    mqtt: MQTTManagerDep,
    automation_manager: AutomationManagerDep,
    logger: LoggerDep,
    db: DatabaseDep,
    settings: SettingsDep,
    file: UploadFile = File(...),
    mode: Literal["replace", "merge"] = Form("replace"),
):
    """Imports a configuration file.

    ``mode=replace`` (default, backwards compatible) overwrites everything;
    ``mode=merge`` keeps existing data and only adds new items.
    """
    logger.info("Configuration import requested from file: %s (mode=%s)", file.filename, mode)

    content = await file.read()

    try:
        data = json.loads(content)
    except json.JSONDecodeError as e:
        logger.error("Configuration import failed: Invalid JSON format. %s", e)
        raise HTTPException(status_code=400, detail=f"Invalid JSON: {e}") from e

    if not isinstance(data, dict):
        logger.error("Configuration import failed: Invalid root JSON type.")
        raise HTTPException(status_code=400, detail="Invalid JSON format. Expected an object.")

    # Safety net: snapshot the current state before a destructive replace.
    if mode == "replace":
        _auto_backup(state, automation_manager, settings, logger)

    device_count, automation_count = await _apply_config(
        data,
        mode,
        state=state,
        mqtt=mqtt,
        automation_manager=automation_manager,
        logger=logger,
        db=db,
    )

    logger.info("Import successful (%s): %s devices and %s automations loaded.", mode, device_count, automation_count)
    return ImportConfigResponse(detail=f"{device_count} devices and {automation_count} automations imported.")

@router.post("/backup/create", response_model=BackupMetadata)
async def create_backup(
    state: StateManagerDep,
    automation_manager: AutomationManagerDep,
    settings: SettingsDep,
    logger: LoggerDep,
):
    """Creates a snapshot of the current configuration on the server."""
    logger.info("Manual backup creation requested.")
    return _create_backup(state, automation_manager, settings, logger)

@router.get("/backup/list", response_model=BackupListResponse)
async def list_backups(settings: SettingsDep, logger: LoggerDep):
    """Lists available server backups, newest first."""
    backup_dir = _get_backup_dir(settings)
    backups: list[BackupMetadata] = []
    if backup_dir.exists():
        candidates = sorted(
            (p for p in backup_dir.iterdir() if p.is_file() and BACKUP_ID_RE.match(p.name)),
            key=lambda p: p.name,
            reverse=True,
        )
        for path in candidates:
            try:
                backups.append(_backup_metadata(path))
            except OSError as e:
                logger.warning("Skipping unreadable backup %s: %s", path.name, e)
    return BackupListResponse(backups=backups)

@router.get("/backup/download/{backup_id}")
async def download_backup(backup_id: str, settings: SettingsDep, logger: LoggerDep):
    """Downloads a single backup file."""
    path = _resolve_backup_file(settings, backup_id)
    if not path.is_file():
        raise HTTPException(status_code=404, detail="Backup not found.")
    logger.info("Backup download requested: %s", backup_id)
    return Response(
        content=path.read_bytes(),
        media_type="application/json",
        headers={"Content-Disposition": f"attachment; filename={backup_id}"},
    )

@router.post("/backup/restore/{backup_id}", response_model=BackupRestoreResponse)
async def restore_backup(
    backup_id: str,
    state: StateManagerDep,
    mqtt: MQTTManagerDep,
    automation_manager: AutomationManagerDep,
    logger: LoggerDep,
    db: DatabaseDep,
    settings: SettingsDep,
    mode: Literal["replace", "merge"] = "replace",
):
    """Restores a server backup using the shared import logic."""
    path = _resolve_backup_file(settings, backup_id)
    if not path.is_file():
        raise HTTPException(status_code=404, detail="Backup not found.")

    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as e:
        logger.error("Failed to read backup %s: %s", backup_id, e)
        raise HTTPException(status_code=400, detail=f"Invalid backup file: {e}") from e
    if not isinstance(data, dict):
        raise HTTPException(status_code=400, detail="Invalid backup file format.")

    logger.info("Restoring backup %s (mode=%s)", backup_id, mode)
    auto_backup = _auto_backup(state, automation_manager, settings, logger) if mode == "replace" else None

    device_count, automation_count = await _apply_config(
        data,
        mode,
        state=state,
        mqtt=mqtt,
        automation_manager=automation_manager,
        logger=logger,
        db=db,
    )

    detail = f"{device_count} devices and {automation_count} automations restored."
    logger.info("Restore complete (%s): %s", mode, detail)
    return BackupRestoreResponse(mode=mode, detail=detail, auto_backup=auto_backup)

@router.delete("/backup/{backup_id}", response_model=StatusOk)
async def delete_backup(backup_id: str, settings: SettingsDep, logger: LoggerDep):
    """Deletes a single server backup."""
    path = _resolve_backup_file(settings, backup_id)
    if not path.is_file():
        raise HTTPException(status_code=404, detail="Backup not found.")
    path.unlink()
    logger.info("Backup deleted: %s", backup_id)
    return StatusOk()
