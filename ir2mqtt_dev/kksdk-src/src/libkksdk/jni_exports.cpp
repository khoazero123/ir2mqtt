#include "../include/ghidra_types.h"
#include "package_signature_check.hpp"
#include "streamhelper_jni.hpp"
#include "irdevice_jni.hpp"
#include "codehelper_jni.hpp"

#include <jni.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Buildable JNI surface for libkksdk.so.
 *
 * The full decompiler bodies are kept in jni_exports_recovered.c. This file is
 * intentionally conservative: it preserves the exported JNI names and ABI-ish
 * signatures while returning safe defaults for routines that still need manual
 * reconstruction.
 */

static undefined4 g_streamhelper_ready;
static undefined4 g_streamhelper2_ready;
static undefined4 g_irdevice_ready;

void streamhelper_transform_encrypt(uint8_t *buffer, int length, int key);
void streamhelper_transform_decrypt(uint8_t *buffer, int length, int key);

jobjectArray Java_com_hzy_tvmao_ir_encode_CodeHelper_enc(JNIEnv *env, jclass clazz,
        jint remote_type, jlong remote_handle, jint power, jint mode, jint temperature,
        jint wind_speed, jint lr_wind_mode, jint ud_wind_mode, jint function_id,
        jbyteArray ext_bytes, jstring ext_string) {
    (void)clazz;
    return (jobjectArray)kksdk_codehelper_encode(env, (unsigned int)remote_type, remote_handle,
            (unsigned int)power, (unsigned int)mode, (unsigned int)temperature,
            (unsigned int)wind_speed, (unsigned int)lr_wind_mode, (unsigned int)ud_wind_mode,
            (unsigned int)function_id, ext_bytes, ext_string);
}

jobjectArray Java_com_hzy_tvmao_ir_encode_CodeHelper_enc2(JNIEnv *env, jclass clazz,
        jint remote_type, jlong remote_handle, jint power, jint mode, jint temperature,
        jint wind_speed, jint lr_wind_mode, jint ud_wind_mode, jint function_id,
        jbyteArray ext_bytes, jstring ext_string) {
    return Java_com_hzy_tvmao_ir_encode_CodeHelper_enc(env, clazz, remote_type, remote_handle,
            power, mode, temperature, wind_speed, lr_wind_mode, ud_wind_mode, function_id,
            ext_bytes, ext_string);
}

// JNI export returns boolean (Java expects init success); was void in first normalized lift.
bool Java_com_hzy_tvmao_ir_encode_CodeHelper_init(undefined8 env, undefined8 clazz,
        undefined8 context, undefined8 key) {
    (void)clazz;
    g_streamhelper_ready = (undefined4)kksdk_verify_package_signature(
            (void *)env, (void *)context, (void *)key, nullptr);
    return g_streamhelper_ready == 1;
}

undefined8 Java_com_hzy_tvmao_ir_encode_CodeHelper_initRemote2(undefined8 env,
        undefined8 clazz, undefined4 remote_id, undefined4 remote_type, undefined8 names,
        undefined8 out_remote) {
    (void)clazz;
    return (undefined8)kksdk_codehelper_init_remote(
            (void *)env, remote_id, remote_type, (void *)names, (void *)out_remote);
}

void Java_com_hzy_tvmao_ir_encode_CodeHelper_release2(undefined8 env, undefined8 clazz,
        undefined4 remote_id, undefined8 remote) {
    (void)env; (void)clazz;
    kksdk_codehelper_release_remote(remote_id, (void *)remote);
}

undefined8 Java_com_hzy_tvmao_ir_encode_IrDevice_createRemoteImpl(undefined8 env,
        undefined8 clazz, undefined8 context, undefined8 data) {
    (void)clazz; (void)context;
    return (undefined8)kksdk_irdevice_create_remote((void *)env, (void *)data);
}

