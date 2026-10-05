#include "legacy_codec_encoder_parse.hpp"

#include "legacy_codec_encoder_blocks.hpp"
#include "legacy_codec_encoder_context.hpp"
#include "legacy_codec_encoder_core.hpp"
#include "legacy_codec_encoder_matchfinder_layout.hpp"
#include "legacy_codec_encoder_parse_price_shared.hpp"
#include "legacy_codec_encoder_state.hpp"
#include "legacy_codec_range_encoder.hpp"

#include <algorithm>

namespace kksdk {
namespace {

using namespace legacy_oem_encoder;
using namespace legacy_oem_matchfinder;
using namespace legacy_oem_parse;
using namespace legacy_oem_parse_detail;
using namespace legacy_oem_price;

std::uint32_t probe_rep_at(const std::uint8_t *cur, std::int32_t rep_distance, std::uint32_t avail) {
    if (rep_distance < 0 || avail == 0U) {
        return 0U;
    }

    const std::uint8_t *ref = cur - static_cast<std::size_t>(rep_distance + 1);
    if (*cur != *ref) {
        return 0U;
    }

    if (avail == 1U) {
        return 1U;
    }

    const std::uint32_t limit = clamp_avail(avail);

    if (limit < 3U) {
        return 2U;
    }

    std::uint32_t length = 2U;
    std::uint32_t probe = 2U;
    while (probe < limit) {
        if (cur[probe - 1U] != ref[probe - 1U]) {
            return probe;
        }
        length = probe + 1U;
        probe = length;
    }
    return length;
}

void store_path_rep_snapshot(std::uint8_t *encoder, std::uint32_t path_length) {
    if (path_length <= 1U) {
        return;
    }

    *reinterpret_cast<std::uint64_t *>(encoder + 0x4fcU) =
            *reinterpret_cast<const std::uint64_t *>(encoder + kRep0Distance);
    *reinterpret_cast<std::uint64_t *>(encoder + 0x504U) =
            *reinterpret_cast<const std::uint64_t *>(encoder + kRep1Distance);
    *reinterpret_cast<std::uint32_t *>(encoder + 0x524U) = 0U;
}

void try_update_parse_node(std::uint8_t *encoder, std::uint32_t node_index, std::uint32_t price,
        std::uint32_t tag) {
    if (price >= *node_price(encoder, node_index)) {
        return;
    }

    *node_price(encoder, node_index) = price;
    *node_parent_index(encoder, node_index) = 0U;
    *node_tag(encoder, node_index) = tag;
    *node_flag(encoder, node_index) = 0U;
}

void get_rep_short_components(std::uint8_t *encoder, std::uint32_t pos_state,
        std::uint32_t lzma_state, std::uint32_t rep_index, std::int32_t &prob_a,
        std::int32_t &prob_b) {
    const auto *prices = prob_prices(encoder);
    const auto *rep0_short = reinterpret_cast<const std::uint16_t *>(encoder + kRep0ShortProbs);
    const auto *rep1_short = reinterpret_cast<const std::uint16_t *>(encoder + kRep1ShortProbs);
    const auto *rep2_short = reinterpret_cast<const std::uint16_t *>(encoder + kRep2ShortProbs);
    const auto *rep3_short = reinterpret_cast<const std::uint16_t *>(encoder + kRep3ShortProbs);

    if (rep_index == 0U) {
        prob_a = lookup_nibble(prices, rep0_short[lzma_state]);
        prob_b = lookup_word_xor(prices, rep3_short[pos_state * 0x10U + lzma_state]);
        return;
    }

    prob_a = lookup_nibble_xor(prices, rep0_short[lzma_state]);
    if (rep_index == 1U) {
        prob_b = lookup_nibble(prices, rep1_short[lzma_state]);
        return;
    }

    prob_b = lookup_nibble_xor(prices, rep1_short[lzma_state]) + prob_a;
    const std::uint32_t rep2_index =
            ((2U - rep_index) & 0x7f0U) ^ ((rep2_short[lzma_state] >> 2U) & 0x3ffcU);
    prob_a = lookup_word(prices, rep2_index);
}

}  // namespace

bool legacy_codec_encoder_probe_rep_lengths(void *encoder, std::uint32_t avail_limit,
        std::uint32_t rep_lengths[4]) {
    if (encoder == nullptr || rep_lengths == nullptr) {
        return false;
    }

    rep_lengths[0] = rep_lengths[1] = rep_lengths[2] = rep_lengths[3] = 0U;
    if (avail_limit == 0U) {
        return true;
    }

    LegacyCodecMatchFinderAccess match_finder{encoder};
    const std::uint8_t *cur = match_finder.current_literal_ptr();
    if (cur == nullptr) {
        return false;
    }

    const auto *base = static_cast<const std::uint8_t *>(encoder);
    const auto probe_if_allowed = [&](std::int32_t rep_distance) -> std::uint32_t {
        if (!legacy_codec_encoder_rep_distance_reachable(encoder, cur, rep_distance)) {
            return 0U;
        }
        return probe_rep_at(cur, rep_distance, avail_limit);
    };

    rep_lengths[0] = probe_if_allowed(
            *reinterpret_cast<const std::int32_t *>(base + kRep0Distance));
    rep_lengths[1] = probe_if_allowed(
            *reinterpret_cast<const std::int32_t *>(base + kRepDist3));
    rep_lengths[2] = probe_if_allowed(
            *reinterpret_cast<const std::int32_t *>(base + kRep1Distance));
    rep_lengths[3] = probe_if_allowed(
            *reinterpret_cast<const std::int32_t *>(base + kRepDist4));
    return true;
}

std::uint32_t legacy_codec_encoder_pick_best_rep_index(const std::uint32_t rep_lengths[4]) {
    if (rep_lengths == nullptr) {
        return 0U;
    }

    std::uint32_t best_index = rep_lengths[0] < rep_lengths[1] ? 1U : 0U;
    if (rep_lengths[2] > rep_lengths[best_index]) {
        best_index = 2U;
    }
    if (rep_lengths[3] > rep_lengths[best_index]) {
        best_index = 3U;
    }
    return best_index;
}

bool legacy_codec_encoder_parse_write_literal_node(void *encoder, std::uint32_t pos_state,
        std::uint32_t main_match_length, std::uint32_t match_distance_index,
        const std::uint32_t rep_lengths[4], std::uint32_t chosen_rep_length,
        std::uint32_t &chosen_tag, LegacyCodecEncoderParsePricingContext &pricing) {
    if (encoder == nullptr || rep_lengths == nullptr) {
        return false;
    }

    const std::uint32_t best_rep_index = legacy_codec_encoder_pick_best_rep_index(rep_lengths);
    const std::uint32_t best_rep_length = rep_lengths[best_rep_index];
    (void)best_rep_length;
    (void)main_match_length;
    (void)match_distance_index;

    auto *base = static_cast<std::uint8_t *>(encoder);
    LegacyCodecMatchFinderAccess match_finder{encoder};
    const std::uint8_t *literal_ptr = match_finder.current_literal_ptr();
    if (literal_ptr == nullptr) {
        return false;
    }

    const std::uint8_t cur_byte = literal_ptr[0];
    const std::uint8_t match_byte = literal_ptr[-static_cast<std::size_t>(
            *reinterpret_cast<const std::int32_t *>(base + kRep0Distance) + 1)];
    const std::uint32_t lzma_state =
            *reinterpret_cast<std::uint32_t *>(base + kLzmaState);
    const auto *prices = prob_prices(base);
    const auto *is_match_probs = reinterpret_cast<const std::uint16_t *>(base + kIsMatchProbs);
    const std::uint16_t is_match_prob = is_match_probs[pos_state * 0x10U + lzma_state];
    const auto *lit_state_probs = reinterpret_cast<const std::uint16_t *>(base + kLitStateProbs);

    std::uint16_t *literal_tree = literal_tree_for(base, pos_state, literal_ptr);
    std::int32_t literal_component = 0;
    if (lzma_state < 7U) {
        literal_component = price_simple_literal(prices, literal_tree, cur_byte);
    } else {
        literal_component = price_matched_literal(prices, literal_tree, cur_byte, match_byte);
    }

    const std::int32_t literal_price = literal_component + lookup_word(prices, is_match_prob);
    chosen_tag = kEncoderTagLiteral;

    constexpr std::uint32_t kFirstPathNode = 1U;
    *node_price(base, kFirstPathNode) = static_cast<std::uint32_t>(literal_price);
    *node_parent_index(base, kFirstPathNode) = 0U;
    *node_tag(base, kFirstPathNode) = kEncoderTagLiteral;
    *node_flag(base, kFirstPathNode) = 0U;

    if (cur_byte == match_byte) {
        const std::int32_t rep0_price = price_rep0_short(base, pos_state, lzma_state);
        if (rep0_price < literal_price) {
            chosen_tag = 0U;
            try_update_parse_node(base, kFirstPathNode, static_cast<std::uint32_t>(rep0_price), 0U);
        }
    }

    pricing.pos_state = pos_state;
    pricing.lzma_state = lzma_state;
    pricing.is_match_xor_price = lookup_nibble_xor(prices, is_match_prob);
    pricing.lit_state_price =
            lookup_word_xor(prices, lit_state_probs[lzma_state]) + pricing.is_match_xor_price;
    pricing.path_length = chosen_rep_length;
    for (std::size_t index = 0; index < 4U; ++index) {
        pricing.rep_lengths[index] = rep_lengths[index];
    }

    if (chosen_rep_length > 1U) {
        *node_flag(base, kFirstPathNode + 1U) = 0U;
        init_infinite_path_prices(base, chosen_rep_length);
        store_path_rep_snapshot(base, chosen_rep_length);
    }

    (void)main_match_length;
    (void)match_distance_index;
    return true;
}

bool legacy_codec_encoder_parse_price_graph(void *encoder,
        const LegacyCodecEncoderParsePricingContext &pricing, std::uint32_t main_match_length,
        std::uint32_t match_distance_index, std::uint32_t &backtrack_node, bool &needs_greedy,
        std::uint32_t &frontier_limit) {
    if (encoder == nullptr) {
        return false;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    const std::uint32_t pos_state = pricing.pos_state;
    const std::uint32_t lzma_state = pricing.lzma_state;
    std::uint32_t path_length = pricing.path_length;
    if (path_length <= main_match_length) {
        path_length = main_match_length;
    }
    frontier_limit = std::max(path_length, 2U);
    needs_greedy = path_length > 1U;

    for (std::uint32_t rep_index = 0U; rep_index < 4U; ++rep_index) {
        std::uint32_t length = pricing.rep_lengths[rep_index];
        if (length <= 1U) {
            continue;
        }

        std::int32_t prob_a = 0;
        std::int32_t prob_b = 0;
        get_rep_short_components(base, pos_state, lzma_state, rep_index, prob_a, prob_b);

        const auto *rep_len_prices = reinterpret_cast<const std::int32_t *>(base +
                kRepLenPriceCache + pos_state * 0x440U);
        while (length > 1U) {
            const std::int32_t total = prob_b + prob_a + pricing.lit_state_price +
                    rep_len_prices[length - 2U];
            try_update_parse_node(base, length, static_cast<std::uint32_t>(total), rep_index);
            if (length + 1U > frontier_limit) {
                frontier_limit = length + 1U;
            }
            length -= 1U;
        }
    }

    std::uint32_t match_length_cursor = 2U;
    if (pricing.rep_lengths[0] > 1U) {
        match_length_cursor = pricing.rep_lengths[0] + 1U;
    }

    if (match_length_cursor <= main_match_length) {
        const auto *match_buffer = reinterpret_cast<const std::uint32_t *>(base + kMatchBuffer);
        const auto *len_prices = reinterpret_cast<const std::int32_t *>(base + kLenPriceCache +
                pos_state * 0x440U);
        const std::int32_t lit_state_word =
                lookup_word(prob_prices(base),
                        reinterpret_cast<const std::uint16_t *>(base + kLitStateProbs)[lzma_state]);

        std::uint32_t pair_index = 0U;
        while (match_buffer[pair_index] < match_length_cursor) {
            pair_index += 2U;
        }

        while (true) {
            const std::uint32_t len_slot = match_length_cursor - 2U;
            std::uint32_t len_class = len_slot;
            if (match_length_cursor > 4U) {
                len_class = 3U;
            }

            const std::uint32_t distance = match_buffer[pair_index + 1U];
            const std::int32_t dist_price = price_match_distance(base, distance, len_class);
            const std::int32_t total = lit_state_word + pricing.is_match_xor_price +
                    len_prices[len_slot] + dist_price;
            try_update_parse_node(base, match_length_cursor, static_cast<std::uint32_t>(total),
                    distance + 4U);
            if (match_length_cursor + 1U > frontier_limit) {
                frontier_limit = match_length_cursor + 1U;
            }

            if (match_length_cursor == match_buffer[pair_index] &&
                    (pair_index += 2U) == match_distance_index) {
                break;
            }
            match_length_cursor += 1U;
        }
    }

    backtrack_node = path_length == 1U ? 1U : path_length;
    if (needs_greedy) {
        return true;
    }
    return legacy_codec_encoder_backtrack_prepare(encoder, backtrack_node);
}

LegacyCodecEncoderParseStepResult legacy_codec_encoder_optimal_parse_step(void *encoder,
        std::uint32_t pos_state) {
    LegacyCodecEncoderParseStepResult result{};
    if (encoder == nullptr) {
        result.status = LegacyCodecEncoderParseStepStatus::kError;
        return result;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    if (*reinterpret_cast<std::int32_t *>(base + kLiteralMode) != 0) {
        result.status = LegacyCodecEncoderParseStepStatus::kUnhandledNative;
        return result;
    }

    std::uint32_t main_match_length = 0U;
    std::uint32_t match_distance_index = 0U;
    if (!legacy_codec_encoder_parse_sync_matchfinder(encoder, main_match_length,
                match_distance_index)) {
        result.status = LegacyCodecEncoderParseStepStatus::kUnhandledNative;
        return result;
    }

    const std::uint32_t avail =
            *reinterpret_cast<std::uint32_t *>(base + kMatchfinderAvail);
    if (avail == 0U) {
        result.status = LegacyCodecEncoderParseStepStatus::kUnhandledNative;
        return result;
    }
    if (avail == 1U) {
        result.status = LegacyCodecEncoderParseStepStatus::kApplyNow;
        result.path_length = 1U;
        result.chosen_tag = kEncoderTagLiteral;
        LegacyCodecMatchFinderAccess match_finder{encoder};
        const std::uint8_t *literal_ptr = match_finder.current_literal_ptr();
        const auto processed =
                *reinterpret_cast<const std::int32_t *>(base + kProcessedInBlock);
        if (literal_ptr != nullptr && processed > 0) {
            literal_ptr -= processed;
        }
        const std::int32_t rep0 =
                *reinterpret_cast<const std::int32_t *>(base + kRep0Distance);
        if (literal_ptr != nullptr && rep0 >= 0 &&
                legacy_codec_encoder_block_bytes(encoder) >
                        static_cast<std::uint64_t>(rep0) &&
                literal_ptr[0] == literal_ptr[-static_cast<std::size_t>(rep0 + 1)]) {
            result.chosen_tag = 0U;
        }
        return result;
    }

    std::uint32_t rep_lengths[4] = {};
    if (!legacy_codec_encoder_probe_rep_lengths(encoder, avail, rep_lengths)) {
        result.status = LegacyCodecEncoderParseStepStatus::kError;
        return result;
    }

    const std::uint32_t min_match = kLzmaMinMatchLength;
    const std::uint32_t best_rep_index = legacy_codec_encoder_pick_best_rep_index(rep_lengths);
    std::uint32_t chosen_rep_length = rep_lengths[best_rep_index];
    if (chosen_rep_length == 0U) {
        chosen_rep_length = 1U;
    }
    if (chosen_rep_length >= min_match) {
        if (chosen_rep_length - 1U != 0U) {
            auto *processed = reinterpret_cast<std::int32_t *>(base + kProcessedInBlock);
            *processed += static_cast<std::int32_t>(chosen_rep_length - 1U);
            LegacyCodecMatchFinderAccess match_finder{encoder};
            match_finder.skip(static_cast<std::int32_t>(chosen_rep_length - 1U));
            result.status = LegacyCodecEncoderParseStepStatus::kApplyNow;
            result.path_length = chosen_rep_length;
            result.chosen_tag = best_rep_index;
            return result;
        }
        chosen_rep_length = 1U;
    }

    LegacyCodecEncoderParsePricingContext pricing{};
    std::uint32_t chosen_tag = kEncoderTagLiteral;
    if (!legacy_codec_encoder_parse_write_literal_node(encoder, pos_state, main_match_length,
                match_distance_index, rep_lengths, chosen_rep_length, chosen_tag, pricing)) {
        result.status = LegacyCodecEncoderParseStepStatus::kUnhandledNative;
        return result;
    }

    std::uint32_t backtrack_node = 0U;
    bool needs_greedy = false;
    std::uint32_t frontier_limit = 0U;
    if (!legacy_codec_encoder_parse_price_graph(encoder, pricing, main_match_length,
                match_distance_index, backtrack_node, needs_greedy, frontier_limit)) {
        result.status = LegacyCodecEncoderParseStepStatus::kUnhandledNative;
        return result;
    }

    if (needs_greedy) {
        if (!legacy_codec_encoder_greedy_extend_graph(encoder, frontier_limit, pos_state,
                    backtrack_node)) {
            result.status = LegacyCodecEncoderParseStepStatus::kUnhandledNative;
            return result;
        }
    }

    result.status = LegacyCodecEncoderParseStepStatus::kBacktrackReady;
    result.path_length = backtrack_node;
    result.chosen_tag = chosen_tag;
    result.native_tail_remainder = false;
    return result;
}

}  // namespace kksdk
