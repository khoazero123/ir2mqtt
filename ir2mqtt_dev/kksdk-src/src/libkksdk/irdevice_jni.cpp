#include "irdevice_jni.hpp"

#ifndef KKSDK_HOST_CORE_ONLY
#include <jni.h>
#endif

#include <cstdint>
#include <string>
#include <vector>

#ifndef KKSDK_HOST_CORE_ONLY
extern "C" int kksdk_verify_package_signature(void *env, void *context,
        void *expected_hash, char **package_name_out);
#endif

namespace {

int g_irdevice_verified = 0;
const unsigned char *g_irdevice_remote_data = nullptr;
unsigned int g_irdevice_remote_size = 0;
kksdk_irdevice_remote_layout g_irdevice_remote_layout{};
std::vector<unsigned char> g_irdevice_remote_storage;

#ifndef KKSDK_HOST_CORE_ONLY
bool clear_pending_exception(JNIEnv *env) {
    if (env->ExceptionCheck() == JNI_FALSE) {
        return false;
    }
    env->ExceptionClear();
    return true;
}

void set_int_array_value(JNIEnv *env, void *array_ptr, jint value) {
    auto array = reinterpret_cast<jintArray>(array_ptr);
    if (env == nullptr || array == nullptr || env->GetArrayLength(array) < 1) {
        clear_pending_exception(env);
        return;
    }
    env->SetIntArrayRegion(array, 0, 1, &value);
    clear_pending_exception(env);
}

void *new_int_array(JNIEnv *env, const jint *values, jsize count) {
    if (env == nullptr || count < 0) {
        return nullptr;
    }
    jintArray array = env->NewIntArray(count);
    if (array == nullptr || clear_pending_exception(env)) {
        return nullptr;
    }
    if (count > 0 && values != nullptr) {
        env->SetIntArrayRegion(array, 0, count, values);
        if (clear_pending_exception(env)) {
            env->DeleteLocalRef(array);
            return nullptr;
        }
    }
    return array;
}

void *new_ir_protocol(JNIEnv *env, jint format, const char *value) {
    if (env == nullptr) {
        return nullptr;
    }

    jclass cls = env->FindClass("com/hzy/tvmao/ir/encode/IrProtocol");
    if (cls == nullptr || clear_pending_exception(env)) {
        return nullptr;
    }

    jmethodID ctor = env->GetMethodID(cls, "<init>", "()V");
    if (ctor == nullptr || clear_pending_exception(env)) {
        env->DeleteLocalRef(cls);
        return nullptr;
    }

    jobject object = env->NewObject(cls, ctor);
    if (object == nullptr || clear_pending_exception(env)) {
        env->DeleteLocalRef(cls);
        return nullptr;
    }

    jfieldID format_field = env->GetFieldID(cls, "format", "I");
    if (format_field != nullptr && !clear_pending_exception(env)) {
        env->SetIntField(object, format_field, format);
        clear_pending_exception(env);
    }

    jfieldID value_field = env->GetFieldID(cls, "value", "Ljava/lang/String;");
    if (value_field != nullptr && !clear_pending_exception(env)) {
        jstring string_value = value != nullptr ? env->NewStringUTF(value) : nullptr;
        if (string_value != nullptr || value == nullptr) {
            env->SetObjectField(object, value_field, string_value);
            clear_pending_exception(env);
        }
        if (string_value != nullptr) {
            env->DeleteLocalRef(string_value);
        }
    }

    env->DeleteLocalRef(cls);
    return object;
}
#endif

}  // namespace

#ifndef KKSDK_HOST_CORE_ONLY
extern "C" bool kksdk_irdevice_init(void *env, void *context, void *expected_hash) {
    g_irdevice_verified = kksdk_verify_package_signature(env, context, expected_hash, nullptr);
    return g_irdevice_verified == 1;
}

extern "C" unsigned short kksdk_irdevice_get_frequency() {
    if (g_irdevice_verified != 1) {
        return 0;
    }
    return static_cast<unsigned short>(
            kksdk_irdevice_decode_frequency(g_irdevice_remote_data, g_irdevice_remote_size));
}

extern "C" unsigned long long kksdk_irdevice_create_remote_error() {
    return 0xffffffffULL;
}

extern "C" unsigned long long kksdk_irdevice_create_remote(void *env, void *data) {
    if (g_irdevice_verified != 1) {
        return static_cast<unsigned long long>(static_cast<std::int64_t>(-99));
    }

    auto *jni = reinterpret_cast<JNIEnv *>(env);
    auto array = reinterpret_cast<jbyteArray>(data);
    if (jni == nullptr || array == nullptr) {
        kksdk_irdevice_reset_remote_view();
        g_irdevice_remote_storage.clear();
        return kksdk_irdevice_create_remote_error();
    }

    const jsize length = jni->GetArrayLength(array);
    if (clear_pending_exception(jni) || length <= 0) {
        kksdk_irdevice_reset_remote_view();
        g_irdevice_remote_storage.clear();
        return kksdk_irdevice_create_remote_error();
    }

    g_irdevice_remote_storage.assign(static_cast<std::size_t>(length), 0);
    jni->GetByteArrayRegion(array, 0, length,
            reinterpret_cast<jbyte *>(g_irdevice_remote_storage.data()));
    if (clear_pending_exception(jni)) {
        kksdk_irdevice_reset_remote_view();
        g_irdevice_remote_storage.clear();
        return kksdk_irdevice_create_remote_error();
    }

    return kksdk_irdevice_set_remote_view(g_irdevice_remote_storage.data(),
            static_cast<unsigned int>(g_irdevice_remote_storage.size()));
}

extern "C" void *kksdk_irdevice_encode(void *env_ptr, void *data, void *out_status) {
    auto *env = reinterpret_cast<JNIEnv *>(env_ptr);
    if (g_irdevice_verified != 1) {
        set_int_array_value(env, out_status, -99);
        return new_int_array(env, nullptr, 0);
    }

    auto array = reinterpret_cast<jbyteArray>(data);
    if (env == nullptr || array == nullptr) {
        set_int_array_value(env, out_status, -3);
        return new_int_array(env, nullptr, 0);
    }

    const jsize length = env->GetArrayLength(array);
    if (clear_pending_exception(env) || length <= 0) {
        set_int_array_value(env, out_status, -3);
        return new_int_array(env, nullptr, 0);
    }

    std::vector<unsigned char> command(static_cast<std::size_t>(length));
    env->GetByteArrayRegion(array, 0, length, reinterpret_cast<jbyte *>(command.data()));
    if (clear_pending_exception(env)) {
        set_int_array_value(env, out_status, -3);
        return new_int_array(env, nullptr, 0);
    }

    unsigned short duration_count = 0;
    unsigned char repeat_count = 0;
    std::vector<unsigned short> durations(0x400U);
    const int status = kksdk_irdevice_encode_command_frame(command.data(),
            static_cast<unsigned int>(command.size()), durations.data(),
            static_cast<unsigned short>(durations.size()), &duration_count, &repeat_count);
    if (status != 0) {
        set_int_array_value(env, out_status, status);
        return new_int_array(env, nullptr, 0);
    }

    std::vector<jint> expanded(kksdk_irdevice_repeat_expanded_count(
            duration_count, repeat_count));
    for (unsigned int repeat = 0; repeat < repeat_count; ++repeat) {
        for (unsigned int index = 0; index < duration_count; ++index) {
            expanded[kksdk_irdevice_repeat_expanded_index(duration_count, repeat, index)] =
                    static_cast<jint>(durations[index]);
        }
    }

    set_int_array_value(env, out_status, 0);
    return new_int_array(env, expanded.data(), static_cast<jsize>(expanded.size()));
}
#endif

extern "C" unsigned int kksdk_irdevice_repeat_expanded_count(
        unsigned int duration_count, unsigned int repeat_count) {
    return duration_count * repeat_count;
}

extern "C" unsigned int kksdk_irdevice_repeat_expanded_index(
        unsigned int duration_count, unsigned int repeat, unsigned int index) {
    return repeat * duration_count + index;
}

