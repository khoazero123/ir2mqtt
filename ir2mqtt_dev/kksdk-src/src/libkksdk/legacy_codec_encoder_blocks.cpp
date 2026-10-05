#include "legacy_codec_encoder_blocks.hpp"

#include "legacy_codec_encoder_parse.hpp"
#include "legacy_codec_encoder_state.hpp"
#include "legacy_codec_range_encoder.hpp"

namespace kksdk {
namespace {

void refresh_len_price_slot(std::uint16_t *len_encoder, std::uint32_t pos_state,
        void *encoder) {
    if (len_encoder == nullptr || encoder == nullptr) {
        return;
    }
    const auto *price_table = reinterpret_cast<const std::int32_t *>(static_cast<std::uint8_t *>(encoder) +
            legacy_oem_encoder::kProbPrices);
    legacy_codec_encoder_refresh_len_price_slot(len_encoder, pos_state, price_table);
}

bool encode_finish_pos_slot(LegacyCodecRangeEncoderStream &stream, std::uint8_t *encoder,
        std::uint32_t pos_slot) {
    std::uint32_t node = 1U;
    auto *probabilities = reinterpret_cast<std::uint16_t *>(encoder +
            legacy_oem_encoder::kPosSlotProbs);
    for (std::int32_t shift = 5; shift >= 0; --shift) {
        const std::uint32_t bit =
                (pos_slot >> static_cast<std::uint32_t>(shift)) & 1U;
        const auto result =
                legacy_codec_range_encoder_encode_bit(stream, probabilities[node], bit);
        if (!result.ok) {
            return false;
        }
        node = (node << 1U) | bit;
    }
    return stream.status == 0;
}

}  // namespace

bool legacy_codec_encoder_encode_finish_pos_slot(void *encoder, std::uint32_t pos_slot) {
    if (encoder == nullptr) {
        return false;
    }
    auto *base = static_cast<std::uint8_t *>(encoder);
    auto &stream = *legacy_codec_range_encoder_from_native_state(encoder);
    return encode_finish_pos_slot(stream, base, pos_slot);
}

bool legacy_codec_encoder_encode_finish_terminal_match(void *encoder,
        std::uint32_t processed_pos_state) {
    if (encoder == nullptr) {
        return false;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    auto &stream = *legacy_codec_range_encoder_from_native_state(encoder);
    if (stream.status != 0) {
        return false;
    }

    const std::uint32_t pos_state =
            *reinterpret_cast<std::uint32_t *>(base + legacy_oem_encoder::kPosStateMask) &
            processed_pos_state;

    auto *lzma_state = reinterpret_cast<std::uint32_t *>(base +
            legacy_oem_encoder::kLzmaStateFinish);
    const std::uint32_t state = *lzma_state;

    {
        auto *probability = reinterpret_cast<std::uint16_t *>(base +
                legacy_oem_encoder::kIsMatchProbs + state * 0x20U + pos_state * 2U);
        if (!legacy_codec_range_encoder_encode_bit(stream, *probability, 1U).ok) {
            return false;
        }
    }

    {
        auto *probability = reinterpret_cast<std::uint16_t *>(base +
                legacy_oem_encoder::kLitStateProbs + state * 2U);
        if (!legacy_codec_range_encoder_encode_bit(stream, *probability, 0U).ok) {
            return false;
        }
    }

    *lzma_state = legacy_oem_encoder::match_state_table()[state];

    const bool decrement_counter =
            *reinterpret_cast<int *>(base + legacy_oem_encoder::kLiteralMode) == 0;
    legacy_codec_encoder_encode_length(reinterpret_cast<std::uint16_t *>(base +
                    legacy_oem_encoder::kLenEncBase),
            stream, 0U, pos_state, decrement_counter, encoder);
    if (stream.status != 0) {
        return false;
    }

    if (!encode_finish_pos_slot(stream, base, 0x3fU)) {
        return false;
    }

    if (!legacy_codec_range_encoder_encode_direct_bits(stream, 26U, 0x03ffffffU).ok) {
        return false;
    }

    if (!legacy_codec_range_encoder_encode_reverse_bit_tree(stream,
                    reinterpret_cast<std::uint16_t *>(base + legacy_oem_encoder::kAlignProbs),
                    4U, 0xfU)
                    .ok) {
        return false;
    }

    return stream.status == 0;
}

void legacy_codec_encoder_encode_length(std::uint16_t *len_encoder,
        LegacyCodecRangeEncoderStream &stream, std::uint32_t length_symbol,
        std::uint32_t pos_state, bool decrement_counter, void *oem_encoder) {
    if (len_encoder == nullptr) {
        return;
    }

    if (length_symbol < 8U) {
        (void)legacy_codec_range_encoder_encode_bit(stream, len_encoder[0], 0U);
        std::uint32_t node = 1U;
        for (std::int32_t shift = 2; shift >= 0; --shift) {
            const std::uint32_t bit =
                    (length_symbol >> static_cast<std::uint32_t>(shift)) & 1U;
            auto &probability = len_encoder[(pos_state << 3U) + node + 2U];
            (void)legacy_codec_range_encoder_encode_bit(stream, probability, bit);
            node = (node << 1U) | bit;
        }
    } else {
        (void)legacy_codec_range_encoder_encode_bit(stream, len_encoder[0], 1U);
        const std::uint32_t high = length_symbol - 8U;
        const std::uint32_t choice_bit = high < 0x10U ? 0U : 1U;
        (void)legacy_codec_range_encoder_encode_bit(stream, len_encoder[1], choice_bit);
        if (high < 0x10U) {
            (void)legacy_codec_range_encoder_encode_bit_tree(
                    stream, len_encoder + 0x82U + pos_state * 8U, 8U, high);
        } else {
            // OEM FUN_0014cbe0: tree symbol is param_3 - 0x10, not (param_3 - 8) - 0x10.
            (void)legacy_codec_range_encoder_encode_bit_tree(
                    stream, len_encoder + 0x102U, 0x100U, length_symbol - 0x10U);
        }
    }

    if (decrement_counter) {
        auto *counter = reinterpret_cast<std::uint32_t *>(
                reinterpret_cast<std::uint8_t *>(len_encoder) + 0x2404U +
                pos_state * sizeof(std::uint16_t));
        if (*counter > 0U) {
            *counter -= 1U;
            if (*counter == 0U) {
                refresh_len_price_slot(len_encoder, pos_state, oem_encoder);
            }
        }
    }
}

void legacy_codec_encoder_finish_flush_only(void *encoder) {
    if (encoder == nullptr) {
        return;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    *reinterpret_cast<std::uint32_t *>(base + legacy_oem_encoder::kFinishFlag) = 1U;

    auto &stream = *legacy_codec_range_encoder_from_native_state(encoder);

    if (!legacy_codec_range_encoder_flush_tail(stream)) {
        legacy_codec_range_encoder_finish_status(encoder, stream);
        return;
    }

    legacy_codec_range_encoder_finish_status(encoder, stream);
}

void legacy_codec_encoder_finish(void *encoder, std::uint32_t processed_pos_state) {
    if (encoder == nullptr) {
        return;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    auto &stream = *legacy_codec_range_encoder_from_native_state(encoder);

    if (*reinterpret_cast<int *>(base + legacy_oem_encoder::kFinishMode) != 0) {
        if (!legacy_codec_encoder_encode_finish_terminal_match(encoder, processed_pos_state)) {
            legacy_codec_range_encoder_finish_status(encoder, stream);
            return;
        }
    }

    legacy_codec_encoder_finish_flush_only(encoder);
}

}  // namespace kksdk
