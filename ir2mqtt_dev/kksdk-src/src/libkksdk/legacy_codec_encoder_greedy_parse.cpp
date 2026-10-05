#include "legacy_codec_encoder_parse.hpp"

#include "legacy_codec_encoder_blocks.hpp"
#include "legacy_codec_encoder_context.hpp"
#include "legacy_codec_encoder_parse_price_shared.hpp"
#include "legacy_codec_encoder_state.hpp"

#include <algorithm>
#include <cstring>

namespace kksdk {
namespace {

using namespace legacy_oem_encoder;
using namespace legacy_oem_parse;
using namespace legacy_oem_parse_detail;
using namespace legacy_oem_price;

struct GreedyParseSession {
    std::uint32_t current_node = 1U;
    std::uint32_t frontier_base = 0U;
    std::uint32_t horizon_left = kGreedyHorizonMax;
    std::uint32_t frontier_limit = 0U;
    std::uint32_t pos_state = 0U;
};

std::uint32_t clamp_avail(std::uint32_t avail) {
    return avail > kMaxMatchProbe ? kMaxMatchProbe : avail;
}

bool greedy_sync_matchfinder(void *encoder, std::uint32_t &match_length,
        std::uint32_t &distance_index) {
    if (encoder == nullptr) {
        return false;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    LegacyCodecMatchFinderAccess match_finder{encoder};
    void **slots = reinterpret_cast<void **>(encoder);
    const auto get_chunk = reinterpret_cast<int (*)(void *)>(slots[2]);
    const auto find_match = reinterpret_cast<int (*)(void *, void *)>(slots[4]);
    const auto get_buffer = reinterpret_cast<std::uint8_t *(*)(void *)>(slots[3]);
    if (get_chunk == nullptr || find_match == nullptr || get_buffer == nullptr) {
        return false;
    }

    void *const ctx = match_finder.context();
    const std::uint32_t avail = static_cast<std::uint32_t>(get_chunk(ctx));
    *reinterpret_cast<std::uint32_t *>(base + kMatchfinderAvail) = avail;

    distance_index = static_cast<std::uint32_t>(find_match(ctx, base + kMatchBuffer));
    if (distance_index == 0U) {
        match_length = 0U;
    } else {
        const auto *match_buffer = reinterpret_cast<const std::uint32_t *>(base + kMatchBuffer);
        match_length = match_buffer[distance_index - 2U];
        const std::uint32_t min_match =
                kLzmaMinMatchLength;
        if (match_length == min_match) {
            std::uint32_t extended = match_length;
            if (!legacy_codec_encoder_extend_match_length(encoder, distance_index, avail,
                        extended)) {
                extended = match_length;
            }
            match_length = extended;
        } else if (match_length > min_match) {
            const std::uint32_t dist =
                    match_buffer[distance_index - 2U];
            if (dist == min_match) {
                const std::uint8_t *window = get_buffer(ctx);
                const std::uint8_t *cur = window - 1U;
                const std::uint32_t limit = clamp_avail(avail);
                std::uint32_t probe = match_length;
                while (probe < limit) {
                    const std::int32_t rep0 =
                            *reinterpret_cast<const std::int32_t *>(base + kRep0Distance);
                    const std::uint8_t *ref = cur - static_cast<std::size_t>(rep0 + 1);
                    if (cur[probe - 1U] != ref[probe - 1U]) {
                        break;
                    }
                    probe += 1U;
                }
                match_length = probe;
            }
        }
    }

    auto *processed = reinterpret_cast<std::int32_t *>(base + kProcessedInBlock);
    *processed += 1;
    return true;
}

bool greedy_inline_backtrack(void *encoder, std::uint32_t current_node) {
    if (encoder == nullptr) {
        return false;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    std::uint32_t walk_node = *node_parent_index(base, current_node);
    std::uint32_t carry_tag = *node_tag(base, current_node);
    *reinterpret_cast<std::uint32_t *>(base + kParseNodeIndex) = current_node;

    while (true) {
        const std::uint32_t parent = walk_node;
        std::uint32_t *parent_tag_slot = node_tag(base, parent);
        std::uint32_t next_parent = 0U;
        std::uint32_t next_carry = 0U;

        if (*node_flag(base, current_node) == 0U) {
            next_carry = *parent_tag_slot;
            next_parent = *node_parent_index(base, parent);
        } else {
            next_carry = kEncoderTagLiteral;
            next_parent = parent - 1U;
            *parent_tag_slot = kEncoderTagLiteral;
            *node_flag(base, parent) = 0U;
            *node_parent_index(base, parent) = next_parent;
            if (*node_aux_rep(base, current_node) != 0U) {
                *node_flag(base, next_parent) = 0U;
                *node_parent_link(base, next_parent) = *node_parent_link(base, current_node);
                next_carry = kEncoderTagLiteral;
            }
        }

        *parent_tag_slot = carry_tag;
        *node_parent_index(base, parent) = current_node;
        const bool continue_walk = parent != 0U;
        current_node = parent;
        walk_node = next_parent;
        carry_tag = next_carry;
        if (!continue_walk) {
            break;
        }
    }

    const std::uint32_t length = *reinterpret_cast<std::uint32_t *>(base + kRootLengthSlot);
    *reinterpret_cast<std::uint32_t *>(base + kPendingLiteral) = length;
    return true;
}

void greedy_price_normal_matches(void *encoder, std::uint8_t *base, GreedyParseSession &session,
        std::uint32_t current_node, std::uint32_t pos_state, std::int32_t lit_state_word,
        std::int32_t is_match_xor, std::uint32_t anchor_len, std::uint32_t lzma_state);

bool greedy_expand_literal_and_reps(void *encoder, GreedyParseSession &session) {
    auto *base = static_cast<std::uint8_t *>(encoder);
    const std::uint32_t current_node = session.current_node;
    const std::uint32_t lzma_state = greedy_resolve_lzma_state(base, current_node);
    set_node_lzma_state(base, current_node, lzma_state);

    greedy_store_rep_snapshot(base, current_node, current_node);

    const std::uint32_t parent_node = *node_parent_index(base, current_node);
    const std::uint32_t pos_state = (session.pos_state + current_node) &
            *reinterpret_cast<const std::uint32_t *>(base + kPosStateMask);
    const auto *prices = prob_prices(base);

    LegacyCodecMatchFinderAccess match_finder{encoder};
    const std::uint8_t *literal_ptr = match_finder.current_literal_ptr();
    if (literal_ptr == nullptr) {
        return false;
    }

    const std::uint8_t cur_byte = literal_ptr[0];
    const std::int32_t literal_ctx =
            *reinterpret_cast<const std::int32_t *>(base + kRep0Distance);
    const std::uint8_t match_byte = literal_ptr[-static_cast<std::size_t>(literal_ctx + 1)];
    const std::uint8_t prev_byte = literal_ptr[-1];

    std::uint16_t *literal_tree = literal_tree_for(base, pos_state, literal_ptr);
    std::int32_t literal_component = 0;
    if (lzma_state < 7U) {
        literal_component = price_simple_literal(prices, literal_tree, cur_byte);
    } else {
        literal_component = price_matched_literal(prices, literal_tree, cur_byte, match_byte);
    }

    const auto *is_match_probs = reinterpret_cast<const std::uint16_t *>(base + kIsMatchProbs);
    const auto *lit_state_probs = reinterpret_cast<const std::uint16_t *>(base + kLitStateProbs);
    const std::uint16_t is_match_prob = is_match_probs[pos_state * 0x10U + lzma_state];
    const std::int32_t base_price = *node_price(base, current_node);
    const std::int32_t is_match_price = lookup_word(prices, is_match_prob);
    const std::int32_t lit_state_price =
            lookup_word_xor(prices, lit_state_probs[lzma_state]) +
            lookup_nibble_xor(prices, is_match_prob);

    const std::uint32_t literal_node = session.frontier_base + 2U;
    const std::int32_t literal_price = base_price + literal_component + is_match_price;
    bool literal_better = false;
    if (literal_price < static_cast<std::int32_t>(*node_price(base, literal_node))) {
        try_update_parse_node(base, literal_node, static_cast<std::uint32_t>(literal_price),
                kEncoderTagLiteral, session.frontier_limit);
        *node_parent_index(base, literal_node) = current_node;
        literal_better = true;
    }

    const auto *rep0_short_probs =
            reinterpret_cast<const std::uint16_t *>(base + kRep0ShortProbs);
    const auto *rep3_short_probs =
            reinterpret_cast<const std::uint16_t *>(base + kRep3ShortProbs);
    const std::int32_t lit_state_only = lookup_word_xor(prices, lit_state_probs[lzma_state]);
    const std::int32_t rep0_component = lookup_word(prices, rep0_short_probs[lzma_state]);

    std::uint32_t best_literal_price = static_cast<std::uint32_t>(literal_price);
    if (cur_byte == match_byte) {
        const std::int32_t short_total = base_price + literal_component + rep0_component +
                lit_state_only +
                lookup_word(prices, rep3_short_probs[pos_state * 0x10U + lzma_state]);
        if (short_total < static_cast<std::int32_t>(best_literal_price)) {
            best_literal_price = static_cast<std::uint32_t>(short_total);
            try_update_parse_node(base, literal_node, best_literal_price, 0U,
                    session.frontier_limit);
            *node_parent_index(base, literal_node) = current_node;
            literal_better = true;
        }
    }

    const std::uint32_t avail =
            *reinterpret_cast<std::uint32_t *>(base + kMatchfinderAvail);
    std::uint32_t rep_limit = kGreedyHorizonMax - session.frontier_base;
    if (avail <= rep_limit) {
        rep_limit = avail;
    }

    const std::uint32_t min_match = kLzmaMinMatchLength;
    if (rep_limit > 1U && cur_byte != prev_byte && !literal_better) {
        const std::int32_t *rep_len_prices = reinterpret_cast<const std::int32_t *>(base +
                kRepLenPriceCache + pos_state * 0x440U);
        std::uint32_t probe_limit = rep_limit;
        const std::uint32_t main_match =
                kLzmaMinMatchLength;
        if (main_match + 1U <= probe_limit) {
            probe_limit = main_match + 1U;
        }
        if (probe_limit > 1U) {
            std::uint32_t match_len = 1U;
            while (match_len < probe_limit) {
                if (literal_ptr[match_len] !=
                        literal_ptr[match_len - static_cast<std::size_t>(literal_ctx + 1)]) {
                    break;
                }
                match_len += 1U;
            }
            if (match_len > 1U) {
                const std::uint32_t target_node = match_len + literal_node;
                const std::uint32_t next_state =
                        legacy_oem_encoder::match_state_table()[lzma_state];
                const auto *next_lit_probs =
                        reinterpret_cast<const std::uint16_t *>(base + kLitStateProbs);
                const std::int32_t match_total = literal_price +
                        lookup_word_xor(prices, next_lit_probs[next_state]) +
                        rep_len_prices[match_len - 2U];
                if (match_total < static_cast<std::int32_t>(*node_price(base, target_node))) {
                    try_update_parse_node(base, target_node, static_cast<std::uint32_t>(match_total),
                            0U, session.frontier_limit);
                    *node_parent_index(base, target_node) = literal_node;
                    *node_flag(base, target_node) = 1U;
                    set_node_lzma_state(base, target_node, next_state);
                }
            }
        }
    }

    if (rep_limit <= 1U) {
        return true;
    }

    const std::int32_t *rep_len_prices = reinterpret_cast<const std::int32_t *>(base +
            kRepLenPriceCache + pos_state * 0x440U);
    const auto *rep0_short = reinterpret_cast<const std::uint16_t *>(base + kRep0ShortProbs);
    const auto *rep1_short = reinterpret_cast<const std::uint16_t *>(base + kRep1ShortProbs);
    const auto *rep2_short = reinterpret_cast<const std::uint16_t *>(base + kRep2ShortProbs);
    const auto *rep3_short = reinterpret_cast<const std::uint16_t *>(base + kRep3ShortProbs);

  std::uint32_t rep_snapshot[4] = {};
    std::memcpy(rep_snapshot, base + kNodeRepSnapshot +
                    static_cast<std::size_t>(current_node) * kNodeStride,
            sizeof(rep_snapshot));

    for (std::uint32_t rep_index = 0U; rep_index < 4U; ++rep_index) {
        const std::int32_t rep_distance = static_cast<std::int32_t>(rep_snapshot[rep_index]);
        std::uint32_t rep_len = 0U;
        if (rep_distance >= 0) {
            const std::uint8_t *ref = literal_ptr - static_cast<std::size_t>(rep_distance + 1);
            if (*literal_ptr == *ref) {
                if (rep_limit < 3U) {
                    rep_len = 2U;
                } else {
                    rep_len = 2U;
                    while (rep_len < rep_limit) {
                        if (literal_ptr[rep_len - 1U] != ref[rep_len - 1U]) {
                            rep_len += 1U;
                            break;
                        }
                        rep_len += 1U;
                    }
                }
            }
        }

        if (rep_len <= 1U) {
            continue;
        }

        std::int32_t prob_a = 0;
        std::int32_t prob_b = 0;
        if (rep_index == 0U) {
            prob_a = lookup_nibble(prices, rep0_short[lzma_state]);
            prob_b = lookup_word_xor(prices, rep3_short[pos_state * 0x10U + lzma_state]);
        } else if (rep_index == 1U) {
            prob_a = lookup_nibble_xor(prices, rep0_short[lzma_state]);
            prob_b = lookup_nibble(prices, rep1_short[lzma_state]);
        } else {
            prob_a = lookup_nibble_xor(prices, rep0_short[lzma_state]);
            prob_b = lookup_nibble_xor(prices, rep1_short[lzma_state]) + prob_a;
            const std::uint32_t rep2_index =
                    ((2U - rep_index) & 0x7f0U) ^ ((rep2_short[lzma_state] >> 2U) & 0x3ffcU);
            prob_a = lookup_word(prices, rep2_index);
        }

        const std::int32_t rep_base = base_price + lit_state_price;
        std::uint32_t length = rep_len;
        while (length > 1U) {
            const std::int32_t total = rep_base + prob_a + prob_b + rep_len_prices[length - 2U];
            const std::uint32_t target_node = length + current_node;
            try_update_rep_node(base, target_node, static_cast<std::uint32_t>(total),
                    current_node, rep_index, session.frontier_limit);
            length -= 1U;
        }

        if (rep_len + 1U < rep_limit) {
            const std::uint8_t *ref = literal_ptr - static_cast<std::size_t>(rep_distance + 1);
            const std::uint32_t after_rep_pos = rep_len;
            if (literal_ptr[after_rep_pos] == ref[after_rep_pos]) {
                std::uint32_t extend_pos = after_rep_pos + 1U;
                const std::uint32_t extend_limit = rep_limit;
                while (extend_pos < extend_limit) {
                    if (literal_ptr[extend_pos] != ref[extend_pos]) {
                        break;
                    }
                    extend_pos += 1U;
                }
                const std::uint32_t tail_len = extend_pos - after_rep_pos;
                if (tail_len > 1U) {
                    const std::uint32_t tail_node = current_node + rep_len + tail_len;
                    const std::uint32_t match_pos_state =
                            (session.pos_state + current_node + rep_len + 1U) &
                            *reinterpret_cast<const std::uint32_t *>(base + kPosStateMask);
                    const std::uint32_t rep_state =
                            legacy_oem_encoder::match_state_table()[lzma_state];
                    const auto *next_lit_probs =
                            reinterpret_cast<const std::uint16_t *>(base + kLitStateProbs);
                    const auto *next_is_match =
                            reinterpret_cast<const std::uint16_t *>(base + kIsMatchProbs);
                    const std::uint16_t next_is_match_prob =
                            next_is_match[match_pos_state * 0x10U + rep_state];
                    const std::uint32_t literal_state =
                            legacy_oem_encoder::literal_state_table()[rep_state];

                    std::uint16_t *tail_literal_tree = literal_tree_for(base, match_pos_state,
                            literal_ptr + after_rep_pos);
                    const std::int32_t matched_lit_price = price_matched_literal(prices,
                            tail_literal_tree, literal_ptr[after_rep_pos], ref[after_rep_pos]);
                    const std::int32_t tail_total = rep_base + prob_a + prob_b +
                            rep_len_prices[rep_len - 2U] +
                            lookup_word(prices, next_is_match_prob) +
                            lookup_word_xor(prices, next_lit_probs[literal_state]) +
                            matched_lit_price + rep_len_prices[tail_len - 2U];

                    if (tail_total < static_cast<std::int32_t>(*node_price(base, tail_node))) {
                        grow_frontier_prices(base, session.frontier_limit, tail_node + 1U);
                        *node_price(base, tail_node) = static_cast<std::uint32_t>(tail_total);
                        *node_parent_index(base, tail_node) = current_node + rep_len + 1U;
                        *node_tag(base, tail_node) = 0U;
                        *node_flag(base, tail_node) = 1U;
                        *node_aux_rep(base, tail_node) = 1U;
                        *reinterpret_cast<std::uint32_t *>(node_ptr(base, tail_node) + 0x8U) =
                                current_node;
                        *reinterpret_cast<std::uint32_t *>(base + kNodeMatchLenSlot +
                                static_cast<std::size_t>(tail_node) * kNodeStride) = rep_index;
                    }
                }
            }
        }
    }

    (void)parent_node;

    std::uint32_t max_rep_len = 0U;
    for (std::uint32_t rep_index = 0U; rep_index < 4U; ++rep_index) {
        const std::int32_t rep_distance = static_cast<std::int32_t>(rep_snapshot[rep_index]);
        if (rep_distance < 0) {
            continue;
        }
        const std::uint8_t *ref = literal_ptr - static_cast<std::size_t>(rep_distance + 1);
        if (*literal_ptr != *ref) {
            continue;
        }
        std::uint32_t probe_len = 2U;
        while (probe_len < rep_limit) {
            if (literal_ptr[probe_len - 1U] != ref[probe_len - 1U]) {
                break;
            }
            probe_len += 1U;
        }
        if (probe_len > max_rep_len) {
            max_rep_len = probe_len;
        }
    }

    if (max_rep_len >= 2U) {
        const std::int32_t lit_state_word =
                lookup_word(prices,
                        reinterpret_cast<const std::uint16_t *>(base + kLitStateProbs)[lzma_state]);
        greedy_price_normal_matches(encoder, base, session, current_node, pos_state, lit_state_word,
                lookup_nibble_xor(prices, is_match_prob), max_rep_len, lzma_state);
    }

    return true;
}

void greedy_update_match_buffer_anchor(std::uint8_t *base, std::uint32_t anchor_len) {
    auto *match_buffer = reinterpret_cast<std::uint32_t *>(base + kMatchBuffer);
    std::uint32_t pair_index = 0U;
    while (match_buffer[pair_index] < anchor_len) {
        pair_index += 2U;
        if (pair_index > 0x100U) {
            return;
        }
    }
    if (match_buffer[pair_index] > anchor_len) {
        match_buffer[pair_index] = anchor_len;
    }
}

void greedy_try_normal_match_literal_tail(void *encoder, std::uint8_t *base,
        GreedyParseSession &session, std::uint32_t current_node, std::uint32_t pos_state,
        std::int32_t lit_state_word, std::int32_t is_match_xor, std::uint32_t lzma_state,
        std::uint32_t match_length, std::uint32_t distance, const std::uint8_t *literal_ptr,
        std::uint32_t avail_limit) {
    if (match_length < 2U || literal_ptr == nullptr || distance == 0U) {
        return;
    }

    const std::uint8_t *ref = literal_ptr - static_cast<std::size_t>(distance + 1U);
    const std::uint32_t tail_start = match_length;
    if (literal_ptr[tail_start] != ref[tail_start]) {
        return;
    }

    std::uint32_t extend_pos = tail_start + 1U;
    const std::uint32_t min_match = kLzmaMinMatchLength;
    std::uint32_t extend_limit = avail_limit;
    if (extend_limit > min_match + match_length + 1U) {
        extend_limit = min_match + match_length + 1U;
    }
    if (extend_limit > tail_start + 2U) {
        while (extend_pos < extend_limit) {
            if (literal_ptr[extend_pos] != ref[extend_pos]) {
                break;
            }
            extend_pos += 1U;
        }
    }

    const std::uint32_t tail_len = extend_pos - tail_start;
    if (tail_len <= 1U) {
        return;
    }

    const auto *prices = prob_prices(base);
    const auto *len_prices = reinterpret_cast<const std::int32_t *>(base + kLenPriceCache +
            pos_state * 0x440U);
    const std::uint32_t match_pos_state =
            (session.pos_state + current_node + match_length + 1U) &
            *reinterpret_cast<const std::uint32_t *>(base + kPosStateMask);
    const std::uint32_t match_state = legacy_oem_encoder::match_state_table()[lzma_state];
    const auto *next_lit_probs =
            reinterpret_cast<const std::uint16_t *>(base + kLitStateProbs);
    const auto *next_is_match =
            reinterpret_cast<const std::uint16_t *>(base + kIsMatchProbs);
    const std::uint16_t next_is_match_prob =
            next_is_match[match_pos_state * 0x10U + match_state];
    const std::uint32_t literal_state =
            legacy_oem_encoder::literal_state_table()[match_state];

    std::uint16_t *tail_literal_tree = literal_tree_for(base, match_pos_state,
            literal_ptr + tail_start);
    const std::int32_t matched_lit_price = price_matched_literal(prices, tail_literal_tree,
            literal_ptr[tail_start], ref[tail_start]);

    std::uint32_t len_class = match_length - 2U;
    if (match_length > 4U) {
        len_class = 3U;
    }
    const std::int32_t base_total = lit_state_word + is_match_xor +
            len_prices[match_length - 2U] + price_match_distance(base, distance, len_class);
    const std::int32_t tail_total = base_total +
            lookup_word(prices, next_is_match_prob) +
            lookup_word_xor(prices, next_lit_probs[literal_state]) + matched_lit_price +
            len_prices[tail_len - 2U];

    const std::uint32_t tail_node = session.frontier_base + match_length + tail_len + 1U;
    if (tail_total >= static_cast<std::int32_t>(*node_price(base, tail_node))) {
        return;
    }

    grow_frontier_prices(base, session.frontier_limit, tail_node + 1U);
    *node_price(base, tail_node) = static_cast<std::uint32_t>(tail_total);
    *node_parent_index(base, tail_node) = session.frontier_base + match_length + 1U;
    *node_tag(base, tail_node) = 0U;
    *node_flag(base, tail_node) = 1U;
    *node_aux_rep(base, tail_node) = 1U;
    *reinterpret_cast<std::uint32_t *>(node_ptr(base, tail_node) + 0x8U) = current_node;
    *reinterpret_cast<std::uint32_t *>(base + kNodeMatchLenSlot +
            static_cast<std::size_t>(tail_node) * kNodeStride) = distance + 4U;
}

void greedy_price_normal_matches(void *encoder, std::uint8_t *base, GreedyParseSession &session,
        std::uint32_t current_node, std::uint32_t pos_state, std::int32_t lit_state_word,
        std::int32_t is_match_xor, std::uint32_t anchor_len, std::uint32_t lzma_state) {
    if (anchor_len < 2U) {
        return;
    }

    LegacyCodecMatchFinderAccess match_finder{encoder};
    const std::uint8_t *literal_ptr = match_finder.current_literal_ptr();
    if (literal_ptr == nullptr) {
        return;
    }

    const std::uint32_t avail =
            *reinterpret_cast<std::uint32_t *>(base + kMatchfinderAvail);
    std::uint32_t avail_limit = kGreedyHorizonMax - session.frontier_base;
    if (avail <= avail_limit) {
        avail_limit = avail;
    }

    greedy_update_match_buffer_anchor(base, anchor_len);

    auto *match_buffer = reinterpret_cast<std::uint32_t *>(base + kMatchBuffer);
    const auto *len_prices = reinterpret_cast<const std::int32_t *>(base + kLenPriceCache +
            pos_state * 0x440U);

    std::uint32_t pair_index = 0U;
    while (match_buffer[pair_index] < anchor_len) {
        pair_index += 2U;
        if (pair_index > 0x100U) {
            return;
        }
    }

    while (pair_index <= 0x100U) {
        const std::uint32_t pair_length = match_buffer[pair_index];
        const std::uint32_t distance = match_buffer[pair_index + 1U];
        std::uint32_t length_cursor = anchor_len;
        if (length_cursor < pair_length) {
            length_cursor = anchor_len;
        }

        while (length_cursor <= pair_length) {
            const std::uint32_t len_slot = length_cursor - 2U;
            std::uint32_t len_class = len_slot;
            if (length_cursor > 4U) {
                len_class = 3U;
            }

            const std::int32_t dist_price = price_match_distance(base, distance, len_class);
            const std::int32_t total = lit_state_word + is_match_xor + len_prices[len_slot] +
                    dist_price;
            const std::uint32_t target_node = session.frontier_base + length_cursor + 1U;
            if (target_node + 1U > session.frontier_limit) {
                grow_frontier_prices(base, session.frontier_limit, target_node + 1U);
            }
            if (total < static_cast<std::int32_t>(*node_price(base, target_node))) {
                try_update_parse_node(base, target_node, static_cast<std::uint32_t>(total),
                        distance + 4U, session.frontier_limit);
                *node_parent_index(base, target_node) = current_node;
            }
            length_cursor += 1U;
        }

        greedy_try_normal_match_literal_tail(encoder, base, session, current_node, pos_state,
                lit_state_word, is_match_xor, lzma_state, pair_length, distance, literal_ptr,
                avail_limit);

        pair_index += 2U;
        if (pair_index > 0x100U || match_buffer[pair_index] < anchor_len) {
            break;
        }
    }
}

}  // namespace

bool legacy_codec_encoder_greedy_extend_graph(void *encoder, std::uint32_t frontier_limit,
        std::uint32_t pos_state, std::uint32_t &backtrack_node) {
    if (encoder == nullptr || frontier_limit <= 1U) {
        return false;
    }

    GreedyParseSession session{};
    session.frontier_limit = frontier_limit;
    session.pos_state = pos_state;

    const std::uint32_t min_match = kLzmaMinMatchLength;

    while (session.horizon_left != 0U) {
        std::uint32_t match_length = 0U;
        std::uint32_t distance_index = 0U;
        if (!greedy_sync_matchfinder(encoder, match_length, distance_index)) {
            return false;
        }

        if (match_length >= min_match) {
            auto *base = static_cast<std::uint8_t *>(encoder);
            *reinterpret_cast<std::uint32_t *>(base + kParseCachedAvailLength) = match_length;
            *reinterpret_cast<std::uint32_t *>(base + kParseDistanceCode) = distance_index;
            if (!greedy_inline_backtrack(encoder, session.current_node)) {
                return false;
            }
            backtrack_node = session.current_node;
            return true;
        }

        if (!greedy_expand_literal_and_reps(encoder, session)) {
            return false;
        }

        session.current_node += 1U;
        session.horizon_left -= 1U;
        session.frontier_base += 1U;
        if (session.current_node == session.frontier_limit) {
            backtrack_node = session.frontier_limit;
            return legacy_codec_encoder_backtrack_prepare(encoder, backtrack_node);
        }
    }

    backtrack_node = session.frontier_limit;
    return legacy_codec_encoder_backtrack_prepare(encoder, backtrack_node);
}

}  // namespace kksdk
