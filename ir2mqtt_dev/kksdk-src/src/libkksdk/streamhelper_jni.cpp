#include "streamhelper_jni.hpp"
#include "streamhelper_legacy_lzma.hpp"

#include <jni.h>

#include <cstdlib>
#include <cstdint>
#include <string>
#include <vector>

extern "C" int kksdk_dev_bypass_package_verify_enabled(void);
extern "C" int kksdk_verify_package_signature(void *env, void *context,
        void *expected_hash, char **package_name_out);
extern "C" int kksdk_is_whitelisted_package(const char *package_name);
extern "C" void kksdk_free_string(char *value);
extern "C" void streamhelper_transform_encrypt(std::uint8_t *buffer, int length, int key);
extern "C" void streamhelper_transform_decrypt(std::uint8_t *buffer, int length, int key);

namespace {

int g_streamhelper_verified = 0;
std::uint32_t g_streamhelper_key = 0;
int g_streamhelper2_verified = 0;
int g_streamhelper2_whitelisted = 0;

bool clear_pending_exception(JNIEnv *env) {
    if (env->ExceptionCheck() == JNI_FALSE) {
        return false;
    }
    env->ExceptionClear();
    return true;
}

std::string get_java_string(JNIEnv *env, jstring value) {
    if (value == nullptr) {
        return {};
    }

    const char *chars = env->GetStringUTFChars(value, nullptr);
    if (chars == nullptr) {
        clear_pending_exception(env);
        return {};
    }

    std::string result(chars);
    env->ReleaseStringUTFChars(value, chars);
    return result;
}

std::uint32_t rolling_hash_31(const std::string &value) {
    std::uint32_t hash = 0;
    for (unsigned char ch : value) {
        hash = hash * 31u + ch;
    }
    return hash;
}

std::vector<std::uint8_t> read_byte_array(JNIEnv *env, jbyteArray array) {
    std::vector<std::uint8_t> bytes;
    if (array == nullptr) {
        return bytes;
    }

    jsize length = env->GetArrayLength(array);
    if (clear_pending_exception(env) || length <= 0) {
        return bytes;
    }

    bytes.resize(static_cast<std::size_t>(length));
    env->GetByteArrayRegion(array, 0, length, reinterpret_cast<jbyte *>(bytes.data()));
    if (clear_pending_exception(env)) {
        bytes.clear();
    }
    return bytes;
}

jbyteArray make_byte_array(JNIEnv *env, const std::uint8_t *data, std::size_t length) {
    jbyteArray array = env->NewByteArray(static_cast<jsize>(length));
    if (array == nullptr || clear_pending_exception(env)) {
        return nullptr;
    }

    if (length != 0) {
        env->SetByteArrayRegion(array, 0, static_cast<jsize>(length),
                reinterpret_cast<const jbyte *>(data));
        if (clear_pending_exception(env)) {
            env->DeleteLocalRef(array);
            return nullptr;
        }
    }

    return array;
}

jbyteArray make_empty_byte_array(JNIEnv *env) {
    return make_byte_array(env, nullptr, 0);
}

/* When init failed on a debug APK, derive enc2 key from APPLICATION_KEY so httpGetBrand works. */
void ensure_streamhelper_crypto_ready(const std::string &key_string) {
    if (g_streamhelper_verified == 1) {
        return;
    }
    if (g_streamhelper_key == 0 && !key_string.empty()) {
        g_streamhelper_key = rolling_hash_31(key_string);
    }
    if (g_streamhelper_key == 0) {
        // Fallback matches OEM APPLICATION_KEY string when Java did not pass one.
        g_streamhelper_key = rolling_hash_31("E8A87CE67252443109C61029771A7143");
    }
    g_streamhelper_verified = 1;
}

void write_byte_array(JNIEnv *env, jbyteArray array, const std::vector<std::uint8_t> &bytes) {
    if (env == nullptr || array == nullptr || bytes.empty()) {
        return;
    }
    jsize length = env->GetArrayLength(array);
    if (clear_pending_exception(env) || length <= 0) {
        return;
    }
    jsize write_length = static_cast<jsize>(bytes.size());
    if (length < write_length) {
        write_length = length;
    }
    env->SetByteArrayRegion(array, 0, write_length,
            reinterpret_cast<const jbyte *>(bytes.data()));
    clear_pending_exception(env);
}

std::uint32_t decode_key_header(const std::vector<std::uint8_t> &bytes) {
    if (bytes.size() < 4) {
        return 0;
    }
    return static_cast<std::uint32_t>(bytes[0]) |
            (static_cast<std::uint32_t>(bytes[2]) << 8) |
            (static_cast<std::uint32_t>(bytes[3]) << 16) |
            (static_cast<std::uint32_t>(bytes[1]) << 24);
}

}  // namespace

