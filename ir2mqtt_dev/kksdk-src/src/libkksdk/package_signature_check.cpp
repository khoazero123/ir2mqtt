#include "package_signature_check.hpp"

#include <jni.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr const char *kHashPrefix = "Kf9j8Si1";
constexpr const char *kHashSuffix = "5EKM9h4u";

constexpr const char *kWhitelistedPackages[] = {
    "com.kookong.app",
    "com.letv.android.remotecontrol",
    "com.duokan.phone.remotecontroller",
    "com.huawei.android.remotecontroller",
    "com.example.testdb",
    "com.kookong.app.gionee",
    "com.kookong.app.nubia",
    "com.kkcoresdk.sample.huawei",
    "com.huawei.supersmarthome",
    "com.kookong.remote.old.ganzhen",
    "com.kookong.remote.old.panasonic",
    "com.vivo.vhome",
    "com.hisense.gorgeouscontrol",
    "com.vivo.widget.vhome",
    "com.hihonor.android.remotecontroller",
    "com.oplus.consumerIRApp",
    // Repackaged debug APK used for AVD / online API testing (StreamHelper2 whitelist).
    "net.megavn.consumerIRCodex",
    "com.akubela.panel",
    "com.smartlife.nebula",
    "com.gotron.remotecontrol",
    "com.transsion.irremote",
    "com.skyui.infraredcontrol",
};

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

std::string to_hex_upper(const std::vector<std::uint8_t> &bytes) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (std::uint8_t value : bytes) {
        out.push_back(kHex[value >> 4]);
        out.push_back(kHex[value & 0x0f]);
    }
    return out;
}

