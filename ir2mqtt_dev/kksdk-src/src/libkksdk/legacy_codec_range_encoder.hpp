#pragma once

#include <cstddef>
#include <cstdint>

namespace kksdk {

struct LegacyCodecRangeEncoderStream {
    std::uint32_t range = 0xffffffffU;
    std::uint8_t cache_byte = 0;
    std::uint8_t padding[3]{};
    std::uint64_t low = 0;
    std::int64_t cache_size = 0;
    std::uint8_t *out_ptr = nullptr;
    std::uint8_t *out_end = nullptr;
    std::uint8_t *out_start = nullptr;
    void *write_callback = nullptr;
    std::uint64_t total_written = 0;
    std::int32_t status = 0;
};

struct LegacyCodecRangeEncodeBitResult {
    bool ok = false;
    std::uint32_t bit = 0;
};

struct LegacyCodecRangeEncodeTreeResult {
    bool ok = false;
    std::uint32_t symbol = 0;
};

LegacyCodecRangeEncoderStream *legacy_codec_range_encoder_from_native_state(void *encoder);
bool legacy_codec_range_encoder_shift_low(LegacyCodecRangeEncoderStream &stream);
LegacyCodecRangeEncodeBitResult legacy_codec_range_encoder_encode_bit(
        LegacyCodecRangeEncoderStream &stream, std::uint16_t &probability,
        std::uint32_t bit);
LegacyCodecRangeEncodeTreeResult legacy_codec_range_encoder_encode_bit_tree(
        LegacyCodecRangeEncoderStream &stream, std::uint16_t *probabilities,
        std::uint32_t symbol_limit, std::uint32_t symbol);
LegacyCodecRangeEncodeTreeResult legacy_codec_range_encoder_encode_reverse_bit_tree(
        LegacyCodecRangeEncoderStream &stream, std::uint16_t *probabilities,
        std::uint32_t bit_count, std::uint32_t symbol);
LegacyCodecRangeEncodeTreeResult legacy_codec_range_encoder_encode_direct_bits(
        LegacyCodecRangeEncoderStream &stream, std::uint32_t bit_count,
        std::uint32_t value);
bool legacy_codec_range_encoder_flush_buffer(LegacyCodecRangeEncoderStream &stream);
bool legacy_codec_range_encoder_flush_tail(LegacyCodecRangeEncoderStream &stream);
void legacy_codec_range_encoder_finish_status(void *encoder,
        LegacyCodecRangeEncoderStream &stream);

// Debug-only bit trace (probe builds); no-op when disabled.
void legacy_codec_range_encoder_trace_reset();
void legacy_codec_range_encoder_trace_set_enabled(bool enabled);
std::size_t legacy_codec_range_encoder_trace_size();
std::uint32_t legacy_codec_range_encoder_trace_bit(std::size_t index);

}  // namespace kksdk