#ifndef KKSDK_HOST_CORE_ONLY
extern "C" void *kksdk_irdevice_parse(void *env_ptr, void *data) {
    auto *env = reinterpret_cast<JNIEnv *>(env_ptr);
    auto array = reinterpret_cast<jintArray>(data);
    if (env == nullptr || array == nullptr) {
        return new_ir_protocol(env, 0, nullptr);
    }

    const jsize length = env->GetArrayLength(array);
    if (clear_pending_exception(env) || length <= 0) {
        return new_ir_protocol(env, 0, nullptr);
    }

    std::vector<int> durations(static_cast<std::size_t>(length));
    env->GetIntArrayRegion(array, 0, length, reinterpret_cast<jint *>(durations.data()));
    if (clear_pending_exception(env)) {
        return new_ir_protocol(env, 0, nullptr);
    }

    char decoded[512] = {};
    kksdk_irdevice_parse_result result{};
    const int decoded_count = kksdk_irdevice_parse_pulse_protocol(durations.data(),
            static_cast<unsigned int>(durations.size()), decoded, &result);
    if (decoded_count <= 0 || result.format <= 0) {
        return new_ir_protocol(env, 0, nullptr);
    }

    decoded[decoded_count] = '\0';
    return new_ir_protocol(env, static_cast<jint>(result.format), decoded);
}
#endif

extern "C" void kksdk_irdevice_reset_remote_view(void) {
    g_irdevice_remote_data = nullptr;
    g_irdevice_remote_size = 0;
    g_irdevice_remote_layout = {};
}

extern "C" unsigned int kksdk_irdevice_set_remote_view(const unsigned char *data,
        unsigned int size) {
    kksdk_irdevice_remote_layout layout{};
    const unsigned int status = kksdk_irdevice_parse_remote_layout(data, size, &layout);
    if (status != 0) {
        kksdk_irdevice_reset_remote_view();
        return status;
    }

    g_irdevice_remote_data = data;
    g_irdevice_remote_size = size;
    g_irdevice_remote_layout = layout;
    return 0;
}

extern "C" unsigned int kksdk_irdevice_parse_remote_layout(const unsigned char *data,
        unsigned int size, kksdk_irdevice_remote_layout *layout) {
    if (layout != nullptr) {
        *layout = {};
    }
    if (data == nullptr || size < 4U) {
        return kksdk_irdevice_result_error();
    }

    kksdk_irdevice_remote_layout parsed{};
    if (data[1] == kksdk_irdevice_raw_payload_marker()) {
        if (layout != nullptr) {
            *layout = parsed;
        }
        return 0;
    }

    unsigned int payload_end = 0;
    if (data[0] == 1U) {
        if (size < 9U) {
            return kksdk_irdevice_result_error();
        }
        payload_end = static_cast<unsigned int>(data[8]) + 9U;
    } else if (data[0] == 0U) {
        if (size < 0x13U) {
            return kksdk_irdevice_result_error();
        }
        const unsigned int table_a = static_cast<unsigned int>(data[0x12]) + 0x13U;
        if (size <= table_a) {
            return kksdk_irdevice_result_error();
        }
        const unsigned int table_b = table_a + static_cast<unsigned int>(data[table_a]) + 1U;
        if (size <= table_b) {
            return kksdk_irdevice_result_error();
        }
        payload_end = table_b + static_cast<unsigned int>(data[table_b]) + 1U;
        parsed.table_a_offset = static_cast<unsigned short>(table_a);
        parsed.table_b_offset = static_cast<unsigned short>(table_b);
    } else {
        payload_end = 0;
    }

    parsed.payload_end_offset = static_cast<unsigned short>(payload_end);
    if (size <= payload_end) {
        return kksdk_irdevice_result_error();
    }
    if (data[payload_end] != 0U) {
        if (size <= payload_end + 1U ||
                size < payload_end + static_cast<unsigned int>(data[payload_end + 1U]) + 2U) {
            return kksdk_irdevice_result_error();
        }
    }

    if (layout != nullptr) {
        *layout = parsed;
    }
    return 0;
}

extern "C" unsigned int kksdk_irdevice_decode_frequency(const unsigned char *data,
        unsigned int size) {
    if (data == nullptr || size < 4U) {
        return 0;
    }
    const unsigned int carrier =
            (static_cast<unsigned int>(data[2]) << 8) | static_cast<unsigned int>(data[3]);
    return carrier * 10U;
}

extern "C" unsigned int kksdk_irdevice_build_frequency_frame(unsigned int packed_frequency,
        unsigned char *out, unsigned short out_capacity) {
    const unsigned int carrier = kksdk_irdevice_carrier_from_packed(packed_frequency);
    if (kksdk_irdevice_packed_frequency_is_valid(packed_frequency) == 0 || out == nullptr ||
            out_capacity <= 3U) {
        return kksdk_irdevice_result_error();
    }

    out[0] = 0x00U;
    out[1] = static_cast<unsigned char>(kksdk_irdevice_raw_payload_marker());
    out[2] = static_cast<unsigned char>(carrier / 0x0a00U);
    out[3] = static_cast<unsigned char>(carrier / 10U);
    return 4U;
}

extern "C" unsigned int kksdk_irdevice_encode_duration_frame(
        unsigned int packed_frequency, const unsigned int *durations,
        unsigned short duration_count, unsigned char *out, unsigned int out_capacity) {
    if (kksdk_irdevice_packed_frequency_is_valid(packed_frequency) == 0 || durations == nullptr ||
            duration_count == 0 || out == nullptr) {
        return kksdk_irdevice_result_error();
    }

    const unsigned int required = kksdk_irdevice_duration_frame_size(duration_count);
    if ((out_capacity & 0xffffU) < required) {
        return kksdk_irdevice_result_error();
    }

    const unsigned int rounded_carrier = kksdk_irdevice_rounded_carrier(packed_frequency);
    const unsigned int period = kksdk_irdevice_carrier_period_us(rounded_carrier);

    out[0] = 0;
    for (unsigned int i = 0; i < duration_count; ++i) {
        if (durations[i] < period) {
            return kksdk_irdevice_result_error();
        }
        const unsigned int ticks = kksdk_irdevice_duration_ticks(durations[i], period);
        if ((ticks >> 16U) != 0) {
            return kksdk_irdevice_result_error();
        }
        out[1U + i * 2U] = static_cast<unsigned char>(ticks >> 8U);
        out[2U + i * 2U] = static_cast<unsigned char>(ticks & 0xffU);
    }
    return required;
}

extern "C" unsigned int kksdk_irdevice_duration_frame_size(unsigned short duration_count) {
    return (static_cast<unsigned int>(duration_count) << 1U) | 1U;
}

extern "C" unsigned int kksdk_irdevice_carrier_from_packed(unsigned int packed_frequency) {
    return packed_frequency & kksdk_irdevice_packed_frequency_carrier_mask();
}

extern "C" unsigned int kksdk_irdevice_packed_frequency_carrier_mask(void) {
    return 0xffffU;
}

extern "C" unsigned int kksdk_irdevice_packed_frequency_valid_shift(void) {
    return 4U;
}

extern "C" unsigned int kksdk_irdevice_packed_frequency_valid_mask(void) {
    return 0xfffU;
}

extern "C" unsigned int kksdk_irdevice_min_valid_carrier_field(void) {
    return 0x270U;
}

extern "C" unsigned int kksdk_irdevice_microseconds_per_second(void) {
    return 1000000U;
}

extern "C" int kksdk_irdevice_packed_frequency_is_valid(unsigned int packed_frequency) {
    const unsigned int carrier_field =
            (packed_frequency >> kksdk_irdevice_packed_frequency_valid_shift()) &
            kksdk_irdevice_packed_frequency_valid_mask();
    return carrier_field > kksdk_irdevice_min_valid_carrier_field() ? 1 : 0;
}

extern "C" unsigned int kksdk_irdevice_rounded_carrier(unsigned int packed_frequency) {
    const unsigned int carrier = kksdk_irdevice_carrier_from_packed(packed_frequency);
    return (packed_frequency + ((carrier / 10U) * 10U - carrier)) &
            kksdk_irdevice_packed_frequency_carrier_mask();
}

extern "C" unsigned int kksdk_irdevice_carrier_period_us(unsigned int carrier) {
    return carrier == 0U ? 0U : kksdk_irdevice_microseconds_per_second() / carrier;
}

