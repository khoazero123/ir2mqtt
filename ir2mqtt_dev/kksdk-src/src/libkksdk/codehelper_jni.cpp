#include "codehelper_jni.hpp"

#ifndef KKSDK_HOST_CORE_ONLY
#include "codehelper_encode_engine.hpp"

#include <jni.h>
#endif

#include <algorithm>
#include <cstring>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

struct kksdk_codehelper_remote_config {
    unsigned int remote_id = 0;
    std::vector<std::string> fixed_records;
    std::vector<std::string> patch_records;
    std::vector<std::string> checksum_records;
    std::vector<std::string> key_records;
};

namespace {

#ifndef KKSDK_HOST_CORE_ONLY
struct CodeHelperRemote {
    unsigned int remote_id = 0;
    unsigned int remote_type = 0;
    kksdk_remote_encoder *encoder = nullptr;
};

bool clear_pending_exception(JNIEnv *env) {
    if (env->ExceptionCheck() == JNI_FALSE) {
        return false;
    }
    env->ExceptionClear();
    return true;
}

std::string get_java_string(JNIEnv *env, jstring value) {
    if (env == nullptr || value == nullptr) {
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

std::vector<std::string> get_string_array(JNIEnv *env, jobjectArray array) {
    std::vector<std::string> values;
    if (env == nullptr || array == nullptr) {
        return values;
    }

    jsize length = env->GetArrayLength(array);
    if (clear_pending_exception(env) || length <= 0) {
        return values;
    }

    values.reserve(static_cast<std::size_t>(length));
    for (jsize i = 0; i < length; ++i) {
        auto item = static_cast<jstring>(env->GetObjectArrayElement(array, i));
        if (clear_pending_exception(env)) {
            continue;
        }
        values.push_back(get_java_string(env, item));
        if (item != nullptr) {
            env->DeleteLocalRef(item);
        }
    }
    return values;
}

void write_remote_handle(JNIEnv *env, void *out_remote_ptr, CodeHelperRemote *remote) {
    auto out_remote = reinterpret_cast<jlongArray>(out_remote_ptr);
    if (env == nullptr || out_remote == nullptr) {
        return;
    }

    if (env->GetArrayLength(out_remote) < 1 || clear_pending_exception(env)) {
        return;
    }

    jlong handle = reinterpret_cast<jlong>(remote);
    env->SetLongArrayRegion(out_remote, 0, 1, &handle);
    clear_pending_exception(env);
}
#endif

int hex_value(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

#ifndef KKSDK_HOST_CORE_ONLY
jobjectArray make_empty_byte_matrix(JNIEnv *env) {
    jclass byte_array_class = env->FindClass("[B");
    if (clear_pending_exception(env) || byte_array_class == nullptr) {
        return nullptr;
    }
    jobjectArray matrix = env->NewObjectArray(0, byte_array_class, nullptr);
    clear_pending_exception(env);
    env->DeleteLocalRef(byte_array_class);
    return matrix;
}

std::vector<unsigned char> get_byte_array(JNIEnv *env, jobject array) {
    std::vector<unsigned char> bytes;
    if (env == nullptr || array == nullptr) {
        return bytes;
    }

    jsize length = env->GetArrayLength(reinterpret_cast<jbyteArray>(array));
    if (clear_pending_exception(env) || length <= 0) {
        return bytes;
    }

    bytes.resize(static_cast<std::size_t>(length));
    env->GetByteArrayRegion(reinterpret_cast<jbyteArray>(array), 0, length,
            reinterpret_cast<jbyte *>(bytes.data()));
    clear_pending_exception(env);
    return bytes;
}

jobjectArray make_byte_matrix(JNIEnv *env, unsigned char **frames,
        unsigned long long *frame_sizes, unsigned long long frame_count) {
    if (env == nullptr || frames == nullptr || frame_sizes == nullptr || frame_count == 0) {
        return make_empty_byte_matrix(env);
    }

    jclass byte_array_class = env->FindClass("[B");
    if (clear_pending_exception(env) || byte_array_class == nullptr) {
        return make_empty_byte_matrix(env);
    }

    jobjectArray matrix = env->NewObjectArray(static_cast<jsize>(frame_count),
            byte_array_class, nullptr);
    if (clear_pending_exception(env) || matrix == nullptr) {
        env->DeleteLocalRef(byte_array_class);
        return make_empty_byte_matrix(env);
    }

    for (unsigned long long i = 0; i < frame_count; ++i) {
        const jsize length = static_cast<jsize>(frame_sizes[i]);
        jbyteArray row = env->NewByteArray(length);
        if (clear_pending_exception(env) || row == nullptr) {
            continue;
        }
        if (length > 0 && frames[i] != nullptr) {
            env->SetByteArrayRegion(row, 0, length,
                    reinterpret_cast<const jbyte *>(frames[i]));
            clear_pending_exception(env);
        }
        env->SetObjectArrayElement(matrix, static_cast<jsize>(i), row);
        clear_pending_exception(env);
        env->DeleteLocalRef(row);
    }

    env->DeleteLocalRef(byte_array_class);
    return matrix;
}
#endif

}  // namespace

#ifndef KKSDK_HOST_CORE_ONLY
extern "C" void *kksdk_codehelper_encode(void *env_ptr, unsigned int remote_type,
        long long remote_handle, unsigned int power, unsigned int mode,
        unsigned int temperature, unsigned int wind_speed, unsigned int lr_wind_mode,
        unsigned int ud_wind_mode, unsigned int function_id, void *ext_bytes_ptr,
        void *ext_string_ptr) {
    auto *env = reinterpret_cast<JNIEnv *>(env_ptr);
    if (env == nullptr) {
        return nullptr;
    }

    auto *remote = reinterpret_cast<CodeHelperRemote *>(remote_handle);
    (void)remote_type;
    if (remote == nullptr || remote->encoder == nullptr) {
        return make_empty_byte_matrix(env);
    }

    const std::vector<unsigned char> ext_bytes =
            get_byte_array(env, reinterpret_cast<jobject>(ext_bytes_ptr));
    const std::string ext_string =
            get_java_string(env, reinterpret_cast<jstring>(ext_string_ptr));

    unsigned char **frames = nullptr;
    unsigned long long *frame_sizes = nullptr;
    unsigned long long frame_count = 0;
    const int ok = kksdk_remote_encoder_encode(remote->encoder, power, mode, temperature,
            wind_speed, lr_wind_mode, ud_wind_mode, function_id,
            ext_bytes.empty() ? nullptr : ext_bytes.data(), ext_bytes.size(),
            ext_string.empty() ? nullptr : ext_string.c_str(), &frames, &frame_count,
            &frame_sizes);
    if (ok == 0) {
        return make_empty_byte_matrix(env);
    }

    jobjectArray matrix = make_byte_matrix(env, frames, frame_sizes, frame_count);
    kksdk_remote_encoder_free_output(frames, frame_sizes, frame_count);
    return matrix;
}

extern "C" unsigned long long kksdk_codehelper_init_remote(void *env_ptr,
        unsigned int remote_id, unsigned int remote_type, void *names_ptr,
        void *out_remote_ptr) {
    auto *env = reinterpret_cast<JNIEnv *>(env_ptr);
    if (env == nullptr) {
        return 1;
    }

    auto *remote = new CodeHelperRemote();
    remote->remote_id = remote_id;
    remote->remote_type = remote_type;
    remote->encoder = kksdk_remote_encoder_create(remote_id);
    if (remote->encoder == nullptr) {
        delete remote;
        return 1;
    }

    const std::vector<std::string> lines =
            get_string_array(env, reinterpret_cast<jobjectArray>(names_ptr));
    for (const std::string &line : lines) {
        if (line.empty()) {
            continue;
        }
        if (kksdk_remote_encoder_add_line(remote->encoder, line.c_str(), line.size()) == 0) {
            kksdk_remote_encoder_destroy(remote->encoder);
            delete remote;
            return 1;
        }
    }

    write_remote_handle(env, out_remote_ptr, remote);
    return 0;
}

extern "C" void kksdk_codehelper_release_remote(unsigned int remote_id, void *remote_ptr) {
    (void)remote_id;
    auto *remote = reinterpret_cast<CodeHelperRemote *>(remote_ptr);
    if (remote == nullptr) {
        return;
    }
    if (remote->encoder != nullptr) {
        kksdk_remote_encoder_destroy(remote->encoder);
        remote->encoder = nullptr;
    }
    delete remote;
}
#endif

extern "C" unsigned int kksdk_codehelper_byte_value(unsigned int value) {
    return value & 0xffU;
}

extern "C" unsigned int kksdk_codehelper_shift5(unsigned int value) {
    return value & 0x1fU;
}

extern "C" unsigned int kksdk_codehelper_merge_byte_window(unsigned int current_byte,
        unsigned int bit_count, unsigned int bit_offset, unsigned int previous_byte) {
    const unsigned int current = kksdk_codehelper_byte_value(current_byte);
    const unsigned int previous = kksdk_codehelper_byte_value(previous_byte);
    const unsigned int offset = kksdk_codehelper_shift5(bit_offset);
    const unsigned int left_shift = kksdk_codehelper_shift5(8U - bit_count);
    const unsigned int right_shift = kksdk_codehelper_shift5(8U - bit_offset);
    const unsigned int count_shift = kksdk_codehelper_shift5(bit_count);

    const unsigned int low_bits = kksdk_codehelper_byte_value(current << offset) >> offset;
    const unsigned int high_bits = kksdk_codehelper_byte_value(current >> left_shift) <<
            left_shift;
    const unsigned int previous_bits =
            kksdk_codehelper_byte_value(
                    kksdk_codehelper_byte_value(previous << right_shift) << count_shift) >>
            count_shift;
    return low_bits + high_bits + previous_bits;
}

extern "C" unsigned int kksdk_codehelper_extract_shifted_byte_bits(unsigned int value,
        unsigned int bit_offset, int bit_count) {
    return kksdk_codehelper_byte_value(
            kksdk_codehelper_byte_value(value) << kksdk_codehelper_shift5(bit_offset)) >>
            kksdk_codehelper_shift5(
                    bit_offset - static_cast<unsigned int>(bit_count) + 8U);
}

extern "C" int kksdk_codehelper_select_patch_index(unsigned int tag,
        int requested_index, unsigned long long record_count) {
    if (requested_index < 0) {
        return static_cast<int>(kksdk_codehelper_invalid_index_result());
    }
    if (kksdk_codehelper_patch_tag_uses_adjusted_index(tag) != 0) {
        if (record_count == 1U) {
            return 0;
        }
        const int adjusted = requested_index - 0x10;
        if (adjusted < 0 ||
                static_cast<unsigned long long>(adjusted) >= record_count) {
            return static_cast<int>(kksdk_codehelper_invalid_index_result());
        }
        return adjusted;
    }
    // FUN_00154ff0: 0x3ed/0x3f5 use the single-record fast path only when count==1.
    if (kksdk_codehelper_patch_tag_requires_single_record(tag) != 0 &&
            record_count == 1U) {
        return requested_index > 0 ? 0 :
                static_cast<int>(kksdk_codehelper_invalid_index_result());
    }
    if (static_cast<unsigned long long>(requested_index) < record_count) {
        return requested_index;
    }
    return static_cast<int>(kksdk_codehelper_invalid_index_result());
}

extern "C" int kksdk_codehelper_patch_tag_uses_adjusted_index(unsigned int tag) {
    return kksdk_codehelper_patch_tag_is_adjusted_byte_pairs(tag) != 0 ||
            kksdk_codehelper_patch_tag_is_adjusted_bit_triplets(tag) != 0 ? 1 : 0;
}

extern "C" int kksdk_codehelper_patch_tag_requires_single_record(unsigned int tag) {
    return kksdk_codehelper_patch_tag_is_single_byte_pairs(tag) != 0 ||
            kksdk_codehelper_patch_tag_is_single_bit_triplets(tag) != 0 ? 1 : 0;
}

extern "C" unsigned int kksdk_codehelper_result_error() {
    return 0xffffffffU;
}

extern "C" int kksdk_codehelper_patch_tag_is_adjusted_byte_pairs(unsigned int tag) {
    return tag == kksdk_codehelper_patch_tag_adjusted_byte_pairs_id() ? 1 : 0;
}

extern "C" int kksdk_codehelper_patch_tag_is_adjusted_bit_triplets(unsigned int tag) {
    return tag == kksdk_codehelper_patch_tag_adjusted_bit_triplets_id() ? 1 : 0;
}

extern "C" int kksdk_codehelper_patch_tag_is_single_byte_pairs(unsigned int tag) {
    return tag == kksdk_codehelper_patch_tag_single_byte_pairs_id() ? 1 : 0;
}

extern "C" int kksdk_codehelper_patch_tag_is_single_bit_triplets(unsigned int tag) {
    return tag == kksdk_codehelper_patch_tag_single_bit_triplets_id() ? 1 : 0;
}

extern "C" unsigned int kksdk_codehelper_patch_tag_adjusted_byte_pairs_id() {
    return 0x3ebU;
}

extern "C" unsigned int kksdk_codehelper_patch_tag_adjusted_bit_triplets_id() {
    return 0x3f3U;
}

extern "C" unsigned int kksdk_codehelper_patch_tag_single_byte_pairs_id() {
    return 0x3edU;
}

extern "C" unsigned int kksdk_codehelper_patch_tag_single_bit_triplets_id() {
    return 0x3f5U;
}

extern "C" unsigned int kksdk_codehelper_apply_byte_pairs(
        kksdk_codehelper_byte_buffer *buffer, const unsigned char *records,
        unsigned long long record_size) {
    if (buffer == nullptr || buffer->data == nullptr || records == nullptr) {
        return kksdk_codehelper_result_error();
    }

    for (unsigned long long offset = 0; offset + 1ULL < record_size; offset += 2ULL) {
        const unsigned int target = records[offset];
        if (target < buffer->size) {
            buffer->data[target] = records[offset + 1ULL];
        }
    }
    return 0;
}

extern "C" unsigned int kksdk_codehelper_apply_repeat_add_pairs(
        kksdk_codehelper_byte_buffer *buffer, const unsigned char *records,
        unsigned long long record_size, unsigned int repeat_count) {
    if (buffer == nullptr || buffer->data == nullptr || records == nullptr) {
        return kksdk_codehelper_result_error();
    }
    if (repeat_count == 0) {
        return 0;
    }

    for (unsigned long long offset = 0; offset + 1ULL < record_size; offset += 2ULL) {
        const unsigned int target = records[offset];
        if (target < buffer->size) {
            const unsigned int delta = records[offset + 1ULL] * repeat_count;
            buffer->data[target] = static_cast<unsigned char>(buffer->data[target] + delta);
        }
    }
    return 0;
}

namespace {

void set_codehelper_bit(unsigned char *data, unsigned long long bit_index, unsigned int bit) {
    unsigned char &target = data[kksdk_codehelper_bit_byte_index(bit_index)];
    const unsigned int shift = kksdk_codehelper_bit_shift(bit_index);
    const unsigned char mask = static_cast<unsigned char>(1U << shift);
    if (bit != 0) {
        target = static_cast<unsigned char>(target | mask);
    } else {
        target = static_cast<unsigned char>(target & ~mask);
    }
}

unsigned int oem_fun_00152de4(unsigned int current_byte, unsigned int bit_offset,
        unsigned int merge_width, unsigned int value_byte) {
    const unsigned int current = current_byte & 0xffU;
    const unsigned int offset = bit_offset & 0x1fU;
    const unsigned int width = merge_width & 0x1fU;
    const unsigned int value = value_byte & 0xffU;
    const unsigned int low_bits = ((current << width) & 0xffU) >> width;
    const unsigned int high_bits =
            ((current >> ((8U - offset) & 0x1fU)) & 0xffU) << ((8U - offset) & 0x1fU);
    const unsigned int inserted =
            ((((value << ((8U - width) & 0x1fU)) & 0xffU) << offset) & 0xffU) >> offset;
    return low_bits + high_bits + inserted;
}

void apply_oem_plain_bit_triplet(unsigned char *data, unsigned long long size,
        unsigned int start_bit, unsigned int end_bit, unsigned int value_byte) {
    const unsigned int span = end_bit - start_bit;
    if (start_bit > end_bit || span >= 9U) {
        return;
    }

    const unsigned int start_byte = start_bit >> 3U;
    if (start_byte >= size) {
        return;
    }

    const int end_bit_minus_1 = static_cast<int>(end_bit) - 1;
    int end_byte_seed = static_cast<int>(end_bit) + 6;
    if (end_bit_minus_1 >= 0) {
        end_byte_seed = end_bit_minus_1;
    }
    const unsigned int end_byte = static_cast<unsigned int>(end_byte_seed >> 3);
    if (end_byte >= size) {
        return;
    }

    unsigned int bit_offset = start_bit & 7U;
    unsigned int merge_value = value_byte;
    unsigned int write_byte = start_byte;
    unsigned int current = data[start_byte];

    if (start_byte != end_byte) {
        unsigned int low_shift = span + (start_bit | 0xfffffff8U);
        low_shift &= 0x1fU;
        current = oem_fun_00152de4(current, bit_offset, 8U, value_byte >> low_shift);
        data[start_byte] = static_cast<unsigned char>(current);

        unsigned int remain_shift = 8U - (span + (start_bit | 0xfffffff8U));
        remain_shift &= 0x1fU;
        merge_value = ((value_byte << remain_shift) & 0xffU) >> remain_shift;
        bit_offset = 0U;
        write_byte = end_byte;
        current = data[end_byte];
    }

    const int merge_width = end_bit_minus_1 - static_cast<int>(end_byte) * 8 + 1;
    if (merge_width <= 0) {
        return;
    }

    current = oem_fun_00152de4(current, bit_offset, static_cast<unsigned int>(merge_width),
            merge_value);
    data[write_byte] = static_cast<unsigned char>(current);
}

unsigned int oem_fun_00152e30(unsigned int value, unsigned int bit_offset, int bit_count) {
    return kksdk_codehelper_extract_shifted_byte_bits(value, bit_offset, bit_count);
}

int oem_triplet_low_shift(unsigned int span, unsigned int start_bit) {
    return span + static_cast<int>(start_bit | 0xfffffff8U);
}

void apply_oem_lab_00154d50_triplet(unsigned char *data, unsigned long long size,
        unsigned int start_bit, unsigned int end_bit, unsigned int value_byte,
        unsigned int add_count) {
    const unsigned int span = end_bit - start_bit;
    if (start_bit > end_bit || span >= 9U) {
        return;
    }

    const unsigned int start_byte = start_bit >> 3U;
    if (start_byte >= size) {
        return;
    }

    const int end_bit_minus_1 = static_cast<int>(end_bit) - 1;
    int end_byte_seed = static_cast<int>(end_bit) + 6;
    if (end_bit_minus_1 >= 0) {
        end_byte_seed = end_bit_minus_1;
    }
    const unsigned int end_byte = static_cast<unsigned int>(end_byte_seed >> 3);
    if (end_byte >= size) {
        return;
    }

    const unsigned int bit_offset = start_bit & 7U;
    const int merge_bits = end_bit_minus_1 - static_cast<int>(end_byte) * 8 + 1;
    if (merge_bits <= 0) {
        return;
    }
    const unsigned int merge_width = static_cast<unsigned int>(merge_bits);

    if (start_byte == end_byte) {
        const unsigned int current = data[start_byte];
        const unsigned int field =
                oem_fun_00152e30(current, bit_offset, static_cast<int>(merge_width));
        unsigned int added = 0U;
        if (add_count > 0U) {
            added = value_byte * add_count;
        }
        const unsigned int merge_high =
                static_cast<unsigned int>(end_bit_minus_1 - static_cast<int>(end_byte) * 8);
        const unsigned int extract_offset =
                (bit_offset | 8U) + static_cast<unsigned int>(~merge_high);
        const unsigned int packed =
                oem_fun_00152e30(field + added, extract_offset, 8);
        data[start_byte] = static_cast<unsigned char>(oem_fun_00152de4(
                current, bit_offset, merge_width, packed));
        return;
    }

    const unsigned int low_shift =
            static_cast<unsigned int>(oem_triplet_low_shift(span, start_bit) & 0x1fU);
    const unsigned int remain_shift = (8U - low_shift) & 0x1fU;
    unsigned int merged = ((((static_cast<unsigned int>(data[start_byte]) << bit_offset) &
                                    0xffU) >>
                            bit_offset)
                    << low_shift) +
            (static_cast<unsigned int>(data[end_byte]) >> remain_shift);
    if (add_count > 0U) {
        merged = (merged & 0xffU) + value_byte * add_count;
    }

    const unsigned int top_extract = oem_fun_00152e30(merged, 8U - span, 8);
    data[start_byte] = static_cast<unsigned char>(oem_fun_00152de4(
            data[start_byte], bit_offset, 8U,
            (top_extract & 0xffU) >> low_shift));
    data[end_byte] = static_cast<unsigned char>(oem_fun_00152de4(
            data[end_byte], 0U, merge_width,
            ((merged & 0xffU) << remain_shift) >> remain_shift));
}

void apply_oem_lab_00154d50_triplets(unsigned char *data, unsigned long long size,
        const unsigned char *records, unsigned long long record_size,
        unsigned int add_count) {
    for (unsigned long long offset = 0; offset + 2ULL < record_size; offset += 3ULL) {
        const unsigned int start_bit = records[offset];
        const unsigned int end_bit = records[offset + 1ULL];
        const unsigned int value_byte = records[offset + 2ULL];
        apply_oem_lab_00154d50_triplet(
                data, size, start_bit, end_bit, value_byte, add_count);
    }
}

}  // namespace

extern "C" unsigned int kksdk_codehelper_apply_literal_bit_triplets(
        kksdk_codehelper_byte_buffer *buffer, const unsigned char *records,
        unsigned long long record_size) {
    if (buffer == nullptr || buffer->data == nullptr || records == nullptr) {
        return kksdk_codehelper_result_error();
    }

    const unsigned long long bit_capacity = kksdk_codehelper_bit_capacity(buffer->size);
    for (unsigned long long offset = 0; offset + 2ULL < record_size; offset += 3ULL) {
        const unsigned int start_bit = records[offset];
        const unsigned int end_bit = records[offset + 1ULL];
        const unsigned int value_byte = records[offset + 2ULL];
        if (start_bit > end_bit || end_bit - start_bit >= 9U) {
            continue;
        }
        if (kksdk_codehelper_is_valid_bit_range(start_bit, end_bit, bit_capacity) == 0) {
            continue;
        }
        const unsigned int bit_count = end_bit - start_bit;
        for (unsigned int bit = 0; bit < bit_count; ++bit) {
            set_codehelper_bit(buffer->data, start_bit + bit,
                    (value_byte >> (bit_count - bit - 1U)) & 1U);
        }
    }
    return 0;
}

extern "C" unsigned int kksdk_codehelper_apply_bit_triplets(
        kksdk_codehelper_byte_buffer *buffer, const unsigned char *records,
        unsigned long long record_size, unsigned int add_count) {
    if (buffer == nullptr || buffer->data == nullptr || records == nullptr) {
        return kksdk_codehelper_result_error();
    }

    const unsigned long long bit_capacity = kksdk_codehelper_bit_capacity(buffer->size);
    for (unsigned long long offset = 0; offset + 2ULL < record_size; offset += 3ULL) {
        const unsigned int start_bit = records[offset];
        const unsigned int end_bit = records[offset + 1ULL];
        if (start_bit > end_bit || end_bit - start_bit >= 9U) {
            continue;
        }

        const unsigned int value_byte = records[offset + 2ULL];
        if (add_count == 0U) {
            apply_oem_plain_bit_triplet(
                    buffer->data, buffer->size, start_bit, end_bit, value_byte);
            continue;
        }

        if (kksdk_codehelper_is_valid_bit_range(start_bit, end_bit, bit_capacity) == 0) {
            continue;
        }

        unsigned int current = 0;
        for (unsigned int bit = start_bit; bit < end_bit; ++bit) {
            current = (current << 1U) | kksdk_codehelper_get_bit(buffer->data, bit);
        }
        const unsigned int value = current + value_byte * add_count;

        const unsigned int bit_count = end_bit - start_bit;
        for (unsigned int bit = 0; bit < bit_count; ++bit) {
            const unsigned int shift = bit_count - bit - 1U;
            set_codehelper_bit(buffer->data, start_bit + bit, (value >> shift) & 1U);
        }
    }
    return 0;
}

extern "C" unsigned int kksdk_codehelper_apply_oem_bit_triplets_with_add(
        kksdk_codehelper_byte_buffer *buffer, const unsigned char *records,
        unsigned long long record_size, unsigned int add_count) {
    if (buffer == nullptr || buffer->data == nullptr || records == nullptr) {
        return kksdk_codehelper_result_error();
    }

    apply_oem_lab_00154d50_triplets(
            buffer->data, buffer->size, records, record_size, add_count);
    return 0;
}

extern "C" unsigned int kksdk_codehelper_apply_oem_ext_bit_triplets(
        kksdk_codehelper_byte_buffer *buffer, const unsigned char *records,
        unsigned long long record_size) {
    if (buffer == nullptr || buffer->data == nullptr || records == nullptr) {
        return kksdk_codehelper_result_error();
    }

    const unsigned long long bit_capacity = kksdk_codehelper_bit_capacity(buffer->size);
    for (unsigned long long offset = 0; offset + 2ULL < record_size; offset += 3ULL) {
        const unsigned int start_bit = records[offset];
        const unsigned int end_bit = records[offset + 1ULL];
        if (kksdk_codehelper_is_valid_bit_range(start_bit, end_bit, bit_capacity) == 0) {
            continue;
        }

        const unsigned int value_byte = records[offset + 2ULL];
        const unsigned int bit_count = end_bit - start_bit;
        for (unsigned int bit = 0; bit < bit_count; ++bit) {
            const unsigned int shift = bit_count - bit - 1U;
            set_codehelper_bit(buffer->data, start_bit + bit, (value_byte >> shift) & 1U);
        }
    }
    return 0;
}

extern "C" void kksdk_codehelper_apply_type2_compact_bits(unsigned char *buffer,
        unsigned long long buffer_size, const unsigned char *body,
        unsigned long long body_size, int repeat, int byte_offset) {
    if (buffer == nullptr || body == nullptr || body_size < 3ULL || repeat < 0 ||
            byte_offset < 0) {
        return;
    }

    for (unsigned long long i = 0; i + 2ULL < body_size; i += 3ULL) {
        const int first = static_cast<int>(body[i]);
        const int last = static_cast<int>(body[i + 1ULL]);
        const int width = last - first + 1;
        if (first < 1 || width < 1 || width > 16) {
            continue;
        }

        const unsigned int value =
                static_cast<unsigned int>(body[i + 2ULL]) * static_cast<unsigned int>(repeat);
        for (int bit = 0; bit < width; ++bit) {
            const int position = first - 1 + bit + byte_offset * 8;
            const std::size_t byte_index = static_cast<std::size_t>(position / 8);
            const int msb_offset = position % 8;
            if (byte_index >= buffer_size) {
                continue;
            }

            const unsigned int source_bit =
                    (value >> static_cast<unsigned int>(width - 1 - bit)) & 1U;
            const unsigned char mask =
                    static_cast<unsigned char>(0x80U >> static_cast<unsigned int>(msb_offset));
            if (source_bit != 0U) {
                buffer[byte_index] = static_cast<unsigned char>(buffer[byte_index] | mask);
            } else {
                buffer[byte_index] =
                        static_cast<unsigned char>(buffer[byte_index] & static_cast<unsigned char>(~mask));
            }
        }
    }
}

extern "C" unsigned long long kksdk_codehelper_bit_capacity(unsigned long long byte_size) {
    return byte_size * 8ULL;
}

extern "C" unsigned int kksdk_codehelper_patch_triplet_max_bits(void) {
    return 9U;
}

extern "C" int kksdk_codehelper_is_valid_bit_range(
        unsigned int start_bit, unsigned int end_bit, unsigned long long bit_capacity) {
    return start_bit < end_bit && end_bit <= bit_capacity &&
            end_bit - start_bit < kksdk_codehelper_patch_triplet_max_bits() ? 1 : 0;
}

extern "C" unsigned long long kksdk_codehelper_bit_byte_index(
        unsigned long long bit_index) {
    return bit_index >> 3U;
}

extern "C" unsigned int kksdk_codehelper_bit_shift(unsigned long long bit_index) {
    return 7U - static_cast<unsigned int>(bit_index & 7U);
}

extern "C" unsigned int kksdk_codehelper_get_bit(
        const unsigned char *data, unsigned long long bit_index) {
    if (data == nullptr) {
        return 0;
    }
    return (data[kksdk_codehelper_bit_byte_index(bit_index)] >>
            kksdk_codehelper_bit_shift(bit_index)) & 1U;
}

extern "C" unsigned int kksdk_codehelper_apply_patch_record(
        kksdk_codehelper_byte_buffer *buffer, const kksdk_codehelper_patch_record *record,
        unsigned int parameter) {
    if (buffer == nullptr || record == nullptr) {
        return kksdk_codehelper_result_error();
    }

    if (kksdk_codehelper_patch_tag_is_byte_pairs(record->tag) != 0) {
        return kksdk_codehelper_apply_byte_pairs(buffer, record->data, record->size);
    }
    if (kksdk_codehelper_patch_tag_is_repeat_add(record->tag) != 0) {
        if (parameter == 0U &&
                kksdk_codehelper_patch_tag_is_adjusted_byte_pairs(record->tag) != 0) {
            return kksdk_codehelper_apply_byte_pairs(buffer, record->data, record->size);
        }
        if (parameter > 0U && parameter < 0x10U) {
            return 0;
        }
        return kksdk_codehelper_apply_repeat_add_pairs(buffer, record->data,
                record->size, parameter == 0U ? 0U : parameter - 0x10U);
    }
    if (kksdk_codehelper_patch_tag_is_bit_triplets_with_add(record->tag) != 0) {
        if (parameter > 0U && parameter < 0x10U) {
            return 0;
        }
        return kksdk_codehelper_apply_bit_triplets(buffer, record->data,
                record->size, parameter == 0U ? 0U : parameter - 0x10U);
    }
    if (record->tag == 0x3f9U) {
        return kksdk_codehelper_apply_oem_ext_bit_triplets(
                buffer, record->data, record->size);
    }
    if (kksdk_codehelper_patch_tag_is_bit_triplets(record->tag) != 0) {
        return kksdk_codehelper_apply_bit_triplets(buffer, record->data, record->size, 0);
    }
    return 0;
}

extern "C" int kksdk_codehelper_patch_tag_is_byte_pairs(unsigned int tag) {
    switch (tag) {
    case 0x3e9U:
    case 0x3ecU:
    case 0x3edU:
    case 0x3eeU:
    case 0x3efU:
    case 0x3f2U:
    case 0x15b38U:
        return 1;
    default:
        return 0;
    }
}

extern "C" int kksdk_codehelper_patch_tag_is_repeat_add(unsigned int tag) {
    switch (tag) {
    case 0x3ebU:
    case 0x15b39U:
        return 1;
    default:
        return 0;
    }
}

extern "C" int kksdk_codehelper_patch_tag_is_bit_triplets_with_add(unsigned int tag) {
    switch (tag) {
    case 0x3f3U:
    case 0x1869eU:
        return 1;
    default:
        return 0;
    }
}

extern "C" int kksdk_codehelper_patch_tag_is_bit_triplets(unsigned int tag) {
    switch (tag) {
    case 0x3f4U:
    case 0x3f5U:
    case 0x3f6U:
    case 0x3f7U:
    case 0x3f8U:
    case 0x3f9U:
    case 99999U:
        return 1;
    default:
        return 0;
    }
}

extern "C" unsigned int kksdk_codehelper_checksum_seed_from_record(
        const unsigned char *record, unsigned long long record_size) {
    if (record == nullptr || record_size <= 4U) {
        return 0;
    }
    return record[4];
}

extern "C" unsigned int kksdk_codehelper_apply_checksum_record(
        kksdk_codehelper_byte_buffer *buffer, const unsigned char *record,
        unsigned long long record_size) {
    if (buffer == nullptr || buffer->data == nullptr || record == nullptr || record_size == 0) {
        return kksdk_codehelper_result_error();
    }

    const unsigned int mode = record[0];
    if (mode > 6U) {
        return 0;
    }

    if (kksdk_codehelper_checksum_mode_is_byte_sum(mode) != 0) {
        if (record_size < 4U) {
            return 0;
        }
        const unsigned int start = record[1];
        const unsigned int end = record[2];
        const unsigned int output = record[3];
        if (output >= buffer->size) {
            return 0;
        }

        const unsigned int sum = kksdk_codehelper_sum_byte_range(
                buffer->data, buffer->size, start, end,
                kksdk_codehelper_checksum_seed_from_record(record, record_size));
        buffer->data[output] = static_cast<unsigned char>(
                kksdk_codehelper_checksum_mode_is_complement(mode) != 0 ? ~sum : sum);
        return 0;
    }

    if (kksdk_codehelper_checksum_mode_is_nibble_sum(mode) != 0) {
        if (record_size < 4U) {
            return 0;
        }
        const unsigned int start = record[1];
        const unsigned int end = record[2];
        const unsigned int output = record[3];
        if (output >= buffer->size) {
            return 0;
        }

        const unsigned int sum = kksdk_codehelper_sum_nibble_range(
                buffer->data, buffer->size, start, end,
                kksdk_codehelper_checksum_seed_from_record(record, record_size));
        buffer->data[output] = static_cast<unsigned char>(
                kksdk_codehelper_checksum_mode_is_complement(mode) != 0 ? ~sum : sum);
        return 0;
    }

    if (kksdk_codehelper_checksum_mode_is_output_nibble(mode) == 0) {
        return 0;
    }
    if (record_size < 5U) {
        return 0;
    }

    const unsigned int output_nibble = record[1];
    if (kksdk_codehelper_nibble_in_bounds(buffer->size, output_nibble) == 0) {
        return 0;
    }

    unsigned int sum = record[2];
    for (unsigned long long offset = 3; offset < record_size; ++offset) {
        const unsigned int source_nibble = record[offset];
        if (kksdk_codehelper_nibble_in_bounds(buffer->size, source_nibble) == 0) {
            continue;
        }
        sum += kksdk_codehelper_read_nibble(
                buffer->data, buffer->size, source_nibble);
    }
    if (kksdk_codehelper_checksum_mode_is_complement(mode) != 0) {
        sum = ~sum;
    }

    kksdk_codehelper_write_nibble(buffer->data, buffer->size, output_nibble, sum);
    return 0;
}

extern "C" int kksdk_codehelper_checksum_mode_is_byte_sum(unsigned int mode) {
    return kksdk_codehelper_checksum_mode_is_byte_sum_plain(mode) != 0 ||
            kksdk_codehelper_checksum_mode_is_byte_sum_complement(mode) != 0 ? 1 : 0;
}

extern "C" int kksdk_codehelper_checksum_mode_is_nibble_sum(unsigned int mode) {
    return kksdk_codehelper_checksum_mode_is_nibble_sum_plain(mode) != 0 ||
            kksdk_codehelper_checksum_mode_is_nibble_sum_complement(mode) != 0 ? 1 : 0;
}

extern "C" int kksdk_codehelper_checksum_mode_is_output_nibble(unsigned int mode) {
    return kksdk_codehelper_checksum_mode_is_output_nibble_plain(mode) != 0 ||
            kksdk_codehelper_checksum_mode_is_output_nibble_complement(mode) != 0 ? 1 : 0;
}

extern "C" int kksdk_codehelper_checksum_mode_is_complement(unsigned int mode) {
    return kksdk_codehelper_checksum_mode_is_byte_sum_complement(mode) != 0 ||
            kksdk_codehelper_checksum_mode_is_nibble_sum_complement(mode) != 0 ||
            kksdk_codehelper_checksum_mode_is_output_nibble_complement(mode) != 0 ? 1 : 0;
}

extern "C" int kksdk_codehelper_checksum_mode_is_byte_sum_plain(unsigned int mode) {
    return mode == kksdk_codehelper_checksum_mode_byte_sum_plain_id() ? 1 : 0;
}

extern "C" int kksdk_codehelper_checksum_mode_is_byte_sum_complement(unsigned int mode) {
    return mode == kksdk_codehelper_checksum_mode_byte_sum_complement_id() ? 1 : 0;
}

extern "C" int kksdk_codehelper_checksum_mode_is_nibble_sum_plain(unsigned int mode) {
    return mode == kksdk_codehelper_checksum_mode_nibble_sum_plain_id() ? 1 : 0;
}

extern "C" int kksdk_codehelper_checksum_mode_is_nibble_sum_complement(unsigned int mode) {
    return mode == kksdk_codehelper_checksum_mode_nibble_sum_complement_id() ? 1 : 0;
}

extern "C" int kksdk_codehelper_checksum_mode_is_output_nibble_plain(unsigned int mode) {
    return mode == kksdk_codehelper_checksum_mode_output_nibble_plain_id() ? 1 : 0;
}

extern "C" int kksdk_codehelper_checksum_mode_is_output_nibble_complement(unsigned int mode) {
    return mode == kksdk_codehelper_checksum_mode_output_nibble_complement_id() ? 1 : 0;
}

extern "C" unsigned int kksdk_codehelper_checksum_mode_byte_sum_plain_id() {
    return 1U;
}

extern "C" unsigned int kksdk_codehelper_checksum_mode_byte_sum_complement_id() {
    return 2U;
}

extern "C" unsigned int kksdk_codehelper_checksum_mode_nibble_sum_plain_id() {
    return 3U;
}

extern "C" unsigned int kksdk_codehelper_checksum_mode_nibble_sum_complement_id() {
    return 4U;
}

extern "C" unsigned int kksdk_codehelper_checksum_mode_output_nibble_plain_id() {
    return 5U;
}

extern "C" unsigned int kksdk_codehelper_checksum_mode_output_nibble_complement_id() {
    return 6U;
}

extern "C" unsigned int kksdk_codehelper_nibble_byte_index(unsigned int nibble_index) {
    return nibble_index >> 1U;
}

extern "C" int kksdk_codehelper_nibble_in_bounds(
        unsigned long long size, unsigned int nibble_index) {
    return kksdk_codehelper_nibble_byte_index(nibble_index) < size ? 1 : 0;
}

extern "C" int kksdk_codehelper_nibble_is_low(unsigned int nibble_index) {
    return (nibble_index & 1U) != 0 ? 1 : 0;
}

extern "C" unsigned int kksdk_codehelper_low_nibble(unsigned int value) {
    return value & 0x0fU;
}

extern "C" unsigned int kksdk_codehelper_high_nibble(unsigned int value) {
    return value >> 4U;
}

extern "C" unsigned int kksdk_codehelper_read_nibble(const unsigned char *data,
        unsigned long long size, unsigned int nibble_index) {
    const unsigned int byte_index = kksdk_codehelper_nibble_byte_index(nibble_index);
    if (data == nullptr || byte_index >= size) {
        return 0;
    }
    const unsigned int value = data[byte_index];
    return kksdk_codehelper_nibble_is_low(nibble_index) != 0 ?
            kksdk_codehelper_low_nibble(value) : kksdk_codehelper_high_nibble(value);
}

extern "C" void kksdk_codehelper_write_nibble(unsigned char *data,
        unsigned long long size, unsigned int nibble_index, unsigned int value) {
    const unsigned int byte_index = kksdk_codehelper_nibble_byte_index(nibble_index);
    if (data == nullptr || byte_index >= size) {
        return;
    }
    unsigned char &target = data[byte_index];
    if (kksdk_codehelper_nibble_is_low(nibble_index) != 0) {
        target = static_cast<unsigned char>((target & 0xf0U) |
                kksdk_codehelper_low_nibble(value));
    } else {
        target = static_cast<unsigned char>(kksdk_codehelper_low_nibble(target) |
                (kksdk_codehelper_low_nibble(value) << 4U));
    }
}

extern "C" unsigned int kksdk_codehelper_sum_byte_range(const unsigned char *data,
        unsigned long long size, unsigned int start, unsigned int end, unsigned int seed) {
    if (data == nullptr) {
        return seed;
    }
    unsigned int sum = seed;
    for (unsigned int index = start; index < end && index < size; ++index) {
        sum += data[index];
    }
    return sum;
}

extern "C" unsigned int kksdk_codehelper_sum_nibble_range(const unsigned char *data,
        unsigned long long size, unsigned int start, unsigned int end, unsigned int seed) {
    if (data == nullptr) {
        return seed;
    }
    unsigned int sum = seed;
    for (unsigned int index = start; index < end && index < size; ++index) {
        sum += kksdk_codehelper_low_nibble(data[index]) +
                kksdk_codehelper_high_nibble(data[index]);
    }
    return sum;
}

extern "C" int kksdk_codehelper_parse_config_line(const char *input,
        unsigned long long input_size, kksdk_codehelper_config_line *out_line) {
    if (out_line != nullptr) {
        *out_line = {};
    }
    if (input == nullptr || out_line == nullptr || input_size == 0) {
        return 0;
    }

    const std::string_view line(input, static_cast<std::size_t>(input_size));
    const std::size_t separator = line.find('|');
    if (separator == std::string_view::npos || separator == 0) {
        return 0;
    }

    unsigned int tag = 0;
    for (char ch : line.substr(0, separator)) {
        if (ch < '0' || ch > '9') {
            return 0;
        }
        tag = tag * 10U + static_cast<unsigned int>(ch - '0');
    }

    out_line->tag = tag;
    out_line->payload = input + separator + 1U;
    out_line->payload_size = static_cast<unsigned long long>(line.size() - separator - 1U);
    return 1;
}

extern "C" unsigned long long kksdk_codehelper_split_payload(const char *input,
        unsigned long long input_size, char delimiter, kksdk_codehelper_config_line *out_parts,
        unsigned long long out_capacity) {
    if (input == nullptr || out_parts == nullptr || out_capacity == 0) {
        return 0;
    }

    const std::string_view payload(input, static_cast<std::size_t>(input_size));
    unsigned long long count = 0;
    std::size_t start = 0;
    while (start <= payload.size() && count < out_capacity) {
        const std::size_t end = payload.find(delimiter, start);
        const std::size_t part_end = end == std::string_view::npos ? payload.size() : end;
        out_parts[count++] = {
                0,
                input + start,
                static_cast<unsigned long long>(part_end - start),
        };
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1U;
    }
    return count;
}

extern "C" kksdk_codehelper_remote_config *kksdk_codehelper_create_remote_config(
        unsigned int remote_id) {
    auto *config = new kksdk_codehelper_remote_config();
    config->remote_id = remote_id;
    return config;
}

extern "C" void kksdk_codehelper_destroy_remote_config(
        kksdk_codehelper_remote_config *config) {
    delete config;
}

extern "C" unsigned int kksdk_codehelper_add_config_line(
        kksdk_codehelper_remote_config *config, const char *line,
        unsigned long long line_size) {
    if (config == nullptr || line == nullptr) {
        return kksdk_codehelper_result_error();
    }

    kksdk_codehelper_config_line parsed{};
    if (kksdk_codehelper_parse_config_line(line, line_size, &parsed) == 0) {
        return 0;
    }

    std::string payload(parsed.payload, static_cast<std::size_t>(parsed.payload_size));
    if (kksdk_codehelper_config_tag_is_patch(parsed.tag) != 0) {
        config->patch_records.push_back(payload);
    } else if (kksdk_codehelper_config_tag_is_checksum(parsed.tag) != 0) {
        config->checksum_records.push_back(payload);
    } else if (kksdk_codehelper_config_tag_is_key(parsed.tag) != 0) {
        kksdk_codehelper_config_line parts[64]{};
        const unsigned long long part_count = kksdk_codehelper_split_payload(
                parsed.payload, parsed.payload_size, '@', parts, 64);
        for (unsigned long long i = 0; i < part_count; ++i) {
            config->key_records.emplace_back(parts[i].payload,
                    static_cast<std::size_t>(parts[i].payload_size));
        }
    } else {
        config->fixed_records.push_back(payload);
    }
    return 0;
}

extern "C" int kksdk_codehelper_config_tag_is_patch(unsigned int tag) {
    switch (tag) {
    case 0x3e9U:
    case 0x3ebU:
    case 0x3ecU:
    case 0x3edU:
    case 0x3efU:
    case 0x3f0U:
    case 0x3f2U:
    case 0x3f3U:
    case 0x3f4U:
    case 0x3f5U:
    case 0x3f7U:
    case 0x3f8U:
        return 1;
    default:
        return 0;
    }
}

extern "C" int kksdk_codehelper_config_tag_is_checksum(unsigned int tag) {
    return tag == kksdk_codehelper_config_tag_checksum_id() ? 1 : 0;
}

extern "C" int kksdk_codehelper_config_tag_is_key(unsigned int tag) {
    return tag == kksdk_codehelper_config_tag_key_id() ? 1 : 0;
}

extern "C" unsigned int kksdk_codehelper_config_tag_checksum_id() {
    return 0x3f1U;
}

extern "C" unsigned int kksdk_codehelper_config_tag_key_id() {
    return 0x3f9U;
}

extern "C" unsigned long long kksdk_codehelper_count_config_records(
        const kksdk_codehelper_remote_config *config, unsigned int group) {
    if (config == nullptr) {
        return 0;
    }

    if (kksdk_codehelper_config_group_is_fixed(group) != 0) {
        return config->fixed_records.size();
    }
    if (kksdk_codehelper_config_group_is_patch(group) != 0) {
        return config->patch_records.size();
    }
    if (kksdk_codehelper_config_group_is_checksum(group) != 0) {
        return config->checksum_records.size();
    }
    if (kksdk_codehelper_config_group_is_key(group) != 0) {
        return config->key_records.size();
    }
    return 0;
}

extern "C" int kksdk_codehelper_config_group_is_fixed(unsigned int group) {
    return group == kksdk_codehelper_config_group_fixed_id() ? 1 : 0;
}

extern "C" int kksdk_codehelper_config_group_is_patch(unsigned int group) {
    return group == kksdk_codehelper_config_group_patch_id() ? 1 : 0;
}

extern "C" int kksdk_codehelper_config_group_is_checksum(unsigned int group) {
    return group == kksdk_codehelper_config_group_checksum_id() ? 1 : 0;
}

extern "C" int kksdk_codehelper_config_group_is_key(unsigned int group) {
    return group == kksdk_codehelper_config_group_key_id() ? 1 : 0;
}

extern "C" unsigned int kksdk_codehelper_config_group_fixed_id() {
    return 0U;
}

extern "C" unsigned int kksdk_codehelper_config_group_patch_id() {
    return 1U;
}

extern "C" unsigned int kksdk_codehelper_config_group_checksum_id() {
    return 2U;
}

extern "C" unsigned int kksdk_codehelper_config_group_key_id() {
    return 3U;
}

extern "C" unsigned long long kksdk_codehelper_bounded_substring(const char *input,
        unsigned long long input_size, unsigned long long offset, unsigned long long max_count,
        char *out, unsigned long long out_capacity) {
    if (input == nullptr || offset > input_size) {
        return 0;
    }

    const std::string_view source(input, static_cast<std::size_t>(input_size));
    const std::size_t start = static_cast<std::size_t>(offset);
    const std::size_t requested = static_cast<std::size_t>(max_count);
    const std::size_t count = std::min(requested, source.size() - start);
    const std::string_view slice = source.substr(start, count);

    if (out != nullptr && out_capacity != 0) {
        const std::size_t copy_count =
                std::min(slice.size(), static_cast<std::size_t>(out_capacity - 1));
        if (copy_count != 0) {
            std::memcpy(out, slice.data(), copy_count);
        }
        out[copy_count] = '\0';
    }
    return static_cast<unsigned long long>(slice.size());
}

extern "C" long long kksdk_codehelper_find_substring(const char *haystack,
        unsigned long long haystack_size, const char *needle, unsigned long long needle_size,
        unsigned long long start_offset) {
    if (haystack == nullptr || needle == nullptr || start_offset > haystack_size) {
        return kksdk_codehelper_invalid_index_result();
    }

    const std::string_view source(haystack, static_cast<std::size_t>(haystack_size));
    const std::string_view target(needle, static_cast<std::size_t>(needle_size));
    const std::size_t found = source.find(target, static_cast<std::size_t>(start_offset));
    if (found == std::string_view::npos) {
        return kksdk_codehelper_invalid_index_result();
    }
    return static_cast<long long>(found);
}

extern "C" long long kksdk_codehelper_invalid_index_result() {
    return -1;
}

extern "C" int kksdk_codehelper_byte_from_hex_nibbles(int high, int low) {
    return high < 0 || low < 0 ? -1 : (high << 4) | low;
}

extern "C" long long kksdk_codehelper_parse_hex_record(const char *input,
        unsigned long long input_size, unsigned long long offset, unsigned char *out,
        unsigned long long out_capacity, unsigned long long *decoded_size) {
    if (decoded_size != nullptr) {
        *decoded_size = 0;
    }
    if (input == nullptr || offset + 2 > input_size) {
        return kksdk_codehelper_invalid_index_result();
    }

    const int byte_count_header = kksdk_codehelper_byte_from_hex_nibbles(
            hex_value(input[offset]), hex_value(input[offset + 1]));
    if (byte_count_header < 0) {
        return kksdk_codehelper_invalid_index_result();
    }

    const unsigned long long byte_count = static_cast<unsigned long long>(byte_count_header);
    const unsigned long long required_chars =
            kksdk_codehelper_hex_record_payload_chars(byte_count);
    if (offset + 2ULL + required_chars > input_size ||
            (out != nullptr && byte_count > out_capacity)) {
        return kksdk_codehelper_invalid_index_result();
    }

    for (unsigned long long i = 0; i < byte_count; ++i) {
        const unsigned long long hex_offset = offset + 2ULL + i * 2ULL;
        const int byte_value = kksdk_codehelper_byte_from_hex_nibbles(
                hex_value(input[hex_offset]), hex_value(input[hex_offset + 1ULL]));
        if (byte_value < 0) {
            return kksdk_codehelper_invalid_index_result();
        }
        if (out != nullptr) {
            out[i] = static_cast<unsigned char>(byte_value);
        }
    }

    if (decoded_size != nullptr) {
        *decoded_size = byte_count;
    }
    return static_cast<long long>(kksdk_codehelper_hex_record_total_chars(byte_count));
}

extern "C" unsigned long long kksdk_codehelper_hex_record_payload_chars(
        unsigned long long byte_count) {
    return byte_count * 2ULL;
}

extern "C" unsigned long long kksdk_codehelper_hex_record_total_chars(
        unsigned long long byte_count) {
    return 2ULL + kksdk_codehelper_hex_record_payload_chars(byte_count);
}

extern "C" void kksdk_codehelper_noop_callback(void) {
}

extern "C" unsigned int kksdk_codehelper_zero_status(void) {
    return 0;
}

extern "C" unsigned int kksdk_codehelper_eof_status(void) {
    return 0xffffffffU;
}

extern "C" int kksdk_codehelper_stream_error(void) {
    return -1;
}

extern "C" unsigned int kksdk_codehelper_putback_any_value(void) {
    return kksdk_codehelper_eof_status();
}

extern "C" unsigned int kksdk_codehelper_stream_copy_chunk_limit(void) {
    return 0x7fffffffU;
}

extern "C" unsigned long long kksdk_codehelper_stream_read(
        kksdk_codehelper_input_stream *input, unsigned char *out,
        unsigned long long requested) {
    if (input == nullptr || out == nullptr || requested == 0) {
        return 0;
    }

    unsigned long long total = 0;
    while (total < requested) {
        if (input->cursor < input->end) {
            const std::size_t available =
                    static_cast<std::size_t>(input->end - input->cursor);
            std::size_t chunk = static_cast<std::size_t>(requested - total);
            chunk = std::min(chunk, available);
            chunk = std::min<std::size_t>(
                    chunk, kksdk_codehelper_stream_copy_chunk_limit());
            if (chunk != 0) {
                std::memcpy(out + total, input->cursor, chunk);
            }
            input->cursor += chunk;
            total += chunk;
            continue;
        }

        if (input->read_byte == nullptr) {
            break;
        }
        const int value = input->read_byte(input->stream);
        if (value == kksdk_codehelper_stream_error()) {
            break;
        }
        out[total++] = static_cast<unsigned char>(value);
    }
    return total;
}

extern "C" int kksdk_codehelper_stream_get_byte(kksdk_codehelper_input_stream *input) {
    if (input == nullptr) {
        return kksdk_codehelper_stream_error();
    }
    if (input->cursor >= input->end) {
        if (input->read_byte == nullptr) {
            return kksdk_codehelper_stream_error();
        }
        return input->read_byte(input->stream);
    }
    return *input->cursor++;
}

extern "C" int kksdk_codehelper_stream_peek_byte(const kksdk_codehelper_input_stream *input) {
    if (input == nullptr || input->cursor >= input->end) {
        return kksdk_codehelper_stream_error();
    }
    return *input->cursor;
}

extern "C" int kksdk_codehelper_stream_putback_byte(kksdk_codehelper_putback_stream *input,
        unsigned int value) {
    if (input == nullptr || input->cursor <= input->begin) {
        return kksdk_codehelper_stream_error();
    }

    unsigned char *previous = input->cursor - 1;
    if (value == kksdk_codehelper_putback_any_value()) {
        input->cursor = previous;
        return 0;
    }

    const unsigned char byte = static_cast<unsigned char>(value);
    if (input->allow_mismatch || *previous == byte) {
        input->cursor = previous;
        if (input->high_water < input->cursor) {
            input->high_water = input->cursor;
        }
        *previous = byte;
        return byte;
    }
    return kksdk_codehelper_stream_error();
}

extern "C" unsigned long long kksdk_codehelper_stream_write(
        kksdk_codehelper_output_stream *output, const unsigned char *data,
        unsigned long long requested) {
    if (output == nullptr || data == nullptr || requested == 0) {
        return 0;
    }

    unsigned long long total = 0;
    while (total < requested) {
        if (output->cursor < output->end) {
            const std::size_t available =
                    static_cast<std::size_t>(output->end - output->cursor);
            std::size_t chunk = static_cast<std::size_t>(requested - total);
            chunk = std::min(chunk, available);
            if (chunk != 0) {
                std::memcpy(output->cursor, data + total, chunk);
            }
            output->cursor += chunk;
            total += chunk;
            continue;
        }

        if (output->write_byte == nullptr) {
            break;
        }
        if (output->write_byte(output->stream, data[total]) ==
                kksdk_codehelper_stream_error()) {
            break;
        }
        ++total;
    }
    return total;
}

extern "C" int kksdk_codehelper_stream_put_byte(kksdk_codehelper_output_stream *output,
        unsigned int value) {
    if (output == nullptr) {
        return kksdk_codehelper_stream_error();
    }
    if (output->cursor < output->end) {
        *output->cursor++ = static_cast<unsigned char>(value);
        return static_cast<int>(kksdk_codehelper_byte_value(value));
    }
    if (output->write_byte == nullptr) {
        return kksdk_codehelper_stream_error();
    }
    return output->write_byte(output->stream, value);
}

extern "C" unsigned long long kksdk_codehelper_append_fill(char *buffer,
        unsigned long long size, unsigned long long capacity, unsigned long long count,
        unsigned int value) {
    if (buffer == nullptr || capacity == 0 || size >= capacity || count == 0) {
        return size;
    }

    const unsigned long long writable = std::min(count, capacity - size - 1ULL);
    std::memset(buffer + size, static_cast<int>(kksdk_codehelper_byte_value(value)),
            static_cast<std::size_t>(writable));
    const unsigned long long new_size = size + writable;
    buffer[new_size] = '\0';
    return new_size;
}

extern "C" unsigned long long kksdk_codehelper_append_bytes(char *buffer,
        unsigned long long size, unsigned long long capacity, const char *data,
        unsigned long long count) {
    if (buffer == nullptr || data == nullptr || capacity == 0 || size >= capacity ||
            count == 0) {
        return size;
    }

    const unsigned long long writable = std::min(count, capacity - size - 1ULL);
    std::memcpy(buffer + size, data, static_cast<std::size_t>(writable));
    const unsigned long long new_size = size + writable;
    buffer[new_size] = '\0';
    return new_size;
}

extern "C" unsigned long long kksdk_codehelper_string_growth_capacity(
        unsigned long long current_capacity, unsigned long long extra_needed) {
    if (extra_needed > kksdk_codehelper_string_max_capacity() - current_capacity) {
        return 0;
    }

    const unsigned long long required_capacity = current_capacity + extra_needed;
    unsigned long long grown_capacity = kksdk_codehelper_string_max_capacity();
    if (current_capacity < kksdk_codehelper_string_double_growth_limit()) {
        grown_capacity = current_capacity << 1;
        if (grown_capacity <= required_capacity) {
            grown_capacity = required_capacity;
        }
        if (grown_capacity < kksdk_codehelper_string_min_capacity()) {
            grown_capacity = kksdk_codehelper_string_min_capacity();
        } else {
            grown_capacity = (grown_capacity + kksdk_codehelper_string_capacity_alignment()) &
                    kksdk_codehelper_string_capacity_alignment_mask();
        }
    }
    return grown_capacity;
}

extern "C" unsigned long long kksdk_codehelper_string_min_capacity() {
    return 0x17ULL;
}

extern "C" unsigned long long kksdk_codehelper_string_capacity_alignment() {
    return 0x10ULL;
}

extern "C" unsigned long long kksdk_codehelper_string_capacity_alignment_mask() {
    return ~0xfULL;
}

extern "C" unsigned long long kksdk_codehelper_string_max_capacity() {
    return 0xffffffffffffffefULL;
}

extern "C" unsigned long long kksdk_codehelper_string_double_growth_limit() {
    return 0x7fffffffffffffe7ULL;
}

extern "C" unsigned long long kksdk_codehelper_rebuild_with_gap(char *out,
        unsigned long long out_capacity, const char *input, unsigned long long input_size,
        unsigned long long prefix_size, unsigned long long remove_count,
        unsigned long long gap_size) {
    if (out == nullptr || input == nullptr || out_capacity == 0 ||
            prefix_size > input_size) {
        return 0;
    }

    unsigned long long suffix_offset = input_size;
    if (remove_count <= input_size - prefix_size) {
        suffix_offset = prefix_size + remove_count;
    }
    const unsigned long long suffix_size = input_size - suffix_offset;
    if (gap_size > out_capacity - 1ULL ||
            prefix_size > out_capacity - 1ULL - gap_size ||
            suffix_size > out_capacity - 1ULL - gap_size - prefix_size) {
        return 0;
    }

    if (prefix_size != 0) {
        std::memcpy(out, input, static_cast<std::size_t>(prefix_size));
    }
    if (gap_size != 0) {
        std::memset(out + prefix_size, 0, static_cast<std::size_t>(gap_size));
    }
    if (suffix_size != 0) {
        std::memcpy(out + prefix_size + gap_size, input + suffix_offset,
                static_cast<std::size_t>(suffix_size));
    }

    const unsigned long long new_size = prefix_size + gap_size + suffix_size;
    out[new_size] = '\0';
    return new_size;
}

extern "C" unsigned long long kksdk_codehelper_rebuild_with_insert(char *out,
        unsigned long long out_capacity, const char *input, unsigned long long input_size,
        unsigned long long prefix_size, unsigned long long remove_count,
        const char *insert, unsigned long long insert_size) {
    if (insert_size != 0 && insert == nullptr) {
        return 0;
    }

    const unsigned long long new_size = kksdk_codehelper_rebuild_with_gap(out,
            out_capacity, input, input_size, prefix_size, remove_count, insert_size);
    if (new_size == 0 && (input_size != 0 || insert_size != 0)) {
        return 0;
    }
    if (insert_size != 0) {
        std::memcpy(out + prefix_size, insert, static_cast<std::size_t>(insert_size));
    }
    return new_size;
}

extern "C" unsigned long long kksdk_codehelper_replace_bytes(char *buffer,
        unsigned long long size, unsigned long long capacity, unsigned long long offset,
        unsigned long long max_remove, const char *insert,
        unsigned long long insert_size) {
    if (buffer == nullptr || capacity == 0 || offset > size ||
            (insert_size != 0 && insert == nullptr)) {
        return 0;
    }

    const unsigned long long removed =
            kksdk_codehelper_clamped_remove_count(size, offset, max_remove);
    const unsigned long long new_size =
            kksdk_codehelper_replace_size_after(size, removed, insert_size);
    if (new_size == std::numeric_limits<unsigned long long>::max()) {
        return 0;
    }

    if (new_size >= capacity) {
        return 0;
    }

    std::string overlap_copy;
    const char *insert_source = insert;
    if (insert_size != 0 && insert >= buffer && insert < buffer + size) {
        overlap_copy.assign(insert, static_cast<std::size_t>(insert_size));
        insert_source = overlap_copy.data();
    }

    const unsigned long long suffix_offset = offset + removed;
    const unsigned long long suffix_size = size - suffix_offset;
    if (suffix_size != 0 && insert_size != removed) {
        std::memmove(buffer + offset + insert_size, buffer + suffix_offset,
                static_cast<std::size_t>(suffix_size));
    }
    if (insert_size != 0) {
        std::memcpy(buffer + offset, insert_source, static_cast<std::size_t>(insert_size));
    }
    buffer[new_size] = '\0';
    return new_size;
}

extern "C" unsigned long long kksdk_codehelper_clamped_remove_count(
        unsigned long long size, unsigned long long offset, unsigned long long max_remove) {
    if (offset > size) {
        return 0;
    }
    return std::min(max_remove, size - offset);
}

extern "C" unsigned long long kksdk_codehelper_replace_size_after(
        unsigned long long size, unsigned long long removed, unsigned long long insert_size) {
    if (removed > size) {
        return std::numeric_limits<unsigned long long>::max();
    }
    const unsigned long long base_size = size - removed;
    if (insert_size > std::numeric_limits<unsigned long long>::max() - 1ULL - base_size) {
        return std::numeric_limits<unsigned long long>::max();
    }
    return base_size + insert_size;
}

extern "C" unsigned long long kksdk_codehelper_hash_bucket_index(
        unsigned long long bucket_count, unsigned long long key) {
    if (bucket_count == 0) {
        return 0;
    }
    if (kksdk_codehelper_is_power_of_two_bucket_count(bucket_count) != 0) {
        return key & (bucket_count - 1ULL);
    }
    return key % bucket_count;
}

extern "C" int kksdk_codehelper_is_power_of_two_bucket_count(
        unsigned long long bucket_count) {
    return bucket_count != 0 && (bucket_count & (bucket_count - 1ULL)) == 0 ? 1 : 0;
}

extern "C" kksdk_codehelper_hash_node *kksdk_codehelper_find_hash_node(
        kksdk_codehelper_hash_node **buckets, unsigned long long bucket_count,
        unsigned long long key, unsigned int tag) {
    if (buckets == nullptr || bucket_count == 0) {
        return nullptr;
    }

    const unsigned long long target_bucket =
            kksdk_codehelper_hash_bucket_index(bucket_count, key);
    kksdk_codehelper_hash_node *node = buckets[target_bucket];
    while (node != nullptr) {
        if (kksdk_codehelper_hash_node_matches(node, key, tag) != 0) {
            return node;
        }

        const unsigned long long node_bucket =
                kksdk_codehelper_hash_bucket_index(bucket_count, node->key);
        if (node_bucket != target_bucket) {
            break;
        }
        node = node->next;
    }
    return nullptr;
}

extern "C" int kksdk_codehelper_hash_node_matches(
        const kksdk_codehelper_hash_node *node, unsigned long long key, unsigned int tag) {
    return node != nullptr && node->key == key && node->tag == tag ? 1 : 0;
}
