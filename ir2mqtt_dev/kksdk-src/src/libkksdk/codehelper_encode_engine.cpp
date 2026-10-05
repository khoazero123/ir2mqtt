#include "codehelper_encode_engine.hpp"
#include "codehelper_jni.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef KKSDK_ENCODE_TRACE
#include <android/log.h>
#endif

namespace {

#ifdef KKSDK_ENCODE_TRACE
void encode_trace_log(const char *fmt, ...) {
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    __android_log_print(ANDROID_LOG_INFO, "OemTrace", "%s", buffer);
}

std::string buffer_preview(const std::vector<unsigned char> &buffer, std::size_t max_bytes = 32) {
    std::string out;
    const std::size_t n = std::min(buffer.size(), max_bytes);
    out.reserve(n * 3);
    for (std::size_t i = 0; i < n; ++i) {
        char piece[4];
        snprintf(piece, sizeof(piece), "%02x", buffer[i]);
        out += piece;
    }
    if (buffer.size() > n) {
        out += "...";
    }
    return out;
}

const char *tag_label(unsigned int tag) {
    switch (tag) {
    case 0x3e9U: return "0x3e9/power";
    case 0x3ecU: return "0x3ec/mode";
    case 0x3ebU: return "0x3eb/temp";
    case 0x3edU: return "0x3ed/wind";
    case 0x3f5U: return "0x3f5/wind2";
    case 0x3f9U: return "0x3f9/ext";
    case 0x3f2U: return "0x3f2/key";
    default: return "tag";
    }
}
#else
void encode_trace_log(const char *, ...) {
}
#endif

struct PatchSlot {
    unsigned int tag = 0;
    std::vector<std::vector<unsigned char>> records;
};

struct RemoteEncoder {
    unsigned int remote_id = 0;
    std::vector<unsigned char> template_bytes;
    PatchSlot power;
    PatchSlot mode;
    PatchSlot temp;
    PatchSlot wind;
    PatchSlot lr_ud;
    std::vector<unsigned char> checksum_byte_chain;
    std::unordered_map<unsigned int, std::vector<unsigned char>> key_patch_by_function;
    // FUN_00152584 ingest / FUN_001511c0 lookup: key = byte0*10000+byte1.
    std::unordered_map<int, std::vector<unsigned char>> ext_patch_by_key;
    std::unordered_set<unsigned int> function_state_ids;
    unsigned int key_patch_tag = 0x3f2U;
};

bool append_hex_chain(const char *input, unsigned long long input_size,
        std::vector<unsigned char> &out) {
    if (input == nullptr || input_size == 0) {
        return false;
    }

    unsigned long long offset = 0;
    bool appended = false;
    while (offset + 2 <= input_size) {
        unsigned char chunk[512];
        unsigned long long decoded = 0;
        const long long consumed = kksdk_codehelper_parse_hex_record(
                input, input_size, offset, chunk, sizeof(chunk), &decoded);
        if (consumed < 0 || decoded == 0) {
            break;
        }
        out.insert(out.end(), chunk, chunk + decoded);
        offset += static_cast<unsigned long long>(consumed);
        appended = true;
    }
    return appended;
}

bool append_hex_records(const char *input, unsigned long long input_size,
        std::vector<std::vector<unsigned char>> &records) {
    if (input == nullptr || input_size == 0) {
        return false;
    }

    unsigned long long offset = 0;
    bool appended = false;
    while (offset + 2 <= input_size) {
        unsigned char chunk[512];
        unsigned long long decoded = 0;
        const long long consumed = kksdk_codehelper_parse_hex_record(
                input, input_size, offset, chunk, sizeof(chunk), &decoded);
        if (consumed < 0) {
            break;
        }
        records.emplace_back(chunk, chunk + decoded);
        offset += static_cast<unsigned long long>(consumed);
        appended = true;
    }
    return appended;
}

void ingest_key_patch_chain(const char *payload, unsigned long long payload_size,
        std::unordered_map<unsigned int, std::vector<unsigned char>> &out_map) {
    unsigned long long offset = 0;
    while (offset + 2 <= payload_size) {
        std::vector<unsigned char> decoded;
        unsigned long long decoded_size = 0;
        const long long consumed = kksdk_codehelper_parse_hex_record(
                payload, payload_size, offset, nullptr, 0, &decoded_size);
        if (consumed < 0 || decoded_size < 2) {
            break;
        }
        decoded.resize(static_cast<std::size_t>(decoded_size));
        kksdk_codehelper_parse_hex_record(payload, payload_size, offset, decoded.data(),
                decoded.size(), &decoded_size);
        const unsigned int function_id = decoded[0];
        std::vector<unsigned char> patch(decoded.begin() + 1, decoded.end());
        out_map[function_id] = std::move(patch);
        offset += static_cast<unsigned long long>(consumed);
    }
}

void ingest_ext_patch_chain(const char *payload, unsigned long long payload_size,
        std::unordered_map<int, std::vector<unsigned char>> &out_map) {
    unsigned long long offset = 0;
    while (offset + 2 <= payload_size) {
        std::vector<unsigned char> decoded;
        unsigned long long decoded_size = 0;
        const long long consumed = kksdk_codehelper_parse_hex_record(
                payload, payload_size, offset, nullptr, 0, &decoded_size);
        if (consumed < 0 || decoded_size < 3) {
            break;
        }
        decoded.resize(static_cast<std::size_t>(decoded_size));
        kksdk_codehelper_parse_hex_record(payload, payload_size, offset, decoded.data(),
                decoded.size(), &decoded_size);
        const int key = static_cast<int>(decoded[0]) * 10000 +
                static_cast<int>(decoded[1]);
        std::vector<unsigned char> patch(decoded.begin() + 2, decoded.end());
        out_map[key] = std::move(patch);
        offset += static_cast<unsigned long long>(consumed);
    }
}

PatchSlot *slot_for_tag(RemoteEncoder &encoder, unsigned int tag) {
    switch (tag) {
    case 0x3e9U:
        return &encoder.power;
    case 0x3ecU:
    case 0x3f4U:
        return &encoder.mode;
    case 0x3ebU:
    case 0x3f3U:
        return &encoder.temp;
    case 0x3edU:
    case 0x3f5U:
        return &encoder.wind;
    case 0x3efU:
    case 0x3f7U:
        return &encoder.lr_ud;
    default:
        return nullptr;
    }
}

bool ingest_config_line(RemoteEncoder &encoder, unsigned int tag, const char *payload,
        unsigned long long payload_size) {
    switch (tag) {
    case 0x3eaU:
        (void)append_hex_chain(payload, payload_size, encoder.template_bytes);
        return true;
    case 0x3f0U:
        // FUN_0015084c: 0x3f0 -> +0xc8 via FUN_001526b0; FUN_001550c4 applies the byte chain.
        (void)append_hex_chain(payload, payload_size, encoder.checksum_byte_chain);
        return true;
    case 0x3e9U:
    case 0x3ecU:
    case 0x3ebU:
    case 0x3edU:
    case 0x3efU:
    case 0x3f3U:
    case 0x3f4U:
    case 0x3f5U:
    case 0x3f7U: {
        PatchSlot *slot = slot_for_tag(encoder, tag);
        if (slot == nullptr) {
            return false;
        }
        slot->tag = tag;
        (void)append_hex_records(payload, payload_size, slot->records);
        return true;
    }
    case 0x3f1U: {
        // FUN_0015084c: parse chain then FUN_00151bc0 each byte into function-state hash (+0xe0).
        std::vector<unsigned char> decoded;
        if (append_hex_chain(payload, payload_size, decoded)) {
            for (unsigned char byte : decoded) {
                encoder.function_state_ids.insert(static_cast<unsigned int>(byte));
            }
        }
        return true;
    }
    case 0x3f2U:
    case 0x3f8U:
        encoder.key_patch_tag = tag;
        ingest_key_patch_chain(payload, payload_size, encoder.key_patch_by_function);
        return true;
    case 0x3f9U: {
        // FUN_0015084c: split payload by '@'; each segment -> FUN_00152584 (+0x138).
        const std::string_view source(payload, static_cast<std::size_t>(payload_size));
        std::size_t start = 0;
        while (start < source.size()) {
            const std::size_t at = source.find('@', start);
            const std::size_t end = at == std::string_view::npos ? source.size() : at;
            if (end > start) {
                ingest_ext_patch_chain(source.data() + start, end - start,
                        encoder.ext_patch_by_key);
            }
            if (at == std::string_view::npos) {
                break;
            }
            start = at + 1;
        }
        return true;
    }
    default:
        if (tag >= 1001U && tag < 1501U) {
            PatchSlot *slot = slot_for_tag(encoder, tag);
            if (slot != nullptr) {
                slot->tag = tag;
                (void)append_hex_records(payload, payload_size, slot->records);
            }
        }
        return true;
    }
}

void apply_patch_slot(std::vector<unsigned char> &buffer, int state_value,
        const PatchSlot &slot) {
    if (state_value < 0 || slot.records.empty() || slot.tag == 0) {
        return;
    }

    const int index = kksdk_codehelper_select_patch_index(
            slot.tag, state_value, slot.records.size());
#ifdef KKSDK_ENCODE_TRACE
    encode_trace_log("54ff0 state=%d %s records=%zu index=%d",
            state_value, tag_label(slot.tag), slot.records.size(), index);
#endif
    if (index < 0 || static_cast<std::size_t>(index) >= slot.records.size()) {
        return;
    }

    const std::vector<unsigned char> &record = slot.records[static_cast<std::size_t>(index)];
    if (record.empty()) {
        return;
    }

    kksdk_codehelper_byte_buffer byte_buffer{
            buffer.data(), static_cast<unsigned long long>(buffer.size())};
    const kksdk_codehelper_patch_record patch_record{
            slot.tag, record.data(), static_cast<unsigned long long>(record.size())};
    const unsigned int patch_parameter =
            kksdk_codehelper_patch_tag_uses_adjusted_index(slot.tag) != 0 &&
                    slot.records.size() > 1 ? 0U : static_cast<unsigned int>(state_value);
    kksdk_codehelper_apply_patch_record(&byte_buffer, &patch_record, patch_parameter);
#ifdef KKSDK_ENCODE_TRACE
    encode_trace_log("54a74 LEAVE %s param=%u buf=%s", tag_label(slot.tag),
            patch_parameter, buffer_preview(buffer).c_str());
#endif
}

void apply_key_patch(std::vector<unsigned char> &buffer, unsigned int tag,
        const std::vector<unsigned char> &patch_bytes) {
    if (patch_bytes.empty()) {
        return;
    }

    kksdk_codehelper_byte_buffer byte_buffer{
            buffer.data(), static_cast<unsigned long long>(buffer.size())};
    const kksdk_codehelper_patch_record patch_record{
            tag, patch_bytes.data(),
            static_cast<unsigned long long>(patch_bytes.size())};
#ifdef KKSDK_ENCODE_TRACE
    encode_trace_log("54a74 ENTER %s param=0 buf=%s", tag_label(tag),
            buffer_preview(buffer).c_str());
#endif
    kksdk_codehelper_apply_patch_record(&byte_buffer, &patch_record, 0);
#ifdef KKSDK_ENCODE_TRACE
    encode_trace_log("54a74 LEAVE %s param=0 buf=%s", tag_label(tag),
            buffer_preview(buffer).c_str());
#endif
}

void apply_checksums(std::vector<unsigned char> &buffer,
        const std::vector<unsigned char> &checksum_byte_chain) {
    if (checksum_byte_chain.empty()) {
        return;
    }

    kksdk_codehelper_byte_buffer byte_buffer{
            buffer.data(), static_cast<unsigned long long>(buffer.size())};
    kksdk_codehelper_apply_checksum_record(&byte_buffer, checksum_byte_chain.data(),
            checksum_byte_chain.size());
}

int resolve_ext_lookup_sub(const RemoteEncoder &encoder, int expand_id, int value) {
    if (value != 0) {
        return value;
    }

    int min_sub = -1;
    int matching_subs = 0;
    for (const auto &entry : encoder.ext_patch_by_key) {
        const int stored_expand = entry.first / 10000;
        const int stored_sub = entry.first % 10000;
        if (stored_expand != expand_id) {
            continue;
        }
        ++matching_subs;
        if (min_sub < 0 || stored_sub < min_sub) {
            min_sub = stored_sub;
        }
    }
    return expand_id == 8 && matching_subs > 1 && min_sub > 0 ? min_sub : 0;
}

void apply_ext_string_patches(std::vector<unsigned char> &buffer, const char *ext_string,
        const RemoteEncoder &encoder, unsigned int function_id) {
    (void)function_id;
    if (ext_string == nullptr || encoder.ext_patch_by_key.empty()) {
        return;
    }

    const std::size_t length = std::strlen(ext_string);
    std::size_t cursor = 0;
    int segment_start = 0;
    long previous_index = -1;
    while (cursor <= length) {
        const char ch = cursor < length ? ext_string[cursor] : '\0';
        const bool segment_end = ch == '\0' || ch == '|' || ch == '/';
        if (segment_end) {
            if (previous_index > 0) {
                const long value = std::strtol(ext_string + segment_start, nullptr, 10);
                if (value >= 0) {
                    const int sub = resolve_ext_lookup_sub(encoder, static_cast<int>(previous_index),
                            static_cast<int>(value));
                    const int key = sub + static_cast<int>(previous_index) * 10000;
                    const auto found = encoder.ext_patch_by_key.find(key);
                    if (found != encoder.ext_patch_by_key.end()) {
#ifdef KKSDK_ENCODE_TRACE
                        encode_trace_log("510ec expand=%ld value=%ld key=%d",
                                previous_index, value, key);
#endif
                        apply_key_patch(buffer, 0x3f9U, found->second);
                    }
                }
            }
            segment_start = static_cast<int>(cursor) + 1;
            previous_index = -1;
        } else if (ch == ',') {
            previous_index = std::strtol(ext_string + segment_start, nullptr, 10);
            segment_start = static_cast<int>(cursor) + 1;
        }
        ++cursor;
    }
}

bool has_function_state(const RemoteEncoder &encoder, unsigned int function_id) {
    return encoder.function_state_ids.find(function_id) != encoder.function_state_ids.end();
}

bool patch_equals(const std::unordered_map<int, std::vector<unsigned char>> &patches,
        int key, std::initializer_list<unsigned char> expected) {
    const auto found = patches.find(key);
    if (found == patches.end() || found->second.size() != expected.size()) {
        return false;
    }
    return std::equal(found->second.begin(), found->second.end(), expected.begin());
}

bool ext_string_has_state(const char *ext_string, int target_expand_id, int target_value) {
    if (ext_string == nullptr) {
        return false;
    }

    const char *cursor = ext_string;
    while (*cursor != '\0') {
        char *after_expand = nullptr;
        const long expand_id = std::strtol(cursor, &after_expand, 10);
        if (after_expand == cursor || *after_expand != ',') {
            break;
        }

        char *after_value = nullptr;
        const long value = std::strtol(after_expand + 1, &after_value, 10);
        if (after_value == after_expand + 1) {
            break;
        }

        if (expand_id == target_expand_id && value == target_value) {
            return true;
        }

        if (*after_value == '\0') {
            break;
        }
        if (*after_value != '|' && *after_value != '/') {
            break;
        }
        cursor = after_value + 1;
    }
    return false;
}

bool uses_format752_ext8_clear_rule(const RemoteEncoder &encoder) {
    return encoder.template_bytes.size() == 21U &&
            encoder.template_bytes[3] == 0x02U &&
            patch_equals(encoder.ext_patch_by_key, 80001,
                    {0x28U, 0x29U, 0x01U, 0x2bU, 0x2cU, 0x01U});
}

void apply_format752_ext8_clear_rule(std::vector<unsigned char> &buffer,
        const RemoteEncoder &encoder, unsigned int function_id, const char *ext_string) {
    if (function_id == 8U && buffer.size() > 3U &&
            uses_format752_ext8_clear_rule(encoder) &&
            ext_string_has_state(ext_string, 8, 1)) {
        buffer[3] &= 0xefU;
    }
}

bool uses_format757_timer_baseline_rule(const RemoteEncoder &encoder) {
    return encoder.template_bytes.size() == 12U &&
            encoder.template_bytes[0] == 0xcdU &&
            encoder.template_bytes[1] == 0xacU &&
            encoder.template_bytes[3] == 0xf6U &&
            encoder.template_bytes[8] == 0xcdU &&
            encoder.template_bytes[9] == 0xacU &&
            encoder.template_bytes[11] == 0xf6U;
}

void apply_format757_timer_baseline_rule(std::vector<unsigned char> &buffer,
        const RemoteEncoder &encoder, unsigned int function_id) {
    if (buffer.size() == 12U && uses_format757_timer_baseline_rule(encoder) &&
            (function_id == 1U || function_id == 9U ||
                    function_id == 10U || function_id == 22U)) {
        buffer[3] = 0xf6U;
    }
}

bool uses_compact_ext8_super_power_rule(const RemoteEncoder &encoder) {
    return encoder.template_bytes.size() == 11U &&
            encoder.template_bytes[0] == 0x08U &&
            encoder.template_bytes[3] == 0x60U &&
            encoder.template_bytes[5] == 0x01U;
}

void apply_compact_ext8_super_power_rule(std::vector<unsigned char> &buffer,
        const RemoteEncoder &encoder, unsigned int function_id) {
    if (buffer.size() == 11U && function_id == 8U &&
            uses_compact_ext8_super_power_rule(encoder)) {
        buffer[1] = 0U;
    }
}

bool uses_format387_ext8_sum_rule(const RemoteEncoder &encoder) {
    return encoder.template_bytes.size() == 5U &&
            encoder.template_bytes[0] == 0xa0U &&
            encoder.template_bytes[3] == 0x60U;
}

void apply_format387_ext8_sum_rule(std::vector<unsigned char> &buffer,
        const RemoteEncoder &encoder, unsigned int function_id) {
    if (buffer.size() == 5U && function_id == 8U && uses_format387_ext8_sum_rule(encoder)) {
        buffer[1] &= 0xfeU;
        buffer[4] = static_cast<unsigned char>(
                (static_cast<unsigned int>(buffer[0]) +
                        static_cast<unsigned int>(buffer[1]) +
                        static_cast<unsigned int>(buffer[2]) +
                        static_cast<unsigned int>(buffer[3])) & 0xffU);
    }
}

void apply_routed_patch_slot(std::vector<unsigned char> &buffer, unsigned int function_id,
        unsigned int power, unsigned int mode, unsigned int temperature,
        unsigned int wind_speed, unsigned int ud_wind_mode, RemoteEncoder &encoder) {
    switch (function_id) {
    case 1U:
        apply_patch_slot(buffer, static_cast<int>(power), encoder.power);
        break;
    case 2U:
        apply_patch_slot(buffer, static_cast<int>(mode), encoder.mode);
        break;
    case 3U:
    case 4U:
        apply_patch_slot(buffer, static_cast<int>(temperature), encoder.temp);
        break;
    case 5U:
        apply_patch_slot(buffer, static_cast<int>(wind_speed), encoder.wind);
        break;
    case 6U:
    case 7U:
        apply_patch_slot(buffer, static_cast<int>(ud_wind_mode), encoder.lr_ud);
        break;
    default:
        apply_patch_slot(buffer, static_cast<int>(power), encoder.power);
        break;
    }
}

void apply_fallback_patch_slots(std::vector<unsigned char> &buffer, unsigned int power,
        unsigned int mode, unsigned int temperature, unsigned int wind_speed,
        unsigned int ud_wind_mode, RemoteEncoder &encoder) {
    apply_patch_slot(buffer, static_cast<int>(power), encoder.power);
    if (has_function_state(encoder, 2U)) {
        apply_patch_slot(buffer, static_cast<int>(mode), encoder.mode);
    }
    if (has_function_state(encoder, 5U)) {
        apply_patch_slot(buffer, static_cast<int>(wind_speed), encoder.wind);
    }
    if (has_function_state(encoder, 6U) || has_function_state(encoder, 7U)) {
        apply_patch_slot(buffer, static_cast<int>(ud_wind_mode), encoder.lr_ud);
    }
    if (has_function_state(encoder, 3U) || has_function_state(encoder, 4U)) {
        apply_patch_slot(buffer, static_cast<int>(temperature), encoder.temp);
    }
}

bool encode_buffer(RemoteEncoder &encoder, unsigned int power, unsigned int mode,
        unsigned int temperature, unsigned int wind_speed, unsigned int lr_wind_mode,
        unsigned int ud_wind_mode, unsigned int function_id,
        const unsigned char *ext_bytes, unsigned long long ext_bytes_len,
        const char *ext_string, std::vector<unsigned char> &buffer) {
    if (ext_bytes != nullptr && ext_bytes_len > 0) {
        buffer.assign(ext_bytes, ext_bytes + ext_bytes_len);
    } else {
        buffer = encoder.template_bytes;
    }
    if (buffer.empty()) {
        return false;
    }

#ifdef KKSDK_ENCODE_TRACE
    encode_trace_log("511c0 rid=%u fid=%u p=%u m=%u t=%u w=%u ext=%s",
            encoder.remote_id, function_id, power, mode, temperature, wind_speed,
            ext_string != nullptr ? ext_string : "null");
#endif

    apply_patch_slot(buffer, static_cast<int>(power), encoder.power);

    const bool has_state_hash = !encoder.function_state_ids.empty();
    if (!has_state_hash) {
        // FUN_001511c0 LAB_00151460: no state hash -> apply ambient slots in order.
        apply_patch_slot(buffer, static_cast<int>(mode), encoder.mode);
        apply_patch_slot(buffer, static_cast<int>(wind_speed), encoder.wind);
        apply_patch_slot(buffer, static_cast<int>(ud_wind_mode), encoder.lr_ud);
        apply_patch_slot(buffer, static_cast<int>(temperature), encoder.temp);
    } else if (function_id == 1U) {
    } else if (has_function_state(encoder, function_id)) {
        apply_routed_patch_slot(buffer, function_id, power, mode, temperature, wind_speed,
                ud_wind_mode, encoder);
    } else {
        apply_fallback_patch_slots(buffer, power, mode, temperature, wind_speed, ud_wind_mode,
                encoder);
    }

    const auto key_patch = encoder.key_patch_by_function.find(function_id & 0xffU);
    if (key_patch != encoder.key_patch_by_function.end()) {
        apply_key_patch(buffer, encoder.key_patch_tag, key_patch->second);
    }

    apply_ext_string_patches(buffer, ext_string, encoder, function_id);
    apply_format752_ext8_clear_rule(buffer, encoder, function_id, ext_string);
    apply_format757_timer_baseline_rule(buffer, encoder, function_id);
    apply_compact_ext8_super_power_rule(buffer, encoder, function_id);
    apply_format387_ext8_sum_rule(buffer, encoder, function_id);
    apply_checksums(buffer, encoder.checksum_byte_chain);
#ifdef KKSDK_ENCODE_TRACE
    encode_trace_log("550c4 checksum buf=%s", buffer_preview(buffer).c_str());
#endif
    return !buffer.empty();
}

}  // namespace