extern "C" unsigned int kksdk_irdevice_duration_ticks(
        unsigned int duration, unsigned int period) {
    return period == 0U ? 0U : duration / period;
}

extern "C" int kksdk_irdevice_encode_command_frame(const unsigned char *command,
        unsigned int command_size, unsigned short *durations, unsigned short duration_capacity,
        unsigned short *duration_count, unsigned char *repeat_count) {
    if (duration_count != nullptr) {
        *duration_count = 0;
    }
    if (repeat_count != nullptr) {
        *repeat_count = 0;
    }
    if (g_irdevice_remote_data == nullptr) {
        return kksdk_irdevice_encode_status_remote_error();
    }
    if (command == nullptr || durations == nullptr || duration_count == nullptr ||
            repeat_count == nullptr) {
        return kksdk_irdevice_encode_status_invalid_command();
    }

    const unsigned int size16 = kksdk_irdevice_command_size16(command_size);
    if (size16 <= 1U) {
        return kksdk_irdevice_encode_status_invalid_command();
    }

    if (command[0] != 0U) {
        const unsigned int embedded_remote_size = command[0];
        const unsigned int embedded_command_offset = embedded_remote_size + 1U;
        if (embedded_remote_size != 0U &&
                command_size > embedded_command_offset) {
            const unsigned char *previous_remote_data = g_irdevice_remote_data;
            const unsigned int previous_remote_size = g_irdevice_remote_size;
            const kksdk_irdevice_remote_layout previous_layout = g_irdevice_remote_layout;
            const unsigned int remote_status = kksdk_irdevice_set_remote_view(
                    command + 1U, embedded_remote_size);
            if (remote_status != 0U) {
                g_irdevice_remote_data = previous_remote_data;
                g_irdevice_remote_size = previous_remote_size;
                g_irdevice_remote_layout = previous_layout;
                return kksdk_irdevice_encode_status_invalid_command();
            }

            const unsigned char *embedded_command = command + embedded_command_offset;
            const unsigned int embedded_command_size = command_size - embedded_command_offset;
            std::vector<unsigned char> normalized_command(
                    static_cast<std::size_t>(embedded_command_size) + 1U);
            normalized_command[0] = 0U;
            for (unsigned int index = 0; index < embedded_command_size; ++index) {
                normalized_command[static_cast<std::size_t>(index) + 1U] =
                        embedded_command[index];
            }
            const int status = kksdk_irdevice_encode_command_frame(
                    normalized_command.data(),
                    static_cast<unsigned int>(normalized_command.size()), durations,
                    duration_capacity, duration_count, repeat_count);
            g_irdevice_remote_data = previous_remote_data;
            g_irdevice_remote_size = previous_remote_size;
            g_irdevice_remote_layout = previous_layout;
            return status;
        }
        return kksdk_irdevice_encode_status_invalid_command();
    }

    const unsigned int payload_bytes = kksdk_irdevice_command_payload_size(size16);
    const unsigned int remote_payload_limit = g_irdevice_remote_data[1];
    if (kksdk_irdevice_payload_fits_template(payload_bytes, remote_payload_limit) != 0) {
        *repeat_count = g_irdevice_remote_data[4U];
        if (g_irdevice_remote_data[0] == 1U) {
            return kksdk_irdevice_encode_symbol_template_command(command + 1U,
                    payload_bytes, durations, duration_capacity, duration_count);
        }
        return kksdk_irdevice_encode_template_command(command + 1U, payload_bytes,
                durations, duration_capacity, duration_count);
    }

    const unsigned int count = kksdk_irdevice_raw_duration_pair_count(payload_bytes);
    if (count <= 1U) {
        return kksdk_irdevice_encode_status_invalid_command();
    }
    if (duration_capacity < count) {
        return kksdk_irdevice_encode_status_capacity_error();
    }

    for (unsigned int index = 0; index < count; ++index) {
        const unsigned int byte_offset = 1U + index * 2U;
        durations[index] = kksdk_irdevice_read_be16(command + byte_offset);
    }

    *duration_count = static_cast<unsigned short>(count);
    *repeat_count = 1U;
    return kksdk_irdevice_encode_status_success();
}

extern "C" unsigned int kksdk_irdevice_command_size16(unsigned int command_size) {
    return command_size & 0xffffU;
}

extern "C" unsigned int kksdk_irdevice_command_payload_size(unsigned int command_size) {
    const unsigned int size16 = kksdk_irdevice_command_size16(command_size);
    return size16 <= 1U ? 0U : size16 - 1U;
}

extern "C" int kksdk_irdevice_encode_status_success() {
    return 0;
}

extern "C" int kksdk_irdevice_encode_status_remote_error() {
    return -1;
}

extern "C" int kksdk_irdevice_encode_status_capacity_error() {
    return -2;
}

extern "C" int kksdk_irdevice_encode_status_invalid_command() {
    return -3;
}

extern "C" unsigned int kksdk_irdevice_result_error() {
    return 0xffffffffU;
}

extern "C" unsigned int kksdk_irdevice_result_capacity_error() {
    return 0xfffffffeU;
}

extern "C" unsigned int kksdk_irdevice_raw_payload_marker() {
    return 0xffU;
}

extern "C" int kksdk_irdevice_payload_fits_template(
        unsigned int payload_size, unsigned int remote_payload_limit) {
    return remote_payload_limit != kksdk_irdevice_raw_payload_marker() &&
            payload_size <= remote_payload_limit ? 1 : 0;
}

extern "C" unsigned int kksdk_irdevice_raw_duration_pair_count(unsigned int payload_size) {
    return payload_size >> 1U;
}

extern "C" unsigned int kksdk_irdevice_apply_optional_tail_segment(
        const unsigned char *remote, unsigned int remote_size,
        unsigned short payload_end_offset, unsigned short *durations,
        unsigned short duration_capacity, unsigned short *duration_count) {
    if (remote == nullptr || durations == nullptr || duration_count == nullptr) {
        return kksdk_irdevice_result_error();
    }
    if (remote_size <= payload_end_offset || remote[payload_end_offset] == 0U) {
        return 0;
    }
    if (remote_size <= static_cast<unsigned int>(payload_end_offset) + 1U) {
        return kksdk_irdevice_result_error();
    }

    const unsigned int tail_size = remote[payload_end_offset + 1U];
    const unsigned int tail_start = static_cast<unsigned int>(payload_end_offset) + 2U;
    const unsigned int tail_end = tail_start + tail_size;
    if (tail_size == 0U || (tail_size & 1U) != 0U || remote_size < tail_end) {
        return kksdk_irdevice_result_error();
    }

    const unsigned short count_before_tail = *duration_count;
    const unsigned int main_total = kksdk_irdevice_sum_durations(durations,
            count_before_tail);

    unsigned int status = kksdk_irdevice_append_be16_range(durations,
            duration_capacity, duration_count, remote,
            static_cast<unsigned short>(tail_start), static_cast<unsigned short>(tail_end));
    if (status != 0) {
        return status;
    }
    const unsigned int tail_total =
            kksdk_irdevice_sum_durations(durations, *duration_count) - main_total;
    if (tail_size == 6U) {
        const unsigned short target_total = remote[5U] != 0U ?
                static_cast<unsigned short>(main_total) :
                kksdk_irdevice_read_be16(remote + 6U);
        if (target_total > tail_total) {
            return kksdk_irdevice_append_duration(durations, duration_capacity,
                    duration_count, static_cast<unsigned short>(target_total - tail_total));
        }
    } else if (tail_size == 10U && main_total > tail_total) {
        return kksdk_irdevice_append_duration(durations, duration_capacity,
                duration_count, static_cast<unsigned short>(main_total - tail_total));
    }
    return 0;
}

extern "C" unsigned int kksdk_irdevice_apply_symbol_optional_tail_segment(
        const unsigned char *remote, unsigned int remote_size,
        unsigned short payload_end_offset, unsigned short *durations,
        unsigned short duration_capacity, unsigned short *duration_count) {
    if (remote == nullptr || remote_size <= static_cast<unsigned int>(payload_end_offset) + 1U ||
            remote[payload_end_offset] == 0U) {
        return 0;
    }
    if (remote[payload_end_offset + 1U] <= 10U) {
        return 0;
    }
    return kksdk_irdevice_apply_optional_tail_segment(remote, remote_size,
            payload_end_offset, durations, duration_capacity, duration_count);
}

