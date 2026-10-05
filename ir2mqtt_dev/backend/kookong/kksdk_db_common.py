#!/usr/bin/env python3
"""Shared helpers for local Kookong DB query/debug tools."""
from __future__ import annotations

import base64
import ctypes
import json
import math
import os
import re
import sqlite3
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
DATA_DIR = ROOT / "data"
DEFAULT_KKOFFLINE_DB = DATA_DIR / "kkoffline.db"
DEFAULT_IRDATA_DB = DATA_DIR / "irData.db"
DEFAULT_STATE_FILE = ROOT / ".cache" / "kkoffline_ac_state.json"
STREAMHELPER2_KEY = 0x0133A133

FUNCTION_ALIASES = {
    "power": 1,
    "mode": 2,
    "temperature_up": 3,
    "temp_up": 3,
    "temperature_down": 4,
    "temp_down": 4,
    "wind": 5,
    "wind_speed": 5,
    "ud_wind": 6,
    "lr_wind": 7,
    "timer_on": 9,
    "timer_off": 10,
}

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")


def ac_state_key(remote_id: str | int) -> str:
    return f"remote:{remote_id}"


def default_ac_state() -> dict[str, int]:
    return {
        "power": 0,
        "mode": 0,
        "temperature": 26,
        "wind_speed": 0,
        "lr_wind_mode": 0,
        "ud_wind_mode": 0,
    }


def apply_ac_key_to_state(state: dict[str, int], key: str, fid: int) -> None:
    normalized = key.lower().replace("-", "_")
    if normalized == "power" or fid == 1:
        state["power"] = 0 if int(state.get("power", 0)) else 1
    elif normalized == "mode" or fid == 2:
        state["mode"] = (int(state.get("mode", 0)) + 1) % 5
    elif normalized in {"temperature_up", "temp_up"} or fid == 3:
        state["temperature"] = min(30, int(state.get("temperature", 26)) + 1)
    elif normalized in {"temperature_down", "temp_down"} or fid == 4:
        state["temperature"] = max(16, int(state.get("temperature", 26)) - 1)
    elif normalized in {"wind", "wind_speed"} or fid == 5:
        state["wind_speed"] = (int(state.get("wind_speed", 0)) + 1) % 6
    elif normalized == "lr_wind" or fid == 7:
        state["lr_wind_mode"] = (int(state.get("lr_wind_mode", 0)) + 1) % 8
    elif normalized == "ud_wind" or fid == 6:
        state["ud_wind_mode"] = (int(state.get("ud_wind_mode", 0)) + 1) % 8


def apply_state_overrides(state: dict[str, int], overrides: dict[str, int | None]) -> None:
    for name, value in overrides.items():
        if value is not None:
            state[name] = int(value)


def config_tags(lines: list[str]) -> dict[int, str]:
    tags: dict[int, str] = {}
    for line in lines:
        if "|" not in line:
            continue
        tag, value = line.split("|", 1)
        try:
            tags[int(tag)] = value.strip()
        except ValueError:
            continue
    return tags


def parse_ac_ext_state(value: str | None) -> dict[int, int]:
    result: dict[int, int] = {}
    if not value:
        return result
    for part in value.split("/"):
        fields = [field.strip() for field in part.split(",")]
        if len(fields) < 2 or not fields[0]:
            continue
        try:
            result[int(fields[0])] = int(fields[1])
        except ValueError:
            continue
    return result


def format_ac_ext_state(values: dict[int, int]) -> str:
    return "/".join(f"{key},{values[key]}" for key in sorted(values))


def _first_int(value: str) -> int | None:
    digits = ""
    for ch in value.strip():
        if ch.isdigit() or (ch == "-" and not digits):
            digits += ch
        elif digits and digits != "-":
            break
    if not digits or digits == "-":
        return None
    return int(digits)


def _range_values(value: str) -> list[int]:
    first = value.split("$", 1)[0].split(",", 1)[0].strip()
    if "-" in first:
        start_text, end_text = first.split("-", 1)
        try:
            start = int(start_text)
            end = int(end_text)
        except ValueError:
            return []
        if start <= end:
            return list(range(start, end + 1))
        return list(range(start, end - 1, -1))
    parsed = _first_int(first)
    return [] if parsed is None else [parsed]


def default_ac_ext_state(lines: list[str]) -> dict[int, int]:
    tags = config_tags(lines)
    defaults: dict[int, int] = {}
    for segment in tags.get(1515, "").split("@"):
        fields = segment.split("|")
        if len(fields) < 2:
            continue
        try:
            ext_id = int(fields[0])
        except ValueError:
            continue
        default = _first_int(fields[1])
        if default is not None:
            defaults[ext_id] = default

    result: dict[int, int] = {}
    for part in tags.get(603, "").split(","):
        try:
            ext_id = int(part.strip())
        except ValueError:
            continue
        if ext_id in defaults:
            result[ext_id] = defaults[ext_id]
    if 10 in defaults:
        result[10] = 0
    return result


def apply_ac_ext_key(ext_state: dict[int, int], lines: list[str], fid: int) -> None:
    tags = config_tags(lines)
    allowed: list[int] = []
    for segment in tags.get(1515, "").split("@"):
        fields = segment.split("|")
        if len(fields) < 2:
            continue
        try:
            ext_id = int(fields[0])
        except ValueError:
            continue
        if ext_id == fid:
            allowed = _range_values(fields[1])
            break
    if not allowed:
        return
    current = ext_state.get(fid, allowed[0])
    if 1 in allowed and current != 1:
        ext_state[fid] = 1
    else:
        choices = [value for value in allowed if value != current]
        ext_state[fid] = choices[0] if choices else allowed[0]


STATE_FIELD_BY_FUNCTION_ID = {
    1: "power",
    2: "mode",
    3: "temperature",
    5: "wind_speed",
    6: "ud_wind_mode",
    7: "lr_wind_mode",
}


def _condition_matches(token: str, state: dict[str, int], ext_state: dict[int, int]) -> bool:
    parts = [part.strip() for part in token.split(",", 1)]
    if len(parts) != 2 or not parts[0]:
        return True
    try:
        key = int(parts[0])
    except ValueError:
        return True
    if key in ext_state:
        current = ext_state[key]
    else:
        field = STATE_FIELD_BY_FUNCTION_ID.get(key)
        if field is None:
            return True
        current = int(state.get(field, 0))
    allowed = _range_values(parts[1])
    return not allowed or current in allowed


def apply_ac_state_rules(
    state: dict[str, int],
    lines: list[str],
    ext_string: str | None,
    function_id: int | None = None,
) -> None:
    ext_state = parse_ac_ext_state(ext_string)
    if not ext_state:
        return
    tags = config_tags(lines)
    for segment in tags.get(600, "").split("|"):
        if "*" not in segment:
            continue
        lhs, rhs = segment.split("*", 1)
        lhs_parts = [part for part in lhs.split("&") if part.strip()]
        if function_id is not None and lhs_parts:
            try:
                trigger_id = int(lhs_parts[0])
            except ValueError:
                trigger_id = None
            if trigger_id != function_id:
                continue
        conditions = lhs_parts[1:]
        if any(not _condition_matches(condition, state, ext_state) for condition in conditions):
            continue
        for assignment in rhs.split("*"):
            fields = [field.strip() for field in assignment.split(",", 1)]
            if len(fields) != 2:
                continue
            try:
                function_id = int(fields[0])
                value = int(fields[1].split("-", 1)[0])
            except ValueError:
                continue
            field = STATE_FIELD_BY_FUNCTION_ID.get(function_id)
            if field is not None:
                state[field] = value


def has_normal_wave_config(lines: list[str]) -> bool:
    tags = config_tags(lines)
    return 300 in tags and (301 in tags or 309 in tags)


def ac_key_override_frames(lines: list[str], frame_size: int) -> list[bytes]:
    tags = config_tags(lines)
    value = tags.get(1002)
    if not value or frame_size <= 0:
        return []
    text = value.strip()
    if len(text) % 2 != 0 or any(ch not in "0123456789abcdefABCDEF" for ch in text):
        return []
    raw = bytes.fromhex(text)
    if raw and raw[0] == len(raw) - 1:
        raw = raw[1:]
    if not raw:
        return []
    return [raw]


def lines_without_delay_codes(lines: list[str]) -> list[str]:
    return [line for line in lines if line.split("|", 1)[0] not in {"303", "311", "312"}]


def _parse_status_ranges(value: str) -> list[tuple[int, int, int]]:
    ranges: list[tuple[int, int, int]] = []
    for part in value.split("$"):
        text = part.strip()
        if not text:
            continue
        if "-" not in text:
            continue
        start_text, rest = text.split("-", 1)
        if "," in rest:
            end_text, step_text = rest.split(",", 1)
        else:
            end_text, step_text = rest, "1"
        try:
            ranges.append((int(start_text), int(end_text), int(step_text)))
        except ValueError:
            continue
    return ranges


def _format_range_matches(ranges: list[tuple[int, int, int]] | None, value: int) -> bool:
    if ranges is None:
        return True
    for start, end, step in ranges:
        if step == 0:
            step = 1
        if start <= value <= end and (value - start) % step == 0:
            return True
    return False


def _find_format(formats: list[tuple[list[tuple[int, int, int]] | None, int]], value: int) -> int | None:
    for ranges, format_id in formats:
        if _format_range_matches(ranges, value):
            return format_id
    return None