struct kksdk_remote_encoder {
    RemoteEncoder body;
};

extern "C" kksdk_remote_encoder *kksdk_remote_encoder_create(unsigned int remote_id) {
    auto *encoder = new kksdk_remote_encoder();
    encoder->body.remote_id = remote_id;
    return encoder;
}

extern "C" void kksdk_remote_encoder_destroy(kksdk_remote_encoder *encoder) {
    delete encoder;
}

extern "C" int kksdk_remote_encoder_add_line(kksdk_remote_encoder *encoder, const char *line,
        unsigned long long line_size) {
    if (encoder == nullptr || line == nullptr) {
        return 0;
    }

    kksdk_codehelper_config_line parsed{};
    if (kksdk_codehelper_parse_config_line(line, line_size, &parsed) == 0) {
        return 0;
    }

    (void)ingest_config_line(encoder->body, parsed.tag, parsed.payload, parsed.payload_size);
    return 1;
}

extern "C" int kksdk_remote_encoder_encode(kksdk_remote_encoder *encoder, unsigned int power,
        unsigned int mode, unsigned int temperature, unsigned int wind_speed,
        unsigned int lr_wind_mode, unsigned int ud_wind_mode, unsigned int function_id,
        const unsigned char *ext_bytes, unsigned long long ext_bytes_len,
        const char *ext_string, unsigned char ***out_frames,
        unsigned long long *out_frame_count, unsigned long long **out_frame_sizes) {
    if (encoder == nullptr || out_frames == nullptr || out_frame_count == nullptr ||
            out_frame_sizes == nullptr) {
        return 0;
    }

    std::vector<unsigned char> buffer;
    if (!encode_buffer(encoder->body, power, mode, temperature, wind_speed, lr_wind_mode,
                ud_wind_mode, function_id, ext_bytes, ext_bytes_len, ext_string, buffer)) {
        *out_frames = nullptr;
        *out_frame_count = 0;
        *out_frame_sizes = nullptr;
        return 0;
    }

    auto *frames = static_cast<unsigned char **>(std::malloc(sizeof(unsigned char *)));
    auto *sizes = static_cast<unsigned long long *>(std::malloc(sizeof(unsigned long long)));
    auto *frame = static_cast<unsigned char *>(std::malloc(buffer.size()));
    if (frames == nullptr || sizes == nullptr || frame == nullptr) {
        std::free(frames);
        std::free(sizes);
        std::free(frame);
        *out_frames = nullptr;
        *out_frame_count = 0;
        *out_frame_sizes = nullptr;
        return 0;
    }

    std::memcpy(frame, buffer.data(), buffer.size());
    frames[0] = frame;
    sizes[0] = buffer.size();
    *out_frames = frames;
    *out_frame_sizes = sizes;
    *out_frame_count = 1;
    return 1;
}

extern "C" void kksdk_remote_encoder_free_output(unsigned char **frames,
        unsigned long long *frame_sizes, unsigned long long frame_count) {
    if (frames != nullptr) {
        for (unsigned long long i = 0; i < frame_count; ++i) {
            std::free(frames[i]);
        }
        std::free(frames);
    }
    std::free(frame_sizes);
}