extern "C" int kksdk_irdevice_encode_template_command(const unsigned char *command,
        unsigned int command_size, unsigned short *durations,
        unsigned short duration_capacity, unsigned short *duration_count) {
    if (g_irdevice_remote_data == nullptr || command == nullptr || durations == nullptr ||
            duration_count == nullptr) {
        return kksdk_irdevice_encode_status_invalid_command();
    }
    const kksdk_irdevice_remote_layout &layout = g_irdevice_remote_layout;
    if (layout.table_a_offset < 0x13U || layout.table_b_offset <= layout.table_a_offset ||
            layout.payload_end_offset <= layout.table_b_offset) {
        return kksdk_irdevice_encode_status_remote_error();
    }

    *duration_count = 0;
    if (layout.table_a_offset >= 0x14U) {
        const unsigned int status = kksdk_irdevice_append_be16_range(durations,
                duration_capacity, duration_count, g_irdevice_remote_data, 0x13U,
                layout.table_a_offset);
        if (status != 0) {
            return static_cast<int>(status);
        }
    }

    for (unsigned int byte_index = 0; byte_index < kksdk_irdevice_command_size16(command_size);
            ++byte_index) {
        unsigned int bit_count = 8U;
        for (unsigned int cursor = layout.table_a_offset + 1U;
                cursor + 1U < layout.table_b_offset; cursor += 2U) {
            if (g_irdevice_remote_data[cursor] == (byte_index & 0xffU)) {
                bit_count = g_irdevice_remote_data[cursor + 1U];
                break;
            }
        }
        if (bit_count > 8U) {
            return kksdk_irdevice_encode_status_remote_error();
        }

        const unsigned int status = kksdk_irdevice_append_binary_template_bits(
                g_irdevice_remote_data, command[byte_index], bit_count,
                g_irdevice_remote_data[0x10U] != 0U, durations, duration_capacity,
                duration_count);
        if (status != 0) {
            return static_cast<int>(status);
        }

        const unsigned int segment_status = kksdk_irdevice_apply_template_segment(
                g_irdevice_remote_data, layout.table_b_offset, layout.payload_end_offset,
                static_cast<unsigned char>(byte_index), durations, duration_capacity,
                duration_count);
        if (segment_status != 0) {
            return static_cast<int>(segment_status);
        }
    }

    if (g_irdevice_remote_data[0x11U] == 0U) {
        const unsigned int status = kksdk_irdevice_append_be16_duration(durations,
                duration_capacity, duration_count, g_irdevice_remote_data + 0x0cU);
        if (status != 0) {
            return static_cast<int>(status);
        }
    }

    unsigned short terminal_segment_start = 0;
    unsigned short terminal_segment_end = 0;
    const bool has_terminal_segment =
            kksdk_irdevice_find_template_segment(g_irdevice_remote_data,
                    layout.table_b_offset, layout.payload_end_offset,
                    static_cast<unsigned char>(kksdk_irdevice_raw_payload_marker()),
                    &terminal_segment_start, &terminal_segment_end) == 0 &&
            terminal_segment_end > terminal_segment_start;

    unsigned int status = kksdk_irdevice_apply_template_segment(g_irdevice_remote_data,
            layout.table_b_offset, layout.payload_end_offset,
            static_cast<unsigned char>(kksdk_irdevice_raw_payload_marker()), durations,
            duration_capacity, duration_count);
    if (status != 0) {
        return static_cast<int>(status);
    }
    const unsigned short final_gap = kksdk_irdevice_read_be16(g_irdevice_remote_data + 6U);
    const bool split_repeated_preamble_gap = layout.table_a_offset > 0x13U &&
            ((g_irdevice_remote_data[1U] != 1U &&
            g_irdevice_remote_data[4U] > 1U && g_irdevice_remote_data[4U] <= 3U) ||
            g_irdevice_remote_data[1U] == 2U);
    const bool split_small_single_preamble_direct_gap = g_irdevice_remote_data[4U] == 1U &&
            layout.table_a_offset > 0x13U && final_gap <= 0x0200U;
    const bool split_type5_terminal_direct_gap = g_irdevice_remote_data[1U] == 5U &&
            layout.table_a_offset > 0x13U && has_terminal_segment;
    if (g_irdevice_remote_data[5U] != 0U) {
        status = kksdk_irdevice_apply_direct_gap(durations, duration_capacity,
                duration_count, final_gap,
                g_irdevice_remote_data[0x11U] != 0U && !split_repeated_preamble_gap &&
                !split_small_single_preamble_direct_gap &&
                !split_type5_terminal_direct_gap);
    } else {
        status = kksdk_irdevice_apply_duration_gap(durations, duration_capacity,
                duration_count,
                final_gap, !split_repeated_preamble_gap &&
                g_irdevice_remote_data[0x11U] != 0U &&
                !has_terminal_segment);
    }
    if (status != 0) {
        return static_cast<int>(status);
    }
    status = kksdk_irdevice_apply_optional_tail_segment(g_irdevice_remote_data,
            g_irdevice_remote_size, layout.payload_end_offset, durations,
            duration_capacity, duration_count);
    if (status != 0U) {
        return static_cast<int>(status);
    }
    if (g_irdevice_remote_data[1U] == 4U &&
            g_irdevice_remote_size >
            static_cast<unsigned int>(layout.payload_end_offset) + 1U &&
            g_irdevice_remote_data[layout.payload_end_offset] > 1U &&
            (g_irdevice_remote_data[layout.payload_end_offset + 1U] == 6U ||
            g_irdevice_remote_data[layout.payload_end_offset + 1U] == 10U)) {
        const unsigned int tail_size = g_irdevice_remote_data[layout.payload_end_offset + 1U];
        const unsigned int tail_copy_count = tail_size / 2U + 1U;
        if (*duration_count < tail_copy_count || tail_copy_count > 6U) {
            return kksdk_irdevice_encode_status_capacity_error();
        }
        unsigned short tail_copy[6] = {};
        for (unsigned int index = 0; index < tail_copy_count; ++index) {
            tail_copy[index] = durations[*duration_count - tail_copy_count + index];
        }
        const unsigned int tail_repeat_count = g_irdevice_remote_data[layout.payload_end_offset];
        for (unsigned int repeat = 1U; repeat < tail_repeat_count; ++repeat) {
            for (unsigned int index = 0; index < tail_copy_count; ++index) {
                status = kksdk_irdevice_append_duration(durations, duration_capacity,
                        duration_count, tail_copy[index]);
                if (status != 0U) {
                    return static_cast<int>(status);
                }
            }
        }
    }
    return static_cast<int>(status);
}