def select_ac_wave_format_id(
    lines: list[str],
    function_id: int,
    power: int,
    mode: int,
    temperature: int,
    wind_speed: int,
    ud_wind_mode: int,
    ext_string: str | None,
) -> int | None:
    tags = config_tags(lines)
    ext_state = parse_ac_ext_state(ext_string)
    if (
        tags.get(1002, "").startswith("124DB2FD0200")
        and "40&&6" in tags.get(1516, "")
        and function_id == 28
        and ext_state.get(28) == 1
    ):
        return 6
    mapping = tags.get(1516)
    if not mapping:
        return None
    states = ext_state
    states.update({
        1: int(power),
        2: int(mode),
        3: int(temperature),
        4: int(temperature),
        5: int(wind_speed),
        6: int(ud_wind_mode),
        7: int(ud_wind_mode),
    })
    key_map: dict[int, list[tuple[list[tuple[int, int, int]] | None, int]]] = {}
    status_map: dict[int, list[tuple[list[tuple[int, int, int]] | None, int]]] = {}
    for segment in mapping.split("|"):
        text = segment.strip()
        if not text:
            continue
        marker_at = text.find("@")
        marker_amp = text.find("&")
        markers = [pos for pos in (marker_at, marker_amp) if pos >= 0]
        if not markers:
            continue
        marker = min(markers)
        marker_ch = text[marker]
        next_amp = text.find("&", marker + 1)
        if next_amp < 0:
            continue
        try:
            selector_id = int(text[:marker].strip())
            format_id = int(text[next_amp + 1:].strip())
        except ValueError:
            continue
        status_text = text[marker + 1:next_amp].strip()
        ranges = _parse_status_ranges(status_text) if status_text else None
        target = key_map if marker_ch == "&" else status_map
        target.setdefault(selector_id, []).append((ranges, format_id))

    function_state = states.get(function_id, -1)
    selected = _find_format(key_map.get(function_id, []), function_state)
    if selected is not None:
        return selected
    for status_key, formats in status_map.items():
        status_value = states.get(status_key, -1)
        if status_value >= 0:
            selected = _find_format(formats, status_value)
            if selected is not None:
                return selected
    return None


def ac_custom_wave(
    lines: list[str],
    function_id: int,
    power: int,
    mode: int,
    temperature: int,
    wind_speed: int,
    ud_wind_mode: int,
    ext_string: str | None,
    allow_single_fallback: bool = False,
) -> list[int] | None:
    tags = config_tags(lines)
    value = tags.get(1510)
    if not value:
        return None
    states = parse_ac_ext_state(ext_string)
    states.update({
        1: int(power),
        2: int(mode),
        3: int(temperature),
        4: int(temperature),
        5: int(wind_speed),
        6: int(ud_wind_mode),
        7: int(ud_wind_mode),
    })
    function_state = states.get(function_id, -1)
    fallback: list[int] | None = None
    candidates: list[list[int]] = []
    for segment in value.split("|"):
        text = segment.strip()
        if not text:
            continue
        first = text.find("&")
        second = text.find("&", first + 1) if first >= 0 else -1
        if first < 0 or second < 0:
            continue
        try:
            fid = int(text[:first].strip())
        except ValueError:
            continue
        if fid != function_id:
            continue
        state_text = text[first + 1:second].strip()
        try:
            wave = _parse_ints(text[second + 1:])
        except ValueError:
            continue
        candidates.append(wave)
        if not state_text:
            fallback = wave
            continue
        for state_part in state_text.split(","):
            try:
                if int(state_part.strip()) == function_state:
                    if function_id == 1 and power == 1:
                        continue
                    return _adjust_ac_custom_wave(tags, wave)
            except ValueError:
                continue
    if fallback is not None:
        return _adjust_ac_custom_wave(tags, fallback)
    # Some OEM databases put a non-state marker between the ampersands
    # (notably Fujitsu power: ``1&1&<wave>``). When only one waveform exists
    # for the requested function, the Android SDK uses it for both power
    # transitions even if the resulting power state is 0.
    if allow_single_fallback and len(candidates) == 1 and not (function_id == 1 and power == 1):
        return _adjust_ac_custom_wave(tags, candidates[0])
    return None


def _adjust_ac_custom_wave(tags: dict[int, str], wave: list[int]) -> list[int]:
    if tags.get(1002, "").startswith("064DB2FD0200") and tags.get(305, "") == "312":
        replacements = {4453: 4400, 539: 540, 1629: 1600}
        return [replacements.get(value, value) for value in wave]
    if tags.get(1002, "").startswith("094FB0C03F80"):
        replacements = {4370: 4400, 4500: 4400, 530: 540, 550: 540}
        return [replacements.get(value, value) for value in wave]
    if tags.get(1002, "").startswith("04C001102E"):
        replacements = {8788: 8800, 4446: 4460, 546: 550, 1638: 1660, 988: 1000}
        return [replacements.get(value, value) for value in wave]
    return wave


def ac_synthetic_wave_lines(lines: list[str]) -> list[str] | None:
    tags = config_tags(lines)
    script = tags.get(1518, "")
    if "bytes[6] = (~bytes[5])" in script and "0x9D" in script and 1514 in tags:
        return [
            "300|4400,4450",
            "301|510,580",
            "302|510,1680",
            "303|-1&510,5280",
            "306|1",
            "307|1",
            "1508|2",
        ]
    return None


def render_ac_duration_pulses(
    lines: list[str],
    frames: list[bytes],
    function_id: int,
    power: int,
    mode: int,
    temperature: int,
    wind_speed: int,
    ud_wind_mode: int,
    ext_string: str | None,
    selected_key_lines: list[str] | None = None,
    allow_single_custom_wave_fallback: bool = False,
) -> tuple[list[list[int]], str]:
    """Render AC CodeHelper frames using the same format-selection order as OEM."""
    synthetic_lines = ac_synthetic_wave_lines(lines)
    custom_wave = None if synthetic_lines is not None else ac_custom_wave(
        lines,
        function_id,
        power,
        mode,
        temperature,
        wind_speed,
        ud_wind_mode,
        ext_string,
        allow_single_fallback=allow_single_custom_wave_fallback,
    )
    if custom_wave is not None:
        return [custom_wave], "RcRemoteControllerExt:1510"

    selected_lines = _ac_selected_wave_override(lines, selected_key_lines or [])
    controller_wave_lines = _ac_controller_wave_override(lines) if not selected_lines else lines
    wave_lines = synthetic_lines or (selected_lines if selected_lines else controller_wave_lines)
    source = (
        "synthetic_1514"
        if synthetic_lines is not None
        else ("RcRemoteKeyExt:selected" if selected_key_lines else "RcRemoteControllerExt")
    )
    rendered: list[list[int]] = []
    for frame in frames:
        scripted = apply_ac_wave_script(
            lines,
            frame,
            function_id,
            power,
            temperature,
            ext_string,
            mode=mode,
            wind_speed=wind_speed,
            ud_wind_mode=ud_wind_mode,
        )
        frame_wave_lines = wave_lines
        if synthetic_lines is not None and len(scripted) > 6:
            frame_wave_lines = [
                "300|4400,4450",
                "301|510,580",
                "302|510,1680",
                "303|5&510,5280,4400,4450|11&510,5280,4400,4450",
                "306|1",
            ]
        elif _is_fujitsu_fgc_function_one_wave(lines, function_id):
            frame_wave_lines = _replace_config_lines(frame_wave_lines, {301: "410,410"})
        normal_wave = normal_codehelper_wave(frame_wave_lines, scripted)
        if _is_fujitsu_fgc_power_on_wave(lines, function_id, power):
            normal_wave = [39250, 99160] + normal_wave
        rendered.append(normal_wave)
    return rendered, source


def _ac_selected_wave_override(controller_lines: list[str], selected_key_lines: list[str]) -> list[str]:
    if not selected_key_lines:
        return selected_key_lines
    controller_tags = config_tags(controller_lines)
    key_tags = config_tags(selected_key_lines)
    if (
        controller_tags.get(1002, "").startswith("124DB2FD0200")
        and key_tags.get(1002, "").startswith("064DB2FD0200")
        and key_tags.get(305, "") == "312"
    ):
        replacements = {
            300: "4390,4430",
            301: "520,560",
            302: "520,1650",
            303: "-1&520,5230",
        }
        return _replace_config_lines(selected_key_lines, replacements)
    return selected_key_lines


def _ac_controller_wave_override(lines: list[str]) -> list[str]:
    tags = config_tags(lines)
    if (
        tags.get(1002, "").startswith("124DB2FD0200")
        and tags.get(300, "") == "4400,4400"
        and "40&&6" in tags.get(1516, "")
    ):
        return _replace_config_lines(
            lines,
            {
                300: "4390,4430",
                301: "520,560",
                302: "520,1650",
                303: "5&520,5230,4390,4430|11&520,5230,4390,4430",
            },
        )
    return lines


def _is_fujitsu_fgc_function_one_wave(lines: list[str], function_id: int) -> bool:
    if function_id != 1:
        return False
    tags = config_tags(lines)
    return (
        tags.get(1002, "") == "101463001010FE0930000100000000206F"
        and tags.get(305, "") == "1190"
        and tags.get(300, "") == "3300,1600"
        and tags.get(301, "") == "410,400"
    )


def _is_fujitsu_fgc_power_on_wave(lines: list[str], function_id: int, power: int) -> bool:
    if power != 1:
        return False
    return _is_fujitsu_fgc_function_one_wave(lines, function_id)


def _replace_config_lines(lines: list[str], replacements: dict[int, str]) -> list[str]:
    replaced: set[int] = set()
    out: list[str] = []
    for line in lines:
        if "|" not in line:
            out.append(line)
            continue
        tag_text, _value = line.split("|", 1)
        try:
            tag = int(tag_text)
        except ValueError:
            out.append(line)
            continue
        if tag in replacements:
            out.append(f"{tag}|{replacements[tag]}")
            replaced.add(tag)
        else:
            out.append(line)
    for tag, value in replacements.items():
        if tag not in replaced:
            out.append(f"{tag}|{value}")
    return out


def _set_byte(buffer: list[int], one_based_index: int, value: int) -> None:
    index = one_based_index - 1
    if 0 <= index < len(buffer):
        buffer[index] = value & 0xff


class _LuaReturn(Exception):
    pass


class _LuaTable:
    def __init__(self, values: list[int] | None = None, mapping: dict[int, int] | None = None) -> None:
        self.values = [int(value) & 0xff for value in (values or [])]
        self.mapping = dict(mapping or {})

    def __getitem__(self, key: int) -> int | None:
        index = int(key)
        if self.mapping:
            return self.mapping.get(index)
        if index <= 0 or index > len(self.values):
            return None
        return self.values[index - 1]

    def __setitem__(self, key: int, value: int | None) -> None:
        index = int(key)
        if self.mapping:
            if value is None:
                self.mapping.pop(index, None)
            else:
                self.mapping[index] = int(value)
            return
        if index <= 0:
            return
        while len(self.values) < index:
            self.values.append(0)
        self.values[index - 1] = 0 if value is None else int(value) & 0xff

    def to_bytes(self) -> bytes:
        return bytes(value & 0xff for value in self.values)


