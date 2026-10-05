/* Kookong enc2/dec2 body crypto — ported from Codex/kksdk (matches vendor kk_crypto.py). */
#ifndef KKSDK_KK_CRYPTO_H
#define KKSDK_KK_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t kk_hash31(const char *secret);

uint32_t kk_wire_magic_to_key(const uint8_t magic[4]);
void kk_key_to_wire_magic(uint32_t key, uint8_t magic[4]);

void kk_encrypt_body(uint8_t *buf, int length, uint32_t key);
void kk_decrypt_body(uint8_t *buf, int length, uint32_t key);

int kk_enc2_blob(const uint8_t *plain, size_t plain_len, uint32_t key,
                 uint8_t *out, size_t out_cap, size_t *out_len);
int kk_dec2_blob(const uint8_t *data, size_t data_len, uint32_t key,
                 uint8_t *out, size_t out_cap, size_t *out_len);

#define KK_STREAMHELPER2_KEY 0x0133A133u

#ifdef __cplusplus
}
#endif

#endif