extern "C" int kksdk_irdevice_encode_symbol_template_command(
        const unsigned char *command, unsigned int command_size,
        unsigned short *durations, unsigned short duration_capacity,
        unsigned short *duration_count) {
    if (g_irdevice_remote_data == nullptr || command == nullptr || durations == nullptr ||
            duration_count == nullptr || g_irdevice_remote_layout.payload_end_offset <= 9U) {
        return kksdk_irdevice_encode_status_remote_error();
    }

    unsigned char symbol_offsets[32] = {};
    unsigned int symbol_count = 0;
    unsigned int cursor = 9U;
    while (cursor < g_irdevice_remote_layout.payload_end_offset) {
        if (symbol_count >= sizeof(symbol_offsets)) {
            return kksdk_irdevice_encode_status_remote_error();
        }
        symbol_offsets[symbol_count++] = static_cast<unsigned char>(cursor);
        cursor += static_cast<unsigned int>(g_irdevice_remote_data[cursor]) + 1U;
    }
    if (symbol_count == 0) {
        return kksdk_irdevice_encode_status_remote_error();
    }

    const unsigned int symbol_bits = kksdk_irdevice_symbol_bits_for_count(symbol_count);
    const unsigned int symbols_per_byte = kksdk_irdevice_symbols_per_byte(symbol_bits);
    unsigned int emitted_symbols = 0;
    unsigned int previous_flag = kksdk_irdevice_unknown_duration_flag();
    *duration_count = 0;

    for (unsigned int byte_index = 0; byte_index < kksdk_irdevice_command_size16(command_size);
            ++byte_index) {
        for (unsigned int symbol_index = 0; symbol_index < symbols_per_byte; ++symbol_index) {
            const unsigned int symbol = kksdk_irdevice_symbol_from_byte(
                    command[byte_index], symbol_bits, symbol_index);
            if (symbol >= symbol_count) {
                return kksdk_irdevice_encode_status_invalid_command();
            }

            const unsigned int segment = symbol_offsets[symbol];
            const unsigned int segment_end = segment + g_irdevice_remote_data[segment];
            for (unsigned int offset = segment + 1U;
                    offset + 1U <= segment_end; offset += 2U) {
                const unsigned int high = g_irdevice_remote_data[offset];
                const unsigned int flag = kksdk_irdevice_duration_flag_from_high_byte(high);
                const unsigned short value = kksdk_irdevice_duration_value_from_pair(
                        high, g_irdevice_remote_data[offset + 1U]);
                if (*duration_count == 0 && flag == 0U) {
                    continue;
                }
                if (*duration_count != 0 && flag == previous_flag) {
                    durations[*duration_count - 1U] = static_cast<unsigned short>(
                            durations[*duration_count - 1U] + value);
                    continue;
                }
                const unsigned int status = kksdk_irdevice_append_duration(durations,
                        duration_capacity, duration_count, value);
                if (status != 0) {
                    return static_cast<int>(status);
                }
                previous_flag = flag;
            }

            ++emitted_symbols;
            if (emitted_symbols >= g_irdevice_remote_data[1U]) {
                const unsigned short final_gap =
                        kksdk_irdevice_read_be16(g_irdevice_remote_data + 6U);
                const unsigned int status = g_irdevice_remote_data[5U] != 0U ?
                        kksdk_irdevice_apply_direct_gap(durations, duration_capacity,
                                duration_count, final_gap, previous_flag == 0U ? 1 : 0) :
                        kksdk_irdevice_apply_duration_gap(durations, duration_capacity,
                                duration_count, final_gap,
                                previous_flag == 0U ? 1 : 0);
                if (status != 0) {
                    return static_cast<int>(status);
                }
                return static_cast<int>(kksdk_irdevice_apply_symbol_optional_tail_segment(
                        g_irdevice_remote_data, g_irdevice_remote_size,
                        g_irdevice_remote_layout.payload_end_offset, durations,
                        duration_capacity, duration_count));
            }
        }
    }

    const unsigned short final_gap = kksdk_irdevice_read_be16(g_irdevice_remote_data + 6U);
    unsigned int status = g_irdevice_remote_data[5U] != 0U ?
            kksdk_irdevice_apply_direct_gap(durations, duration_capacity, duration_count,
                    final_gap, previous_flag == 0U ? 1 : 0) :
            kksdk_irdevice_apply_duration_gap(durations, duration_capacity, duration_count,
                    final_gap, previous_flag == 0U ? 1 : 0);
    if (status != 0) {
        return static_cast<int>(status);
    }
    status = kksdk_irdevice_apply_symbol_optional_tail_segment(g_irdevice_remote_data,
            g_irdevice_remote_size, g_irdevice_remote_layout.payload_end_offset,
            durations, duration_capacity, duration_count);
    return static_cast<int>(status);
}

extern "C" unsigned int kksdk_irdevice_symbol_bits_for_count(unsigned int symbol_count) {
    if (symbol_count < 3U) {
        return 1U;
    }
    if (symbol_count < 5U) {
        return 2U;
    }
    if (symbol_count < 0x11U) {
        return 4U;
    }
    return 8U;
}

extern "C" unsigned int kksdk_irdevice_symbols_per_byte(unsigned int symbol_bits) {
    return symbol_bits == 0U ? 0U : 8U / symbol_bits;
}

extern "C" unsigned int kksdk_irdevice_symbol_from_byte(
        unsigned char value, unsigned int symbol_bits, unsigned int symbol_index) {
    if (symbol_bits == 0U || symbol_bits > 8U) {
        return 0;
    }
    const unsigned int shift = symbol_bits * symbol_index;
    return ((static_cast<unsigned int>(value) << shift) & 0xffU) >> (8U - symbol_bits);
}

extern "C" unsigned int kksdk_irdevice_unknown_duration_flag() {
    return 0xffU;
}

extern "C" unsigned int kksdk_irdevice_duration_flag_mask() {
    return 0x80U;
}

extern "C" unsigned int kksdk_irdevice_duration_high_value_mask() {
    return 0x7fU;
}

extern "C" unsigned int kksdk_irdevice_duration_low_value_mask() {
    return 0xffU;
}

extern "C" unsigned int kksdk_irdevice_duration_flag_from_high_byte(unsigned int high) {
    return high & kksdk_irdevice_duration_flag_mask();
}

extern "C" unsigned short kksdk_irdevice_duration_value_from_pair(
        unsigned int high, unsigned int low) {
    return static_cast<unsigned short>(
            ((high & kksdk_irdevice_duration_high_value_mask()) << 8U) |
            (low & kksdk_irdevice_duration_low_value_mask()));
}

extern "C" unsigned int kksdk_irdevice_zero_mark_template_offset() {
    return 0x08U;
}

extern "C" unsigned int kksdk_irdevice_zero_space_template_offset() {
    return 0x0aU;
}

extern "C" unsigned int kksdk_irdevice_one_mark_template_offset() {
    return 0x0cU;
}

extern "C" unsigned int kksdk_irdevice_one_space_template_offset() {
    return 0x0eU;
}

extern "C" unsigned int kksdk_irdevice_append_binary_template_bits(
        const unsigned char *remote, unsigned char value, unsigned int bit_count,
        int lsb_first, unsigned short *durations, unsigned short capacity,
        unsigned short *count) {
    if (remote == nullptr || durations == nullptr || count == nullptr || bit_count > 8U) {
        return kksdk_irdevice_result_error();
    }

    for (unsigned int bit_index = 0; bit_index < bit_count; ++bit_index) {
        const unsigned int shift = lsb_first != 0 ?
                bit_index : (bit_count - 1U - bit_index);
        const bool is_one = ((value >> shift) & 1U) != 0;
        const unsigned int mark_offset = is_one ?
                kksdk_irdevice_one_mark_template_offset() :
                kksdk_irdevice_zero_mark_template_offset();
        const unsigned int space_offset = is_one ?
                kksdk_irdevice_one_space_template_offset() :
                kksdk_irdevice_zero_space_template_offset();

        unsigned int status = kksdk_irdevice_append_be16_duration(
                durations, capacity, count, remote + mark_offset);
        if (status != 0) {
            return status;
        }
        status = kksdk_irdevice_append_be16_duration(
                durations, capacity, count, remote + space_offset);
        if (status != 0) {
            return status;
        }
    }
    return 0;
}

extern "C" unsigned int kksdk_irdevice_append_duration(unsigned short *durations,
        unsigned short capacity, unsigned short *count, unsigned short value) {
    if (durations == nullptr || count == nullptr) {
        return kksdk_irdevice_result_error();
    }
    if (*count >= capacity) {
        return kksdk_irdevice_result_capacity_error();
    }
    durations[*count] = value;
    ++(*count);
    return 0;
}

extern "C" unsigned int kksdk_irdevice_sum_durations(const unsigned short *durations,
        unsigned short count) {
    if (durations == nullptr || count == 0) {
        return 0;
    }

    unsigned int total = 0;
    for (unsigned short i = 0; i < count; ++i) {
        total += durations[i];
    }
    return total & 0xffffU;
}

extern "C" unsigned short kksdk_irdevice_read_be16(const unsigned char *data) {
    if (data == nullptr) {
        return 0;
    }
    return static_cast<unsigned short>(
            (static_cast<unsigned short>(data[0]) << 8U) | data[1]);
}

extern "C" unsigned int kksdk_irdevice_append_be16_duration(
        unsigned short *durations, unsigned short capacity, unsigned short *count,
        const unsigned char *data) {
    return kksdk_irdevice_append_duration(durations, capacity, count,
            kksdk_irdevice_read_be16(data));
}