def _lua_range(start: int, end: int, step: int = 1) -> range:
    start_i = int(start)
    end_i = int(end)
    step_i = int(step)
    if step_i == 0:
        step_i = 1
    stop = end_i + (1 if step_i > 0 else -1)
    return range(start_i, stop, step_i)


def _lua_sub(value: str, start: int, end: int | None = None) -> str:
    start_i = int(start)
    end_i = int(end) if end is not None else len(value)
    if start_i < 1:
        start_i = 1
    return str(value)[start_i - 1:end_i]


def _tonumber(value: object, base: int = 10) -> int:
    return int(str(value), int(base))


def _lua_table(*values: object) -> _LuaTable:
    return _LuaTable([0 if value is None else int(value) for value in values])


def _translate_lua_expr(expr: str) -> str:
    out = expr.strip().rstrip(";")
    out = out.replace("~=", "!=")
    out = re.sub(r"\)\s*and\s*\(", ") and (", out)
    out = re.sub(r"\)\s*or\s*\(", ") or (", out)
    out = re.sub(r"\bnil\b", "None", out)
    out = re.sub(r"\btrue\b", "True", out)
    out = re.sub(r"\bfalse\b", "False", out)
    out = re.sub(r"\bmath\.floor\b", "math.floor", out)
    out = re.sub(r"\[\s*0+(\d+)\s*\]", r"[\1]", out)
    out = re.sub(
        r"(\w+):sub\(\(([^()]*)\)\s*\*\s*2\s*,\s*([^)]*)\)",
        r"_lua_sub(\1, (\2) * 2, \3)",
        out,
    )
    out = re.sub(r"(\w+):sub\(([^)]*)\)", r"_lua_sub(\1, \2)", out)
    out = re.sub(r"\btonumber\(", "_tonumber(", out)
    out = re.sub(r"(?<=[\]\)\w\d])\s*~\s*(?=[\[\(\w\d])", " ^ ", out)
    out = re.sub(
        r"\(\(\(([^()]+?)\s*%\s*([^()]+?)\)\s*/\s*([^()]+?)\)\s*(<<|>>)",
        r"(int(((\1 % \2) / \3)) \4",
        out,
    )
    return out


def _lua_script_statements(script: str) -> list[str]:
    statements: list[str] = []
    pending: str | None = None
    pending_control: str | None = None
    brace_balance = 0

    def split_inline_body(text: str) -> list[str]:
        return [
            part.strip()
            for part in re.split(r"\s+(?=\w+(?:\[[^\]]+\])?\s*=|if\s*\()", text)
            if part.strip()
        ]

    def push_statement(text: str) -> None:
        if text.startswith("else "):
            statements.append("else")
            push_statement(text[5:].strip())
            return
        trailing_end = re.match(r"^(.+?)\s+end$", text)
        if trailing_end and not text.startswith(("if ", "elseif ", "for ")):
            push_statement(trailing_end.group(1).strip())
            statements.append("end")
            return
        inline_for = re.match(r"^(for\s+.+?\s+do)\s+(.+?)\s+end$", text)
        if inline_for:
            statements.append(inline_for.group(1).strip())
            for part in split_inline_body(inline_for.group(2).strip()):
                push_statement(part)
            statements.append("end")
            return
        inline = re.match(r"^((?:if|elseif)\s*(?:\(.+?\)|.+?)\s*then)\s+(.+)$", text)
        if inline:
            statements.append(inline.group(1).strip())
            push_statement(inline.group(2).strip())
        else:
            statements.append(text)

    for raw_line in script.replace("\r", "").splitlines():
        text = raw_line.strip()
        if not text:
            continue
        text = text.split("--", 1)[0].strip().rstrip(";").strip()
        if not text:
            continue

        if pending_control is not None:
            pending_control += " " + text
            if re.search(r"\b(?:then|do)\b", text):
                text = pending_control
                pending_control = None
            else:
                continue
        elif re.match(r"^(?:if|elseif|for)\b", text) and not re.search(r"\b(?:then|do)\b", text):
            pending_control = text
            continue

        for part in [part.strip() for part in text.split(";") if part.strip()]:
            if pending is not None:
                pending += " " + part
                brace_balance += part.count("{") - part.count("}")
                if brace_balance <= 0:
                    push_statement(pending)
                    pending = None
                continue

            brace_balance = part.count("{") - part.count("}")
            if re.search(r"=\s*$", part) or brace_balance > 0:
                pending = part
                continue

            push_statement(part)

    if pending is not None:
        push_statement(pending)
    if pending_control is not None:
        push_statement(pending_control)
    return statements


def _translate_lua_line(line: str) -> str:
    text = line.strip().rstrip(";").strip()
    if text.startswith("local "):
        text = text[6:].strip()
    text = re.sub(r"\{\s*\}", "_LuaTable()", text)
    text = re.sub(r"=\s*\{([^{}]+)\}", lambda m: "= _lua_table(" + m.group(1) + ")", text)
    concat_match = re.match(r"^(\w+)\s*=\s*(.+?)\s*\.\.\s*(.+)$", text)
    if concat_match:
        name = concat_match.group(1)
        left = _translate_lua_expr(concat_match.group(2))
        right = _translate_lua_expr(concat_match.group(3))
        return f"{name} = str({left}) + str({right})"
    return _translate_lua_expr(text)


def _execute_lua_1518_script(
    script: str,
    frame: bytes,
    function_id: int,
    power: int,
    mode: int,
    temperature: int,
    wind_speed: int,
    ud_wind_mode: int,
    ext_string: str | None,
    nmt: float | None = None,
    nms: int | None = None,
) -> bytes:
    py_lines: list[str] = []
    indent = 0

    def close_empty_block() -> None:
        if py_lines and py_lines[-1].rstrip().endswith(":"):
            py_lines.append("    " * indent + "pass")

    for text in _lua_script_statements(script):
        text = re.sub(r"^(if|elseif)\s*\(", r"\1 (", text)
        text = re.sub(r"\)\s*then$", ") then", text)
        if text == "end" or text.startswith("end "):
            close_empty_block()
            indent = max(0, indent - 1)
            continue
        if text.startswith("elseif "):
            close_empty_block()
            indent = max(0, indent - 1)
            condition = text[7:].strip()
            if condition.endswith("then"):
                condition = condition[:-4].strip()
            py_lines.append("    " * indent + f"elif {_translate_lua_expr(condition)}:")
            indent += 1
            continue
        if text == "else":
            close_empty_block()
            indent = max(0, indent - 1)
            py_lines.append("    " * indent + "else:")
            indent += 1
            continue
        if text.startswith("if "):
            condition = text[3:].strip()
            if condition.endswith("then"):
                condition = condition[:-4].strip()
            py_lines.append("    " * indent + f"if {_translate_lua_expr(condition)}:")
            indent += 1
            continue
        if text.startswith("for "):
            match = re.match(r"for\s+(\w+)\s*=\s*(.+?),\s*(.+?)(?:,\s*(.+?))?\s+do$", text)
            if match:
                step = match.group(4) or "1"
                py_lines.append(
                    "    " * indent
                    + f"for {match.group(1)} in _lua_range({_translate_lua_expr(match.group(2))}, "
                    + f"{_translate_lua_expr(match.group(3))}, {_translate_lua_expr(step)}):"
                )
                indent += 1
                continue
        if text.startswith("return"):
            py_lines.append("    " * indent + "raise _LuaReturn()")
            continue
        py_lines.append("    " * indent + _translate_lua_line(text))

    close_empty_block()
    env: dict[str, object] = {
        "_LuaReturn": _LuaReturn,
        "_LuaTable": _LuaTable,
        "_lua_table": _lua_table,
        "_lua_range": _lua_range,
        "_lua_sub": _lua_sub,
        "_tonumber": _tonumber,
        "math": math,
        "bytes": _LuaTable(list(frame)),
        "exts": _LuaTable(mapping=parse_ac_ext_state(ext_string)) if ext_string else None,
        "functionId": int(function_id),
        "power": int(power),
        "mode": int(mode),
        "temperature": int(temperature),
        "windSpeed": int(wind_speed),
        "udWindMode": int(ud_wind_mode),
        "nmt": nmt,
        "nms": nms,
    }
    for name in (
        "a",
        "b",
        "flag",
        "lr",
        "nf",
        "sp",
        "tf",
        "timing_off",
        "timing_on",
        "to",
        "ud",
        "value",
        "v",
        "wsV",
        "zy",
    ):
        env.setdefault(name, None)
    code = "\n".join(py_lines)
    try:
        exec(code, {"__builtins__": {"int": int, "str": str, "range": range}, **env}, env)
    except _LuaReturn:
        pass
    result = env.get("bytes")
    if isinstance(result, _LuaTable):
        return result.to_bytes()
    return frame


def _post_ac_wave_script_frame(
    tags: dict[int, str],
    before_script: bytes,
    after_script: bytes,
    function_id: int,
    ext_state: dict[int, int],
) -> bytes:
    out = list(after_script)
    if (
        function_id == 28
        and ext_state.get(28, 0) > 0
        and tags.get(1002, "").startswith("1760381305FF")
        and len(out) >= 23
    ):
        out[3] |= 0x80
        out[19] = 0x44
        checksum = 0
        for value in out[3:22]:
            checksum ^= value
        out[22] = checksum & 0xff
    if (
        tags.get(1002, "").startswith("121463001010")
        and len(out) >= 18
        and out != list(before_script)
    ):
        out[17] = (-sum(out[7:17])) & 0xff
    return bytes(out)


