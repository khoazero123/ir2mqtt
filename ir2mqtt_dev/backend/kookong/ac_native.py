"""Native Daikin air-conditioner encoder (ESPHome ``platform: daikin`` layout).

Why this exists
---------------
The Kookong database encodes Daikin AC remotes with a *different* variant than
the one our units accept: mode byte ``0x40``/``0x48``, fan nibble ``0xaf`` and a
slightly different fixed frame.  Real-hardware testing showed those frames are
ignored, while a frame built exactly like ESPHome's ``daikin`` component is
accepted::

    11 da 27 00 00 31 32 00 a0 00 00 06 60 00 00 c0 00 00 3b   # COOL + ON, 25 °C, fan auto

So for a remote whose brand is Daikin we build the frame here instead of using
the Kookong bytes.  Everything below mirrors
``esphome/components/daikin/daikin.cpp`` + ``daikin.h`` (see the constants they
expose: ``DAIKIN_MODE_*``, ``DAIKIN_FAN_*``, ``DAIKIN_TEMP_MIN/MAX`` and the
timing table).  Kept dependency-free so it can be tested without the native
``libkksdk_host.so``.

Byte layout (35 bytes, three frames of 8 + 8 + 19 bytes)
--------------------------------------------------------
====================  ==========================================================
``state[21]``         mode: cool ``0x30``, heat ``0x40``, dry ``0x20``,
                      fan ``0x60``, auto ``0x00``, plus ``DAIKIN_MODE_ON``
                      ``0x01`` while powered — and ``0x00`` (no ON bit) when off.
``state[22]``         ``temp_c << 1`` for cool/heat/auto; ESPHome *forces*
                      ``0x32`` for fan-only and ``0xC0`` for dry.
``state[24:26]``      big-endian 16-bit fan speed: auto ``0xA0``, silent
                      ``0xB0``, ``1..5`` = ``0x30/0x40/0x50/0x60/0x70``, with
                      the swing bits OR-ed in *before* the split (vertical
                      ``0x0F00``, horizontal ``0x000F``, both ``0x0F0F``).
``state[34]``         checksum: ``sum(state[16:34]) & 0xFF``.
====================  ==========================================================

Timings are emitted LSB-first per byte (``for mask in 1, 2, 4, ... 128``) and
signed for the IR2MQTT raw protocol: mark positive, space negative, microseconds.
"""

from __future__ import annotations

from typing import Any

DAIKIN = {
    "frequency": 38000,
    "header_mark": 3360,
    "header_space": 1760,
    "bit_mark": 520,
    "one_space": 1370,
    "zero_space": 360,
    "message_space": 32300,
}

# Fixed part of the frame.  ``state[20], state[21], state[22], state[24],
# state[25], state[34]`` are filled in by :func:`daikin_state` (they are zero
# here, exactly like the ``remote_state`` initialiser in ``daikin.cpp``).
BASELINE_35 = bytes(
    [
        0x11, 0xDA, 0x27, 0x00, 0xC5, 0x00, 0x00, 0xD7,
        0x11, 0xDA, 0x27, 0x00, 0x42, 0x49, 0x05, 0xA2,
        0x11, 0xDA, 0x27, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x06, 0x60, 0x00, 0x00, 0xC0,
        0x00, 0x00, 0x00,
    ]
)

# daikin.h
MODE_ON = 0x01
MODE_AUTO = 0x00
MODE_COOL = 0x30
MODE_HEAT = 0x40
MODE_DRY = 0x20
MODE_FAN = 0x60

FAN_AUTO = 0xA0
FAN_SILENT = 0xB0
FAN_1 = 0x30
FAN_2 = 0x40
FAN_3 = 0x50
FAN_4 = 0x60
FAN_5 = 0x70

TEMP_MIN = 10
TEMP_MAX = 30

# Frame boundaries: 8 + 8 + 19 bytes.
_FRAMES = ((0, 8), (8, 16), (16, 35))