extern "C" unsigned int kksdk_irdevice_append_be16_range(unsigned short *durations,
        unsigned short capacity, unsigned short *count, const unsigned char *data,
        unsigned short start, unsigned short end) {
    if (data == nullptr || durations == nullptr || count == nullptr) {
        return kksdk_irdevice_result_error();
    }
    if (start >= end) {
        return 0;
    }

    for (unsigned int offset = start; offset < end; offset += 2U) {
        const unsigned int status = kksdk_irdevice_append_be16_duration(
                durations, capacity, count, data + offset);
        if (status != 0) {
            return status;
        }
    }
    return 0;
}

extern "C" unsigned int kksdk_irdevice_find_template_segment(const unsigned char *remote,
        unsigned short table_start, unsigned short table_end, unsigned char segment_code,
        unsigned short *payload_start, unsigned short *payload_end) {
    if (remote == nullptr || payload_start == nullptr || payload_end == nullptr) {
        return kksdk_irdevice_result_error();
    }

    unsigned int cursor = static_cast<unsigned int>(table_start) + 2U;
    while (cursor < table_end) {
        const unsigned int segment_end = cursor + remote[cursor - 1U];
        if (remote[cursor] == segment_code) {
            *payload_start = static_cast<unsigned short>(cursor + 1U);
            *payload_end = static_cast<unsigned short>(segment_end);
            return 0;
        }
        cursor = segment_end + 1U;
    }

    *payload_start = 0;
    *payload_end = 0;
    return kksdk_irdevice_result_error();
}

extern "C" unsigned int kksdk_irdevice_apply_template_segment(
        const unsigned char *remote, unsigned short table_start, unsigned short table_end,
        unsigned char segment_code, unsigned short *durations, unsigned short capacity,
        unsigned short *count) {
    if (remote == nullptr || durations == nullptr || count == nullptr) {
        return kksdk_irdevice_result_error();
    }

    unsigned short payload_start = 0;
    unsigned short payload_end = 0;
    if (kksdk_irdevice_find_template_segment(remote, table_start, table_end,
                segment_code, &payload_start, &payload_end) != 0) {
        return 0;
    }
    if (payload_end <= payload_start) {
        return 0;
    }

    unsigned short cursor = payload_start;
    unsigned short target_total = kksdk_irdevice_read_be16(remote + cursor);
    bool has_target_total = false;
    if (target_total != 0U && target_total > kksdk_irdevice_sum_durations(durations, *count)) {
        has_target_total = true;
        cursor = static_cast<unsigned short>(cursor + 2U);
    } else if (target_total == 0U) {
        if (*count == 0) {
            return kksdk_irdevice_result_error();
        }
        if (static_cast<unsigned int>(cursor + 2U) <= payload_end) {
            durations[*count - 1U] = static_cast<unsigned short>(
                    durations[*count - 1U] + kksdk_irdevice_read_be16(remote + cursor));
            cursor = static_cast<unsigned short>(cursor + 2U);
        }
        has_target_total = true;
        target_total = 1U;
    }

    bool appended_literal = false;
    while (static_cast<unsigned int>(cursor + 2U) <= payload_end) {
        const unsigned char high = remote[cursor];
        const unsigned char low = remote[cursor + 1U];
        const unsigned short value = kksdk_irdevice_duration_value_from_pair(high, low);
        if (kksdk_irdevice_duration_flag_from_high_byte(high) != 0) {
            return kksdk_irdevice_apply_duration_gap(durations, capacity, count,
                    value, (has_target_total || appended_literal) ? 0 : 1);
        }

        const unsigned int status = kksdk_irdevice_append_duration(durations,
                capacity, count, kksdk_irdevice_read_be16(remote + cursor));
        if (status != 0) {
            return status;
        }
        appended_literal = true;
        cursor = static_cast<unsigned short>(cursor + 2U);
    }

    if (target_total == 0 || !has_target_total) {
        return 0;
    }
    return kksdk_irdevice_apply_duration_gap(durations, capacity, count,
            target_total, 0);
}

extern "C" unsigned int kksdk_irdevice_apply_duration_gap(unsigned short *durations,
        unsigned short capacity, unsigned short *count, unsigned short target_total,
        int merge_with_previous) {
    if (durations == nullptr || count == nullptr) {
        return kksdk_irdevice_result_error();
    }

    const unsigned short current_total = static_cast<unsigned short>(
            kksdk_irdevice_sum_durations(durations, *count));
    if (target_total <= current_total) {
        return 0;
    }

    const unsigned short gap = static_cast<unsigned short>(target_total - current_total);
    if (merge_with_previous) {
        if (*count == 0) {
            return kksdk_irdevice_result_error();
        }
        durations[*count - 1U] = static_cast<unsigned short>(durations[*count - 1U] + gap);
        return 0;
    }

    return kksdk_irdevice_append_duration(durations, capacity, count, gap);
}

extern "C" unsigned int kksdk_irdevice_apply_direct_gap(unsigned short *durations,
        unsigned short capacity, unsigned short *count, unsigned short gap,
        int merge_with_previous) {
    if (durations == nullptr || count == nullptr) {
        return kksdk_irdevice_result_error();
    }
    if (gap == 0U) {
        return 0;
    }
    if (merge_with_previous) {
        if (*count == 0) {
            return kksdk_irdevice_result_error();
        }
        durations[*count - 1U] = static_cast<unsigned short>(durations[*count - 1U] + gap);
        return 0;
    }
    return kksdk_irdevice_append_duration(durations, capacity, count, gap);
}

extern "C" int kksdk_irdevice_decode_fixed_32_bits(const int *durations, int count,
        char *out_bits, int *out_format) {
    if (durations == nullptr || out_bits == nullptr || out_format == nullptr ||
            count <= 0x43 ||
            kksdk_irdevice_value_in_window(durations[0], 0x2134U, 0x3e9U) == 0 ||
            kksdk_irdevice_value_in_window(durations[1], 4000U, 0x3e9U) == 0) {
        return 0;
    }

    int bit_count = 0;
    int total = durations[0] + durations[1];
    for (unsigned int i = 2; i != 0x43U; ++i) {
        const int value = durations[i];
        if ((i & 1U) == 0) {
            if (kksdk_irdevice_value_in_window(value, 0x168U, 401U) == 0) {
                return 0;
            }
        } else {
            if (kksdk_irdevice_value_in_window(value, 0x168U, 0x191U) != 0) {
                out_bits[bit_count++] = '0';
            } else {
                if (kksdk_irdevice_value_in_window(value, 0x5c8U, 401U) == 0) {
                    return 0;
                }
                out_bits[bit_count++] = '1';
            }
        }
        total += value;
    }

    *out_format = 2;
    for (int i = 0; i < 8; ++i) {
        if (out_bits[0x10 + i] == out_bits[0x18 + i]) {
            *out_format = 0x11;
            break;
        }
    }

    if (count == 0x44) {
        return bit_count;
    }
    if (count > 0x47 &&
            kksdk_irdevice_value_in_window(total + durations[0x43], 0x17bb0U, 0x5461U) != 0 &&
            kksdk_irdevice_value_in_window(durations[0x44], 0x1fa4U, 0x709U) != 0 &&
            kksdk_irdevice_value_in_window(durations[0x45], 0x77aU, 0x2a1U) != 0 &&
            kksdk_irdevice_value_in_window(durations[0x46], 0x180U, 0x161U) != 0) {
        return kksdk_irdevice_accepts_trailer_gap(count, 0x48U, durations[0x47]) != 0 ?
                bit_count : 0;
    }
    return 0;
}

extern "C" int kksdk_irdevice_value_in_window(
        int value, unsigned int base, unsigned int width) {
    return static_cast<unsigned int>(value - static_cast<int>(base)) < width ? 1 : 0;
}

extern "C" unsigned int kksdk_irdevice_unknown_pulse_state() {
    return 0xffffffffU;
}

extern "C" int kksdk_irdevice_pulse_state_is_unknown(unsigned int state) {
    return state == kksdk_irdevice_unknown_pulse_state() ? 1 : 0;
}

extern "C" unsigned int kksdk_irdevice_mid_sequence_pause_min() {
    return 0x1701U;
}