def normalize_ac_native_frames(
    lines: list[str],
    frames: list[bytes],
    function_id: int,
    ext_string: str | None = None,
) -> list[bytes]:
    tags = config_tags(lines)
    signature = tags.get(1002, "")
    if (
        tags.get(305, "") == "752"
        and function_id == 8
        and parse_ac_ext_state(ext_string).get(8) == 1
        and _ac_format752_super_power_clear_signature(signature)
    ):
        normalized = []
        for frame in frames:
            out = bytearray(frame)
            if len(out) > 3:
                out[3] &= 0xef
            normalized.append(bytes(out))
        return normalized
    if signature == "0F360FE8803400000000080000000016" and tags.get(305, "") == "1948":
        ext_state = parse_ac_ext_state(ext_string)
        return [_normalize_0f360f_frame(frame, function_id, ext_state) for frame in frames]
    if signature == "090900605802002000D0" and tags.get(305, "") == "197":
        return [_normalize_09090060_frame(frame) for frame in frames]
    if signature == "0CFF00FF00FF00F906D52AA25D" and tags.get(305, "") == "1928":
        return [_normalize_0cff_a25d_frame(frame, function_id) for frame in frames]
    if signature == "072C110900018094" and tags.get(305, "") == "1160":
        ext_state = parse_ac_ext_state(ext_string)
        return [_normalize_072c1109_frame(frame, function_id, ext_state) for frame in frames]
    if signature == "05A080006080" and tags.get(305, "") == "387" and function_id == 8:
        return [_normalize_05a080_frame(frame) for frame in frames]
    if signature == "0EA60C000040A000200000000001B3" and tags.get(305, "") == "67" and function_id == 13:
        return [_normalize_0ea60c_frame(frame) for frame in frames]
    if signature == "0F566C00002002000000000000000000" and tags.get(305, "") == "472" and function_id == 41:
        return [_normalize_0f566c_frame(frame) for frame in frames]
    if signature == "14A60C000040A000200000000001B3B500000000B5" and tags.get(305, "") == "1655" and function_id == 28:
        return [_normalize_14a60c_frame(frame) for frame in frames]
    if signature == "0FA65A47E000A0002000002000010008" and tags.get(305, "") == "1939" and function_id == 28:
        return [_normalize_0fa65a_frame(frame) for frame in frames]
    if signature == "1C23CB26020040200883000000001023CB26010624030F380000008009" and tags.get(305, "") == "1794" and function_id == 28:
        return [_normalize_1c23cb_frame(frame) for frame in frames]
    if signature == "15830600E2000080000000000020A20002000030182A" and tags.get(305, "") == "752" and function_id == 28:
        return [_normalize_158306_20a2_frame(frame) for frame in frames]
    if signature == "0E23CB26010004230F380000EC006F" and tags.get(305, "") == "212":
        return [_normalize_0e23cb_006f_frame(frame, function_id) for frame in frames]
    if signature == "0E23CB26010024030F380000000083" and tags.get(305, "") == "212":
        return [_normalize_0e23cb_0083_frame(frame, function_id) for frame in frames]
    if signature == "0E23CB26010024030F380000008003" and tags.get(305, "") == "212" and function_id == 18:
        return [_normalize_0e23cb_8003_frame(frame) for frame in frames]
    if signature == "124900205002014000704900207002000080B0" and tags.get(305, "") == "482":
        return [_normalize_124900_80b0_frame(frame, function_id) for frame in frames]
    if _is_124900_0030_target(signature, tags.get(305, ""), function_id, ext_string):
        return [_normalize_124900_0030_frame(frame, function_id, parse_ac_ext_state(ext_string)) for frame in frames]
    if signature == "0B52AEC326D9FF00EF108619" and tags.get(305, "") == "1185" and function_id in {1, 10, 21}:
        return [_normalize_0b52ae_frame(frame) for frame in frames]
    if signature == "1B0220E004000000060220E00400312080AF00000660000080000672" and tags.get(305, "") == "237":
        ext_state = parse_ac_ext_state(ext_string)
        return [_normalize_1b0220_2587_frame(frame, function_id, ext_state) for frame in frames]
    if signature == "0DC340E000E00020000020004144" and tags.get(305, "") == "652":
        ext_state = parse_ac_ext_state(ext_string)
        return [_normalize_0dc340_8110_frame(frame, function_id, ext_state) for frame in frames]
    if signature == "0CCDACF9F632530609CDACF9F6" and tags.get(305, "") == "757" and function_id in {1, 9, 10, 22}:
        return [_normalize_0ccdac_frame(frame) for frame in frames]
    if signature == "1223CB260100201800367800000000000000FB" and tags.get(305, "") == "302":
        return [_normalize_1223cb_native_frame(frame, function_id) for frame in frames]
    if signature.startswith("094FB0C03F80"):
        ext_state = parse_ac_ext_state(ext_string)
        normalized = [bytes(int(f"{value:08b}"[::-1], 2) for value in frame) for frame in frames]
        if function_id == 106 and ext_state.get(106) == 1:
            out = []
            for frame in normalized:
                data = bytearray(frame)
                if len(data) >= 7:
                    data[6] = 0x21
                out.append(bytes(data))
            return out
        return normalized
    if signature.startswith("0E23CB2601002403EF") and function_id == 10:
        normalized = []
        for frame in frames:
            if len(frame) != 14:
                normalized.append(frame)
                continue
            out = bytearray(frame)
            out[8] = 0x40
            out[13] = 0x61
            normalized.append(bytes(out))
        return normalized
    if signature.startswith("0E23CB26010004230F38") and function_id == 22:
        normalized = []
        for frame in frames:
            if len(frame) != 14:
                normalized.append(frame)
                continue
            out = bytearray(frame)
            out[8] = 0x39
            out[13] = 0x66
            normalized.append(bytes(out))
        return normalized
    if signature.startswith("10FBFB0A0AFBFB0A0AF4F436") and function_id == 1:
        normalized = []
        for frame in frames:
            if len(frame) != 16:
                normalized.append(frame)
                continue
            out = bytearray(frame)
            out[2] = 0x02
            out[3] = 0x02
            out[6] = 0x02
            out[7] = 0x02
            normalized.append(bytes(out))
        return normalized
    if signature.startswith("184DB2FD0200FF4DB2FD0200"):
        normalized = []
        for frame in frames:
            out = bytearray(frame)
            if (
                function_id == 8
                and signature == "184DB2FD0200FF4DB2FD0200FFDD22807F00FF000000000000"
                and len(out) == 24
            ):
                out[2] = 0xfd
                out[3] = 0x02
                out[14] = 0x80
                out[15] = 0x7f
            elif (
                function_id == 30
                and signature == "184DB2FD0200FF4DB2FD0200FFDD22807F00FFDD008200003F"
                and len(out) == 24
            ):
                out[14] = 0x28
                out[15] = 0xd7
            normalized.append(bytes(out))
        return normalized
    if signature.startswith("1760381305FF") and function_id == 28:
        normalized = []
        for frame in frames:
            if len(frame) != 23:
                normalized.append(frame)
                continue
            out = bytearray(frame)
            out[19] = 0x44
            normalized.append(bytes(out))
        return normalized
    if (
        signature.startswith("0B08000060000100000000FF")
        or signature == "0B0800006000010000000005"
    ) and function_id == 8:
        normalized = []
        for frame in frames:
            if len(frame) != 11:
                normalized.append(frame)
                continue
            out = bytearray(frame)
            out[1] = 0
            normalized.append(bytes(out))
        return normalized
    if signature.startswith("0CFF00FF00FF00F50AD6292A"):
        normalized = []
        for frame in frames:
            if len(frame) != 12:
                normalized.append(frame)
                continue
            out = bytearray(frame)
            out[3] = 0
            out[5] = 0
            out[7] = 0x06
            out[9] = 0x20
            normalized.append(bytes(out))
        return normalized
    if not signature.startswith("07D311090001"):
        return frames
    normalized: list[bytes] = []
    for frame in frames:
        if len(frame) != 7:
            normalized.append(frame)
            continue
        out = bytearray(frame)
        if function_id == 73:
            out[3] |= 0x80
        out[6] = 0
        normalized.append(bytes(out))
    return normalized


def _normalize_1223cb_native_frame(frame: bytes, function_id: int) -> bytes:
    if len(frame) != 18:
        return frame
    out = bytearray(frame)
    if function_id == 26:
        out[6] = 0x58
    if function_id == 22:
        out[7] = 0x0c
    elif function_id == 18:
        out[7] = 0x0a
    else:
        out[7] = 0x08
    if out[8] == 0x26:
        out[8] = 0x36
    if function_id == 18:
        out[14] |= 0x20
    elif function_id == 22:
        out[14] |= 0x40
    elif function_id == 27:
        out[14] |= 0x04
    out[17] = sum(out[:17]) & 0xff
    return bytes(out)


def _ac_format752_super_power_clear_signature(signature: str) -> bool:
    return signature in {
        "1583060002000080000000008017150002000008000A",
        "1583060002000080000000000000820002000038407A",
        "1583060002000080000000000000820002000038003A",
        "1583060002000080000000000000820002000008000A",
        "15830600020000000000000000000200010000380039",
        "1538680002000080000000000015970002000138003B",
    }


def _normalize_0f360f_frame(frame: bytes, function_id: int, ext_state: dict[int, int]) -> bytes:
    if len(frame) != 15:
        return frame
    out = bytearray(frame)
    if function_id == 106 and ext_state.get(106) == 1:
        out[9] |= 0x80
    elif function_id == 18 and ext_state.get(18) == 1:
        out[10] |= 0x40
    elif function_id == 73 and ext_state.get(73) == 1:
        out[9] |= 0x01
    out[14] = (~sum(out[:14])) & 0xff
    return bytes(out)


def _normalize_09090060_frame(frame: bytes) -> bytes:
    if len(frame) != 9:
        return frame
    out = bytearray(frame)
    out[3] &= 0xf7
    out[8] = 0xf0
    return bytes(out)


def _normalize_0cff_a25d_frame(frame: bytes, function_id: int) -> bytes:
    if len(frame) != 12:
        return frame
    out = bytearray(frame)
    if function_id == 106:
        out[2] = 0xbf
        out[3] = 0x40
        out[4] = 0xf1
        out[5] = 0x0e
    elif function_id == 18:
        out[2] = 0xef
        out[3] = 0x10
        out[4] = 0xf2
        out[5] = 0x0d
    return bytes(out)


def _normalize_072c1109_frame(frame: bytes, function_id: int, ext_state: dict[int, int]) -> bytes:
    if len(frame) != 7:
        return frame
    out = bytearray(frame)
    if function_id == 106 and ext_state.get(106) == 1:
        out[2] |= 0x10
        out[3] |= 0x40
    elif function_id == 8 and ext_state.get(8) == 1:
        out[2] |= 0x80
    return bytes(out)


def _normalize_05a080_frame(frame: bytes) -> bytes:
    if len(frame) != 5:
        return frame
    out = bytearray(frame)
    out[1] &= 0xfe
    out[4] = sum(out[:4]) & 0xff
    return bytes(out)


def _normalize_0ea60c_frame(frame: bytes) -> bytes:
    if len(frame) != 14:
        return frame
    out = bytearray(frame)
    out[12] = 0x07
    out[13] = sum(out[:13]) & 0xff
    return bytes(out)


