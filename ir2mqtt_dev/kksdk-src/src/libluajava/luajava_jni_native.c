#include "luajava_core.h"

#include <jni.h>
#include <stdlib.h>
#include <string.h>

#ifdef __ANDROID__
#include <android/log.h>
#define KK_LOG_TAG "KOOKONG"
#define KK_LOGW(...) __android_log_print(ANDROID_LOG_WARN, KK_LOG_TAG, __VA_ARGS__)
#else
#define KK_LOGW(...) ((void)0)
#endif

static kk_lua_state_t *from_handle(jlong h) {
    return (kk_lua_state_t *)(intptr_t)h;
}

JNIEXPORT jlong JNICALL Java_com_hzy_tvmao_ir_encode_CodeHelper_createLuaState(JNIEnv *env,
                                                                              jclass clazz) {
    (void)env;
    (void)clazz;
    kk_lua_state_t *L = kk_lua_create_state();
    if (!L) {
        KK_LOGW("cls:new lua state error");
        return 0;
    }
    return (jlong)(intptr_t)L;
}

JNIEXPORT void JNICALL Java_com_hzy_tvmao_ir_encode_CodeHelper_releaseLuaState(JNIEnv *env,
                                                                                 jclass clazz,
                                                                                 jlong handle) {
    (void)env;
    (void)clazz;
    if (handle != 0) {
        kk_lua_release_state(from_handle(handle));
    }
}

JNIEXPORT jint JNICALL Java_com_hzy_tvmao_ir_encode_CodeHelper_setLuaGlobalNumberVariable(
    JNIEnv *env, jclass clazz, jlong handle, jstring name, jdouble value) {
    (void)clazz;
    if (!name) {
        KK_LOGW("setLuaGlobalNumberVariable: name null");
        return -1;
    }
    const char *cname = (*env)->GetStringUTFChars(env, name, NULL);
    if (!cname) {
        return -2;
    }
    int rc = kk_lua_set_global_number(from_handle(handle), cname, (double)value);
    (*env)->ReleaseStringUTFChars(env, name, cname);
    return rc;
}

JNIEXPORT jint JNICALL Java_com_hzy_tvmao_ir_encode_CodeHelper_setLuaGlobalArrayBufferVariable(
    JNIEnv *env, jclass clazz, jlong handle, jstring name, jbyteArray arr) {
    (void)clazz;
    if (!name) {
        KK_LOGW("ggabv:addr is 0");
        return -1;
    }
    if (!arr) {
        KK_LOGW("ggabv:get name buf error");
        return -2;
    }
    const char *cname = (*env)->GetStringUTFChars(env, name, NULL);
    if (!cname) {
        return -2;
    }
    jsize n = (*env)->GetArrayLength(env, arr);
    jbyte *bytes = (*env)->GetByteArrayElements(env, arr, NULL);
    int rc = kk_lua_set_global_byte_array(from_handle(handle), cname, (const uint8_t *)bytes,
                                          (size_t)n);
    if (bytes) {
        (*env)->ReleaseByteArrayElements(env, arr, bytes, JNI_ABORT);
    }
    (*env)->ReleaseStringUTFChars(env, name, cname);
    return rc;
}

JNIEXPORT jint JNICALL Java_com_hzy_tvmao_ir_encode_CodeHelper_setLuaGlobalNumberMapVariable(
    JNIEnv *env, jclass clazz, jlong handle, jstring name, jdoubleArray keys,
    jdoubleArray values) {
    (void)clazz;
    if (!name || !keys || !values) {
        return -1;
    }
    jsize nkeys = (*env)->GetArrayLength(env, keys);
    jsize nvals = (*env)->GetArrayLength(env, values);
    if (nkeys != nvals) {
        return -2;
    }
    const char *cname = (*env)->GetStringUTFChars(env, name, NULL);
    if (!cname) {
        return -2;
    }
    jdouble *k = (*env)->GetDoubleArrayElements(env, keys, NULL);
    jdouble *v = (*env)->GetDoubleArrayElements(env, values, NULL);
    int rc = kk_lua_set_global_number_map(from_handle(handle), cname, k, v, (size_t)nkeys);
    if (k) {
        (*env)->ReleaseDoubleArrayElements(env, keys, k, JNI_ABORT);
    }
    if (v) {
        (*env)->ReleaseDoubleArrayElements(env, values, v, JNI_ABORT);
    }
    (*env)->ReleaseStringUTFChars(env, name, cname);
    return rc;
}

JNIEXPORT jstring JNICALL Java_com_hzy_tvmao_ir_encode_CodeHelper_runLuaScript(JNIEnv *env,
                                                                               jclass clazz,
                                                                               jlong handle,
                                                                               jstring script) {
    (void)clazz;
    if (!script) {
        KK_LOGW("rls:get script buf error");
        return NULL;
    }
    const char *src = (*env)->GetStringUTFChars(env, script, NULL);
    if (!src) {
        return NULL;
    }
    char *msg = kk_lua_run_script(from_handle(handle), src);
    (*env)->ReleaseStringUTFChars(env, script, src);
    if (!msg) {
        return NULL;
    }
    jstring out = (*env)->NewStringUTF(env, msg);
    if (msg[0] != 'O' || msg[1] != 'K' || msg[2] != '\0') {
        free(msg);
    }
    return out;
}

JNIEXPORT jbyteArray JNICALL
Java_com_hzy_tvmao_ir_encode_CodeHelper_getLuaGlobalArrayBufferVariable(JNIEnv *env, jclass clazz,
                                                                        jlong handle,
                                                                        jstring name) {
    (void)clazz;
    if (!name) {
        KK_LOGW("ggabv:addr is 0");
        return NULL;
    }
    const char *cname = (*env)->GetStringUTFChars(env, name, NULL);
    if (!cname) {
        return NULL;
    }
    uint8_t *data = NULL;
    size_t len = 0;
    int rc = kk_lua_get_global_byte_array(from_handle(handle), cname, &data, &len);
    (*env)->ReleaseStringUTFChars(env, name, cname);
    if (rc != 0) {
        free(data);
        return NULL;
    }
    if (len == 0) {
        free(data);
        return (*env)->NewByteArray(env, 0);
    }
    jbyteArray out = (*env)->NewByteArray(env, (jsize)len);
    if (!out) {
        free(data);
        return NULL;
    }
    (*env)->SetByteArrayRegion(env, out, 0, (jsize)len, (const jbyte *)data);
    free(data);
    return out;
}