_MODE_BYTES = {
    "auto": MODE_AUTO,
    "cool": MODE_COOL,
    "heat": MODE_HEAT,
    "dry": MODE_DRY,
    "fan": MODE_FAN,
    "fan_only": MODE_FAN,
    "fanonly": MODE_FAN,
}

# ESPHome maps its climate fan levels like this:
#   QUIET -> SILENT, LOW -> FAN_1, MEDIUM -> FAN_3, HIGH -> FAN_5, AUTO -> AUTO
_FAN_BYTES = {
    "auto": FAN_AUTO,
    "silent": FAN_SILENT,
    "quiet": FAN_SILENT,
    "low": FAN_1,
    "medium": FAN_3,
    "high": FAN_5,
    "1": FAN_1,
    "2": FAN_2,
    "3": FAN_3,
    "4": FAN_4,
    "5": FAN_5,
}

_SWING_BITS = {
    "off": 0x0000,
    "vertical": 0x0F00,
    "horizontal": 0x000F,
    "both": 0x0F0F,
}

def _mode_key(mode: Any) -> str:
    if mode is None:
        return "auto"
    key = str(mode).strip().lower().replace("-", "_").replace(" ", "_")
    aliases = {
        "heat_cool": "auto",
        "cooling": "cool",
        "heating": "heat",
        "fanon": "fan",
        "fan_only": "fan",
    }
    key = aliases.get(key, key)
    if key not in _MODE_BYTES:
        raise ValueError(f"unsupported Daikin mode: {mode!r}")
    return key

def _fan_bits(fan: Any) -> int:
    if fan is None:
        key = "auto"
    elif isinstance(fan, int):
        key = str(fan)
    else:
        key = str(fan).strip().lower()
    if key not in _FAN_BYTES:
        raise ValueError(f"unsupported Daikin fan speed: {fan!r}")
    return _FAN_BYTES[key] << 8

def _swing_bits(swing: Any) -> int:
    if swing is None:
        key = "off"
    else:
        key = str(swing).strip().lower()
    if key not in _SWING_BITS:
        raise ValueError(f"unsupported Daikin swing mode: {swing!r}")
    return _SWING_BITS[key]

def _clamp_temperature(temp_c: Any) -> int:
    """ESPHome: ``roundf(clamp(target_temperature, 10, 30))``."""
    try:
        value = float(temp_c)
    except (TypeError, ValueError):
        value = 25.0
    value = max(float(TEMP_MIN), min(float(TEMP_MAX), value))
    return int(round(value))

def _mode_byte(mode_key: str, power: bool) -> int:
    if not power:
        # daikin.cpp: CLIMATE_MODE_OFF -> DAIKIN_MODE_OFF (0x00), no ON bit.
        return 0x00
    return _MODE_BYTES[mode_key] | MODE_ON

def _temperature_byte(mode_key: str, temp_c: Any) -> int:
    if mode_key in ("fan", "fan_only", "fanonly"):
        return 0x32
    if mode_key == "dry":
        return 0xC0
    return _clamp_temperature(temp_c) << 1

def daikin_state(
    power: bool = True,
    mode: str = "cool",
    temp_c: int | float = 25,
    fan: Any = "auto",
    swing: str = "off",
) -> bytes:
    """Build the 35-byte Daikin state frame (checksum included)."""
    state = bytearray(BASELINE_35)
    mode_key = _mode_key(mode)

    state[21] = _mode_byte(mode_key, bool(power))
    state[22] = _temperature_byte(mode_key, temp_c)

    fan_speed = _fan_bits(fan) | _swing_bits(swing)
    state[24] = (fan_speed >> 8) & 0xFF
    state[25] = fan_speed & 0xFF

    state[34] = sum(state[16:34]) & 0xFF
    return bytes(state)

