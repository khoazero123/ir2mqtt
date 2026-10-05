#include "kksdk/kk_crypto.h"

#include "../include/ghidra_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* JNI enc1/enc2 call these; implementation lives in kk_body.c for Codex parity. */
void streamhelper_transform_encrypt(uint8_t *buffer, int length, int key) {
    // Delegate to verified kk_encrypt_body instead of the old decompiler lift.
    kk_encrypt_body(buffer, length, (uint32_t)key);
}

void streamhelper_transform_decrypt(uint8_t *buffer, int length, int key) {
    // Delegate to verified kk_decrypt_body instead of the old decompiler lift.
    kk_decrypt_body(buffer, length, (uint32_t)key);
}

#ifdef __cplusplus
}
#endif