extern "C" unsigned int kksdk_irdevice_terminal_pause_min() {
    return 0x2aaU;
}

extern "C" unsigned int kksdk_irdevice_trailer_gap_min() {
    return 5000U;
}

extern "C" int kksdk_irdevice_accepts_terminal_pause(
        unsigned int count, unsigned int index, int value) {
    if (index + 1U < count && value >= static_cast<int>(kksdk_irdevice_mid_sequence_pause_min())) {
        return 1;
    }
    return index + 1U == count &&
            value >= static_cast<int>(kksdk_irdevice_terminal_pause_min()) ? 1 : 0;
}

extern "C" int kksdk_irdevice_accepts_trailer_gap(
        unsigned int count, unsigned int exact_count, int trailer_value) {
    return count == exact_count ||
            trailer_value >= static_cast<int>(kksdk_irdevice_trailer_gap_min()) ? 1 : 0;
}

extern "C" unsigned int kksdk_irdevice_next_pulse_kind(unsigned int index) {
    return (index + 1U) & 1U;
}

extern "C" int kksdk_irdevice_decode_alternating_14_bits(const int *durations,
        unsigned int count, char *out_bits) {
    return kksdk_irdevice_decode_alternating_bits(durations, count, out_bits, 0xeU);
}

extern "C" int kksdk_irdevice_decode_alternating_bits(const int *durations,
        unsigned int count, char *out_bits, unsigned int target_bits) {
    if (durations == nullptr || out_bits == nullptr || static_cast<int>(count) < 0xe) {
        return 0;
    }

    unsigned int last_kind = 0;
    int bit_count = 0;
    unsigned int index = 0;
    while (index < count && bit_count <= static_cast<int>(target_bits - 1U)) {
        const int value = durations[index];
        const unsigned int next_kind = kksdk_irdevice_next_pulse_kind(index);
        if (last_kind == 1U && bit_count == static_cast<int>(target_bits - 1U) &&
                next_kind == 0U) {
            if (kksdk_irdevice_accepts_terminal_pause(count, index, value) == 0) {
                return 0;
            }
            last_kind = kksdk_irdevice_unknown_pulse_state();
            out_bits[bit_count++] = '0';
        } else if (kksdk_irdevice_value_in_window(value, 0x2aaU, 0x1a0U) != 0) {
            if (last_kind == 1U) {
                last_kind = kksdk_irdevice_unknown_pulse_state();
                out_bits[bit_count++] = '0';
            } else if (last_kind == 0U) {
                last_kind = kksdk_irdevice_unknown_pulse_state();
                out_bits[bit_count++] = '1';
            } else if (kksdk_irdevice_pulse_state_is_unknown(last_kind) != 0) {
                last_kind = next_kind;
            } else {
                return 0;
            }
        } else {
            if (kksdk_irdevice_value_in_window(value, 0x623U, 0x19fU) == 0) {
                return 0;
            }
            if (last_kind == 1U) {
                last_kind = 0U;
                out_bits[bit_count++] = '0';
            } else if (last_kind == 0U) {
                last_kind = 1U;
                out_bits[bit_count++] = '1';
            } else {
                return 0;
            }
        }
        ++index;
    }

    return bit_count == static_cast<int>(target_bits) ? bit_count : 0;
}

extern "C" int kksdk_irdevice_decode_alternating_15_bits(const int *durations,
        unsigned int count, char *out_bits) {
    return kksdk_irdevice_decode_alternating_bits(durations, count, out_bits, 0xfU);
}

extern "C" int kksdk_irdevice_decode_odd_index_bits(const int *durations, int count,
        char *out_bits, unsigned int terminal_index, unsigned int header_base,
        unsigned int header_width, unsigned int zero_base, unsigned int zero_width,
        unsigned int one_base, unsigned int one_width, int exact_count) {
    if (durations == nullptr || out_bits == nullptr ||
            count <= static_cast<int>(terminal_index) ||
            kksdk_irdevice_value_in_window(durations[0], header_base, header_width) == 0) {
        return 0;
    }

    int bit_count = 0;
    for (unsigned int index = 1; index < terminal_index; ++index) {
        const int value = durations[index];
        if ((index & 1U) == 0) {
            continue;
        }
        const int bit = kksdk_irdevice_classify_binary_pulse(value, zero_base,
                zero_width, one_base, one_width);
        if (bit < 0) {
            return 0;
        }
        out_bits[bit_count++] = static_cast<char>('0' + bit);
    }

    if (kksdk_irdevice_accepts_trailer_gap(static_cast<unsigned int>(count),
                static_cast<unsigned int>(exact_count), durations[terminal_index]) == 0) {
        return 0;
    }
    return bit_count;
}

extern "C" int kksdk_irdevice_decode_23_odd_bits(const int *durations, int count,
        char *out_bits) {
    return kksdk_irdevice_decode_odd_index_bits(durations, count, out_bits,
            0x17U, 1U, 0x154U, 0x1136U, 0x405U, 0x1a31U, 0x5d2U, 0x18);
}

extern "C" int kksdk_irdevice_decode_25_odd_bits(const int *durations, int count,
        char *out_bits) {
    return kksdk_irdevice_decode_odd_index_bits(durations, count, out_bits,
            0x19U, 0xe6U, 0x1f5U, 0x708U, 0x1f5U, 0x10b8U, 600U, 0x1a);
}

extern "C" int kksdk_irdevice_decode_23_high_odd_bits(const int *durations, int count,
        char *out_bits) {
    return kksdk_irdevice_decode_odd_index_bits(durations, count, out_bits,
            0x17U, 1U, 0x177U, 0x13a7U, 0x45fU, 0x1db5U, 0x69aU, 0x18);
}

extern "C" int kksdk_irdevice_classify_binary_pulse(int value, unsigned int zero_base,
        unsigned int zero_width, unsigned int one_base, unsigned int one_width) {
    if (kksdk_irdevice_value_in_window(value, zero_base, zero_width) != 0) {
        return 0;
    }
    if (kksdk_irdevice_value_in_window(value, one_base, one_width + 1U) != 0) {
        return 1;
    }
    return -1;
}

extern "C" int kksdk_irdevice_decode_variable_pulse_bits(const int *durations,
        unsigned int count, char *out_bits, unsigned int target_bits) {
    if (durations == nullptr || out_bits == nullptr || count < target_bits + 2U ||
            kksdk_irdevice_value_in_window(durations[0], 0x8ffU, 0x2d7U) == 0 ||
            kksdk_irdevice_value_in_window(durations[1], 0x2a9U, 0x1a1U) == 0) {
        return 0;
    }

    unsigned int state = kksdk_irdevice_unknown_pulse_state();
    unsigned int source_index = 0;
    unsigned int bit_count = 0;
    while (bit_count <= target_bits - 1U && source_index + 2U < count) {
        const int value = durations[source_index + 2U];
        const unsigned int next_kind = kksdk_irdevice_next_pulse_kind(source_index + 2U);

        unsigned int low = 0x2b1U;
        unsigned int high = 0x440U;
        unsigned int split = kksdk_irdevice_variable_pulse_default_split();
        if (bit_count == 3U || bit_count == 4U) {
            low = 0x46eU;
            high = 0x5fdU;
            split = bit_count == 4U ?
                    kksdk_irdevice_variable_pulse_bit4_split() :
                    kksdk_irdevice_variable_pulse_default_split();
        }

        bool long_pulse = false;
        if (kksdk_irdevice_value_in_window(
                    value, kksdk_irdevice_variable_pulse_long_min(),
                    split - kksdk_irdevice_variable_pulse_long_min()) != 0) {
            long_pulse = true;
        } else {
            if (kksdk_irdevice_value_in_window(value, low, high - low) == 0) {
                return 0;
            }
        }

        if (state == 1U) {
            state = long_pulse ? kksdk_irdevice_unknown_pulse_state() : 0U;
            out_bits[bit_count++] = '1';
        } else if (state == 0U) {
            state = long_pulse ? kksdk_irdevice_unknown_pulse_state() : 1U;
            out_bits[bit_count++] = '0';
        } else if (kksdk_irdevice_pulse_state_is_unknown(state) != 0) {
            state = next_kind;
            if (!long_pulse) {
                return 0;
            }
        } else {
            return 0;
        }

        ++source_index;
    }

    return bit_count == target_bits ? static_cast<int>(bit_count) : 0;
}

