"""Stateful air-conditioner remotes — behaves like a real IR remote.

A physical AC remote keeps state: pressing "Temp +" sends the *whole* new state,
not a fixed code.  A flat list of "On Cool 25C" buttons cannot do that (76
buttons, and the user has to know which state the unit is in).

So an AC device created from a matched remote is stored as six **action**
buttons (power, temp −/+, mode, fan, swing).  Each press:

1. loads the device's current state from ``/data/kookong_ac_state.json``,
2. applies the action (toggle / step / cycle),
3. encodes the resulting frame through the native kksdk encoder,
4. transmits it and persists the new state.

The state file lives next to the other add-on data, so it survives restarts.
"""

from __future__ import annotations

import json
import logging
import os
import threading
from pathlib import Path
from typing import Any

logger = logging.getLogger("ir2mqtt")

AC_PROTOCOL = "kookong_ac"

TEMP_MIN = 16
TEMP_MAX = 30
MODE_COUNT = 5
WIND_COUNT = 4

# action -> (function_id, icon, label)
AC_ACTIONS: dict[str, tuple[int, str, str]] = {
    "power": (1, "power", "Power"),
    "temp_down": (4, "thermometer-minus", "Temp \u2212"),
    "temp_up": (3, "thermometer-plus", "Temp +"),
    "mode": (2, "tune", "Mode"),
    "fan": (5, "fan", "Fan"),
    "swing": (6, "arrow-oscillating", "Swing"),
}

_state_lock = threading.Lock()


def _state_file() -> Path:
    return Path(os.environ.get("KKOOKONG_AC_STATE", "/data/kookong_ac_state.json"))


def default_state() -> dict[str, int]:
    return {
        "power": 1,
        "mode": 1,
        "temperature": 25,
        "wind_speed": 0,
        "lr_wind_mode": 0,
        "ud_wind_mode": 0,
    }


def _read_all() -> dict[str, Any]:
    path = _state_file()
    try:
        if path.exists():
            with open(path, encoding="utf-8") as fh:
                data = json.load(fh)
            if isinstance(data, dict):
                return data
    except Exception:
        logger.debug("could not read AC state file %s", path, exc_info=True)
    return {}


def _write_all(data: dict[str, Any]) -> None:
    path = _state_file()
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        tmp = path.with_suffix(".tmp")
        with open(tmp, "w", encoding="utf-8") as fh:
            json.dump(data, fh)
        tmp.replace(path)
    except Exception:
        logger.warning("could not persist AC state to %s", path, exc_info=True)


def load_state(device_id: str) -> dict[str, int]:
    with _state_lock:
        stored = _read_all().get(device_id)
    state = default_state()
    if isinstance(stored, dict):
        state.update({k: int(v) for k, v in stored.items() if k in state})
    return state


def save_state(device_id: str, state: dict[str, int]) -> None:
    with _state_lock:
        data = _read_all()
        data[device_id] = state
        _write_all(data)


def apply_action(state: dict[str, int], action: str) -> tuple[dict[str, int], int]:
    """Return the new state and the function_id used to encode it."""
    new = dict(default_state())
    new.update(state)

    if action == "power":
        new["power"] = 0 if new.get("power") else 1
        return new, 1
    if action == "temp_up":
        new["temperature"] = min(TEMP_MAX, int(new.get("temperature", 25)) + 1)
        return new, 3
    if action == "temp_down":
        new["temperature"] = max(TEMP_MIN, int(new.get("temperature", 25)) - 1)
        return new, 4
    if action == "mode":
        new["mode"] = (int(new.get("mode", 0)) + 1) % MODE_COUNT
        return new, 2
    if action == "fan":
        new["wind_speed"] = (int(new.get("wind_speed", 0)) + 1) % WIND_COUNT
        return new, 5
    if action == "swing":
        new["ud_wind_mode"] = 0 if new.get("ud_wind_mode") else 1
        return new, 6 if new["ud_wind_mode"] else 7

    raise ValueError(f"unknown AC action: {action}")


def action_buttons(
    category_id: int, brand_id: str, remote_id: str
) -> list[dict[str, Any]]:
    """The six action buttons that make up a stateful AC device."""
    buttons = []
    for action, (_fid, icon, label) in AC_ACTIONS.items():
        buttons.append(
            {
                "name": label,
                "icon": icon,
                "code": {
                    "protocol": AC_PROTOCOL,
                    "payload": {
                        "category_id": int(category_id),
                        "brand_id": brand_id,
                        "remote_id": remote_id,
                        "action": action,
                    },
                },
            }
        )
    return buttons


def is_ac_button_code(code: Any) -> bool:
    if code is None:
        return False
    protocol = code.get("protocol") if isinstance(code, dict) else getattr(code, "protocol", None)
    return protocol == AC_PROTOCOL


async def send_action(
    device_id: str,
    payload: dict[str, Any],
    mqtt,
    targets: list[str] | str | None,
) -> dict[str, Any]:
    """Apply one AC key press and transmit the resulting state frame."""
    from .match_service import get_match_service

    action = str(payload.get("action") or "")
    state = load_state(device_id)
    new_state, function_id = apply_action(state, action)

    service = get_match_service()
    built = service.test_code(
        int(payload["category_id"]),
        str(payload["brand_id"]),
        str(payload["remote_id"]),
        power=new_state["power"],
        mode=new_state["mode"],
        temperature=new_state["temperature"],
        wind_speed=new_state["wind_speed"],
        lr_wind_mode=new_state["lr_wind_mode"],
        ud_wind_mode=new_state["ud_wind_mode"],
        function_id=function_id,
    )
    if built is None:
        raise RuntimeError(f"could not encode AC action '{action}'")

    await mqtt.send_ir_code(built["code"], target=targets)
    save_state(device_id, new_state)

    return {
        "status": "sent",
        "action": action,
        "button_name": built["button_name"],
        "function_id": function_id,
        "state": new_state,
        "targets": targets if isinstance(targets, list) else ([targets] if targets else []),
    }