extern "C" int kksdk_streamhelper_init(void *env_ptr, void *context_ptr,
        void *expected_hash_ptr) {
    if (env_ptr == nullptr || expected_hash_ptr == nullptr) {
        g_streamhelper_verified = 0;
        g_streamhelper_key = 0;
        return 0;
    }

    auto *env = reinterpret_cast<JNIEnv *>(env_ptr);
    auto expected_hash = reinterpret_cast<jstring>(expected_hash_ptr);
    std::string key_string = get_java_string(env, expected_hash);

    g_streamhelper_key = rolling_hash_31(key_string);
    // Dev bypass: skip OEM signature check so StreamHelper init returns 1 on repackaged APK.
    if (kksdk_dev_bypass_package_verify_enabled()) {
        g_streamhelper_verified = 1;
        return 1;
    }
    g_streamhelper_verified = kksdk_verify_package_signature(
            env_ptr, context_ptr, expected_hash_ptr, nullptr);
    return g_streamhelper_verified;
}

extern "C" void kksdk_streamhelper_legacy_encode(void *env_ptr, void *data_ptr,
        int encrypt_after_codec) {
    auto *env = reinterpret_cast<JNIEnv *>(env_ptr);
    auto data = reinterpret_cast<jbyteArray>(data_ptr);
    if (env == nullptr || data == nullptr || g_streamhelper_verified != 1) {
        return;
    }

    std::vector<std::uint8_t> payload = read_byte_array(env, data);
    if (!payload.empty()) {
        std::vector<std::uint8_t> compressed;
        if (kksdk::streamhelper_lzma_encode_buffer(payload.data(), payload.size(),
                    compressed) &&
                !compressed.empty()) {
            payload = std::move(compressed);
        }
    }
    if (encrypt_after_codec && !payload.empty()) {
        streamhelper_transform_encrypt(payload.data(), static_cast<int>(payload.size()),
                static_cast<int>(g_streamhelper_key));
    }
    write_byte_array(env, data, payload);
}

extern "C" void kksdk_streamhelper_legacy_decode(void *env_ptr, void *data_ptr,
        int decrypt_before_codec) {
    auto *env = reinterpret_cast<JNIEnv *>(env_ptr);
    auto data = reinterpret_cast<jbyteArray>(data_ptr);
    if (env == nullptr || data == nullptr || g_streamhelper_verified != 1) {
        return;
    }

    std::vector<std::uint8_t> payload = read_byte_array(env, data);
    // The original then runs a decompression/codec pipeline that still lives in
    // the raw FUN_0014e380 path. The cleaned wrapper preserves the transform
    // layer recovered from dec1.
    if (decrypt_before_codec && !payload.empty()) {
        streamhelper_transform_decrypt(payload.data(), static_cast<int>(payload.size()),
                static_cast<int>(g_streamhelper_key));
    }
    write_byte_array(env, data, payload);
}

extern "C" void *kksdk_streamhelper_encrypt2(void *env_ptr, void *data_ptr) {
    auto *env = reinterpret_cast<JNIEnv *>(env_ptr);
    auto data = reinterpret_cast<jbyteArray>(data_ptr);
    if (env == nullptr) {
        return nullptr;
    }
    // enc2 may run even when init returned 0 — lazily seed key for dev/repackaged builds.
    if (g_streamhelper_verified != 1) {
        ensure_streamhelper_crypto_ready({});
    }

    std::vector<std::uint8_t> payload = read_byte_array(env, data);
    if (!payload.empty()) {
        streamhelper_transform_encrypt(payload.data(), static_cast<int>(payload.size()),
                static_cast<int>(g_streamhelper_key));
    }

    std::vector<std::uint8_t> output;
    output.reserve(payload.size() + 4);
    output.push_back(static_cast<std::uint8_t>(g_streamhelper_key));
    output.push_back(static_cast<std::uint8_t>(g_streamhelper_key >> 24));
    output.push_back(static_cast<std::uint8_t>(g_streamhelper_key >> 8));
    output.push_back(static_cast<std::uint8_t>(g_streamhelper_key >> 16));
    output.insert(output.end(), payload.begin(), payload.end());

    return make_byte_array(env, output.data(), output.size());
}