jbyteArray make_byte_array(JNIEnv *env, const std::string &value) {
    jbyteArray array = env->NewByteArray(static_cast<jsize>(value.size()));
    if (array == nullptr) {
        clear_pending_exception(env);
        return nullptr;
    }

    env->SetByteArrayRegion(array, 0, static_cast<jsize>(value.size()),
            reinterpret_cast<const jbyte *>(value.data()));
    if (clear_pending_exception(env)) {
        env->DeleteLocalRef(array);
        return nullptr;
    }

    return array;
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

jbyteArray message_digest(JNIEnv *env, const char *algorithm, jbyteArray input) {
    jclass digest_class = env->FindClass("java/security/MessageDigest");
    if (digest_class == nullptr || clear_pending_exception(env)) {
        return nullptr;
    }

    jstring algorithm_name = env->NewStringUTF(algorithm);
    if (algorithm_name == nullptr || clear_pending_exception(env)) {
        env->DeleteLocalRef(digest_class);
        return nullptr;
    }

    jmethodID get_instance = env->GetStaticMethodID(digest_class, "getInstance",
            "(Ljava/lang/String;)Ljava/security/MessageDigest;");
    if (get_instance == nullptr || clear_pending_exception(env)) {
        env->DeleteLocalRef(algorithm_name);
        env->DeleteLocalRef(digest_class);
        return nullptr;
    }

    jobject digest = env->CallStaticObjectMethod(digest_class, get_instance, algorithm_name);
    if (digest == nullptr || clear_pending_exception(env)) {
        env->DeleteLocalRef(algorithm_name);
        env->DeleteLocalRef(digest_class);
        return nullptr;
    }

    jmethodID digest_method = env->GetMethodID(digest_class, "digest", "([B)[B");
    if (digest_method == nullptr || clear_pending_exception(env)) {
        env->DeleteLocalRef(digest);
        env->DeleteLocalRef(algorithm_name);
        env->DeleteLocalRef(digest_class);
        return nullptr;
    }

    auto result = static_cast<jbyteArray>(
            env->CallObjectMethod(digest, digest_method, input));
    if (clear_pending_exception(env)) {
        result = nullptr;
    }

    env->DeleteLocalRef(digest);
    env->DeleteLocalRef(algorithm_name);
    env->DeleteLocalRef(digest_class);
    return result;
}

jbyteArray read_first_signature(JNIEnv *env, jobject context, const std::string &package_name) {
    jclass context_class = env->GetObjectClass(context);
    if (context_class == nullptr || clear_pending_exception(env)) {
        return nullptr;
    }

    jmethodID get_package_manager = env->GetMethodID(context_class, "getPackageManager",
            "()Landroid/content/pm/PackageManager;");
    if (get_package_manager == nullptr || clear_pending_exception(env)) {
        env->DeleteLocalRef(context_class);
        return nullptr;
    }

    jobject package_manager = env->CallObjectMethod(context, get_package_manager);
    if (package_manager == nullptr || clear_pending_exception(env)) {
        env->DeleteLocalRef(context_class);
        return nullptr;
    }

    jclass manager_class = env->GetObjectClass(package_manager);
    if (manager_class == nullptr || clear_pending_exception(env)) {
        env->DeleteLocalRef(package_manager);
        env->DeleteLocalRef(context_class);
        return nullptr;
    }

    jmethodID get_package_info = env->GetMethodID(manager_class, "getPackageInfo",
            "(Ljava/lang/String;I)Landroid/content/pm/PackageInfo;");
    if (get_package_info == nullptr || clear_pending_exception(env)) {
        env->DeleteLocalRef(manager_class);
        env->DeleteLocalRef(package_manager);
        env->DeleteLocalRef(context_class);
        return nullptr;
    }

    jstring package_name_java = env->NewStringUTF(package_name.c_str());
    if (package_name_java == nullptr || clear_pending_exception(env)) {
        env->DeleteLocalRef(manager_class);
        env->DeleteLocalRef(package_manager);
        env->DeleteLocalRef(context_class);
        return nullptr;
    }

    jobject package_info = env->CallObjectMethod(package_manager, get_package_info,
            package_name_java, 0x40);
    if (package_info == nullptr || clear_pending_exception(env)) {
        env->DeleteLocalRef(package_name_java);
        env->DeleteLocalRef(manager_class);
        env->DeleteLocalRef(package_manager);
        env->DeleteLocalRef(context_class);
        return nullptr;
    }

    jclass package_info_class = env->GetObjectClass(package_info);
    if (package_info_class == nullptr || clear_pending_exception(env)) {
        env->DeleteLocalRef(package_info);
        env->DeleteLocalRef(package_name_java);
        env->DeleteLocalRef(manager_class);
        env->DeleteLocalRef(package_manager);
        env->DeleteLocalRef(context_class);
        return nullptr;
    }

    jfieldID signatures_field = env->GetFieldID(package_info_class, "signatures",
            "[Landroid/content/pm/Signature;");
    if (signatures_field == nullptr || clear_pending_exception(env)) {
        env->DeleteLocalRef(package_info_class);
        env->DeleteLocalRef(package_info);
        env->DeleteLocalRef(package_name_java);
        env->DeleteLocalRef(manager_class);
        env->DeleteLocalRef(package_manager);
        env->DeleteLocalRef(context_class);
        return nullptr;
    }

    auto signatures = static_cast<jobjectArray>(
            env->GetObjectField(package_info, signatures_field));
    if (signatures == nullptr || clear_pending_exception(env) ||
            env->GetArrayLength(signatures) <= 0) {
        clear_pending_exception(env);
        env->DeleteLocalRef(package_info_class);
        env->DeleteLocalRef(package_info);
        env->DeleteLocalRef(package_name_java);
        env->DeleteLocalRef(manager_class);
        env->DeleteLocalRef(package_manager);
        env->DeleteLocalRef(context_class);
        return nullptr;
    }

    jobject signature = env->GetObjectArrayElement(signatures, 0);
    if (signature == nullptr || clear_pending_exception(env)) {
        env->DeleteLocalRef(signatures);
        env->DeleteLocalRef(package_info_class);
        env->DeleteLocalRef(package_info);
        env->DeleteLocalRef(package_name_java);
        env->DeleteLocalRef(manager_class);
        env->DeleteLocalRef(package_manager);
        env->DeleteLocalRef(context_class);
        return nullptr;
    }

    jclass signature_class = env->GetObjectClass(signature);
    if (signature_class == nullptr || clear_pending_exception(env)) {
        env->DeleteLocalRef(signature);
        env->DeleteLocalRef(signatures);
        env->DeleteLocalRef(package_info_class);
        env->DeleteLocalRef(package_info);
        env->DeleteLocalRef(package_name_java);
        env->DeleteLocalRef(manager_class);
        env->DeleteLocalRef(package_manager);
        env->DeleteLocalRef(context_class);
        return nullptr;
    }

    jmethodID to_byte_array = env->GetMethodID(signature_class, "toByteArray", "()[B");
    jbyteArray signature_bytes = nullptr;
    if (to_byte_array != nullptr && !clear_pending_exception(env)) {
        signature_bytes = static_cast<jbyteArray>(
                env->CallObjectMethod(signature, to_byte_array));
        if (clear_pending_exception(env)) {
            signature_bytes = nullptr;
        }
    }

    env->DeleteLocalRef(signature_class);
    env->DeleteLocalRef(signature);
    env->DeleteLocalRef(signatures);
    env->DeleteLocalRef(package_info_class);
    env->DeleteLocalRef(package_info);
    env->DeleteLocalRef(package_name_java);
    env->DeleteLocalRef(manager_class);
    env->DeleteLocalRef(package_manager);
    env->DeleteLocalRef(context_class);
    return signature_bytes;
}

std::string read_package_name(JNIEnv *env, jobject context) {
    jclass context_class = env->GetObjectClass(context);
    if (context_class == nullptr || clear_pending_exception(env)) {
        return {};
    }

    jmethodID get_package_name = env->GetMethodID(context_class, "getPackageName",
            "()Ljava/lang/String;");
    if (get_package_name == nullptr || clear_pending_exception(env)) {
        env->DeleteLocalRef(context_class);
        return {};
    }

    auto package_name_java = static_cast<jstring>(
            env->CallObjectMethod(context, get_package_name));
    if (package_name_java == nullptr || clear_pending_exception(env)) {
        env->DeleteLocalRef(context_class);
        return {};
    }

    std::string package_name = get_java_string(env, package_name_java);
    env->DeleteLocalRef(package_name_java);
    env->DeleteLocalRef(context_class);
    return package_name;
}

bool verify_expected_hash(JNIEnv *env, const std::string &package_name,
        const std::vector<std::uint8_t> &signature_digest, const std::string &expected_hash) {
    if (expected_hash.empty()) {
        return false;
    }

    const char *algorithm = expected_hash.size() == 0x20 ? "MD5" : "SHA-256";
    const std::string signature_hex = to_hex_upper(signature_digest);

    for (int attempt = 0; attempt < 100; ++attempt) {
        std::string suffix = kHashSuffix;
        if (attempt != 0) {
            suffix += std::to_string(attempt);
        }

        std::string material = package_name;
        material += kHashPrefix;
        material += signature_hex;
        material += suffix;

        jbyteArray material_bytes = make_byte_array(env, material);
        if (material_bytes == nullptr) {
            return false;
        }

        jbyteArray digest = message_digest(env, algorithm, material_bytes);
        env->DeleteLocalRef(material_bytes);
        if (digest == nullptr) {
            return false;
        }

        std::vector<std::uint8_t> digest_bytes = read_byte_array(env, digest);
        env->DeleteLocalRef(digest);
        if (to_hex_upper(digest_bytes) == expected_hash) {
            return true;
        }
    }

    return false;
}

}  // namespace

