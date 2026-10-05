#pragma once

#include "legacy_codec_encoder_parse.hpp"
#include "legacy_codec_encoder_context.hpp"
#include "legacy_codec_encoder_price_lookup.hpp"
#include "legacy_codec_encoder_state.hpp"

#include <cstdint>
#include <cstring>

namespace kksdk {
namespace legacy_oem_parse_detail {

using namespace legacy_oem_parse;
using namespace legacy_oem_price;

constexpr std::uint32_t kInfiniteParsePrice = 0x40000000U;
constexpr std::uint32_t kMaxMatchProbe = 0x111U;
constexpr std::uint32_t kGreedyHorizonMax = 0xffeU;

inline std::uint32_t clamp_avail(std::uint32_t avail) {
    return avail > kMaxMatchProbe ? kMaxMatchProbe : avail;
}

constexpr std::size_t kNodeLzmaStateSlot = 0x4e0U;
constexpr std::size_t kNodeRepAuxFlag = 0x4e8U;
constexpr std::size_t kNodeMatchLenSlot = 0x4f0U;
constexpr std::size_t kNodeRepSnapshot = 0x4fcU;

inline const std::int32_t *prob_prices(const std::uint8_t *encoder) {
    return reinterpret_cast<const std::int32_t *>(encoder + legacy_oem_encoder::kProbPrices);
}

inline std::uint16_t *literal_tree_for(std::uint8_t *encoder, std::uint32_t pos_state,
        const std::uint8_t *literal_ptr) {
    const auto tree_root = reinterpret_cast<std::uintptr_t>(
            *reinterpret_cast<void **>(encoder + legacy_oem_encoder::kLiteralTreePrimary));
    if (tree_root == 0U) {
        return nullptr;
    }
    const std::uint32_t ctx_bits =
            *reinterpret_cast<std::uint32_t *>(encoder + legacy_oem_encoder::kLiteralCtxBits);
    const std::uint32_t pos_mask =
            *reinterpret_cast<std::uint32_t *>(encoder + legacy_oem_encoder::kLiteralPosMask);
    std::uint32_t prev_byte = 0U;
    if (*reinterpret_cast<const std::int32_t *>(encoder + legacy_oem_encoder::kLiteralMode) != 0) {
        const std::int32_t processed =
                *reinterpret_cast<const std::int32_t *>(encoder + legacy_oem_encoder::kProcessedInBlock);
        prev_byte = legacy_codec_matchfinder_get_byte(encoder, -1 - processed) & 0xffU;
    } else if (literal_ptr != nullptr) {
        prev_byte = static_cast<std::uint32_t>(literal_ptr[-1]);
    }
    const std::uint32_t context =
            (prev_byte >> (8U - (ctx_bits & 0x1fU))) +
            ((pos_mask & pos_state) << (ctx_bits & 0x1fU));
    return reinterpret_cast<std::uint16_t *>(tree_root + context * 0x300U * 2U);
}

inline std::int32_t price_simple_literal(const std::int32_t *prices, const std::uint16_t *tree,
        std::uint8_t literal_byte) {
    std::uint32_t symbol = static_cast<std::uint32_t>(literal_byte) | 0x100U;
    std::int32_t total = 0;
    while (symbol < 0x10000U) {
        total += legacy_oem_price::lookup_nibble(prices, tree[symbol >> 8U]);
        symbol <<= 1U;
    }
    return total;
}

inline std::int32_t price_matched_literal(const std::int32_t *prices, const std::uint16_t *tree,
        std::uint8_t literal_byte, std::uint8_t match_byte) {
    std::uint32_t symbol = static_cast<std::uint32_t>(literal_byte) | 0x100U;
    std::uint32_t mask = 0x100U;
    std::uint32_t prediction = match_byte;
    std::int32_t total = 0;
    while (symbol < 0x10000U) {
        const std::uint32_t node =
                mask + (symbol >> 8U) + ((prediction << 1U) & mask);
        total += legacy_oem_price::lookup_nibble(prices, tree[node]);
        // Keep optimal/default pricing in lockstep with non-fast matched-literal encoding.
        const std::uint32_t old_symbol = symbol;
        const std::uint32_t old_prediction = prediction;
        symbol <<= 1U;
        prediction <<= 1U;
        mask &= ((old_symbol ^ old_prediction) << 1U) ^ 0xffffffffU;
    }
    return total;
}

inline std::int32_t price_rep0_short(std::uint8_t *encoder, std::uint32_t pos_state,
        std::uint32_t lzma_state) {
    const auto *prices = prob_prices(encoder);
    const auto *lit_state_probs = reinterpret_cast<const std::uint16_t *>(encoder +
            legacy_oem_encoder::kLitStateProbs);
    const auto *rep0_short_probs = reinterpret_cast<const std::uint16_t *>(encoder +
            legacy_oem_encoder::kRep0ShortProbs);
    const auto *rep3_short_probs = reinterpret_cast<const std::uint16_t *>(encoder +
            legacy_oem_encoder::kRep3ShortProbs);

    const std::uint16_t is_match_prob =
            reinterpret_cast<const std::uint16_t *>(encoder + legacy_oem_encoder::kIsMatchProbs)
                    [pos_state * 0x10U + lzma_state];
    const std::int32_t lit_state_component =
            lookup_word_xor(prices, lit_state_probs[lzma_state]) +
            lookup_nibble_xor(prices, is_match_prob);

    return lookup_word(prices, rep0_short_probs[lzma_state]) + lit_state_component +
            lookup_word(prices, rep3_short_probs[pos_state * 0x10U + lzma_state]);
}

inline std::int32_t price_match_distance(const std::uint8_t *encoder, std::uint32_t distance,
        std::uint32_t len_class) {
    const auto *prices = prob_prices(encoder);
    if (distance < 0x80U) {
        const auto *table = reinterpret_cast<const std::int32_t *>(encoder +
                legacy_oem_encoder::kMatchDistPriceShort);
        return table[distance + len_class * 0x200U];
    }

    const std::int32_t shift =
            static_cast<std::int32_t>((static_cast<std::int32_t>(0x7ffffU - distance) >> 31) &
                    0xc) +
            6;
    const std::uint32_t slot =
            encoder[legacy_oem_encoder::kPosSlotTable +
                    (distance >> static_cast<std::uint32_t>(shift))] +
            static_cast<std::uint32_t>(shift) * 2U;
    const auto *slot_prices = reinterpret_cast<const std::int32_t *>(encoder +
            legacy_oem_encoder::kMatchPosSlotPrice);
    const auto *align_prices = reinterpret_cast<const std::int32_t *>(encoder +
            legacy_oem_encoder::kAlignPriceCache);
    return slot_prices[slot + len_class * 0x100U] + align_prices[distance & 0xfU];
}

inline void init_infinite_path_prices(std::uint8_t *encoder, std::uint32_t path_length) {
    for (std::uint32_t node_index = 2U; node_index < path_length; ++node_index) {
        *node_price(encoder, node_index) = kInfiniteParsePrice;
    }
}

inline void init_infinite_nodes_pair(std::uint8_t *encoder, std::uint32_t from_node,
        std::uint32_t to_node_exclusive) {
    if (to_node_exclusive <= from_node + 1U) {
        if (from_node < to_node_exclusive) {
            *node_price(encoder, from_node) = kInfiniteParsePrice;
        }
        return;
    }

    std::uint32_t cursor = from_node;
    const std::uint32_t paired_end = to_node_exclusive & ~1U;
    while (cursor + 1U < paired_end) {
        auto *slot_a = reinterpret_cast<std::uint32_t *>(encoder + 0x50cU +
                static_cast<std::size_t>(cursor) * kNodeStride);
        auto *slot_b = reinterpret_cast<std::uint32_t *>(encoder + 0x53cU +
                static_cast<std::size_t>(cursor) * kNodeStride);
        *slot_a = kInfiniteParsePrice;
        *slot_b = kInfiniteParsePrice;
        cursor += 2U;
    }
    while (cursor < to_node_exclusive) {
        *reinterpret_cast<std::uint32_t *>(encoder + 0x50cU +
                static_cast<std::size_t>(cursor) * kNodeStride) = kInfiniteParsePrice;
        cursor += 1U;
    }
}

inline void grow_frontier_prices(std::uint8_t *encoder, std::uint32_t &frontier_limit,
        std::uint32_t target_node) {
    if (target_node <= frontier_limit) {
        return;
    }

    if (frontier_limit + 1U < target_node) {
        init_infinite_nodes_pair(encoder, frontier_limit, target_node);
    } else {
        *node_price(encoder, frontier_limit) = kInfiniteParsePrice;
    }
    frontier_limit = target_node;
}

inline void try_update_parse_node(std::uint8_t *encoder, std::uint32_t node_index,
        std::uint32_t price, std::uint32_t tag, std::uint32_t &frontier_limit) {
    if (price >= *node_price(encoder, node_index)) {
        return;
    }

    grow_frontier_prices(encoder, frontier_limit, node_index + 1U);
    *node_price(encoder, node_index) = price;
    *node_parent_index(encoder, node_index) = 0U;
    *node_tag(encoder, node_index) = tag;
    *node_flag(encoder, node_index) = 0U;
}

inline void try_update_rep_node(std::uint8_t *encoder, std::uint32_t node_index,
        std::uint32_t price, std::uint32_t parent_node, std::uint32_t rep_index,
        std::uint32_t &frontier_limit) {
    if (price >= *node_price(encoder, node_index)) {
        return;
    }

    grow_frontier_prices(encoder, frontier_limit, node_index + 1U);
    *node_price(encoder, node_index) = price;
    *node_parent_index(encoder, node_index) = parent_node;
    *node_tag(encoder, node_index) = rep_index;
    *node_flag(encoder, node_index) = 0U;
}

inline void try_update_match_node(std::uint8_t *encoder, std::uint32_t node_index,
        std::uint32_t price, std::uint32_t parent_node, std::uint32_t rep_index,
        std::uint32_t aux_parent, std::uint32_t &frontier_limit) {
    if (price >= *node_price(encoder, node_index)) {
        return;
    }

    grow_frontier_prices(encoder, frontier_limit, node_index + 1U);
    *node_price(encoder, node_index) = price;
    *node_parent_index(encoder, node_index) = parent_node;
    *node_tag(encoder, node_index) = 0U;
    *node_flag(encoder, node_index) = 1U;
    *node_aux_rep(encoder, node_index) = 1U;
    *reinterpret_cast<std::uint32_t *>(node_ptr(encoder, node_index) + 0x8U) = aux_parent;
    *reinterpret_cast<std::uint32_t *>(encoder + kNodeMatchLenSlot +
            static_cast<std::size_t>(node_index) * kNodeStride) = rep_index;
}

inline std::uint32_t node_lzma_state(const std::uint8_t *encoder, std::uint32_t node_index) {
    return *reinterpret_cast<const std::uint32_t *>(encoder + kNodeLzmaStateSlot +
            static_cast<std::size_t>(node_index) * kNodeStride);
}

inline void set_node_lzma_state(std::uint8_t *encoder, std::uint32_t node_index,
        std::uint32_t state) {
    *reinterpret_cast<std::uint32_t *>(encoder + kNodeLzmaStateSlot +
            static_cast<std::size_t>(node_index) * kNodeStride) = state;
}

inline std::uint32_t greedy_resolve_lzma_state(const std::uint8_t *encoder,
        std::uint32_t current_node) {
    auto *mutable_encoder = const_cast<std::uint8_t *>(encoder);
    const std::uint32_t flag = *node_flag(mutable_encoder, current_node);
    std::uint32_t parent = *node_parent_index(mutable_encoder, current_node);
    std::uint32_t state = 0U;

    if (flag == 0U) {
        state = node_lzma_state(encoder, parent);
    } else {
        parent -= 1U;
        const std::uint32_t aux_flag = *reinterpret_cast<const std::uint32_t *>(encoder +
                kNodeRepAuxFlag + static_cast<std::size_t>(current_node) * kNodeStride);
        if (aux_flag == 0U) {
            state = node_lzma_state(encoder, parent);
        } else {
            const std::uint32_t match_len = *reinterpret_cast<const std::uint32_t *>(encoder +
                    kNodeMatchLenSlot + static_cast<std::size_t>(current_node) * kNodeStride);
            const std::uint32_t parent_state = node_lzma_state(encoder, parent);
            const std::uint32_t *transition = match_len < 4U ? legacy_oem_encoder::rep_state_table()
                                                             : legacy_oem_encoder::rep0_long_state_table();
            state = transition[parent_state];
        }
        state = legacy_oem_encoder::literal_state_table()[state];
    }
    return state;
}

inline void greedy_store_rep_snapshot(std::uint8_t *encoder, std::uint32_t node_index,
        std::uint32_t current_node) {
    const std::uint32_t flag = *node_flag(encoder, current_node);
    std::uint32_t parent = *node_parent_index(encoder, current_node);
    std::uint8_t *node_base = encoder + kNodeRepSnapshot +
            static_cast<std::size_t>(node_index) * kNodeStride;

    if (flag == 0U) {
        std::memcpy(node_base, encoder + kNodeRepSnapshot +
                        static_cast<std::size_t>(parent) * kNodeStride,
                16U);
        return;
    }

    parent -= 1U;
    const std::uint32_t aux_flag = *reinterpret_cast<std::uint32_t *>(encoder + kNodeRepAuxFlag +
            static_cast<std::size_t>(current_node) * kNodeStride);
    if (aux_flag == 0U) {
        std::memcpy(node_base, encoder + kNodeRepSnapshot +
                        static_cast<std::size_t>(parent) * kNodeStride,
                16U);
        return;
    }

    const std::uint32_t rep_len = *reinterpret_cast<std::uint32_t *>(encoder + kNodeMatchLenSlot +
            static_cast<std::size_t>(current_node) * kNodeStride);
    if (rep_len < 4U) {
        auto *rep_words = reinterpret_cast<std::uint32_t *>(encoder + kNodeRepSnapshot +
                static_cast<std::size_t>(parent) * kNodeStride);
        for (std::uint32_t index = 0U; index < rep_len; ++index) {
            reinterpret_cast<std::uint32_t *>(node_base)[index] = rep_words[index];
        }
        if (rep_len != 0U) {
            const std::uint32_t tail_words = 4U - rep_len;
            std::memcpy(node_base + rep_len * 4U,
                    encoder + kNodeRepSnapshot + static_cast<std::size_t>(parent) * kNodeStride +
                            rep_len * 4U,
                    tail_words * 4U);
        }
        return;
    }

    *reinterpret_cast<std::uint32_t *>(node_base) = rep_len - 4U;
    std::memcpy(node_base + 4U,
            encoder + kNodeRepSnapshot + static_cast<std::size_t>(parent) * kNodeStride,
            8U);
}

}  // namespace legacy_oem_parse_detail
}  // namespace kksdk
