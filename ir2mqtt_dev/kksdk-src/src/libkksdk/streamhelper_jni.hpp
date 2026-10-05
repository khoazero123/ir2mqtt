#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int kksdk_streamhelper_init(void *env, void *context, void *expected_hash);
void kksdk_streamhelper_legacy_encode(void *env, void *data, int encrypt_after_codec);
void kksdk_streamhelper_legacy_decode(void *env, void *data, int decrypt_before_codec);
void *kksdk_streamhelper_encrypt2(void *env, void *data);
void *kksdk_streamhelper_decrypt2(void *env, void *data);
int kksdk_streamhelper2_init(void *env, void *context, void *expected_hash);
int kksdk_streamhelper2_active_key(void);
void *kksdk_streamhelper2_encrypt(void *env, void *data);
void *kksdk_streamhelper2_decrypt(void *env, void *data);
void kksdk_streamhelper_free_buffer(void *buffer);
void kksdk_streamhelper_cleanup_buffers(void (*release_env)(void *env), void *env,
        void *buffer1, void *buffer2, void *buffer3, void *cpp_object);

#ifdef __cplusplus
}
#endif
