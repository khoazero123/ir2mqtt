#include "legacy_codec_encoder_core.hpp"

#include "legacy_codec_encoder_blocks.hpp"
#include "legacy_codec_encoder_context.hpp"
#include "legacy_codec_encoder_parse.hpp"
#include "legacy_codec_range_encoder.hpp"

#include <cstdlib>
#include <cstring>

namespace kksdk {
namespace {

bool stream_out_failed(void *encoder) {
    const auto *base = static_cast<const std::uint8_t *>(encoder);
    return *reinterpret_cast<const std::int32_t *>(base + legacy_oem_encoder::kStreamOutStatus) != 0;
}

void set_status_code(void *encoder, std::uint32_t status) {
    *reinterpret_cast<std::uint32_t *>(static_cast<std::uint8_t *>(encoder) +
            legacy_oem_encoder::kStatusCode) = status;
}

}  // namespace

bool legacy_codec_encoder_preamble(void *encoder, int *early_status) {
    if (encoder == nullptr || early_status == nullptr) {
        return true;
    }

    if (legacy_codec_encoder_status_code(encoder) != legacy_oem_encoder::kStatusOk ||
            legacy_codec_encoder_finish_flag(encoder) != 0U) {
        *early_status = static_cast<int>(legacy_codec_encoder_status_code(encoder));
        return true;
    }

    if (stream_out_failed(encoder)) {
        set_status_code(encoder, legacy_oem_encoder::kStatusOutputError);
        *early_status = static_cast<int>(legacy_oem_encoder::kStatusOutputError);
        return true;
    }

    const auto *base = static_cast<const std::uint8_t *>(encoder);
    if (*reinterpret_cast<const std::int32_t *>(base + legacy_oem_encoder::kExtraInputFlag) != 0) {
        set_status_code(encoder, legacy_oem_encoder::kStatusInputEof);
        *early_status = static_cast<int>(legacy_oem_encoder::kStatusInputEof);
        return true;
    }

    *early_status = 0;
    return false;
}

bool legacy_codec_encoder_encode_stream_entry_literal(void *encoder) {
    if (encoder == nullptr || legacy_codec_encoder_block_bytes(encoder) != 0U) {
        return false;
    }

    if (legacy_codec_encoder_uses_lifted_matchfinder(encoder)) {
        if (!legacy_codec_encoder_run_matchfinder_init_if_needed(encoder)) {
            return false;
        }
    }

    const auto *base = static_cast<const std::uint8_t *>(encoder);
    if (*reinterpret_cast<const std::int32_t *>(base + legacy_oem_encoder::kStreamInitFlag) != 0) {
        return false;
    }

    if (!legacy_codec_matchfinder_warmup(encoder)) {
        return false;
    }

    auto *mutable_base = static_cast<std::uint8_t *>(encoder);
    auto *processed = reinterpret_cast<std::int32_t *>(mutable_base +
            legacy_oem_encoder::kProcessedInBlock);
    *processed += 1;

    LegacyCodecMatchFinderAccess match_finder{encoder};
    const std::uint8_t *literal_ptr = match_finder.current_literal_ptr();
    if (literal_ptr == nullptr) {
        *processed -= 1;
        return false;
    }

    auto *lzma_state = reinterpret_cast<std::uint32_t *>(mutable_base +
            legacy_oem_encoder::kLzmaState);
    const std::uint32_t state = *lzma_state;
    const std::uint32_t pos_state = legacy_codec_encoder_pos_state(encoder);
    if (!legacy_codec_encoder_encode_is_match(encoder, pos_state, state, false) ||
            !legacy_codec_encoder_encode_simple_literal_root(encoder, literal_ptr[0])) {
        *processed -= 1;
        return false;
    }
    *lzma_state = legacy_oem_encoder::literal_state_table()[state];

    legacy_codec_encoder_after_symbol_ex(encoder, 1U, kEncoderTagLiteral, true, false);
    match_finder.skip(1);

    return legacy_codec_range_encoder_from_native_state(encoder)->status == 0;
}

bool legacy_codec_encoder_encode_fast_stream_entry_after_warmup(void *encoder) {
    if (encoder == nullptr || legacy_codec_encoder_block_bytes(encoder) != 0U) {
        return false;
    }

    auto *mutable_base = static_cast<std::uint8_t *>(encoder);
    if (*reinterpret_cast<const std::int32_t *>(mutable_base + legacy_oem_encoder::kLiteralMode) ==
            0) {
        return false;
    }

    LegacyCodecMatchFinderAccess match_finder{encoder};
    const std::uint8_t *literal_ptr = match_finder.current_literal_ptr();
    if (literal_ptr == nullptr) {
        return false;
    }

    auto *processed = reinterpret_cast<std::int32_t *>(mutable_base +
            legacy_oem_encoder::kProcessedInBlock);
    *processed += 1;

    const std::uint32_t pos_state = legacy_codec_encoder_fast_pos_state(encoder);
    auto *lzma_state = reinterpret_cast<std::uint32_t *>(mutable_base + legacy_oem_encoder::kLzmaState);
    const std::uint32_t state = *lzma_state;
    if (!legacy_codec_encoder_encode_is_match(encoder, pos_state, state, false)) {
        *processed -= 1;
        return false;
    }
    if (!legacy_codec_encoder_encode_simple_literal_root(encoder, literal_ptr[0])) {
        *processed -= 1;
        return false;
    }
    *lzma_state = legacy_oem_encoder::literal_state_table()[state];
    legacy_codec_encoder_after_symbol_ex(encoder, 1U, kEncoderTagLiteral, true, false);
    match_finder.skip(1);
    return legacy_codec_range_encoder_from_native_state(encoder)->status == 0;
}

bool legacy_codec_encoder_encode_fast_stream_entry_literal(void *encoder) {
    if (encoder == nullptr || legacy_codec_encoder_block_bytes(encoder) != 0U) {
        return false;
    }

    auto *mutable_base = static_cast<std::uint8_t *>(encoder);
    if (*reinterpret_cast<const std::int32_t *>(mutable_base + legacy_oem_encoder::kLiteralMode) ==
            0) {
        return legacy_codec_encoder_encode_stream_entry_literal(encoder);
    }

    if (legacy_codec_encoder_uses_lifted_matchfinder(encoder) &&
            !legacy_codec_encoder_run_matchfinder_init_if_needed(encoder)) {
        return false;
    }

    if (*reinterpret_cast<const std::int32_t *>(mutable_base + legacy_oem_encoder::kStreamInitFlag) !=
            0) {
        return false;
    }

    if (!legacy_codec_matchfinder_warmup(encoder)) {
        return false;
    }

    return legacy_codec_encoder_encode_fast_stream_entry_after_warmup(encoder);
}

bool legacy_codec_encoder_native_open_prepare_matchfinder(void *encoder) {
    if (encoder == nullptr) {
        return false;
    }

    auto *snap = static_cast<std::uint8_t *>(std::malloc(legacy_oem_encoder::kStateSize));
    if (snap == nullptr) {
        return false;
    }
    std::memcpy(snap, encoder, legacy_oem_encoder::kStateSize);

    std::uint32_t match_length = 0U;
    std::uint32_t distance_index = 0U;
    const bool synced = legacy_codec_encoder_sync_matchfinder_when_idle(
            encoder, &match_length, &distance_index);

    std::memcpy(encoder, snap, legacy_oem_encoder::kStateSize);
    std::free(snap);
    return synced;
}

bool legacy_codec_encoder_native_fast_block_zero_open(void *encoder) {
    if (encoder == nullptr || legacy_codec_encoder_block_bytes(encoder) != 0U) {
        return false;
    }

    auto *mutable_base = static_cast<std::uint8_t *>(encoder);
    if (*reinterpret_cast<const std::int32_t *>(mutable_base + legacy_oem_encoder::kLiteralMode) ==
            0) {
        return false;
    }

    if (legacy_codec_encoder_uses_lifted_matchfinder(encoder) &&
            !legacy_codec_encoder_run_matchfinder_init_if_needed(encoder)) {
        return false;
    }

    if (!legacy_codec_matchfinder_native_open_prefetch(encoder, true, false)) {
        return false;
    }

    LegacyCodecMatchFinderAccess match_finder{encoder};
    const std::uint8_t *literal_ptr = match_finder.current_literal_ptr();
    if (literal_ptr == nullptr) {
        return false;
    }

    auto *processed = reinterpret_cast<std::int32_t *>(mutable_base +
            legacy_oem_encoder::kProcessedInBlock);
    *processed += 1;

    auto *lzma_state = reinterpret_cast<std::uint32_t *>(mutable_base +
            legacy_oem_encoder::kLzmaState);
    const std::uint32_t state = *lzma_state;
    const std::uint32_t pos_state = legacy_codec_encoder_fast_pos_state(encoder);
    if (!legacy_codec_encoder_encode_is_match(encoder, pos_state, state, false) ||
            !legacy_codec_encoder_encode_simple_literal_root(encoder, literal_ptr[0])) {
        *processed -= 1;
        return false;
    }
    *lzma_state = legacy_oem_encoder::literal_state_table()[state];
    legacy_codec_encoder_after_symbol_ex(encoder, 1U, kEncoderTagLiteral, true, false);

    match_finder.skip(1);

    if (!legacy_codec_matchfinder_native_open_prefetch(encoder, true, false)) {
        return false;
    }

    if (!legacy_codec_encoder_native_open_prepare_matchfinder(encoder)) {
        return false;
    }

    return legacy_codec_range_encoder_from_native_state(encoder)->status == 0;
}

bool legacy_codec_encoder_native_fast_block_zero_body(void *encoder) {
    if (!legacy_codec_encoder_native_fast_block_zero_open(encoder)) {
        return false;
    }

    auto *mutable_base = static_cast<std::uint8_t *>(encoder);
    const int open_size = legacy_codec_encoder_open_input_size(encoder);
    if (open_size <= 1) {
        return true;
    }

    std::uint32_t distance_index =
            *reinterpret_cast<std::uint32_t *>(mutable_base + legacy_oem_parse::kParseDistanceCode);
    std::uint32_t synced_length =
            *reinterpret_cast<std::uint32_t *>(mutable_base +
                    legacy_oem_encoder::kParseCachedAvailLength);
    if (synced_length == legacy_oem_encoder::kParseSyncDeferSkipFlag) {
        synced_length = 0U;
    }
    if (distance_index == 0U) {
        synced_length = 0U;
        if (!legacy_codec_encoder_sync_matchfinder_when_idle(encoder, &synced_length,
                &distance_index) ||
                distance_index == 0U) {
            return false;
        }
    }

    const auto *match_buffer = reinterpret_cast<const std::uint32_t *>(mutable_base +
            legacy_oem_encoder::kMatchBuffer);
    const std::uint32_t encode_distance =
            legacy_codec_match_buffer_lz_distance(match_buffer, distance_index);

    const std::uint32_t ldc = legacy_codec_encoder_fast_local_dc(encoder);
    if (ldc == 0U || static_cast<std::uint32_t>(open_size) <= ldc) {
        return false;
    }
    std::uint32_t encode_len = synced_length;
    if (legacy_codec_encoder_fast_open_body_active(encoder)) {
        const std::uint32_t oem_open_match =
                static_cast<std::uint32_t>(open_size) - ldc;
        if (oem_open_match >= 2U) {
            encode_len = oem_open_match;
        }
    } else if (encode_len < 2U) {
        encode_len = static_cast<std::uint32_t>(open_size) - ldc;
    }
    if (encode_len < 2U) {
        return false;
    }

    *reinterpret_cast<std::uint32_t *>(mutable_base + legacy_oem_encoder::kFastFinishDcAdjust) =
            1U;
    *reinterpret_cast<std::uint32_t *>(mutable_base + legacy_oem_encoder::kParseChoiceTag) =
            encode_distance + 4U;
    *reinterpret_cast<std::uint32_t *>(mutable_base + legacy_oem_encoder::kPendingLiteral) =
            encode_len;
    if (!legacy_codec_encoder_commit_parse_choice(encoder)) {
        return false;
    }
    legacy_codec_encoder_after_apply_batch(encoder);
    return legacy_codec_range_encoder_from_native_state(encoder)->status == 0;
}

bool legacy_codec_encoder_try_first_literal(void *encoder) {
    return legacy_codec_encoder_encode_stream_entry_literal(encoder);
}

}  // namespace kksdk