def daikin_state_frame(
    power: bool = True,
    mode: str = "cool",
    temp_c: int | float = 25,
    fan: Any = "auto",
    swing: str = "off",
) -> bytes:
    """The 19-byte state frame that is actually transmitted last (= ``state[16:35]``).

    ESPHome reads exactly this frame back (``DAIKIN_STATE_FRAME_SIZE == 19``), so
    it is the natural unit for the hardware-verified vector::

        11 da 27 00 00 31 32 00 a0 00 00 06 60 00 00 c0 00 00 3b
    """
    return daikin_state(power=power, mode=mode, temp_c=temp_c, fan=fan, swing=swing)[16:35]

def _frame_timings(frame: bytes) -> list[int]:
    """One frame: header, then every byte LSB-first, then the 32.3 ms gap."""
    out = [DAIKIN["header_mark"], -DAIKIN["header_space"]]
    for byte in frame:
        mask = 1
        while mask <= 0x80:
            bit = 1 if byte & mask else 0
            out.append(DAIKIN["bit_mark"])
            out.append(-(DAIKIN["one_space"] if bit else DAIKIN["zero_space"]))
            mask <<= 1
    out.append(DAIKIN["bit_mark"])
    out.append(-DAIKIN["message_space"])
    return out

def daikin_timings(
    power: bool = True,
    mode: str = "cool",
    temp_c: int | float = 25,
    fan: Any = "auto",
    swing: str = "off",
) -> list[int]:
    """Signed microsecond timings for the whole 3-frame transmission.

    Note: ESPHome terminates the *last* frame with ``space(0)``; the verified
    on-device frame this module reproduces keeps the 32.3 ms gap after every
    frame (572 entries, 432940 µs total), so that trailing gap is intentional.
    """
    state = daikin_state(power=power, mode=mode, temp_c=temp_c, fan=fan, swing=swing)
    timings: list[int] = []
    for start, end in _FRAMES:
        timings.extend(_frame_timings(state[start:end]))
    return timings

def daikin_code(
    power: bool = True,
    mode: str = "cool",
    temp_c: int | float = 25,
    fan: Any = "auto",
    swing: str = "off",
) -> dict:
    """IR2MQTT ``raw`` code, ready to hand to ``mqtt.send_ir_code``."""
    return {
        "protocol": "raw",
        "payload": {
            "timings": daikin_timings(power=power, mode=mode, temp_c=temp_c, fan=fan, swing=swing),
            "frequency": DAIKIN["frequency"],
        },
    }

def decode_timings(timings: list[int]) -> bytes:
    """Inverse of :func:`daikin_timings` — used by the tests as a sanity check.

    Walks the three frames structurally (8 + 8 + 19 bytes) instead of scanning
    for bit pairs, so the header/trailer pairs cannot be mistaken for data.  A
    space longer than 800 µs is a 1-bit, shorter is a 0-bit — the two spaces are
    1370 µs and 360 µs, exactly how ESPHome's receiver decides.
    """
    state = bytearray()
    pos = 0
    for size in (8, 8, 19):
        if pos + 2 > len(timings):
            raise ValueError("truncated Daikin timings: missing frame header")
        if timings[pos] != DAIKIN["header_mark"] or abs(timings[pos + 1]) != DAIKIN["header_space"]:
            raise ValueError("unexpected Daikin frame header")
        pos += 2

        for _ in range(size):
            byte = 0
            for bit in range(8):
                if pos + 2 > len(timings):
                    raise ValueError("truncated Daikin timings: missing bit")
                if abs(timings[pos + 1]) > 800:
                    byte |= 1 << bit
                pos += 2
            state.append(byte)

        if pos + 2 > len(timings):
            raise ValueError("truncated Daikin timings: missing frame trailer")
        if timings[pos] != DAIKIN["bit_mark"] or abs(timings[pos + 1]) != DAIKIN["message_space"]:
            raise ValueError("unexpected Daikin frame trailer")
        pos += 2

    if pos != len(timings):
        raise ValueError("trailing data after the Daikin frames")
    return bytes(state)
