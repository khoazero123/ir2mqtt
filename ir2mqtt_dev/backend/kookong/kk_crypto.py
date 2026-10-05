#!/usr/bin/env python3
"""Giải mã blob Kookong (StreamHelper dec2) — port từ kk_decrypt_body @ libkksdk.so."""

from __future__ import annotations

import base64
import gzip
import json
import struct
from collections import OrderedDict
from typing import Any, Union

Buffer = Union[bytearray, memoryview]

# Key = hash31(secret) trong StreamHelper_init; 4 byte đầu file là hoán vị byte của key.
KK_MAGIC_KEY = 0xD84346A8  # hash31("E8A87CE67252443109C61029771A7143")


def wire_magic_to_key(magic4: bytes) -> int:
    """Đảo hoán vị byte header giống dec2 (ldrb + bfi)."""
    if len(magic4) < 4:
        return 0
    b0, b1, b2, b3 = magic4[0], magic4[1], magic4[2], magic4[3]
    return (b0 | (b2 << 8) | (b3 << 16) | (b1 << 24)) & 0xFFFFFFFF


def key_to_wire_magic(key: int) -> bytes:
    """Byte header như enc2 (strb b0, b3, b1, b2)."""
    key &= 0xFFFFFFFF
    return bytes((key & 0xFF, (key >> 24) & 0xFF, (key >> 8) & 0xFF, (key >> 16) & 0xFF))


def hash31(s: str) -> int:
    """Cùng thuật toán gán DAT_001df0d4 trong StreamHelper_init."""
    h = 0
    for c in s:
        h = (31 * h + ord(c)) & 0xFFFFFFFF
    return h


def _c_int32(x: int) -> int:
    x &= 0xFFFFFFFF
    return x - 0x100000000 if x >= 0x80000000 else x


def _c_int64(x: int) -> int:
    x &= 0xFFFFFFFFFFFFFFFF
    return x - 0x10000000000000000 if x >= 0x8000000000000000 else x


def _shift_mask(i: int) -> int:
    """Mask shift giống asm `<< (n*8) & 0x38` trong FUN_0014ecbc / FUN_0014eb5c."""
    return (i * 8) & 0x38


def _pack_chunk_backward_xor(buf: Buffer, start: int, end: int) -> int:
    """Gom byte [start, end) đọc ngược (kk_decrypt_body)."""
    n = end - start
    acc = 0
    for i in range(n):
        acc ^= buf[end - 1 - i] << _shift_mask(i)
    return acc & 0xFFFFFFFFFFFFFFFF


def _pack_chunk_forward_xor(buf: Buffer, start: int, end: int) -> int:
    """Gom byte [start, end) đọc xuôi (kk_encrypt_body @ 0x4eb5c)."""
    n = end - start
    acc = 0
    for i in range(n):
        acc ^= buf[start + i] << _shift_mask(i)
    return acc & 0xFFFFFFFFFFFFFFFF


def _key_rot(key: int, chunk_size: int) -> int:
    """int32 rotate/add như native (param_3 >> … + param_3 << …)."""
    k = _c_int32(key)
    return _c_int64(_c_int32(k >> ((9 - chunk_size) & 0x1F)) + _c_int32(k << (chunk_size & 0x1F)))


def _key_div(key: int, offset: int) -> int:
    """param_3 / (offset+1) với param_3 là int32 (C integer division)."""
    k = _c_int32(key)
    denom = offset + 1
    if denom == 0:
        return 0
    return int(k / denom)  # toward zero giống C cho int32


def kk_encrypt_body(buf: Buffer, length: int, key: int) -> None:
    """Mã hóa tại chỗ (kk_encrypt_body @ 0x4eb5c)."""
    key &= 0xFFFFFFFF
    if length <= 0:
        return

    offset = 0
    chunk_size = 1
    while True:
        end_mark = chunk_size + offset
        end = length if end_mark > length else end_mark
        chunk_len = end - offset
        if chunk_len > 0:
            packed = _pack_chunk_forward_xor(buf, offset, end)
            div_term = _key_div(key, offset)
            state = _c_int64(packed ^ _key_rot(key, chunk_size)) + offset + div_term
            idx = end - 1
            for _ in range(chunk_len):
                buf[idx] = state & 0xFF
                state = _c_int64(_c_int64(state) >> 8)
                idx -= 1

        chunk_size = 1 if chunk_size >= 8 else chunk_size + 1
        offset = end
        if end_mark >= length:
            break


def kk_decrypt_body(buf: Buffer, length: int, key: int) -> None:
    """Giải mã tại chỗ (in-place), giống native kk_decrypt_body."""
    key &= 0xFFFFFFFF
    if length <= 0:
        return

    offset = 0
    chunk_size = 1
    while True:
        end_mark = chunk_size + offset
        end = length if end_mark > length else end_mark
        chunk_len = end - offset
        if chunk_len > 0:
            packed = _pack_chunk_backward_xor(buf, offset, end)
            div_term = _key_div(key, offset)
            state = _c_int64(packed - offset - div_term) ^ _key_rot(key, chunk_size)
            for i in range(chunk_len):
                buf[offset + i] = state & 0xFF
                state = _c_int64(_c_int64(state) >> 8)

        chunk_size = 1 if chunk_size >= 8 else chunk_size + 1
        offset = end
        if end_mark >= length:
            break


def dec2_blob(data: bytes, key: int = KK_MAGIC_KEY) -> bytes:
    """Toàn bộ luồng dec2: 4 byte magic + body."""
    if len(data) < 4:
        return b""
    if wire_magic_to_key(data[:4]) != (key & 0xFFFFFFFF):
        return b""
    body = bytearray(data[4:])
    kk_decrypt_body(body, len(body), key)
    return bytes(body)


def enc2_blob(plain: bytes, key: int = KK_MAGIC_KEY) -> bytes:
    body = bytearray(plain)
    kk_encrypt_body(body, len(body), key)
    return key_to_wire_magic(key) + bytes(body)


def enc2_request_b64(params: dict[str, Any], secret: str) -> str:
    """Giống StreamHelper.a(JSON(params)) — Base64(enc2(utf8)), flag DEFAULT có xuống dòng."""
    key = hash31(secret)
    ordered = OrderedDict(params)
    payload = json.dumps(ordered, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    raw = base64.b64encode(enc2_blob(payload, key)).decode("ascii")
    # Android Base64.DEFAULT (flags=0): wrap 76 cột + newline cuối
    lines = [raw[i : i + 76] for i in range(0, len(raw), 76)]
    return "\n".join(lines) + "\n"


def dec2_gzip_json(data: bytes, key: int = KK_MAGIC_KEY) -> str:
    plain = dec2_blob(data, key)
    if not plain:
        return ""
    return gzip.decompress(plain).decode("utf-8")


# StreamHelper2 (offline DB blobs: pulse_data, param) — không có header magic 4 byte
STREAMHELPER2_KEY = 0x0133A133


def streamhelper2_dec_blob(data: bytes, key: int = STREAMHELPER2_KEY) -> bytes:
    """Giống EncryptDataUtil2.dec / h0.a(byte[]) — kk_decrypt_body in-place."""
    if not data:
        return b""
    buf = bytearray(data)
    kk_decrypt_body(buf, len(buf), key)
    return bytes(buf)
