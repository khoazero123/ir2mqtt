#include "legacy_codec_encoder_parse.hpp"

#include "legacy_codec_encoder_blocks.hpp"
#include "legacy_codec_encoder_context.hpp"
#include "legacy_codec_encoder_core.hpp"
#include "legacy_codec_encoder_state.hpp"
#include "legacy_codec_range_encoder.hpp"

#include <cstring>

namespace kksdk {
namespace {

void bump_post_symbol_counters(std::uint8_t *base, std::uint32_t encoded_length,
        bool bump_match_count) {
    auto *processed = reinterpret_cast<std::int32_t *>(base + legacy_oem_encoder::kProcessedInBlock);
    *processed -= static_cast<std::int32_t>(encoded_length);

    auto *block_bytes = reinterpret_cast<std::uint64_t *>(base +
            legacy_oem_encoder::kBlockBytesEncoded);
    *block_bytes += encoded_length;

    if (bump_match_count) {
        auto *match_count = reinterpret_cast<std::uint32_t *>(base +
                legacy_oem_encoder::kMatchEncodeCount);
        *match_count += 1U;
    }
}

// OEM LAB_0014866c post-symbol tail for rep/new-match packets.
void oem_post_symbol_rep_tail(std::uint8_t *base, std::uint32_t new_rep0) {
    auto *rep0 = reinterpret_cast<std::uint32_t *>(base + legacy_oem_encoder::kRep0Distance);
    auto *rep1 = reinterpret_cast<std::uint32_t *>(base + legacy_oem_encoder::kRep1Distance);
    *reinterpret_cast<std::uint32_t *>(base + legacy_oem_encoder::kRepDist4) = *rep1;
    std::memcpy(reinterpret_cast<void *>(base + legacy_oem_encoder::kRepDist3), rep0,
            sizeof(std::uint32_t) * 2U);
    *rep0 = new_rep0;
}

}  // namespace

void legacy_codec_encoder_maybe_refresh_prices(void *encoder) {
    if (encoder == nullptr) {
        return;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    if (*reinterpret_cast<std::int32_t *>(base + legacy_oem_encoder::kStreamOutStatus) != 0) {
        return;
    }

    auto *match_count = reinterpret_cast<std::uint32_t *>(base +
            legacy_oem_encoder::kMatchEncodeCount);
    if (*match_count > 0x7fU) {
        legacy_codec_encoder_refresh_match_prices(encoder);
    }

    auto *align_counter = reinterpret_cast<std::uint32_t *>(base +
            legacy_oem_encoder::kAlignPriceCounter);
    if (*align_counter > 0xfU) {
        legacy_codec_encoder_refresh_align_prices(encoder);
    }
}

void legacy_codec_encoder_after_symbol(void *encoder, std::uint32_t encoded_length,
        std::uint32_t tag, bool native_rep_tail) {
    legacy_codec_encoder_after_symbol_ex(encoder, encoded_length, tag, native_rep_tail, true);
}

void legacy_codec_encoder_after_symbol_ex(void *encoder, std::uint32_t encoded_length,
        std::uint32_t tag, bool native_rep_tail, bool bump_match_count) {
    if (encoder == nullptr || encoded_length == 0U) {
        return;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    const std::uint64_t block_before = legacy_codec_encoder_block_bytes(encoder);
    const bool native_open_body_match =
            *reinterpret_cast<const std::int32_t *>(base + legacy_oem_encoder::kLiteralMode) != 0 &&
            block_before == 0U && tag != kEncoderTagLiteral && tag >= 4U &&
            legacy_codec_encoder_fast_local_dc(encoder) > 0U;
    const bool first_open_body_match =
            *reinterpret_cast<const std::int32_t *>(base + legacy_oem_encoder::kLiteralMode) != 0 &&
            block_before == 1U && tag != kEncoderTagLiteral && tag >= 4U &&
            legacy_codec_encoder_fast_local_dc(encoder) == 1U;
    bump_post_symbol_counters(base, encoded_length, bump_match_count);

    if (*reinterpret_cast<const std::int32_t *>(base + legacy_oem_encoder::kLiteralMode) != 0) {
        auto *local_dc = reinterpret_cast<std::uint32_t *>(base + legacy_oem_encoder::kFastLocalDc);
        if (*local_dc == 0U && block_before == 0U && tag == kEncoderTagLiteral &&
                encoded_length == 1U) {
            *local_dc = 1U;
        } else {
            std::uint32_t delta = encoded_length;
            auto *finish_dc_adjust = reinterpret_cast<std::uint32_t *>(base +
                    legacy_oem_encoder::kFastFinishDcAdjust);
            if (*finish_dc_adjust != 0U) {
                delta -= 1U;
                if (*finish_dc_adjust != 3U) {
                    *finish_dc_adjust = 0U;
                }
            }
            *local_dc += delta;
        }
        if (native_open_body_match) {
            auto *block_slot = reinterpret_cast<std::uint64_t *>(base +
                    legacy_oem_encoder::kBlockBytesEncoded);
            *block_slot += 1U;
            const int open_size = legacy_codec_encoder_open_input_size(encoder);
            if (open_size > 0 && *block_slot >= static_cast<std::uint64_t>(open_size)) {
                legacy_codec_encoder_reset_fast_local_dc(encoder);
            }
            *reinterpret_cast<std::int32_t *>(base + legacy_oem_encoder::kProcessedInBlock) = 0;
        } else if (first_open_body_match) {
            *local_dc = 1U;
            *reinterpret_cast<std::int32_t *>(base + legacy_oem_encoder::kProcessedInBlock) = 0;
        }
    }

    auto *sync_cached = reinterpret_cast<std::uint32_t *>(base +
            legacy_oem_encoder::kParseCachedAvailLength);
    if (*sync_cached == legacy_oem_encoder::kParseSyncDeferSkipFlag) {
        LegacyCodecMatchFinderAccess match_finder{encoder};
        match_finder.skip(1);
        *sync_cached = 0U;
        *reinterpret_cast<std::uint32_t *>(base + legacy_oem_encoder::kMatchfinderAvail) =
                static_cast<std::uint32_t>(match_finder.has_data());
    }

    if (native_rep_tail && tag != kEncoderTagLiteral) {
        oem_post_symbol_rep_tail(base, 0U);
    }
}

void legacy_codec_encoder_after_apply_batch(void *encoder) {
    if (encoder == nullptr) {
        return;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    if (*reinterpret_cast<std::int32_t *>(base + legacy_oem_encoder::kLiteralMode) != 0) {
        return;
    }

    legacy_codec_encoder_maybe_refresh_prices(encoder);
}

bool legacy_codec_encoder_commit_symbol(void *encoder, std::uint32_t tag,
        std::uint32_t length, const std::uint8_t *literal_ptr) {
    return legacy_codec_encoder_commit_symbol_ex(encoder, tag, length, literal_ptr, true);
}

bool legacy_codec_encoder_commit_symbol_ex(void *encoder, std::uint32_t tag,
        std::uint32_t length, const std::uint8_t *literal_ptr, bool bump_match_count) {
    if (encoder == nullptr || length == 0U) {
        return false;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    const std::uint32_t pos_state =
            *reinterpret_cast<const std::int32_t *>(base + legacy_oem_encoder::kLiteralMode) != 0
            ? legacy_codec_encoder_fast_match_pos_state(encoder)
            : legacy_codec_encoder_pos_state(encoder);
    if (!legacy_codec_encoder_encode_symbol(encoder, tag, length, pos_state, literal_ptr)) {
        return false;
    }

    const bool native_rep_tail = false;
    legacy_codec_encoder_after_symbol_ex(encoder, length, tag, native_rep_tail, bump_match_count);
    return legacy_codec_range_encoder_from_native_state(encoder)->status == 0;
}

bool legacy_codec_encoder_commit_parse_choice(void *encoder) {
    return legacy_codec_encoder_commit_parse_choice_with_literal(encoder, nullptr);
}

bool legacy_codec_encoder_commit_parse_choice_with_literal(void *encoder,
        const std::uint8_t *forced_literal_ptr) {
    if (encoder == nullptr) {
        return false;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    std::uint32_t tag =
            *reinterpret_cast<std::uint32_t *>(base + legacy_oem_encoder::kParseChoiceTag);
    std::uint32_t length =
            *reinterpret_cast<std::uint32_t *>(base + legacy_oem_encoder::kPendingLiteral);

    if (legacy_codec_encoder_lift_fast_full_native()) {
        const int open_size = legacy_codec_encoder_open_input_size(encoder);
        const std::uint64_t block = legacy_codec_encoder_block_bytes(encoder);
        if (open_size > 0) {
            if (block >= static_cast<std::uint64_t>(open_size)) {
                return false;
            }
            const std::uint64_t max_len = static_cast<std::uint64_t>(open_size) - block;
            if (static_cast<std::uint64_t>(length) > max_len) {
                length = static_cast<std::uint32_t>(max_len);
            }
            if (length == 0U) {
                return false;
            }
        }
    }

    const std::uint8_t *literal_ptr = nullptr;
    if (tag == kEncoderTagLiteral) {
        literal_ptr = forced_literal_ptr;
        if (literal_ptr == nullptr) {
            const bool optimal_pending_literal =
                    *reinterpret_cast<const std::int32_t *>(
                            base + legacy_oem_encoder::kLiteralMode) == 0 &&
                    *reinterpret_cast<const std::int32_t *>(
                            base + legacy_oem_encoder::kProcessedInBlock) > 0;
            if (optimal_pending_literal) {
                LegacyCodecMatchFinderAccess match_finder{encoder};
                const std::uint8_t *current = match_finder.current_literal_ptr();
                const auto processed = *reinterpret_cast<const std::int32_t *>(
                        base + legacy_oem_encoder::kProcessedInBlock);
                if (current != nullptr) {
                    literal_ptr = current - processed;
                }
            }
            if (literal_ptr == nullptr) {
                LegacyCodecMatchFinderAccess match_finder{encoder};
                literal_ptr = match_finder.current_literal_ptr();
            }
        }
        if (literal_ptr == nullptr) {
            return false;
        }
    }

    auto *processed = reinterpret_cast<std::int32_t *>(base + legacy_oem_encoder::kProcessedInBlock);
    const bool full_native_input_match =
            legacy_codec_encoder_lift_fast_full_native() &&
            *reinterpret_cast<const std::int32_t *>(base + legacy_oem_encoder::kLiteralMode) !=
                    0 &&
            !legacy_codec_encoder_fast_open_body_active(encoder) &&
            legacy_codec_encoder_open_input_size(encoder) > 0;
    const bool full_native_sparse_tail_rep =
            full_native_input_match && tag < 4U && length == 2U &&
            legacy_codec_encoder_block_bytes(encoder) >= 35U;
    if (tag == kEncoderTagLiteral &&
            *reinterpret_cast<const std::int32_t *>(base + legacy_oem_encoder::kLiteralMode) != 0 &&
            legacy_codec_encoder_block_bytes(encoder) >= 1U && *processed == 0) {
        *processed += 1;
    }
    if (full_native_sparse_tail_rep &&
            *processed < static_cast<std::int32_t>(length)) {
        *processed = static_cast<std::int32_t>(length);
    }
    if (tag >= 4U && (length > 2U || (full_native_input_match && length > 1U)) &&
            !legacy_codec_encoder_fast_open_body_active(encoder)) {
        const bool skip_processed_for_first_open_match =
                *reinterpret_cast<const std::int32_t *>(base + legacy_oem_encoder::kLiteralMode) !=
                        0 &&
                legacy_codec_encoder_block_bytes(encoder) == 1U &&
                legacy_codec_encoder_fast_local_dc(encoder) == 1U;
        if (!skip_processed_for_first_open_match) {
            *processed += static_cast<std::int32_t>(length - 1U);
        }
    }

    const bool ok = [&]() {
        bool bump_match_count = tag != kEncoderTagLiteral;
        if (*reinterpret_cast<const std::int32_t *>(base + legacy_oem_encoder::kLiteralMode) != 0 &&
                tag != kEncoderTagLiteral) {
            bump_match_count = false;
        }
        return legacy_codec_encoder_commit_symbol_ex(encoder, tag, length, literal_ptr,
                bump_match_count);
    }();
    if (ok && tag < 4U && length > 1U) {
        LegacyCodecMatchFinderAccess match_finder{encoder};
        match_finder.skip(static_cast<std::int32_t>(
                full_native_sparse_tail_rep ? length : length - 1U));
    } else if (ok && tag >= 4U &&
            (length > 2U || (full_native_input_match && length > 1U))) {
        LegacyCodecMatchFinderAccess match_finder{encoder};
        match_finder.skip(static_cast<std::int32_t>(
                full_native_input_match ? length - 1U : length - 2U));
    }
    return ok;
}

}  // namespace kksdk