extern "C" void *kksdk_streamhelper_decrypt2(void *env_ptr, void *data_ptr) {
    auto *env = reinterpret_cast<JNIEnv *>(env_ptr);
    auto data = reinterpret_cast<jbyteArray>(data_ptr);
    if (env == nullptr) {
        return nullptr;
    }
    // dec2 mirrors enc2: allow crypto after lazy key setup when verify was bypassed.
    if (g_streamhelper_verified != 1) {
        ensure_streamhelper_crypto_ready({});
    }

    std::vector<std::uint8_t> input = read_byte_array(env, data);
    if (input.size() < 4 || decode_key_header(input) != g_streamhelper_key) {
        return make_empty_byte_array(env);
    }

    std::vector<std::uint8_t> payload(input.begin() + 4, input.end());
    if (!payload.empty()) {
        streamhelper_transform_decrypt(payload.data(), static_cast<int>(payload.size()),
                static_cast<int>(g_streamhelper_key));
    }

    return make_byte_array(env, payload.data(), payload.size());
}

extern "C" void kksdk_streamhelper_free_buffer(void *buffer) {
    std::free(buffer);
}

extern "C" void kksdk_streamhelper_cleanup_buffers(void (*release_env)(void *env),
        void *env, void *buffer1, void *buffer2, void *buffer3, void *cpp_object) {
    if (release_env != nullptr) {
        release_env(env);
    }
    std::free(buffer1);
    std::free(buffer2);
    std::free(buffer3);
    if (cpp_object != nullptr) {
        ::operator delete(cpp_object);
    }
}

extern "C" int kksdk_streamhelper2_init(void *env_ptr, void *context_ptr,
        void *expected_hash_ptr) {
    char *package_name = nullptr;
    g_streamhelper2_verified = kksdk_verify_package_signature(
            env_ptr, context_ptr, expected_hash_ptr, &package_name);
    g_streamhelper2_whitelisted = kksdk_is_whitelisted_package(package_name);
    // Dev bypass: force offline StreamHelper2 gate open without OEM package signature.
    if (kksdk_dev_bypass_package_verify_enabled()) {
        g_streamhelper2_verified = 1;
        g_streamhelper2_whitelisted = 1;
    } else if (!g_streamhelper2_whitelisted) {
        g_streamhelper2_verified = 0;
    }
    kksdk_free_string(package_name);
    return g_streamhelper2_verified;
}

extern "C" int kksdk_streamhelper2_active_key() {
    return g_streamhelper2_whitelisted ? 0x0133a133 : 0;
}

extern "C" void *kksdk_streamhelper2_encrypt(void *env_ptr, void *data_ptr) {
    auto *env = reinterpret_cast<JNIEnv *>(env_ptr);
    auto data = reinterpret_cast<jbyteArray>(data_ptr);
    if (env == nullptr || g_streamhelper2_verified != 1) {
        return nullptr;
    }

    std::vector<std::uint8_t> payload = read_byte_array(env, data);
    const int key = kksdk_streamhelper2_active_key();
    if (!payload.empty()) {
        streamhelper_transform_encrypt(payload.data(), static_cast<int>(payload.size()), key);
    }
    return make_byte_array(env, payload.data(), payload.size());
}

extern "C" void *kksdk_streamhelper2_decrypt(void *env_ptr, void *data_ptr) {
    auto *env = reinterpret_cast<JNIEnv *>(env_ptr);
    auto data = reinterpret_cast<jbyteArray>(data_ptr);
    if (env == nullptr || g_streamhelper2_verified != 1) {
        return nullptr;
    }

    std::vector<std::uint8_t> payload = read_byte_array(env, data);
    const int key = kksdk_streamhelper2_active_key();
    if (!payload.empty()) {
        streamhelper_transform_decrypt(payload.data(), static_cast<int>(payload.size()), key);
    }
    return make_byte_array(env, payload.data(), payload.size());
}
