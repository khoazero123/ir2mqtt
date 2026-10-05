#include "legacy_codec_range_encoder.hpp"

#include "legacy_codec_encoder_state.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace kksdk {
namespace {

using WriteFn = unsigned long (*)(std::intptr_t ctx, void *buffer, unsigned long size);

constexpr std::size_t kRangeBitTraceCapacity = 4096U;
bool g_range_bit_trace_enabled = false;
std::size_t g_range_bit_trace_size = 0U;
std::uint8_t g_range_bit_trace[kRangeBitTraceCapacity]{};

void record_range_bit(std::uint32_t bit) {
    if (!g_range_bit_trace_enabled || g_range_bit_trace_size >= kRangeBitTraceCapacity) {
        return;
    }
    g_range_bit_trace[g_range_bit_trace_size++] = static_cast<std::uint8_t>(bit & 1U);
}

bool flush_output_buffer_impl(LegacyCodecRangeEncoderStream &stream) {
    if (stream.status != 0 || stream.out_ptr == nullptr || stream.out_start == nullptr) {
        return stream.status == 0;
    }

    const std::size_t count =
            static_cast<std::size_t>(stream.out_ptr - stream.out_start);
    if (count == 0) {
        return true;
    }

    if (stream.write_callback == nullptr) {
        stream.status = legacy_oem_encoder::kStatusOutputError;
        return false;
    }

    // write_callback aliases encoder+0x3d320: pointer to LzmaOutputDesc (write fn at +0).
    auto *descriptor = stream.write_callback;
    const auto write = *reinterpret_cast<WriteFn *>(descriptor);
    if (write == nullptr) {
        stream.status = legacy_oem_encoder::kStatusOutputError;
        return false;
    }

    const auto ctx = reinterpret_cast<std::intptr_t>(descriptor);
    const unsigned long written = write(ctx, stream.out_start, count);
    if (written != count) {
        stream.status = legacy_oem_encoder::kStatusOutputError;
        return false;
    }

    stream.total_written += count;
    stream.out_ptr = stream.out_start;
    return true;
}

bool emit_cached_byte(LegacyCodecRangeEncoderStream &stream, int value) {
    if (stream.out_ptr == nullptr) {
        stream.status = legacy_oem_encoder::kStatusOutputError;
        return false;
    }

    if (stream.out_ptr == stream.out_end && !flush_output_buffer_impl(stream)) {
        return false;
    }

    *stream.out_ptr = static_cast<std::uint8_t>(value);
    ++stream.out_ptr;

    if (stream.out_ptr == stream.out_end && stream.status == 0) {
        return flush_output_buffer_impl(stream);
    }
    return true;
}

std::uint64_t oem_low_shift8(std::uint64_t low) {
    const auto low32 = static_cast<std::uint32_t>(low);
    return static_cast<std::uint64_t>(
            static_cast<std::uint32_t>(static_cast<std::int32_t>(low32) << 8));
}

}  // namespace