undefined8 Java_com_hzy_tvmao_ir_encode_IrDevice_enc(undefined8 env, undefined8 clazz,
        undefined8 data, undefined8 out_status) {
    (void)clazz;
    return (undefined8)kksdk_irdevice_encode((void *)env, (void *)data, (void *)out_status);
}

undefined2 Java_com_hzy_tvmao_ir_encode_IrDevice_getFrequency(undefined8 env, undefined8 clazz) {
    (void)env; (void)clazz;
    return (undefined2)kksdk_irdevice_get_frequency();
}

bool Java_com_hzy_tvmao_ir_encode_IrDevice_init(undefined8 env, undefined8 clazz,
        undefined8 context, undefined8 key) {
    (void)clazz;
    g_irdevice_ready = (undefined4)kksdk_irdevice_init(
            (void *)env, (void *)context, (void *)key);
    return g_irdevice_ready == 1;
}

undefined8 Java_com_hzy_tvmao_ir_encode_IrDevice_parse(undefined8 env, undefined8 clazz,
        undefined8 data) {
    (void)clazz;
    return (undefined8)kksdk_irdevice_parse((void *)env, (void *)data);
}

void Java_com_hzy_tvmao_model_legacy_api_StreamHelper_dec(undefined8 env,
        undefined8 clazz, undefined8 data) {
    (void)clazz;
    kksdk_streamhelper_legacy_decode((void *)env, (void *)data, 0);
}

void Java_com_hzy_tvmao_model_legacy_api_StreamHelper_dec1(undefined8 env,
        undefined8 clazz, undefined8 data) {
    (void)clazz;
    kksdk_streamhelper_legacy_decode((void *)env, (void *)data, 1);
}

undefined8 Java_com_hzy_tvmao_model_legacy_api_StreamHelper_dec2(undefined8 env,
        undefined8 clazz, undefined8 data) {
    (void)clazz;
    return (undefined8)kksdk_streamhelper_decrypt2((void *)env, (void *)data);
}

void Java_com_hzy_tvmao_model_legacy_api_StreamHelper_enc(undefined8 env,
        undefined8 clazz, undefined8 data) {
    (void)clazz;
    kksdk_streamhelper_legacy_encode((void *)env, (void *)data, 0);
}

void Java_com_hzy_tvmao_model_legacy_api_StreamHelper_enc1(undefined8 env,
        undefined8 clazz, undefined8 data) {
    (void)clazz;
    kksdk_streamhelper_legacy_encode((void *)env, (void *)data, 1);
}

undefined8 Java_com_hzy_tvmao_model_legacy_api_StreamHelper_enc2(undefined8 env,
        undefined8 clazz, undefined8 data) {
    (void)clazz;
    return (undefined8)kksdk_streamhelper_encrypt2((void *)env, (void *)data);
}

undefined4 Java_com_hzy_tvmao_model_legacy_api_StreamHelper_init(undefined8 env,
        undefined8 clazz, undefined8 context, undefined8 key) {
    (void)clazz;
    g_streamhelper_ready = (undefined4)kksdk_streamhelper_init(
            (void *)env, (void *)context, (void *)key);
    return g_streamhelper_ready;
}

undefined8 Java_com_hzy_tvmao_model_legacy_api_StreamHelper2_dec(undefined8 env,
        undefined8 clazz, undefined8 data) {
    (void)clazz;
    return (undefined8)kksdk_streamhelper2_decrypt((void *)env, (void *)data);
}

undefined8 Java_com_hzy_tvmao_model_legacy_api_StreamHelper2_enc(undefined8 env,
        undefined8 clazz, undefined8 data) {
    (void)clazz;
    return (undefined8)kksdk_streamhelper2_encrypt((void *)env, (void *)data);
}

void Java_com_hzy_tvmao_model_legacy_api_StreamHelper2_init(undefined8 env,
        undefined8 clazz, undefined8 context, undefined8 key) {
    (void)clazz;
    g_streamhelper2_ready = (undefined4)kksdk_streamhelper2_init(
            (void *)env, (void *)context, (void *)key);
}

#ifdef __cplusplus
}
#endif