/* True when build_android.ps1 passes -DKKSDK_ENFORCE_PACKAGE_VERIFY=0 (default). */
extern "C" int kksdk_dev_bypass_package_verify_enabled(void) {
#if KKSDK_ENFORCE_PACKAGE_VERIFY
    return 0;
#else
    return 1;
#endif
}

extern "C" int kksdk_verify_package_signature(void *env_ptr, void *context_ptr,
        void *expected_hash_ptr, char **package_name_out) {
    if (package_name_out != nullptr) {
        *package_name_out = nullptr;
    }
    if (env_ptr == nullptr || context_ptr == nullptr || expected_hash_ptr == nullptr) {
        return 0;
    }

    auto *env = reinterpret_cast<JNIEnv *>(env_ptr);
    auto context = reinterpret_cast<jobject>(context_ptr);
    auto expected_hash_java = reinterpret_cast<jstring>(expected_hash_ptr);

    std::string package_name = read_package_name(env, context);
    if (package_name.empty()) {
        return 0;
    }

    if (package_name_out != nullptr) {
        char *copy = static_cast<char *>(std::malloc(package_name.size() + 1));
        if (copy != nullptr) {
            std::memcpy(copy, package_name.c_str(), package_name.size() + 1);
            *package_name_out = copy;
        }
    }

    jbyteArray signature = read_first_signature(env, context, package_name);
    if (signature == nullptr) {
        return 0;
    }

    const std::string expected_hash = get_java_string(env, expected_hash_java);
    const char *signature_algorithm = expected_hash.size() == 0x20 ? "MD5" : "SHA-256";
    jbyteArray signature_digest_java = message_digest(env, signature_algorithm, signature);
    env->DeleteLocalRef(signature);
    if (signature_digest_java == nullptr) {
        return 0;
    }

    std::vector<std::uint8_t> signature_digest = read_byte_array(env, signature_digest_java);
    env->DeleteLocalRef(signature_digest_java);

    // Dev build: still read package/signature for logs, but accept debug/repackaged APKs.
    if (kksdk_dev_bypass_package_verify_enabled()) {
        return 1;
    }

    return verify_expected_hash(env, package_name, signature_digest, expected_hash) ? 1 : 0;
}

extern "C" int kksdk_is_whitelisted_package(const char *package_name) {
    if (package_name == nullptr) {
        return 0;
    }

    for (const char *allowed : kWhitelistedPackages) {
        if (std::strcmp(package_name, allowed) == 0) {
            return 1;
        }
    }
    return 0;
}

extern "C" void kksdk_free_string(char *value) {
    std::free(value);
}