LegacyCodecRangeEncoderStream *legacy_codec_range_encoder_from_native_state(void *encoder) {
    if (encoder == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<LegacyCodecRangeEncoderStream *>(
            static_cast<std::uint8_t *>(encoder) + legacy_oem_encoder::kRangeStream);
}

bool shift_low_emit_impl(LegacyCodecRangeEncoderStream &stream, bool force) {
    if (!force && (stream.range >> 24U) != 0U) {
        return true;
    }

    stream.range <<= 8U;
    std::uint64_t low = stream.low;
    if ((low >> 32U) == 0U && static_cast<std::uint32_t>(low >> 24U) > 0xfeU) {
        stream.cache_size += 1;
        stream.low = oem_low_shift8(low);
        return stream.status == 0;
    }

    int carry = static_cast<int>(stream.cache_byte) + static_cast<int>(low >> 32U);
    std::int64_t pending = stream.cache_size;
    while (true) {
        if (!emit_cached_byte(stream, carry)) {
            return false;
        }

        pending -= 1;
        if (pending == 0) {
            break;
        }
        carry = static_cast<int>(low >> 32U) - 1;
    }

    stream.cache_byte = static_cast<std::uint8_t>(low >> 24U);
    stream.cache_size = 1;
    stream.low = oem_low_shift8(low);
    return stream.status == 0;
}

bool legacy_codec_range_encoder_shift_low(LegacyCodecRangeEncoderStream &stream) {
    return shift_low_emit_impl(stream, false);
}

LegacyCodecRangeEncodeBitResult legacy_codec_range_encoder_encode_bit(
        LegacyCodecRangeEncoderStream &stream, std::uint16_t &probability,
        std::uint32_t bit) {
    LegacyCodecRangeEncodeBitResult result{};
    if (!legacy_codec_range_encoder_shift_low(stream)) {
        return result;
    }

    const std::uint32_t bound =
            (stream.range >> 11U) * static_cast<std::uint32_t>(probability);
    if (bit == 0U) {
        stream.range = bound;
        probability = static_cast<std::uint16_t>(
                probability + ((0x800U - probability) >> 5U));
        result.bit = 0;
    } else {
        stream.low += bound;
        stream.range -= bound;
        probability = static_cast<std::uint16_t>(probability - (probability >> 5U));
        result.bit = 1;
    }

    if (!legacy_codec_range_encoder_shift_low(stream)) {
        return result;
    }

    result.ok = true;
    record_range_bit(bit);
    return result;
}

LegacyCodecRangeEncodeTreeResult legacy_codec_range_encoder_encode_bit_tree(
        LegacyCodecRangeEncoderStream &stream, std::uint16_t *probabilities,
        std::uint32_t symbol_limit, std::uint32_t symbol) {
    LegacyCodecRangeEncodeTreeResult result{};
    if (probabilities == nullptr || symbol_limit <= 1U || symbol >= symbol_limit) {
        return result;
    }

    std::uint32_t bit_count = 0U;
    for (std::uint32_t probe = 1U; probe < symbol_limit; probe <<= 1U) {
        ++bit_count;
    }

    std::uint32_t node = 1U;
    for (std::int32_t shift = static_cast<std::int32_t>(bit_count) - 1; shift >= 0;
            --shift) {
        const std::uint32_t bit =
                (symbol >> static_cast<std::uint32_t>(shift)) & 1U;
        const auto bit_result =
                legacy_codec_range_encoder_encode_bit(stream, probabilities[node], bit);
        if (!bit_result.ok) {
            return result;
        }
        node = (node << 1U) | bit;
    }

    result.ok = true;
    result.symbol = symbol;
    return result;
}

LegacyCodecRangeEncodeTreeResult legacy_codec_range_encoder_encode_reverse_bit_tree(
        LegacyCodecRangeEncoderStream &stream, std::uint16_t *probabilities,
        std::uint32_t bit_count, std::uint32_t symbol) {
    LegacyCodecRangeEncodeTreeResult result{};
    if (probabilities == nullptr || bit_count == 0U || bit_count > 30U) {
        return result;
    }

    std::uint32_t node = 1U;
    for (std::uint32_t index = 0; index < bit_count; ++index) {
        const std::uint32_t bit = (symbol >> index) & 1U;
        const auto bit_result =
                legacy_codec_range_encoder_encode_bit(stream, probabilities[node], bit);
        if (!bit_result.ok) {
            return result;
        }
        node = (node << 1U) | bit;
    }

    result.ok = true;
    result.symbol = symbol;
    return result;
}

LegacyCodecRangeEncodeTreeResult legacy_codec_range_encoder_encode_direct_bits(
        LegacyCodecRangeEncoderStream &stream, std::uint32_t bit_count,
        std::uint32_t value) {
    LegacyCodecRangeEncodeTreeResult result{};
    if (bit_count > 30U) {
        return result;
    }

    for (std::int32_t index = static_cast<std::int32_t>(bit_count) - 1; index >= 0;
            --index) {
        const std::uint32_t old_range = stream.range;
        stream.range >>= 1U;
        const std::uint32_t mask =
                1U << (static_cast<std::uint32_t>(index) & 0x1fU);
        const std::uint32_t bit = (value & mask) != 0U ? 1U : 0U;
        if (bit != 0U) {
            stream.low += stream.range;
        }
        // OEM FUN_0014c378 direct loop: renorm when pre-shift range >> 25 == 0, forced
        // shift_low (not the guarded encode_bit path).
        if ((old_range >> 25U) == 0U) {
            if (!shift_low_emit_impl(stream, true)) {
                return result;
            }
        }
    }

    result.ok = true;
    result.symbol = value;
    return result;
}

bool legacy_codec_range_encoder_flush_buffer(LegacyCodecRangeEncoderStream &stream) {
    return flush_output_buffer_impl(stream);
}

bool emit_high_carry(LegacyCodecRangeEncoderStream &stream, std::uint64_t low,
        std::uint32_t high_word) {
    std::int32_t value = static_cast<std::int32_t>(stream.cache_byte) +
            static_cast<std::int32_t>(high_word);
    std::int64_t pending = stream.cache_size;
    while (true) {
        if (!emit_cached_byte(stream, value)) {
            return false;
        }

        pending -= 1;
        if (pending == 0) {
            break;
        }
        value = static_cast<std::int32_t>(low >> 32U) - 1;
    }

    stream.cache_byte = static_cast<std::uint8_t>(low >> 24U);
    stream.cache_size = 1;
    return true;
}

bool legacy_codec_range_encoder_flush_tail(LegacyCodecRangeEncoderStream &stream) {
    // OEM FUN_0014c378 flush tail: uVar11 = low>>32 is fixed for all outer emits.
    std::uint64_t low = stream.low;
    const std::uint32_t emit_high = static_cast<std::uint32_t>(low >> 32U);
    std::int32_t round = 0;
    bool initial_goto_lab = (emit_high == 0U);

    for (std::int32_t outer = 0; outer < 6; ++outer) {
        if (!initial_goto_lab) {
            if (!emit_high_carry(stream, low, emit_high)) {
                stream.low = low;
                return false;
            }
            low = stream.low;
            round = 0;
        } else {
            initial_goto_lab = false;
        }

        bool lab_only = (outer == 0 && emit_high == 0U);

        for (;;) {
            if (!lab_only) {
                round += 1;
                low = static_cast<std::uint64_t>(static_cast<std::uint32_t>(
                        static_cast<std::int32_t>(static_cast<std::uint32_t>(low)) << 8));
                stream.low = low;
                stream.cache_size = 1;

                if (round == 5) {
                    stream.low = low;
                    return legacy_codec_range_encoder_flush_buffer(stream);
                }
            } else {
                lab_only = false;
            }

            if (((low >> 24U) & 0xffU) < 0xffU) {
                break;
            }
            stream.cache_size += 1;
            low = oem_low_shift8(low);
            stream.low = low;
        }
    }

    stream.low = low;
    return legacy_codec_range_encoder_flush_buffer(stream);
}

void legacy_codec_range_encoder_finish_status(void *encoder,
        LegacyCodecRangeEncoderStream &stream) {
    if (encoder == nullptr) {
        return;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    auto *status_code =
            reinterpret_cast<std::uint32_t *>(base + legacy_oem_encoder::kStatusCode);
    if (*status_code != legacy_oem_encoder::kStatusOk) {
        return;
    }

    if (stream.status != 0) {
        *status_code = legacy_oem_encoder::kStatusOutputError;
        return;
    }

    if (*reinterpret_cast<std::int32_t *>(base + legacy_oem_encoder::kExtraInputFlag) != 0) {
        *status_code = legacy_oem_encoder::kStatusInputEof;
    }
}

void legacy_codec_range_encoder_trace_reset() {
    g_range_bit_trace_size = 0U;
}

void legacy_codec_range_encoder_trace_set_enabled(bool enabled) {
    g_range_bit_trace_enabled = enabled;
    if (!enabled) {
        g_range_bit_trace_size = 0U;
    }
}

std::size_t legacy_codec_range_encoder_trace_size() {
    return g_range_bit_trace_size;
}

std::uint32_t legacy_codec_range_encoder_trace_bit(std::size_t index) {
    if (index >= g_range_bit_trace_size) {
        return 0U;
    }
    return static_cast<std::uint32_t>(g_range_bit_trace[index]);
}

}  // namespace kksdk
