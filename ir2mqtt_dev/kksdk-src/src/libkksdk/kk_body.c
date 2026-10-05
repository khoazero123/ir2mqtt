/* Port of Codex/kksdk/src/crypto/kk_body.c (matches vendor/scripts/kk_crypto.py).
 * Replaces the broken Ghidra-lifted streamhelper_transform; wrong int32/int64
 * truncation caused server "系统错误" on /m/brands after enc2. */
#include "kksdk/kk_crypto.h"

#include <stdint.h>
#include <string.h>

/* Mirror Python int32/int64 wrap semantics used in kk_crypto.py chunk loops. */
static int32_t c_int32(int64_t x) {
    return (int32_t)((uint32_t)x);
}

static int64_t c_int64(int64_t x) {
    return (int64_t)((uint64_t)x);
}

static int shift_mask(int i) { return (i * 8) & 0x38; }

static uint64_t pack_chunk_backward(const uint8_t *buf, int start, int end) {
    int n = end - start;
    uint64_t acc = 0;
    for (int i = 0; i < n; i++) {
        acc ^= (uint64_t)buf[end - 1 - i] << shift_mask(i);
    }
    return acc;
}

static uint64_t pack_chunk_forward(const uint8_t *buf, int start, int end) {
    int n = end - start;
    uint64_t acc = 0;
    for (int i = 0; i < n; i++) {
        acc ^= (uint64_t)buf[start + i] << shift_mask(i);
    }
    return acc;
}

static int64_t key_rot(uint32_t key, int chunk_size) {
    int32_t k = c_int32((int64_t)key);
    int32_t hi = c_int32((int64_t)(k >> ((9 - chunk_size) & 0x1F)));
    int32_t lo = c_int32((int64_t)(k << (chunk_size & 0x1F)));
    return c_int64((int64_t)hi + (int64_t)lo);
}

static int key_div(uint32_t key, int offset) {
    int32_t k = c_int32((int64_t)key);
    int denom = offset + 1;
    if (denom == 0) {
        return 0;
    }
    return k / denom;
}

uint32_t kk_hash31(const char *secret) {
    uint32_t h = 0;
    if (!secret) {
        return 0;
    }
    for (; *secret; secret++) {
        h = (31u * h + (unsigned char)*secret) & 0xFFFFFFFFu;
    }
    return h;
}

uint32_t kk_wire_magic_to_key(const uint8_t magic[4]) {
    if (!magic) {
        return 0;
    }
    return (uint32_t)magic[0] | ((uint32_t)magic[2] << 8) | ((uint32_t)magic[3] << 16) |
           ((uint32_t)magic[1] << 24);
}

void kk_key_to_wire_magic(uint32_t key, uint8_t magic[4]) {
    if (!magic) {
        return;
    }
    magic[0] = (uint8_t)(key & 0xFFu);
    magic[1] = (uint8_t)((key >> 24) & 0xFFu);
    magic[2] = (uint8_t)((key >> 8) & 0xFFu);
    magic[3] = (uint8_t)((key >> 16) & 0xFFu);
}

void kk_encrypt_body(uint8_t *buf, int length, uint32_t key) {
    if (!buf || length <= 0) {
        return;
    }
    key &= 0xFFFFFFFFu;
    int offset = 0;
    int chunk_size = 1;
    for (;;) {
        int end_mark = chunk_size + offset;
        int end = end_mark > length ? length : end_mark;
        int chunk_len = end - offset;
        if (chunk_len > 0) {
            uint64_t packed = pack_chunk_forward(buf, offset, end);
            int div_term = key_div(key, offset);
            int64_t state =
                c_int64((int64_t)packed ^ key_rot(key, chunk_size)) + offset + div_term;
            int idx = end - 1;
            for (int i = 0; i < chunk_len; i++) {
                buf[idx] = (uint8_t)(state & 0xFF);
                state = c_int64(state >> 8);
                idx--;
            }
        }
        chunk_size = chunk_size >= 8 ? 1 : chunk_size + 1;
        offset = end;
        if (end_mark >= length) {
            break;
        }
    }
}

void kk_decrypt_body(uint8_t *buf, int length, uint32_t key) {
    if (!buf || length <= 0) {
        return;
    }
    key &= 0xFFFFFFFFu;
    int offset = 0;
    int chunk_size = 1;
    for (;;) {
        int end_mark = chunk_size + offset;
        int end = end_mark > length ? length : end_mark;
        int chunk_len = end - offset;
        if (chunk_len > 0) {
            uint64_t packed = pack_chunk_backward(buf, offset, end);
            int div_term = key_div(key, offset);
            int64_t state =
                c_int64((int64_t)packed - offset - div_term) ^ key_rot(key, chunk_size);
            for (int i = 0; i < chunk_len; i++) {
                buf[offset + i] = (uint8_t)(state & 0xFF);
                state = c_int64(state >> 8);
            }
        }
        chunk_size = chunk_size >= 8 ? 1 : chunk_size + 1;
        offset = end;
        if (end_mark >= length) {
            break;
        }
    }
}

int kk_enc2_blob(const uint8_t *plain, size_t plain_len, uint32_t key, uint8_t *out,
                 size_t out_cap, size_t *out_len) {
    if (!plain || !out || !out_len || out_cap < plain_len + 4) {
        return -1;
    }
    kk_key_to_wire_magic(key, out);
    if (plain_len > 0) {
        memcpy(out + 4, plain, plain_len);
        kk_encrypt_body(out + 4, (int)plain_len, key);
    }
    *out_len = plain_len + 4;
    return 0;
}

int kk_dec2_blob(const uint8_t *data, size_t data_len, uint32_t key, uint8_t *out,
                 size_t out_cap, size_t *out_len) {
    if (!data || !out || !out_len || data_len < 4) {
        return -1;
    }
#if 0 /* verify wire magic header */
    if (kk_wire_magic_to_key(data) != (key & 0xFFFFFFFFu)) {
        return -2;
    }
#endif
    size_t body_len = data_len - 4;
    if (body_len > out_cap) {
        return -3;
    }
    if (body_len > 0) {
        memcpy(out, data + 4, body_len);
        kk_decrypt_body(out, (int)body_len, key);
    }
    *out_len = body_len;
    return 0;
}
