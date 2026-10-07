"""Tests for the native Daikin encoder (``backend/kookong/ac_native.py``).

The frame below was verified on real hardware: a Daikin AC ignores the Kookong
Daikin variant but reacts to the ESPHome ``platform: daikin`` frame, so these
numbers are the contract the encoder must keep.
"""

from __future__ import annotations

import pytest

from backend.kookong.ac_native import (  # noqa: E402
    BASELINE_35,
    daikin_code,
    daikin_state,
    daikin_state_frame,
    daikin_timings,
    decode_timings,
)

# Verified on the real unit: the AC *turns on* with the first frame and *turns off*
# with the second one.  Both are the 19-byte state frame (``state[16:35]``).
VERIFIED_FRAMES = {
    "on": (
        dict(power=True, mode="cool", temp_c=25, fan="auto", swing="off"),
        "11 da 27 00 00 31 32 00 a0 00 00 06 60 00 00 c0 00 00 3b",
        432940,  # total |timings| in µs
    ),
    "off": (
        dict(power=False, mode="cool", temp_c=25, fan="auto", swing="off"),
        "11 da 27 00 00 00 32 00 a0 00 00 06 60 00 00 c0 00 00 0a",
        426880,  # on-frame minus 6 one-bits (mode byte 0x31->0x00, checksum 0x3b->0x0a)
    ),
}

@pytest.mark.parametrize("name", sorted(VERIFIED_FRAMES))
def test_state_is_35_bytes_with_checksum(name):
    kwargs, _vector, _total = VERIFIED_FRAMES[name]
    state = daikin_state(**kwargs)
    assert len(state) == 35
    assert len(BASELINE_35) == 35
    assert state[34] == sum(state[16:34]) & 0xFF

@pytest.mark.parametrize("name", sorted(VERIFIED_FRAMES))
def test_state_frame_matches_verified_vector(name):
    kwargs, vector, _total = VERIFIED_FRAMES[name]
    assert daikin_state_frame(**kwargs).hex(" ") == vector
    # The verified 19-byte frame is the tail of the 35-byte buffer.
    assert daikin_state(**kwargs)[16:35].hex(" ") == vector

@pytest.mark.parametrize("name", sorted(VERIFIED_FRAMES))
def test_timings_length_and_total(name):
    kwargs, _vector, total = VERIFIED_FRAMES[name]
    timings = daikin_timings(**kwargs)
    assert len(timings) == 572
    assert sum(abs(value) for value in timings) == total
    # mark positive, space negative, microseconds
    assert all(timings[index] > 0 for index in range(0, len(timings), 2))
    assert all(timings[index] < 0 for index in range(1, len(timings), 2))

@pytest.mark.parametrize("name", sorted(VERIFIED_FRAMES))
def test_decode_round_trip(name):
    kwargs, vector, _total = VERIFIED_FRAMES[name]
    timings = daikin_timings(**kwargs)
    state = decode_timings(timings)
    assert state == daikin_state(**kwargs)
    assert state[16:35].hex(" ") == vector

def test_off_clears_the_mode_byte_only():
    """power=False -> state[21] = 0x00 but state[22] stays temp_c << 1.

    daikin.cpp: CLIMATE_MODE_OFF returns DAIKIN_MODE_OFF without the ON bit,
    while temperature_()'s default branch still packs the temperature.
    """
    on = daikin_state(power=True, mode="cool", temp_c=25)
    off = daikin_state(power=False, mode="cool", temp_c=25)
    assert on[21] == 0x31  # DAIKIN_MODE_COOL | DAIKIN_MODE_ON
    assert off[21] == 0x00  # no DAIKIN_MODE_ON
    assert off[22] == 25 << 1
    assert off[22] == on[22]
    assert off[24] == 0xA0 and off[25] == 0x00
    assert off[34] == sum(off[16:34]) & 0xFF == 0x0A

def test_temp_forced_for_dry_and_fan():
    assert daikin_state(mode="dry", temp_c=25)[22] == 0xC0
    assert daikin_state(mode="fan", temp_c=25)[22] == 0x32

def test_temperature_is_clamped():
    assert daikin_state(temp_c=99)[22] == 30 << 1
    assert daikin_state(temp_c=5)[22] == 10 << 1

@pytest.mark.parametrize(
    ("fan", "high"),
    [("auto", 0xA0), ("silent", 0xB0), ("1", 0x30), ("2", 0x40), ("3", 0x50), ("4", 0x60), ("5", 0x70)],
)
def test_fan_nibble(fan, high):
    state = daikin_state(fan=fan)
    assert (state[24], state[25]) == (high, 0x00)

@pytest.mark.parametrize(
    ("swing", "expected"),
    [("off", (0xA0, 0x00)), ("vertical", (0xAF, 0x00)), ("horizontal", (0xA0, 0x0F)), ("both", (0xAF, 0x0F))],
)
def test_swing_bits(swing, expected):
    state = daikin_state(fan="auto", swing=swing)
    assert (state[24], state[25]) == expected

def test_mode_bytes():
    assert daikin_state(power=True, mode="cool")[21] == 0x31
    assert daikin_state(power=True, mode="heat")[21] == 0x41
    assert daikin_state(power=True, mode="dry")[21] == 0x21
    assert daikin_state(power=True, mode="fan")[21] == 0x61
    assert daikin_state(power=True, mode="auto")[21] == 0x01

def test_daikin_code_shape():
    code = daikin_code(power=True, mode="cool", temp_c=25, fan="auto")
    assert code["protocol"] == "raw"
    assert code["payload"]["frequency"] == 38000
    assert len(code["payload"]["timings"]) == 572

def test_invalid_arguments_raise():
    with pytest.raises(ValueError):
        daikin_state(mode="turbo")
    with pytest.raises(ValueError):
        daikin_state(fan="hurricane")
    with pytest.raises(ValueError):
        daikin_state(swing="sideways")
