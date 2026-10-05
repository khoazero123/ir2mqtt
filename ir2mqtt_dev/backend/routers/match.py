"""Match wizard API — find the right remote for a device, like the phone app.

Flow: categories -> brands -> candidates -> test (transmit) -> save as device.
Candidate order and test codes come from the Kookong source database and the
native kksdk encoder, so the experience mirrors the original OnePlus/OPPO app.
"""

import logging
import uuid

from fastapi import APIRouter, HTTPException, Query
from pydantic import BaseModel

from ..dependencies import (
    DatabaseDep,
    LoggerDep,
    MQTTManagerDep,
    StateManagerDep,
)
from ..kookong.match_service import get_match_service
from ..models import IRButton, IRCode, IRDevice, StatusOk
from ..websockets import broadcast_ws

logger = logging.getLogger("ir2mqtt")

router = APIRouter(prefix="/api/match", tags=["match"])


class MatchTestPayload(BaseModel):
    category_id: int
    brand_id: str
    remote_id: str
    target: list[str] | str | None = None
    # Air-conditioner state + which key is being pressed (phone-app key model).
    power: int | None = None
    mode: int | None = None
    temperature: int | None = None
    wind_speed: int | None = None
    lr_wind_mode: int | None = None
    ud_wind_mode: int | None = None
    function_id: int | None = None


class MatchSavePayload(BaseModel):
    category_id: int
    brand_id: str
    remote_id: str
    name: str | None = None
    icon: str = "remote-tv"
    target_bridges: list[str] = []
    # Air conditioners become a stateful 6-key remote by default (power,
    # temp -/+, mode, fan, swing).  Set false to store the flat state grid.
    as_ac: bool | None = None


def _service():
    service = get_match_service()
    if not service.available():
        raise HTTPException(
            503,
            "Kookong match wizard unavailable: kkoffline.db or libkksdk_host.so missing",
        )
    return service


@router.get("/available")
async def match_available():
    service = get_match_service()
    return {"available": service.available()}


@router.get("/categories")
async def match_categories():
    return _service().categories()


@router.get("/brands")
async def match_brands(category_id: int = Query(...)):
    return _service().brands(category_id)


@router.get("/candidates")
async def match_candidates(
    category_id: int = Query(...),
    brand_id: str = Query(...),
    country: str | None = Query(None),
    limit: int = Query(300, ge=1, le=2000),
):
    return _service().candidates(category_id, brand_id, country, limit)

@router.get("/countries")
async def match_countries(
    category_id: int = Query(...),
    brand_id: str = Query(...),
):
    """Countries with data for this brand, plus the one to preselect."""
    return _service().countries(category_id, brand_id)


@router.post("/test")
async def match_test(
    payload: MatchTestPayload,
    mqtt: MQTTManagerDep,
    logger: LoggerDep,
):
    """Encode and transmit one candidate's test code."""
    service = _service()
    built = service.test_code(
        payload.category_id,
        payload.brand_id,
        payload.remote_id,
        power=payload.power,
        mode=payload.mode,
        temperature=payload.temperature,
        wind_speed=payload.wind_speed,
        lr_wind_mode=payload.lr_wind_mode,
        ud_wind_mode=payload.ud_wind_mode,
        function_id=payload.function_id,
    )
    if built is None:
        raise HTTPException(422, "Could not build a test code for this remote")

    logger.info(
        "Match test: sending '%s' for remote %s (category %s, brand %s) to %s",
        built["button_name"],
        payload.remote_id,
        payload.category_id,
        payload.brand_id,
        payload.target or "broadcast",
    )
    await mqtt.send_ir_code(built["code"], target=payload.target)

    targets = payload.target if isinstance(payload.target, list) else (
        [payload.target] if payload.target else []
    )
    return {
        "sent": True,
        "button_name": built["button_name"],
        "is_ac": built["is_ac"],
        "state": built.get("state"),
        "function_id": built.get("function_id"),
        "targets": targets,
    }


@router.get("/ac_state")
async def match_ac_state(device_id: str = Query(...)):
    """Current state of a stateful AC device (for the remote UI)."""
    from ..kookong.ac_remote import load_state

    return load_state(device_id)


@router.post("/save", response_model=IRDevice)
async def match_save(
    payload: MatchSavePayload,
    db: DatabaseDep,
    mqtt: MQTTManagerDep,
    state: StateManagerDep,
    logger: LoggerDep,
):
    """Save a matched remote as an IR2MQTT device.

    Air conditioners become a stateful six-key remote (power, temp -/+, mode,
    fan, swing) that keeps its own state — just like the physical handset.
    """
    from ..kookong.ac_remote import action_buttons, default_state, save_state

    service = _service()
    is_ac = payload.as_ac if payload.as_ac is not None else service.is_ac_remote(
        payload.category_id, payload.brand_id, payload.remote_id
    )

    if is_ac:
        buttons = action_buttons(payload.category_id, payload.brand_id, payload.remote_id)
    else:
        buttons = service.buttons_for(payload.category_id, payload.brand_id, payload.remote_id)
    if not buttons:
        raise HTTPException(422, "This remote produced no usable buttons")

    name = payload.name or f"Kookong {payload.remote_id}"
    logger.info("Match save: creating device '%s' with %d buttons.", name, len(buttons))

    initial_buttons = []
    for i, btn in enumerate(buttons):
        code = btn.get("code") or {}
        initial_buttons.append(
            IRButton(
                id=str(uuid.uuid4())[:8],
                ordering=i,
                name=btn["name"],
                icon=btn.get("icon") or "remote",
                code=IRCode(
                    protocol=code.get("protocol", "raw"),
                    payload=code.get("payload", {}),
                ),
            )
        )

    dev = IRDevice(
        id=str(uuid.uuid4())[:8],
        name=name,
        icon=payload.icon.replace("mdi:", "").replace("mdi-", ""),
        target_bridges=payload.target_bridges,
        allowed_bridges=[],
        buttons=initial_buttons,
        ordering=len(state.devices),
    )

    await db.save_device(dev)
    await db.commit()
    state.devices.append(dev)
    if is_ac:
        save_state(dev.id, default_state())
    await mqtt.integration.on_device_updated(dev, mqtt)
    await broadcast_ws({"type": "devices_updated"})
    logger.info("Match save: device '%s' created with id %s.", dev.name, dev.id)
    return dev


@router.get("/status", response_model=StatusOk)
async def match_status():
    return StatusOk()
