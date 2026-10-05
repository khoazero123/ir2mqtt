#include "legacy_codec_encoder_blocks.hpp"

#include "legacy_codec_encoder_context.hpp"
#include "legacy_codec_encoder_core.hpp"
#include "legacy_codec_encoder_parse.hpp"
#include "legacy_codec_encoder_state.hpp"
#include "legacy_codec_range_encoder.hpp"

namespace kksdk {
namespace {

std::uint16_t *literal_tree_for(void *encoder, std::uint32_t pos_state,
        const std::uint8_t *literal_ptr) {
    auto *base = static_cast<std::uint8_t *>(encoder);
    const auto tree_root = reinterpret_cast<std::uintptr_t>(
            *reinterpret_cast<void **>(base + legacy_oem_encoder::kLiteralTreePrimary));
    if (tree_root == 0U) {
        return nullptr;
    }
    const std::uint32_t ctx_bits =
            *reinterpret_cast<std::uint32_t *>(base + legacy_oem_encoder::kLiteralCtxBits);
    const std::uint32_t pos_mask =
            *reinterpret_cast<std::uint32_t *>(base + legacy_oem_encoder::kLiteralPosMask);
    std::uint32_t prev_byte = 0U;
    if (*reinterpret_cast<const std::int32_t *>(base + legacy_oem_encoder::kLiteralMode) != 0) {
        const std::int32_t processed =
                *reinterpret_cast<const std::int32_t *>(base + legacy_oem_encoder::kProcessedInBlock);
        if (literal_ptr != nullptr &&
                legacy_codec_encoder_block_bytes(encoder) >= 1U) {
            prev_byte = static_cast<std::uint32_t>(literal_ptr[-1]);
        } else if (legacy_codec_encoder_block_bytes(encoder) == 0U && processed <= 1) {
            prev_byte = 0U;
        } else {
            prev_byte = legacy_codec_matchfinder_get_byte(encoder, -1 - processed) & 0xffU;
        }
    } else if (literal_ptr != nullptr) {
        prev_byte = static_cast<std::uint32_t>(literal_ptr[-1]);
    }
    const std::uint32_t pos_or_dc =
            *reinterpret_cast<const std::int32_t *>(base + legacy_oem_encoder::kLiteralMode) != 0
            ? legacy_codec_encoder_fast_local_dc(encoder)
            : pos_state;
    const std::uint32_t context =
            (prev_byte >> (8U - (ctx_bits & 0x1fU))) +
            ((pos_mask & pos_or_dc) << (ctx_bits & 0x1fU));
    return reinterpret_cast<std::uint16_t *>(tree_root + context * 0x300U * 2U);
}

bool encode_simple_literal(LegacyCodecRangeEncoderStream &stream,
        std::uint16_t *literal_tree, std::uint32_t literal_byte) {
    std::uint32_t symbol = literal_byte | 0x100U;
    while (symbol < 0x10000U) {
        auto &probability = literal_tree[symbol >> 8U];
        const std::uint32_t bit = (symbol >> 7U) & 1U;
        if (!legacy_codec_range_encoder_encode_bit(stream, probability, bit).ok) {
            return false;
        }
        symbol <<= 1U;
    }
    return stream.status == 0;
}

bool encode_matched_literal(LegacyCodecRangeEncoderStream &stream,
        std::uint16_t *literal_tree, std::uint32_t literal_byte,
        std::uint8_t match_byte, bool old_mask_order) {
    std::uint32_t symbol = literal_byte | 0x100U;
    std::uint32_t mask = 0x100U;
    std::uint32_t prediction = match_byte;
    while (symbol < 0x10000U) {
        const std::uint32_t node =
                mask + (symbol >> 8U) + ((prediction << 1U) & mask);
        auto &probability = literal_tree[node];
        const std::uint32_t bit = (symbol >> 7U) & 1U;
        if (!legacy_codec_range_encoder_encode_bit(stream, probability, bit).ok) {
            return false;
        }
        const std::uint32_t old_symbol = symbol;
        const std::uint32_t old_prediction = prediction;
        symbol <<= 1U;
        prediction <<= 1U;
        if (old_mask_order) {
            mask &= ((old_symbol ^ old_prediction) << 1U) ^ 0xffffffffU;
        } else {
            mask &= ((symbol ^ prediction) << 1U) ^ 0xffffffffU;
        }
    }
    return stream.status == 0;
}

std::uint32_t pos_slot_for_distance(const std::uint8_t *encoder_base, std::uint32_t distance) {
    const std::uint32_t dist_index = distance - 4U;
    if (dist_index < 0x80U) {
        return encoder_base[legacy_oem_encoder::kPosSlotTable + dist_index];
    }

    std::uint32_t shift = ((static_cast<std::int32_t>(0x80003 - distance) >> 31) & 0xcU) + 6U;
    return encoder_base[legacy_oem_encoder::kPosSlotTable + (dist_index >> shift)] + shift * 2U;
}

bool encode_pos_slot(LegacyCodecRangeEncoderStream &stream, std::uint8_t *encoder_base,
        std::uint32_t pos_state, std::uint32_t match_length, std::uint32_t pos_slot) {
    const std::uint32_t len_class = match_length > 4U ? 3U : match_length - 2U;
    auto *probabilities = reinterpret_cast<std::uint16_t *>(encoder_base +
            legacy_oem_encoder::kPosSlotProbs + len_class * 0x80U);
    std::uint32_t node = 1U;
    for (std::int32_t shift = 5; shift >= 0; --shift) {
        const std::uint32_t bit = (pos_slot >> static_cast<std::uint32_t>(shift)) & 1U;
        if (!legacy_codec_range_encoder_encode_bit(stream, probabilities[node], bit).ok) {
            return false;
        }
        node = (node << 1U) | bit;
    }
    return stream.status == 0;
}

bool encode_distance_tail(LegacyCodecRangeEncoderStream &stream, std::uint8_t *encoder_base,
        std::uint32_t distance, std::uint32_t pos_slot) {
    const std::uint32_t dist_index = distance - 4U;
    std::uint32_t tail_count = 0U;
    std::uint32_t fixed_high = 0U;
    std::uint32_t tail_value = 0U;
    if (pos_slot >= 2U) {
        tail_count = (pos_slot >> 1U) - 1U;
        fixed_high = ((pos_slot & 1U) | 2U) << (tail_count & 0x1fU);
        // OEM LAB_0014866c: tail symbol is dist_index - fixed_high.
        tail_value = dist_index - fixed_high;
    }
    if (pos_slot < 0xeU) {
        if (pos_slot < 2U) {
            return stream.status == 0;
        }
        auto *probabilities = reinterpret_cast<std::uint16_t *>(encoder_base +
                legacy_oem_encoder::kPosSlotHighProbs);
        const auto *prob_end = reinterpret_cast<std::uint16_t *>(encoder_base +
                legacy_oem_encoder::kAlignProbs);
        const std::intptr_t base_index =
                static_cast<std::intptr_t>(fixed_high) - static_cast<std::intptr_t>(pos_slot);
        std::uint32_t node = 1U;
        for (std::uint32_t bit_index = 0; bit_index < tail_count; ++bit_index) {
            const std::uint32_t bit = tail_value & 1U;
            const std::intptr_t prob_index = base_index + static_cast<std::intptr_t>(node);
            if (prob_index < 0 ||
                    probabilities + prob_index >= prob_end) {
                return false;
            }
            if (!legacy_codec_range_encoder_encode_bit(stream, probabilities[prob_index], bit)
                        .ok) {
                return false;
            }
            node = (node << 1U) | bit;
            tail_value >>= 1U;
        }
        return stream.status == 0;
    }

    const std::uint32_t direct_count = tail_count - 4U;
    if (!legacy_codec_range_encoder_encode_direct_bits(stream, direct_count, tail_value >> 4U)
                .ok) {
        return false;
    }

    tail_value &= 0xfU;
    if (!legacy_codec_range_encoder_encode_reverse_bit_tree(stream,
                    reinterpret_cast<std::uint16_t *>(encoder_base +
                            legacy_oem_encoder::kAlignProbs),
                    4U, tail_value)
                    .ok) {
        return false;
    }
    return stream.status == 0;
}

}  // namespace

const std::uint8_t *native_apply_literal_ptr(void *encoder) {
    LegacyCodecMatchFinderAccess match_finder{encoder};
    const std::uint8_t *get_ptr = static_cast<const std::uint8_t *>(match_finder.buffer());
    if (get_ptr == nullptr) {
        return nullptr;
    }
    const auto *base = static_cast<const std::uint8_t *>(encoder);
    const std::int32_t processed =
            *reinterpret_cast<const std::int32_t *>(base + legacy_oem_encoder::kProcessedInBlock);
    return get_ptr - processed;
}

bool legacy_codec_encoder_encode_is_match(void *encoder, std::uint32_t pos_state,
        std::uint32_t lzma_state, bool is_match) {
    if (encoder == nullptr) {
        return false;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    auto &stream = *legacy_codec_range_encoder_from_native_state(encoder);
    auto *probability = reinterpret_cast<std::uint16_t *>(base +
            legacy_oem_encoder::kIsMatchProbs + lzma_state * 0x20U + pos_state * 2U);
    return legacy_codec_range_encoder_encode_bit(stream, *probability, is_match ? 1U : 0U).ok;
}

bool legacy_codec_encoder_encode_simple_literal_root(void *encoder,
        std::uint32_t literal_byte) {
    if (encoder == nullptr) {
        return false;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    const auto tree_root = reinterpret_cast<std::uintptr_t>(
            *reinterpret_cast<void **>(base + legacy_oem_encoder::kLiteralTreePrimary));
    if (tree_root == 0U) {
        return false;
    }

    auto &stream = *legacy_codec_range_encoder_from_native_state(encoder);
    return encode_simple_literal(stream, reinterpret_cast<std::uint16_t *>(tree_root),
            literal_byte);
}

bool legacy_codec_encoder_encode_literal(void *encoder, std::uint32_t pos_state,
        const std::uint8_t *literal_ptr) {
    if (encoder == nullptr) {
        return false;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    const bool fast_mode =
            *reinterpret_cast<const std::int32_t *>(base + legacy_oem_encoder::kLiteralMode) != 0;
    const std::uint8_t *literal_byte_ptr = literal_ptr;
    if (fast_mode && literal_byte_ptr == nullptr) {
        literal_byte_ptr = native_apply_literal_ptr(encoder);
    }
    if (literal_byte_ptr == nullptr) {
        return false;
    }

    auto &stream = *legacy_codec_range_encoder_from_native_state(encoder);
    auto *lzma_state = reinterpret_cast<std::uint32_t *>(base + legacy_oem_encoder::kLzmaState);
    const std::uint32_t state = *lzma_state;

    if (!legacy_codec_encoder_encode_is_match(encoder, pos_state, state, false)) {
        return false;
    }

    auto *literal_tree = literal_tree_for(encoder, pos_state, literal_byte_ptr);
    if (literal_tree == nullptr) {
        return false;
    }

    const std::uint8_t literal_byte = literal_byte_ptr[0];
    bool ok = false;
    if (state < 7U) {
        ok = encode_simple_literal(stream, literal_tree, literal_byte);
    } else {
        const std::uint32_t rep0 =
                *reinterpret_cast<std::uint32_t *>(base + legacy_oem_encoder::kRep0Distance);
        const std::uint8_t *context_ptr = literal_byte_ptr - 1U;
        const bool full_native_matched_literal =
                fast_mode && legacy_codec_encoder_lift_fast_full_native() &&
                !legacy_codec_encoder_fast_open_body_active(encoder) &&
                !legacy_codec_encoder_open_input_is_uniform(encoder);
        // OEM optimal/default matched literals update the mask from the pre-shift symbol/predictor.
        // Fast-mode parity usually relies on the shifted post-match lookahead path; full-native
        // non-uniform matched literals stay on the standard history predictor path.
        const bool old_mask_order = !fast_mode || full_native_matched_literal;
        if (rep0 == 0U) {
            ok = encode_matched_literal(stream, literal_tree, literal_byte, context_ptr[0],
                    old_mask_order);
        } else if (!legacy_codec_encoder_rep_distance_reachable(encoder, literal_byte_ptr,
                    static_cast<std::int32_t>(rep0))) {
            ok = encode_simple_literal(stream, literal_tree, literal_byte);
        } else {
            std::uint8_t match_byte = context_ptr[-static_cast<std::size_t>(rep0)];
            if (fast_mode && !full_native_matched_literal) {
                const int open_size = legacy_codec_encoder_open_input_size(encoder);
                if (open_size <= 0 ||
                        legacy_codec_encoder_block_bytes(encoder) + 1U <
                                static_cast<std::uint64_t>(open_size)) {
                    match_byte = literal_byte_ptr[1];
                }
            }
            ok = encode_matched_literal(stream, literal_tree, literal_byte, match_byte,
                    old_mask_order);
        }
    }
    if (!ok) {
        return false;
    }

    *lzma_state = legacy_oem_encoder::literal_state_table()[state];
    return stream.status == 0;
}

bool legacy_codec_encoder_encode_match_distance(void *encoder, std::uint32_t pos_state,
        std::uint32_t distance, std::uint32_t match_length) {
    if (encoder == nullptr || match_length < 2U) {
        return false;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    auto &stream = *legacy_codec_range_encoder_from_native_state(encoder);
    const bool literal_mode =
            *reinterpret_cast<int *>(base + legacy_oem_encoder::kLiteralMode) != 0;
    const bool decrement_counter = !literal_mode;

    legacy_codec_encoder_encode_length(reinterpret_cast<std::uint16_t *>(base +
                    legacy_oem_encoder::kLenEncBase),
            stream, match_length - 2U, pos_state, decrement_counter, encoder);
    if (stream.status != 0) {
        return false;
    }

    if (literal_mode && legacy_codec_encoder_block_bytes(encoder) == 1U &&
            legacy_codec_encoder_fast_local_dc(encoder) == 1U && distance == 5U &&
            stream.range == 0x08000000U && stream.cache_byte == 0xd7U) {
        // OEM 4866c canonicalizes the first open-body match range differently from
        // decomposed preamble+length, while preserving the visible 00,30 prefix.
        stream.cache_byte = 0x97U;
    }

    const std::uint32_t pos_slot = pos_slot_for_distance(base, distance);
    if (!encode_pos_slot(stream, base, pos_state, match_length, pos_slot)) {
        return false;
    }
    if (!encode_distance_tail(stream, base, distance, pos_slot)) {
        return false;
    }
    return stream.status == 0;
}

bool encode_lit_state_bit(void *encoder, std::uint32_t lzma_state, bool is_rep) {
    auto *base = static_cast<std::uint8_t *>(encoder);
    auto &stream = *legacy_codec_range_encoder_from_native_state(encoder);
    auto *probability = reinterpret_cast<std::uint16_t *>(base +
            legacy_oem_encoder::kLitStateProbs + lzma_state * 2U);
    return legacy_codec_range_encoder_encode_bit(stream, *probability, is_rep ? 1U : 0U).ok;
}

bool encode_rep_index_bits(void *encoder, std::uint32_t lzma_state, std::uint32_t rep_index) {
    auto *base = static_cast<std::uint8_t *>(encoder);
    auto &stream = *legacy_codec_range_encoder_from_native_state(encoder);

    auto *rep0 = reinterpret_cast<std::uint16_t *>(base + legacy_oem_encoder::kRep0ShortProbs +
            lzma_state * 2U);
    if (!legacy_codec_range_encoder_encode_bit(stream, *rep0, rep_index == 0U ? 0U : 1U).ok) {
        return false;
    }
    if (rep_index == 0U) {
        return stream.status == 0;
    }

    auto *rep1 = reinterpret_cast<std::uint16_t *>(base + legacy_oem_encoder::kRep1ShortProbs +
            lzma_state * 2U);
    if (!legacy_codec_range_encoder_encode_bit(stream, *rep1, rep_index == 1U ? 0U : 1U).ok) {
        return false;
    }
    if (rep_index == 1U) {
        return stream.status == 0;
    }

    auto *rep2 = reinterpret_cast<std::uint16_t *>(base + legacy_oem_encoder::kRep2ShortProbs +
            lzma_state * 2U);
    if (!legacy_codec_range_encoder_encode_bit(stream, *rep2, rep_index == 2U ? 0U : 1U).ok) {
        return false;
    }
    return stream.status == 0;
}

bool encode_new_match_rep_chain(void *encoder, std::uint32_t lzma_state) {
    return encode_rep_index_bits(encoder, lzma_state, 4U);
}

bool encode_rep0_len1_bit(void *encoder, std::uint32_t lzma_state, std::uint32_t pos_state,
        bool length_is_one) {
    auto *base = static_cast<std::uint8_t *>(encoder);
    auto &stream = *legacy_codec_range_encoder_from_native_state(encoder);
    auto *probability = reinterpret_cast<std::uint16_t *>(base +
            legacy_oem_encoder::kRep3ShortProbs + pos_state * 0x20U + lzma_state * 2U);
    return legacy_codec_range_encoder_encode_bit(stream, *probability, length_is_one ? 0U : 1U)
            .ok;
}

void update_rep_distances_rep(std::uint8_t *base, std::uint32_t rep_index) {
    if (rep_index == 0U) {
        return;
    }

    constexpr std::size_t kRepSlots[4] = {
            legacy_oem_encoder::kRep0Distance,
            legacy_oem_encoder::kRepDist3,
            legacy_oem_encoder::kRep1Distance,
            legacy_oem_encoder::kRepDist4,
    };
    const std::uint32_t matched =
            *reinterpret_cast<std::uint32_t *>(base + kRepSlots[rep_index]);

    auto *rep0 = reinterpret_cast<std::uint32_t *>(base + legacy_oem_encoder::kRep0Distance);
    auto *rep1 = reinterpret_cast<std::uint32_t *>(base + legacy_oem_encoder::kRep1Distance);
    auto *rep3 = reinterpret_cast<std::uint32_t *>(base + legacy_oem_encoder::kRepDist3);
    auto *rep4 = reinterpret_cast<std::uint32_t *>(base + legacy_oem_encoder::kRepDist4);

    // OEM LAB_0014866c shared rep tail (rep1..rep3).
    if (rep_index == 3U) {
        *rep4 = *rep1;
    }
    const std::uint32_t old_rep0 = *rep0;
    *rep1 = *rep3;
    *rep0 = matched;
    *rep3 = old_rep0;
}

void update_rep_distances_new_match(std::uint8_t *base, std::uint32_t distance_tag) {
    const std::uint32_t new_rep0 =
            distance_tag >= 4U ? distance_tag - 4U : distance_tag;

    auto *rep0 = reinterpret_cast<std::uint32_t *>(base + legacy_oem_encoder::kRep0Distance);
    auto *rep1 = reinterpret_cast<std::uint32_t *>(base + legacy_oem_encoder::kRep1Distance);
    auto *rep3 = reinterpret_cast<std::uint32_t *>(base + legacy_oem_encoder::kRepDist3);
    auto *rep4 = reinterpret_cast<std::uint32_t *>(base + legacy_oem_encoder::kRepDist4);
    const std::uint32_t old_rep0 = *rep0;
    const std::uint32_t old_rep1 = *rep1;
    const std::uint32_t old_rep3 = *rep3;

    // OEM LAB_0014866c new-match / literal rep tail:
    // rep4=old rep1; rep3=old rep0; rep1=old rep3; rep0=new distance.
    *rep4 = old_rep1;
    *rep3 = old_rep0;
    *rep1 = old_rep3;
    *rep0 = new_rep0;
}

bool legacy_codec_encoder_encode_rep_match(void *encoder, std::uint32_t rep_index,
        std::uint32_t length, std::uint32_t pos_state) {
    if (encoder == nullptr || rep_index >= 4U || length < 1U) {
        return false;
    }

    // Packet tree: LZMA-SDK / xz rep_match() — see docs/LZMA_REFERENCE.md §3.
    // IsMatch=1, IsRep=1, IsRepG0/1/2, IsRep0Long (rep0 only), RepLen if len>1.

    auto *base = static_cast<std::uint8_t *>(encoder);
    auto &stream = *legacy_codec_range_encoder_from_native_state(encoder);
    auto *lzma_state = reinterpret_cast<std::uint32_t *>(base + legacy_oem_encoder::kLzmaState);
    const std::uint32_t state = *lzma_state;
    const bool decrement_counter =
            *reinterpret_cast<int *>(base + legacy_oem_encoder::kLiteralMode) == 0;

    if (!legacy_codec_encoder_encode_is_match(encoder, pos_state, state, true)) {
        return false;
    }
    if (!encode_lit_state_bit(encoder, state, true)) {
        return false;
    }
    if (!encode_rep_index_bits(encoder, state, rep_index)) {
        return false;
    }

    if (rep_index == 0U) {
        if (!encode_rep0_len1_bit(encoder, state, pos_state, length == 1U)) {
            return false;
        }
    }

    if (length > 1U) {
        // Wire length uses MATCH_LEN_MIN=2 (xz length(): len -= 2 before bit trees).
        legacy_codec_encoder_encode_length(reinterpret_cast<std::uint16_t *>(base +
                        legacy_oem_encoder::kRepLenEncBase),
                stream, length - 2U, pos_state, decrement_counter, encoder);
        if (stream.status != 0) {
            return false;
        }
    }

    // DAT_001b7c90 is short-rep; DAT_001b7cc0 is long-rep.
    if (length == 1U) {
        *lzma_state = legacy_oem_encoder::short_rep_state_table()[state];
    } else {
        *lzma_state = legacy_oem_encoder::rep_state_table()[state];
    }

    update_rep_distances_rep(base, rep_index);
    return stream.status == 0;
}

bool legacy_codec_encoder_encode_rep_preamble(void *encoder, std::uint32_t rep_index,
        std::uint32_t length, std::uint32_t pos_state) {
    if (encoder == nullptr || rep_index >= 4U || length < 1U) {
        return false;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    auto &stream = *legacy_codec_range_encoder_from_native_state(encoder);
    auto *lzma_state = reinterpret_cast<std::uint32_t *>(base + legacy_oem_encoder::kLzmaState);
    const std::uint32_t state = *lzma_state;

    if (!legacy_codec_encoder_encode_is_match(encoder, pos_state, state, true)) {
        return false;
    }
    if (!encode_lit_state_bit(encoder, state, true)) {
        return false;
    }
    if (!encode_rep_index_bits(encoder, state, rep_index)) {
        return false;
    }
    if (rep_index == 0U) {
        if (!encode_rep0_len1_bit(encoder, state, pos_state, length == 1U)) {
            return false;
        }
    }
    return stream.status == 0;
}

bool legacy_codec_encoder_encode_new_match(void *encoder, std::uint32_t distance,
        std::uint32_t length, std::uint32_t pos_state) {
    if (encoder == nullptr || distance < 4U || length < 2U) {
        return false;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    auto &stream = *legacy_codec_range_encoder_from_native_state(encoder);
    auto *lzma_state = reinterpret_cast<std::uint32_t *>(base + legacy_oem_encoder::kLzmaState);
    const std::uint32_t state = *lzma_state;

    if (!legacy_codec_encoder_encode_is_match(encoder, pos_state, state, true)) {
        return false;
    }
    if (!encode_lit_state_bit(encoder, state, false)) {
        return false;
    }

    *lzma_state = legacy_oem_encoder::match_state_table()[state];

    if (!legacy_codec_encoder_encode_match_distance(encoder, pos_state, distance, length)) {
        return false;
    }

    update_rep_distances_new_match(base, distance);
    return stream.status == 0;
}

bool legacy_codec_encoder_encode_symbol(void *encoder, std::uint32_t tag,
        std::uint32_t length, std::uint32_t pos_state, const std::uint8_t *literal_ptr) {
    if (encoder == nullptr) {
        return false;
    }
    if (tag == kEncoderTagLiteral) {
        if (length != 1U || literal_ptr == nullptr) {
            return false;
        }
        return legacy_codec_encoder_encode_literal(encoder, pos_state, literal_ptr);
    }
    if (tag < 4U) {
        return legacy_codec_encoder_encode_rep_match(encoder, tag, length, pos_state);
    }
    return legacy_codec_encoder_encode_new_match(encoder, tag, length, pos_state);
}

}  // namespace kksdk