def _normalize_0f566c_frame(frame: bytes) -> bytes:
    if len(frame) != 15:
        return frame
    out = bytearray(frame)
    out[6] = 0x02
    out[14] = (frame[14] + 0x02) & 0xff
    return bytes(out)


def _normalize_14a60c_frame(frame: bytes) -> bytes:
    if len(frame) != 20:
        return frame
    out = bytearray(frame)
    out[1] = 0xa1
    out[12] = 0x02
    out[13] = 0x49
    return bytes(out)


def _normalize_0fa65a_frame(frame: bytes) -> bytes:
    if len(frame) != 15:
        return frame
    out = bytearray(frame)
    out[6] = 0x40
    out[12] = 0x4e
    out[14] = 0xde
    return bytes(out)


def _normalize_1c23cb_frame(frame: bytes) -> bytes:
    if len(frame) != 28:
        return frame
    out = bytearray(frame)
    out[5] = 0x41
    out[18] = 0x30
    out[27] = 0xf1
    return bytes(out)


def _normalize_158306_20a2_frame(frame: bytes) -> bytes:
    if len(frame) != 21:
        return frame
    out = bytearray(frame)
    out[5] = 0x01
    out[15] = 0x36
    return bytes(out)


def _normalize_0e23cb_006f_frame(frame: bytes, function_id: int) -> bytes:
    if len(frame) != 14:
        return frame
    out = bytearray(frame)
    if function_id == 11:
        out[6] = 0x03
        out[13] = 0x45
    elif function_id == 8:
        out[5] = 0x44
        out[8] = 0x78
        out[13] = 0xe5
    elif function_id == 22:
        out[8] = 0x39
        out[13] = 0x66
    return bytes(out)


def _normalize_0e23cb_0083_frame(frame: bytes, function_id: int) -> bytes:
    if len(frame) != 14:
        return frame
    out = bytearray(frame)
    if function_id == 11:
        out[5] = 0x64
        out[13] = 0xb9
    elif function_id == 8:
        out[6] = 0x43
        out[13] = 0xbf if out[12] == 0x01 else 0xc8
    return bytes(out)


def _normalize_0e23cb_8003_frame(frame: bytes) -> bytes:
    if len(frame) != 14:
        return frame
    out = bytearray(frame)
    out[5] = 0xa4
    out[13] = 0x79
    return bytes(out)


def _normalize_124900_80b0_frame(frame: bytes, function_id: int) -> bytes:
    if len(frame) != 18:
        return frame
    out = bytearray(frame)
    if function_id == 14:
        out[3] = 0x51
        out[8] = 0x00
        out[12] = 0x71
    elif function_id == 18:
        out[8] = 0xf4
    return bytes(out)


def _is_124900_0030_target(signature: str, code_format: str, function_id: int, ext_string: str | None) -> bool:
    if signature != "12490020500201000030490020700200000030" or code_format != "482":
        return False
    if function_id not in {73, 106}:
        return False
    ext_state = parse_ac_ext_state(ext_string)
    return 14 not in ext_state and 16 not in ext_state


def _normalize_124900_0030_frame(frame: bytes, function_id: int, ext_state: dict[int, int]) -> bytes:
    if len(frame) != 18:
        return frame
    out = bytearray(frame)
    if function_id == 106 and ext_state.get(106) == 1:
        out[14] = 0x80
    elif function_id == 73 and ext_state.get(73) == 1:
        out[6] = 0x40
    return bytes(out)


def _normalize_0b52ae_frame(frame: bytes) -> bytes:
    if len(frame) != 11:
        return frame
    out = bytearray(frame)
    out[9] = 0x66
    out[10] = 0xf9
    return bytes(out)


def _render_0b52ae_timer_off_frame(function_id: int, ext_state: dict[int, int]) -> bytes | None:
    if function_id == 10 and ext_state.get(10, 0) > 0:
        return bytes.fromhex("52aec326d97f80ea156699ff005fa0ff00")
    return None


def _normalize_1b0220_2587_frame(frame: bytes, function_id: int, ext_state: dict[int, int]) -> bytes:
    if len(frame) != 27:
        return frame
    out = bytearray(frame)
    if function_id == 8 and ext_state.get(8) == 1:
        out[21] = 0x01
        out[26] = (out[26] + 0x01) & 0xff
    elif function_id == 106 and ext_state.get(106) == 1:
        out[21] = 0x20
        out[26] = (out[26] + 0x20) & 0xff
    return bytes(out)


def _normalize_0dc340_8110_frame(frame: bytes, function_id: int, ext_state: dict[int, int]) -> bytes:
    if len(frame) != 13:
        return frame
    if function_id != 8 or ext_state.get(8) != 1:
        return frame
    out = bytearray(frame)
    out[11] = 0x44
    out[12] = 0xd7
    return bytes(out)


def _render_0dc340_8110_frame(function_id: int, ext_state: dict[int, int]) -> bytes | None:
    if function_id == 8 and ext_state.get(8) == 1:
        return bytes.fromhex("c390e000a04020000020004497")
    return None


def _render_094fb_frame(function_id: int, ext_state: dict[int, int]) -> bytes | None:
    if function_id == 1:
        return bytes.fromhex("4fb0c03f8009800009")
    if function_id == 8 and ext_state.get(8) == 1:
        return bytes.fromhex("4fb020df900980008099")
    if function_id == 10 and ext_state.get(10, 0) > 0:
        return bytes.fromhex("4fb0a05fc009800000feb7")
    if function_id == 22 and ext_state.get(22) == 1:
        return bytes.fromhex("4fb020df90098000c0d9")
    if function_id == 106 and ext_state.get(106) == 1:
        return bytes.fromhex("4fb0c03f800984000d")
    return None


def _normalize_0ccdac_frame(frame: bytes) -> bytes:
    if len(frame) != 12:
        return frame
    out = bytearray(frame)
    out[3] = 0xf6
    return bytes(out)