extern "C" unsigned int kksdk_irdevice_variable_pulse_long_min() {
    return 0xf5U;
}

extern "C" unsigned int kksdk_irdevice_variable_pulse_default_split() {
    return 0x284U;
}

extern "C" unsigned int kksdk_irdevice_variable_pulse_bit4_split() {
    return 0x441U;
}

extern "C" int kksdk_irdevice_decode_21_variable_bits(const int *durations,
        unsigned int count, char *out_bits) {
    return kksdk_irdevice_decode_variable_pulse_bits(durations, count, out_bits, 0x15U);
}

extern "C" int kksdk_irdevice_decode_37_variable_bits(const int *durations,
        unsigned int count, char *out_bits) {
    return kksdk_irdevice_decode_variable_pulse_bits(durations, count, out_bits, 0x25U);
}

extern "C" int kksdk_irdevice_decode_4level_17_symbols(const int *durations, int count,
        char *out_symbols) {
    if (durations == nullptr || out_symbols == nullptr || count <= 0x23 ||
            kksdk_irdevice_value_in_window(durations[0], 0xffU, 0x143U) == 0 ||
            kksdk_irdevice_value_in_window(durations[1], 0x83U, 0x127U) == 0) {
        return 0;
    }

    int symbol_count = 0;
    for (unsigned int index = 0; index != 0x21U; ++index) {
        const int value = durations[index + 2U];
        if (((index + 2U) & 1U) == 0) {
            if (kksdk_irdevice_value_in_window(value, 0x10U, 301U) == 0) {
                return 0;
            }
            continue;
        }

        if (kksdk_irdevice_value_in_window(value, 0xc4U, 0xa5U) != 0) {
            out_symbols[symbol_count++] = '0';
        } else if (kksdk_irdevice_value_in_window(value, 0x16aU, 0xa5U) != 0) {
            out_symbols[symbol_count++] = '1';
        } else if (kksdk_irdevice_value_in_window(value, 0x211U, 0xa5U) != 0) {
            out_symbols[symbol_count++] = '2';
        } else {
            if (kksdk_irdevice_value_in_window(value, 0x2b8U, 0xa5U) == 0) {
                return 0;
            }
            out_symbols[symbol_count++] = '3';
        }
    }

    if (kksdk_irdevice_accepts_trailer_gap(
                static_cast<unsigned int>(count), 0x24U, durations[0x23]) == 0) {
        return 0;
    }
    return symbol_count;
}

extern "C" unsigned int kksdk_irdevice_special_decoder_target_bits() {
    return 0x14U;
}

extern "C" int kksdk_irdevice_decode_20_special_bits(const int *durations,
        unsigned int count, char *out_bits) {
    const unsigned int target_bits = kksdk_irdevice_special_decoder_target_bits();
    if (durations == nullptr || out_bits == nullptr || count < target_bits) {
        return 0;
    }

    unsigned int state = 0;
    unsigned int index = 0;
    int bit_count = 0;
    while (index < count && bit_count < static_cast<int>(target_bits)) {
        const int value = durations[index];
        const unsigned int next_kind = kksdk_irdevice_next_pulse_kind(index);
        if (state == 1U && bit_count == static_cast<int>(target_bits - 1U) &&
                next_kind == 0U) {
            if (kksdk_irdevice_accepts_terminal_pause(count, index, value) == 0) {
                return 0;
            }
            out_bits[bit_count++] = '0';
            state = kksdk_irdevice_unknown_pulse_state();
        } else if (bit_count == 8) {
            if (kksdk_irdevice_pulse_state_is_unknown(state) == 0 || next_kind != 0U) {
                return 0;
            }
            if (kksdk_irdevice_value_in_window(value, 0xc80U, 0x259U) != 0) {
                state = kksdk_irdevice_unknown_pulse_state();
            } else {
                if (kksdk_irdevice_value_in_window(value, 0xff9U, 601U) == 0) {
                    return 0;
                }
                state = 0;
            }
        } else {
            bool long_pulse = false;
            unsigned int low = 0x623U;
            unsigned int high = 0x7c2U;
            unsigned int split = 0x44aU;
            if (bit_count == 7) {
                if (state != 1U) {
                    low = 0x623U;
                    high = 0x7c2U;
                    split = 0x44aU;
                } else {
                    low = 0x13cfU;
                    high = 0x156eU;
                    split = 0x1252U;
                }
            }

            if (kksdk_irdevice_value_in_window(
                        value, kksdk_irdevice_terminal_pause_min(),
                        split - kksdk_irdevice_terminal_pause_min()) != 0) {
                long_pulse = true;
            } else {
                if (kksdk_irdevice_value_in_window(value, low, high - low) == 0) {
                    return 0;
                }
            }

            if (state == 1U) {
                out_bits[bit_count++] = '0';
                state = long_pulse ? kksdk_irdevice_unknown_pulse_state() : 0U;
            } else if (state == 0U) {
                out_bits[bit_count++] = '1';
                state = long_pulse ? kksdk_irdevice_unknown_pulse_state() : 1U;
            } else if (kksdk_irdevice_pulse_state_is_unknown(state) != 0) {
                state = next_kind;
                if (!long_pulse) {
                    return 0;
                }
            } else {
                return 0;
            }
        }
        ++index;
    }

    return bit_count == static_cast<int>(target_bits) ? bit_count : 0;
}

extern "C" int kksdk_irdevice_parse_pulse_protocol(const int *durations,
        unsigned int count, char *out_bits, kksdk_irdevice_parse_result *result) {
    if (result != nullptr) {
        result->format = 0;
        result->bit_count = 0;
    }
    if (durations == nullptr || out_bits == nullptr || result == nullptr) {
        return 0;
    }

    int format = 0;
    int bit_count = kksdk_irdevice_decode_fixed_32_bits(durations,
            static_cast<int>(count), out_bits, &format);
    if (bit_count > 0) {
        result->format = format;
        result->bit_count = bit_count;
        return bit_count;
    }

    bit_count = kksdk_irdevice_decode_alternating_15_bits(durations, count, out_bits);
    if (bit_count > 0) {
        result->format = 0x334;
        result->bit_count = bit_count;
        return bit_count;
    }

    bit_count = kksdk_irdevice_decode_alternating_14_bits(durations, count, out_bits);
    if (bit_count > 0) {
        result->format = 0x2a;
        result->bit_count = bit_count;
        return bit_count;
    }

    bit_count = kksdk_irdevice_decode_37_variable_bits(durations, count, out_bits);
    if (bit_count > 0) {
        result->format = 0x16a;
        result->bit_count = bit_count;
        return bit_count;
    }

    bit_count = kksdk_irdevice_decode_23_odd_bits(durations, static_cast<int>(count), out_bits);
    if (bit_count > 0) {
        result->format = 0x2f;
        result->bit_count = bit_count;
        return bit_count;
    }

    bit_count = kksdk_irdevice_decode_21_variable_bits(durations, count, out_bits);
    if (bit_count > 0) {
        result->format = 0x98;
        result->bit_count = bit_count;
        return bit_count;
    }

    bit_count = kksdk_irdevice_decode_4level_17_symbols(durations,
            static_cast<int>(count), out_bits);
    if (bit_count > 0) {
        result->format = 0x1ec;
        result->bit_count = bit_count;
        return bit_count;
    }

    bit_count = kksdk_irdevice_decode_25_odd_bits(durations, static_cast<int>(count), out_bits);
    if (bit_count > 0) {
        result->format = 0xac;
        result->bit_count = bit_count;
        return bit_count;
    }

    bit_count = kksdk_irdevice_decode_23_high_odd_bits(durations,
            static_cast<int>(count), out_bits);
    if (bit_count > 0) {
        result->format = 0x3ed;
        result->bit_count = bit_count;
        return bit_count;
    }

    bit_count = kksdk_irdevice_decode_20_special_bits(durations, count, out_bits);
    if (bit_count > 0) {
        result->format = 0x1a1;
        result->bit_count = bit_count;
        return bit_count;
    }

    return 0;
}