def apply_ac_wave_script(
    lines: list[str],
    frame: bytes,
    function_id: int,
    power: int,
    temperature: int,
    ext_string: str | None,
    mode: int = 0,
    wind_speed: int = 0,
    ud_wind_mode: int = 0,
    nmt: float | None = None,
    nms: int | None = None,
) -> bytes:
    """Apply the subset of NormalCodeHelper tag 1518 used by offline AC wave render.

    Java's getRawEncodedBytes returns the native frame before this script, while
    getWaveCodes renders the post-script frame. These small script patterns are
    present in kkoffline AC records and are intentionally mirrored here.
    """
    tags = config_tags(lines)
    script = tags.get(1518, "")
    if not script:
        return frame

    b = list(frame)
    exts = parse_ac_ext_state(ext_string)

    if tags.get(1002, "") == "0B52AEC326D9FF00EF108619" and tags.get(305, "") == "1185":
        rendered = _render_0b52ae_timer_off_frame(function_id, exts)
        if rendered is not None:
            return rendered

    if tags.get(1002, "") == "1B0220E004000000060220E00400312080AF00000660000080000672" and tags.get(305, "") == "237":
        frame = _normalize_1b0220_2587_frame(frame, function_id, exts)
        b = list(frame)

    if tags.get(1002, "") == "0DC340E000E00020000020004144" and tags.get(305, "") == "652":
        rendered = _render_0dc340_8110_frame(function_id, exts)
        if rendered is not None:
            return rendered

    if tags.get(1002, "").startswith("094FB0C03F80"):
        rendered = _render_094fb_frame(function_id, exts)
        if rendered is not None:
            return rendered

    if (
        tags.get(305, "") == "752"
        and function_id == 8
        and exts.get(8) == 1
        and _ac_format752_super_power_clear_signature(tags.get(1002, ""))
        and len(b) > 3
    ):
        b[3] &= 0xef
        frame = bytes(b)

    if tags.get(1002, "") == "0F360FE8803400000000080000000016" and tags.get(305, "") == "1948":
        frame = _normalize_0f360f_frame(frame, function_id, exts)
        b = list(frame)

    if tags.get(1002, "") == "090900605802002000D0" and tags.get(305, "") == "197":
        frame = _normalize_09090060_frame(frame)
        b = list(frame)

    if tags.get(1002, "") == "0B0800006000010000000005" and tags.get(305, "") == "1933" and function_id == 8:
        frame = normalize_ac_native_frames(lines, [frame], function_id, ext_string)[0]
        b = list(frame)

    if tags.get(1002, "") == "0CFF00FF00FF00F906D52AA25D" and tags.get(305, "") == "1928":
        frame = _normalize_0cff_a25d_frame(frame, function_id)
        b = list(frame)

    if tags.get(1002, "") == "072C110900018094" and tags.get(305, "") == "1160":
        frame = _normalize_072c1109_frame(frame, function_id, exts)
        b = list(frame)

    if tags.get(1002, "") == "05A080006080" and tags.get(305, "") == "387" and function_id == 8:
        frame = _normalize_05a080_frame(frame)
        b = list(frame)

    if tags.get(1002, "") == "0EA60C000040A000200000000001B3" and tags.get(305, "") == "67" and function_id == 13:
        frame = _normalize_0ea60c_frame(frame)
        b = list(frame)

    if tags.get(1002, "") == "0F566C00002002000000000000000000" and tags.get(305, "") == "472" and function_id == 41:
        frame = _normalize_0f566c_frame(frame)
        b = list(frame)

    if tags.get(1002, "") == "14A60C000040A000200000000001B3B500000000B5" and tags.get(305, "") == "1655" and function_id == 28:
        frame = _normalize_14a60c_frame(frame)
        b = list(frame)

    if tags.get(1002, "") == "0FA65A47E000A0002000002000010008" and tags.get(305, "") == "1939" and function_id == 28:
        frame = _normalize_0fa65a_frame(frame)
        b = list(frame)

    if tags.get(1002, "") == "1C23CB26020040200883000000001023CB26010624030F380000008009" and tags.get(305, "") == "1794" and function_id == 28:
        frame = _normalize_1c23cb_frame(frame)
        b = list(frame)

    if tags.get(1002, "") == "15830600E2000080000000000020A20002000030182A" and tags.get(305, "") == "752" and function_id == 28:
        frame = _normalize_158306_20a2_frame(frame)
        b = list(frame)

    if tags.get(1002, "") == "0E23CB26010004230F380000EC006F" and tags.get(305, "") == "212":
        frame = _normalize_0e23cb_006f_frame(frame, function_id)
        b = list(frame)

    if tags.get(1002, "") == "0E23CB26010024030F380000000083" and tags.get(305, "") == "212":
        frame = _normalize_0e23cb_0083_frame(frame, function_id)
        b = list(frame)

    if tags.get(1002, "") == "0E23CB26010024030F380000008003" and tags.get(305, "") == "212" and function_id == 18:
        frame = _normalize_0e23cb_8003_frame(frame)
        b = list(frame)

    if tags.get(1002, "") == "124900205002014000704900207002000080B0" and tags.get(305, "") == "482":
        frame = _normalize_124900_80b0_frame(frame, function_id)
        b = list(frame)

    if _is_124900_0030_target(tags.get(1002, ""), tags.get(305, ""), function_id, ext_string):
        frame = _normalize_124900_0030_frame(frame, function_id, exts)
        b = list(frame)

    if tags.get(1002, "") == "0B52AEC326D9FF00EF108619" and tags.get(305, "") == "1185" and function_id in {1, 21}:
        frame = _normalize_0b52ae_frame(frame)
        b = list(frame)

    if tags.get(1002, "") == "0CCDACF9F632530609CDACF9F6" and tags.get(305, "") == "757" and function_id in {1, 9, 10, 22}:
        frame = _normalize_0ccdac_frame(frame)
        b = list(frame)

    if (
        function_id == 8
        and exts.get(8, 0) > 0
        and tags.get(1002, "").startswith("0B0800006000")
        and "030C1001" in tags.get(1011, "")
        and "b = bytes" in script
        and len(b) >= 2
    ):
        b[1] = 0
        frame = bytes(b)

    use_manual_first = "bytes[6] = (~bytes[5])" in script and "0x9D" in script
    if tags.get(1002, "").startswith("064DB2") and "functionId == 70" in script:
        use_manual_first = False
    if (
        tags.get(1002, "").startswith("124DB2FD0200")
        and "40&&6" in tags.get(1516, "")
        and function_id in {8, 22, 73}
    ):
        use_manual_first = False
    if (
        tags.get(1002, "").startswith("124DB2FD0200")
        and "40&&6" in tags.get(1516, "")
        and function_id == 28
        and exts.get(28) == 1
    ):
        return bytes.fromhex("9d62af50da25")
    if tags.get(1002, "") == "1223CB260100201800367800000000000000FB" and tags.get(305, "") == "302":
        out = bytearray(_normalize_1223cb_native_frame(frame, function_id))
        timing_off = exts.get(10)
        if function_id == 10 and timing_off is not None and 0 < timing_off <= 1440 and len(out) == 18:
            out[11] = timing_off // 10
            out[13] |= 0x03
            out[17] = sum(out[:17]) & 0xff
        return bytes(out)
    if tags.get(1002, "").startswith("07D311090001") and function_id == 73 and len(b) >= 7:
        b[3] |= 0x80
        b[6] = 0xaf
        return bytes(b)

    if not use_manual_first:
        try:
            scripted = _execute_lua_1518_script(
                script,
                frame,
                function_id,
                power,
                mode,
                temperature,
                wind_speed,
                ud_wind_mode,
                ext_string,
                nmt,
                nms,
            )
            return _post_ac_wave_script_frame(tags, frame, scripted, function_id, exts)
        except Exception:
            pass

    if "b[13] = 0" in script and len(b) >= 13:
        if power == 1:
            _set_byte(b, 10, b[9] & 0xcf)
        if nmt is not None and 16 <= nmt <= 32:
            _set_byte(b, 2, (b[1] & 0x07) + ((int(nmt - 8)) << 3))
        if exts:
            if power == 1:
                to = exts.get(9)
                if to is not None and 0 < to <= 1440:
                    _set_byte(b, 10, b[9] | 0x80)
                    _set_byte(b, 6, to % 60)
                    _set_byte(b, 5, b[4] + (to // 60))
            elif power == 0:
                tf = exts.get(10)
                if tf is not None and 0 < tf <= 1440:
                    _set_byte(b, 10, b[9] | 0x40)
                    _set_byte(b, 6, tf % 60)
                    _set_byte(b, 5, b[4] + (tf // 60))
        _set_byte(b, 13, sum(b[:12]))
        return bytes(b)

    if "bytes[6] = (~bytes[5])" in script and "0x9D" in script:
        if len(b) >= 6:
            _set_byte(b, 6, ~b[4])
        if function_id == 6:
            return bytes.fromhex("9d62af5020df")
        if function_id == 7:
            return bytes.fromhex("9d62af50a05f")
        if exts and function_id == 40:
            ev = exts.get(function_id)
            value6_by_state = {0: 0x14, 1: 0x14, 2: 0x3c, 3: 0x0a, 4: 0x26}
            if ev in value6_by_state:
                return bytes([0x9d, 0x62, 0x6f, 0x90, 0x40 if ev == 0 else 0x80, value6_by_state[ev]])
        if exts and function_id in {8, 12, 14, 15, 18, 21, 27, 55, 65, 69, 82, 131}:
            ev = exts.get(function_id)
            value_by_function = {
                8: {0: 0x40, 1: 0x80},
                12: {0: 0x38, 1: 0xd8},
                14: {0: 0x18, 1: 0xa8},
                15: {0: 0x28, 1: 0x84},
                18: {0: 0xa4, 1: 0x24},
                21: {1: 0xe0, 2: 0x10, 3: 0xf4, 4: 0x0c, 5: 0x8c, 6: 0x4c, 7: 0xcc},
                27: {0: 0x0a, 1: 0xf2},
                55: {0: 0x0d, 1: 0xf5},
                65: {0: 0x42, 1: 0x82},
                69: {0: 0xad, 1: 0x3d},
                82: {0: 0xab, 1: 0x2b},
                131: {1: 0x90},
            }
            mapped = value_by_function.get(function_id, {}).get(ev)
            if function_id == 8 and "functionId == 8" not in script:
                mapped = None
            if mapped is not None:
                return bytes([0x9d, 0x62, 0xaf, 0x50, mapped, (~mapped) & 0xff])
        if exts:
            if power == 1:
                timing_on = exts.get(9)
                if function_id == 9 and timing_on is not None and timing_on == 0 and len(b) >= 2:
                    return bytes([b[0], b[1], 0xde, (~0xde) & 0xff, 0x07, (~0x07) & 0xff])
                if timing_on is not None and timing_on > 0 and len(b) >= 6:
                    timing_on = timing_on // 30 - 1
                    value = 0
                    for i in range(1, 7):
                        value |= (((timing_on >> (6 - i)) & 1) << (i - 1))
                    _set_byte(b, 5, b[4] | 0xc0)
                    _set_byte(b, 6, 0x81 | (value << 1))
                    if 15 < temperature < 25:
                        _set_byte(b, 6, b[5] - 1)
            elif power == 0:
                timing_off = exts.get(10)
                if timing_off is not None and timing_off > 0 and len(b) >= 6:
                    timing_off = timing_off // 30 - 1
                    value = timing_off & 0x0f
                    _set_byte(b, 3, b[2] & 0x87)
                    for i in range(1, 5):
                        _set_byte(b, 3, b[2] | (((value >> (i - 1)) & 1) << (7 - i)))
                    value = timing_off >> 4
                    _set_byte(b, 5, b[4] & 0x3f)
                    _set_byte(b, 5, b[4] | ((value & 1) << 7))
                    _set_byte(b, 5, b[4] | ((value & 2) << 5))
                    _set_byte(b, 6, 0xff)
                    if 15 < temperature < 25:
                        _set_byte(b, 6, b[5] - 1)
        if len(b) >= 6:
            _set_byte(b, 4, ~b[2])
            if len(b) < 12:
                b.extend([0] * (12 - len(b)))
            for index in range(6):
                b[index + 6] = b[index]
        return bytes(b)

    if "bytes[7] = 0" in script and "0x53" in script and "timing_on" in script:
        if exts:
            timing_on = exts.get(9)
            if timing_on is not None and timing_on > 0 and len(b) >= 6:
                out = b[:6]
                out[1] = 0x21
                out.extend([0x22, 0x00, 0x08 + (timing_on >> 8), timing_on & 0xff, 0x00, 0x00])
                out[11] = (sum(out[:11]) ^ 0x53) & 0xff
                return bytes(out)
            timing_off = exts.get(10)
            if timing_off is not None and timing_off > 0 and len(b) >= 6:
                out = b[:6]
                out[1] = 0x21
                out.extend([0x22, 0x00, 0x80 + ((timing_off >> 8) << 4), 0x00, timing_off & 0xff, 0x00])
                out[11] = (sum(out[:11]) ^ 0x53) & 0xff
                return bytes(out)
        if len(b) >= 7:
            _set_byte(b, 7, (sum(b[:6]) ^ 0x53) & 0xff)
        return bytes(b)

    if "v = \"4\"" in script and "tonumber(v:sub" in script:
        if power == 1 and len(b) >= 6:
            _set_byte(b, 6, b[5] & 0xf7)
        nf = 0
        if exts:
            timing_on = exts.get(9)
            timing_off = exts.get(10)
            if power == 1 and timing_on is not None and 0 < timing_on <= 1440 and len(b) >= 11:
                b = b[:2] + [0x01, 0x20, 0xb4] + b[5:11] + [0x21, 0x08 + (timing_on >> 8), timing_on & 0xff, 0, 0, 0, 0]
                nf = 1
            elif power == 0 and timing_off is not None and 0 < timing_off <= 1440 and len(b) >= 11:
                b = b[:2] + [0x01, 0x20, 0xb4] + b[5:11] + [0x21, 0x10 + (timing_off >> 8), timing_off & 0xff, 0, 0, 0, 0]
                nf = 1
        if nf == 0:
            if len(b) < 12:
                b.extend([0] * (12 - len(b)))
            checksum = b[0] ^ b[1]
            for value in b[2:11]:
                checksum ^= value
            b[11] = checksum & 0xff
            text = "4"
            for value in b[:12]:
                for j in range(1, 5):
                    text += str((value >> (8 - j * 2)) & 3)
            text += "5"
            return bytes(int(text[index * 2:index * 2 + 2], 16) for index in range(25))
        if len(b) < 19:
            b.extend([0] * (19 - len(b)))
        checksum = b[5] ^ b[6]
        for value in b[7:11]:
            checksum ^= value
        checksum ^= b[12]
        for value in b[13:18]:
            checksum ^= value
        b[18] = checksum & 0xff
        text = "4"
        for value in b[:11]:
            for j in range(1, 5):
                text += str((value >> (8 - j * 2)) & 3)
        text += "5"
        for value in b[11:19]:
            for j in range(1, 5):
                text += str((value >> (8 - j * 2)) & 3)
        text += "50"
        return bytes(int(text[index * 2:index * 2 + 2], 16) for index in range(40))

    if "b = bytes" in script and "exts[10]" in script and "math.floor(h / 10)" in script:
        if exts:
            timer = exts.get(9) if power == 1 else exts.get(10) if power == 0 else None
            if timer is not None and 0 < timer <= 1440 and len(b) >= 3:
                h = timer // 60
                m = timer % 60
                _set_byte(b, 2, (b[1] & 0x0f) + (m & 0xf0) + ((h // 10) << 5) + 0x80)
                _set_byte(b, 3, (b[2] & 0xf0) + (h % 10))
        return bytes(b)

    if "bytes = { 0xAD, 0x52, 0xAF, 0x50, 0x45, 0xBA }" in script:
        if function_id == 1 and power == 1:
            _set_byte(b, 3, 0xde)
            _set_byte(b, 4, 0x21)
            _set_byte(b, 5, 0x07)
            _set_byte(b, 6, 0xf8)
            return bytes(b)
        if "functionId == 6) or (functionId == 7" in script and function_id in {6, 7}:
            return bytes.fromhex("ad52af50956a")
        if exts:
            if function_id == 1:
                return bytes(b)
            if function_id == 8:
                return bytes.fromhex("ad52af5045ba")
            if "functionId == 83" in script and function_id == 83:
                return bytes.fromhex("ad52af50857a")
            if "functionId == 12" in script and function_id in {12, 15}:
                return bytes.fromhex("ad52af5055aa")
            if "functionId == 34" in script and function_id == 34:
                return bytes.fromhex("ad52af50a55a")
            if "functionId == 41" in script and function_id == 41:
                return bytes.fromhex("ad52af50c53a")
            if function_id == 21:
                branch_start = script.find("if (functionId == 21)")
                branch_end = script.find("end", branch_start) if branch_start >= 0 else -1
                branch = script[branch_start:branch_end] if branch_end > branch_start else ""
                if "bytes = { 0xAD, 0x52, 0xAF, 0x50, 0x95, 0x6A }" in branch:
                    return bytes.fromhex("ad52af50956a")
                _set_byte(b, 3, 0xf0)
                _set_byte(b, 4, 0x0f)
                _set_byte(b, 5, 0x07)
                _set_byte(b, 6, 0xf8)
                return bytes(b)
            if function_id != 6:
                if power == 1:
                    to = exts.get(9)
                    if function_id == 9 and to is not None and to == 0:
                        _set_byte(b, 3, 0xde)
                        _set_byte(b, 4, 0x21)
                        _set_byte(b, 5, 0x07)
                        _set_byte(b, 6, 0xf8)
                    if to is not None and 0 < to <= 1440:
                        to = to // 30 - 1
                        value = 0
                        for i in range(1, 7):
                            value |= (((to >> (6 - i)) & 1) << (i - 1))
                        _set_byte(b, 5, b[4] | 0xc0)
                        _set_byte(b, 6, b[5] | 0x01)
                        _set_byte(b, 6, (b[5] & 0x81) | (value << 1))
                        if 15 < temperature < 25:
                            _set_byte(b, 6, b[5] - 1)
                if power == 0:
                    tf = exts.get(10)
                    if tf is not None and 0 < tf <= 1440:
                        tf = tf // 30 - 1
                        value = tf & 0x0f
                        _set_byte(b, 3, b[2] & 0x87)
                        for i in range(1, 5):
                            _set_byte(b, 3, b[2] | (((value >> (i - 1)) & 1) << (7 - i)))
                        _set_byte(b, 4, ~b[2])
                        value = tf >> 4
                        _set_byte(b, 5, b[4] & 0x3f)
                        _set_byte(b, 5, b[4] | ((value & 1) << 7))
                        _set_byte(b, 5, b[4] | ((value & 2) << 5))
                        _set_byte(b, 6, 0xff)
                        if 15 < temperature < 25:
                            _set_byte(b, 6, 0xfe)
            if exts.get(18) == 1 and len(b) >= 6:
                return bytes.fromhex("4db207f8c03f") + bytes(b[:6]) + bytes(b[:6])
    return bytes(b)


def connect_db(path: Path) -> sqlite3.Connection:
    if not path.exists():
        raise SystemExit(f"missing DB: {path}")
    con = sqlite3.connect(str(path))
    con.row_factory = sqlite3.Row
    return con


def row_to_dict(row: sqlite3.Row | None) -> dict[str, Any] | None:
    if row is None:
        return None
    result: dict[str, Any] = {}
    for key in row.keys():
        value = row[key]
        if isinstance(value, bytes):
            try:
                result[key] = value.decode("utf-8")
            except UnicodeDecodeError:
                result[key] = value.hex()
        else:
            result[key] = value
    return result


def print_json(value: Any) -> None:
    print(json.dumps(value, ensure_ascii=False, indent=2, sort_keys=False))


def parse_function_id(value: str | int) -> int:
    if isinstance(value, int):
        return value
    text = str(value).strip()
    if not text:
        raise ValueError("empty function key")
    if text.isdigit():
        return int(text)
    key = text.lower().replace("-", "_")
    if key in FUNCTION_ALIASES:
        return FUNCTION_ALIASES[key]
    raise ValueError(f"unknown function key: {value}")


def ticks_to_us(ticks: list[int], frequency: int) -> list[int]:
    if frequency <= 0:
        return ticks
    return [round(value * 1_000_000 / frequency) for value in ticks]


def _parse_ints(value: str, separators: str = ",") -> list[int]:
    text = value
    for sep in separators[1:]:
        text = text.replace(sep, separators[0])
    return [int(part.strip()) for part in text.split(separators[0]) if part.strip()]


def _parse_delay_codes(value: str) -> list[list[int]]:
    return [_parse_ints(part, ",&") for part in value.strip().split("|") if part.strip()]


def _append_values(values: list[int] | None, out: list[int]) -> None:
    if values:
        out.extend(values)


def _add_delay_codes(delay_codes: list[list[int]], byte_index: int, out: list[int]) -> None:
    for code in delay_codes:
        if len(code) <= 1 or code[0] != byte_index:
            continue
        merge = code[1] == 0
        start = 1
        if merge:
            if not out:
                continue
            start = 2
        if len(code) - start > 1:
            if merge:
                out[-1] += code[start]
                start += 1
                merge = False
            out.extend(code[start:-1])
        tail = code[-1]
        if tail > 0:
            if merge and out:
                out[-1] += tail
            else:
                out.append(tail)
        elif tail < 0:
            gap = (-tail) - sum(out)
            if gap > 0:
                if merge and out:
                    out[-1] += gap
                else:
                    out.append(gap)


def _parse_normal_wave_config(lines: list[str]) -> dict[str, Any]:
    tags = config_tags(lines)
    config: dict[str, Any] = {
        "repeat_count": int(tags.get(1508, "1") or "1"),
        "little_endian": int(tags.get(306, "0") or "0") == 1,
        "add_trailer_one": int(tags.get(307, "0") or "0") != 1,
        "delay_codes": [],
        "byte_bit_nums": {},
        "patterns": None,
        "byte_num": int(tags.get(310, "0") or "0"),
    }
    # OEM NormalCodeHelper applies selected TAG_CODE_FORMAT presets before
    # expanding bits. The offline DB still carries the raw timing rows, so keep
    # these overrides keyed by format id instead of remote id.
    code_format_overrides: dict[int, dict[str, list[int]]] = {
        307: {
            "lead_codes": [8300, 4110],
            "zero_codes": [550, 520],
            "one_codes": [550, 1600],
        },
    }
    signature_format_overrides: dict[tuple[int, str], dict[str, list[int]]] = {
        (137, "0E23CB260200"): {
            "lead_codes": [3500, 1700],
            "zero_codes": [500, 500],
            "one_codes": [500, 1300],
        },
        (1916, "0A33E8908800"): {
            "zero_codes": [1010, 530],
            "one_codes": [1010, 2500],
        },
    }
    if 300 in tags:
        config["lead_codes"] = _parse_ints(tags[300])
    if 301 in tags:
        config["zero_codes"] = _parse_ints(tags[301])
    if 302 in tags:
        config["one_codes"] = _parse_ints(tags[302])
    try:
        code_format = int(tags.get(305, ""))
        override = code_format_overrides.get(code_format)
    except ValueError:
        code_format = None
        override = None
    if override:
        config.update(override)
    if code_format is not None:
        signature = tags.get(1002, "")
        for (format_id, prefix), signature_override in signature_format_overrides.items():
            if code_format == format_id and signature.startswith(prefix):
                config.update(signature_override)
                break
        if (
            code_format == 137
            and signature.startswith("0E23CB260100")
            and tags.get(300, "") == "3500,1500"
        ):
            config["lead_codes"] = [3600, 1700]
        if (
            code_format == 237
            and signature.startswith("1B0220E00400")
            and tags.get(300, "") == "3490,1740"
            and tags.get(301, "") == "411,474"
        ):
            config["lead_codes"] = [3500, 1750]
            config["zero_codes"] = [430, 430]
            config["one_codes"] = [430, 1300]
        if code_format == 302 and signature == "1223CB260100201800367800000000000000FB":
            config["zero_codes"] = [430, 430]
        if code_format == 737 and signature.startswith("094FB0C03F80"):
            config["lead_codes"] = [4400, 4400]
            config["zero_codes"] = [540, 540]
            config["one_codes"] = [540, 1630]
    for delay_tag in (303, 311, 312):
        if delay_tag in tags:
            config["delay_codes"] = _parse_delay_codes(tags[delay_tag])
    if (
        code_format == 237
        and tags.get(1002, "").startswith("1B0220E00400")
        and tags.get(300, "") == "3490,1740"
        and tags.get(301, "") == "411,474"
    ):
        config["delay_codes"] = _parse_delay_codes("7&430,10000,3500,1750")
    if code_format == 302 and tags.get(1002, "") == "1223CB260100201800367800000000000000FB":
        config["delay_codes"] = _parse_delay_codes("-1&13500")
    if code_format == 737 and tags.get(1002, "").startswith("094FB0C03F80"):
        config["delay_codes"] = _parse_delay_codes("-1&7500")
    if 1509 in tags:
        for part in tags[1509].split("|"):
            if not part.strip():
                continue
            pair = _parse_ints(part, "&")
            if len(pair) >= 2:
                config["byte_bit_nums"][pair[0]] = pair[1]
    if 309 in tags:
        config["patterns"] = [
            _parse_ints(part) for part in tags[309].split("|") if part.strip()
        ]
    return config


def normal_codehelper_wave(lines: list[str], frame: bytes) -> list[int]:
    """Render a NormalCodeHelper byte frame to microsecond durations."""
    config = _parse_normal_wave_config(lines)
    out: list[int] = []
    patterns: list[list[int]] | None = config.get("patterns")
    if patterns:
        symbol_bits = 4
        if len(patterns) > 16:
            symbol_bits = 8
        elif len(patterns) <= 4:
            symbol_bits = 2 if len(patterns) > 2 else 1
        symbols_per_byte = 8 // symbol_bits
        emitted = 0
        for byte in frame:
            for symbol_index in range(symbols_per_byte):
                symbol = ((byte << (symbol_index * symbol_bits)) & 0xff) >> (8 - symbol_bits)
                for value in patterns[symbol & 0xff]:
                    if out or value > 0:
                        if len(out) % 2 == 1:
                            if value < 0:
                                out.append(-value)
                            else:
                                out[-1] += value
                        elif value > 0:
                            out.append(value)
                        else:
                            out[-1] -= value
                emitted += 1
                byte_num = int(config.get("byte_num", 0) or 0)
                if byte_num > 0 and emitted >= byte_num:
                    break
            else:
                continue
            break
    else:
        zero_codes: list[int] | None = config.get("zero_codes")
        one_codes: list[int] | None = config.get("one_codes")
        _append_values(config.get("lead_codes"), out)
        byte_bit_nums: dict[int, int] = config.get("byte_bit_nums", {})
        for byte_index, byte in enumerate(frame):
            bit_count = byte_bit_nums.get(byte_index, 8)
            if byte_index == len(frame) - 1 and bit_count == 8:
                bit_count = byte_bit_nums.get(-1, 8)
            bits = f"{byte & 0xff:08b}"
            if config.get("little_endian"):
                iterable = range(7, 7 - bit_count, -1)
            else:
                iterable = range(8 - bit_count, 8)
            for bit_index in iterable:
                _append_values(zero_codes if bits[bit_index] == "0" else one_codes, out)
            _add_delay_codes(config.get("delay_codes", []), byte_index, out)
        if config.get("add_trailer_one") and one_codes:
            out.append(one_codes[0])
        _add_delay_codes(config.get("delay_codes", []), -1, out)
    if len(out) % 2 == 1:
        out.append(1000)
    repeat_count = int(config.get("repeat_count", 1) or 1)
    return out * repeat_count


def find_host_library(explicit: str | None = None) -> Path:
    candidates: list[Path] = []
    if explicit:
        candidates.append(Path(explicit))
    if sys.platform.startswith("win"):
        candidates.extend([
            ROOT / "build/host_x64/windows/package/bin/kksdk_host.dll",
            ROOT / "build/host_x64/windows/build/Release/kksdk_host.dll",
        ])
    elif sys.platform == "darwin":
        candidates.extend([
            ROOT / "build/host_x64/macos/package/lib/libkksdk_host.dylib",
            ROOT / "build/host_x64/linux/package/lib/libkksdk_host.so",
        ])
    else:
        candidates.extend([
            ROOT / "build/host_x64/linux/package/lib/libkksdk_host.so",
            ROOT / "build/host_x64/linux/build/libkksdk_host.so",
        ])

    for path in candidates:
        if path.exists():
            return path
    raise SystemExit(
        "missing host library. Build it first with tools/compile_host_x64.ps1 "
        "on Windows or tools/compile_host_x64.sh on Linux."
    )


class KkHost:
    def __init__(self, dll_path: str | None = None) -> None:
        path = find_host_library(dll_path)
        if sys.platform.startswith("win"):
            os.add_dll_directory(str(path.parent))
        self.path = path
        self.lib = ctypes.CDLL(str(path))
        self._bind()

    def _bind(self) -> None:
        self.lib.streamhelper_transform_decrypt.argtypes = [
            ctypes.POINTER(ctypes.c_uint8),
            ctypes.c_int,
            ctypes.c_int,
        ]
        self.lib.streamhelper_transform_decrypt.restype = None

        self.lib.kksdk_remote_encoder_create.argtypes = [ctypes.c_uint]
        self.lib.kksdk_remote_encoder_create.restype = ctypes.c_void_p
        self.lib.kksdk_remote_encoder_destroy.argtypes = [ctypes.c_void_p]
        self.lib.kksdk_remote_encoder_destroy.restype = None
        self.lib.kksdk_remote_encoder_add_line.argtypes = [
            ctypes.c_void_p,
            ctypes.c_char_p,
            ctypes.c_ulonglong,
        ]
        self.lib.kksdk_remote_encoder_add_line.restype = ctypes.c_int
        self.lib.kksdk_remote_encoder_encode.argtypes = [
            ctypes.c_void_p,
            ctypes.c_uint,
            ctypes.c_uint,
            ctypes.c_uint,
            ctypes.c_uint,
            ctypes.c_uint,
            ctypes.c_uint,
            ctypes.c_uint,
            ctypes.POINTER(ctypes.c_uint8),
            ctypes.c_ulonglong,
            ctypes.c_char_p,
            ctypes.POINTER(ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8))),
            ctypes.POINTER(ctypes.c_ulonglong),
            ctypes.POINTER(ctypes.POINTER(ctypes.c_ulonglong)),
        ]
        self.lib.kksdk_remote_encoder_encode.restype = ctypes.c_int
        self.lib.kksdk_remote_encoder_free_output.argtypes = [
            ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),
            ctypes.POINTER(ctypes.c_ulonglong),
            ctypes.c_ulonglong,
        ]
        self.lib.kksdk_remote_encoder_free_output.restype = None
        self.lib.kksdk_irdevice_encode_pulse.argtypes = [
            ctypes.POINTER(ctypes.c_uint8),
            ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_uint8),
            ctypes.c_size_t,
            ctypes.POINTER(ctypes.POINTER(ctypes.c_uint32)),
            ctypes.POINTER(ctypes.c_size_t),
        ]
        self.lib.kksdk_irdevice_encode_pulse.restype = ctypes.c_int
        self.lib.kksdk_irdevice_free_pulse.argtypes = [
            ctypes.POINTER(ctypes.c_uint32),
        ]
        self.lib.kksdk_irdevice_free_pulse.restype = None

    def decrypt_bytes(self, data: bytes) -> bytes:
        if not data:
            return b""
        buf = (ctypes.c_uint8 * len(data)).from_buffer_copy(data)
        self.lib.streamhelper_transform_decrypt(buf, len(data), STREAMHELPER2_KEY)
        return bytes(buf)

    def decrypt_text_blob(self, data: bytes) -> str:
        return self.decrypt_bytes(data).decode("utf-8", "replace")

    def decrypt_token(self, token: str | None) -> str:
        if token is None:
            return ""
        padded = token + "=" * ((4 - len(token) % 4) % 4)
        return self.decrypt_text_blob(base64.b64decode(padded))

    def encode_ac(
        self,
        remote_id: int,
        lines: list[str],
        *,
        power: int,
        mode: int,
        temperature: int,
        wind_speed: int,
        lr_wind_mode: int,
        ud_wind_mode: int,
        function_id: int,
        ext_string: str | None,
    ) -> list[bytes]:
        encoder = self.lib.kksdk_remote_encoder_create(remote_id)
        if not encoder:
            raise RuntimeError(f"kksdk_remote_encoder_create({remote_id}) failed")
        try:
            for line in lines:
                raw = line.encode("utf-8")
                ok = self.lib.kksdk_remote_encoder_add_line(encoder, raw, len(raw))
                if ok == 0:
                    raise RuntimeError(f"encoder rejected config line: {line}")

            frames = ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8))()
            frame_count = ctypes.c_ulonglong()
            sizes = ctypes.POINTER(ctypes.c_ulonglong)()
            ext_raw = ext_string.encode("utf-8") if ext_string else None
            ok = self.lib.kksdk_remote_encoder_encode(
                encoder,
                power,
                mode,
                temperature,
                wind_speed,
                lr_wind_mode,
                ud_wind_mode,
                function_id,
                None,
                0,
                ext_raw,
                ctypes.byref(frames),
                ctypes.byref(frame_count),
                ctypes.byref(sizes),
            )
            if ok == 0:
                return []
            out: list[bytes] = []
            for index in range(frame_count.value):
                size = sizes[index]
                out.append(bytes(frames[index][:size]))
            self.lib.kksdk_remote_encoder_free_output(frames, sizes, frame_count)
            return out
        finally:
            self.lib.kksdk_remote_encoder_destroy(encoder)

    def encode_pulse(self, remote_data: bytes, command: bytes) -> list[int]:
        if not remote_data:
            raise RuntimeError("missing remote template data")
        if not command:
            raise RuntimeError("missing IR command payload")
        remote_buf = (ctypes.c_uint8 * len(remote_data)).from_buffer_copy(remote_data)
        command_buf = (ctypes.c_uint8 * len(command)).from_buffer_copy(command)
        durations = ctypes.POINTER(ctypes.c_uint32)()
        duration_count = ctypes.c_size_t()
        status = self.lib.kksdk_irdevice_encode_pulse(
            remote_buf,
            len(remote_data),
            command_buf,
            len(command),
            ctypes.byref(durations),
            ctypes.byref(duration_count),
        )
        if status != 0:
            raise RuntimeError(f"kksdk_irdevice_encode_pulse failed: {status}")
        try:
            return [int(durations[index]) for index in range(duration_count.value)]
        finally:
            self.lib.kksdk_irdevice_free_pulse(durations)


def load_json(path: Path, default: Any) -> Any:
    if not path.exists():
        return default
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def save_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        json.dump(value, f, ensure_ascii=False, indent=2, sort_keys=True)
        f.write("\n")
