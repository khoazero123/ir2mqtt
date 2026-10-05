#include "legacy_codec_encoder_blocks.hpp"

#include "legacy_codec_encoder_price_lookup.hpp"
#include "legacy_codec_encoder_state.hpp"
#include "legacy_codec_range_encoder.hpp"

#include <cstring>

namespace kksdk {
namespace {

using namespace legacy_oem_price;

}  // namespace

void legacy_codec_encoder_refresh_len_price_slot(std::uint16_t *len_encoder,
        std::uint32_t pos_state, const std::int32_t *price_table) {
    if (len_encoder == nullptr || price_table == nullptr) {
        return;
    }

    auto *base = reinterpret_cast<std::uint8_t *>(len_encoder);
    const std::uint32_t slot_count =
            *reinterpret_cast<std::uint32_t *>(base + legacy_oem_encoder::kLenPriceSlotCount);
    if (slot_count == 0U) {
        return;
    }

    const std::uint32_t pos_shift = pos_state << 3U;
    const std::uint32_t prob_a = len_encoder[pos_shift + 4U] >> 4U;
    const std::uint32_t prob_b = (len_encoder[pos_shift + 6U] >> 2U) & 0x3ffcU;
    const std::uint32_t prob_c = len_encoder[pos_shift + 3U] >> 4U;

    const std::int32_t base_price = lookup_nibble(price_table, len_encoder[0]);
    const std::int32_t choice_price = lookup_nibble(price_table, len_encoder[1]);
    const std::int32_t choice_xor = lookup_nibble_xor(price_table, len_encoder[0]);
    const std::int32_t choice2_xor = lookup_nibble_xor(price_table, len_encoder[1]);

    auto *slot_prices = reinterpret_cast<std::int32_t *>(base + 0x404U + pos_state * 0x440U);
    slot_prices[0] = lookup_nibble(price_table, prob_c) +
            lookup_nibble(price_table, prob_a) + lookup_word(price_table, prob_b) +
            base_price;

    if (slot_count > 1U) {
        slot_prices[1] = lookup_nibble(price_table, prob_c) +
                lookup_nibble(price_table, prob_a) +
                lookup_word_xor(price_table, prob_b) + base_price;
    }
    if (slot_count > 2U) {
        const std::uint32_t prob_d = (len_encoder[pos_shift + 7U] >> 2U) & 0x3ffcU;
        slot_prices[2] = lookup_nibble(price_table, prob_c) +
                lookup_nibble_xor(price_table, prob_a) +
                lookup_word(price_table, prob_d) + base_price;
    }
    if (slot_count > 3U) {
        const std::uint32_t prob_d = (len_encoder[pos_shift + 7U] >> 2U) & 0x3ffcU;
        slot_prices[3] = lookup_nibble(price_table, prob_c) +
                lookup_nibble_xor(price_table, prob_a) +
                lookup_word_xor(price_table, prob_d) + base_price;
    }
    if (slot_count > 4U) {
        const std::uint32_t prob_e = len_encoder[pos_shift + 5U] >> 4U;
        const std::uint32_t prob_f = (len_encoder[pos_shift + 8U] >> 2U) & 0x3ffcU;
        slot_prices[4] = lookup_nibble_xor(price_table, prob_c) +
                lookup_nibble(price_table, prob_e) + lookup_word(price_table, prob_f) +
                base_price;
    }
    if (slot_count > 5U) {
        const std::uint32_t prob_e = len_encoder[pos_shift + 5U] >> 4U;
        const std::uint32_t prob_f = (len_encoder[pos_shift + 8U] >> 2U) & 0x3ffcU;
        slot_prices[5] = lookup_nibble_xor(price_table, prob_c) +
                lookup_nibble(price_table, prob_e) +
                lookup_word_xor(price_table, prob_f) + base_price;
    }
    if (slot_count > 6U) {
        const std::uint32_t prob_e = len_encoder[pos_shift + 5U] >> 4U;
        const std::uint32_t prob_g = (len_encoder[pos_shift + 9U] >> 2U) & 0x3ffcU;
        slot_prices[6] = lookup_nibble_xor(price_table, prob_c) +
                lookup_nibble_xor(price_table, prob_e) +
                lookup_word(price_table, prob_g) + base_price;
    }
    if (slot_count > 7U) {
        const std::uint32_t prob_e = len_encoder[pos_shift + 5U] >> 4U;
        const std::uint32_t prob_g = (len_encoder[pos_shift + 9U] >> 2U) & 0x3ffcU;
        slot_prices[7] = lookup_nibble_xor(price_table, prob_c) +
                lookup_nibble_xor(price_table, prob_e) +
                lookup_word_xor(price_table, prob_g) + base_price;
    }

    for (std::uint32_t slot = 8U; slot < slot_count; ++slot) {
        const std::uint32_t high_shift = slot - 8U;
        const std::uint32_t high_mask = high_shift | 8U;
        std::int32_t high_price = 0;
        std::uint32_t probe = high_mask;
        std::uint32_t parent = probe;
        do {
            parent = probe >> 1U;
            high_price += lookup_nibble(price_table,
                    *reinterpret_cast<std::uint16_t *>(base + 0x208U + parent * 2U +
                            pos_shift * 2U));
            probe = parent;
        } while (parent != 1U);
        slot_prices[slot] = choice_price + choice_xor + high_price;
    }

    for (std::uint32_t slot = 0x10U; slot < slot_count; ++slot) {
        const std::uint32_t high_shift = slot - 0x10U;
        const std::uint32_t high_mask = high_shift | 0x100U;
        std::int32_t high_price = 0;
        std::uint32_t probe = high_mask;
        std::uint32_t parent = probe;
        do {
            parent = probe >> 1U;
            high_price += lookup_nibble(price_table,
                    *reinterpret_cast<std::uint16_t *>(base + 0x408U + parent * 2U));
            probe = parent;
        } while (parent != 1U);
        slot_prices[slot] = choice2_xor + choice_xor + high_price;
    }

    *reinterpret_cast<std::uint32_t *>(base + 0x2404U + pos_state * 2U) = slot_count;
}

void legacy_codec_encoder_refresh_align_prices(void *encoder) {
    if (encoder == nullptr) {
        return;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    const auto *prices = reinterpret_cast<const std::int32_t *>(base + legacy_oem_encoder::kProbPrices);
    const auto *align_probs = reinterpret_cast<const std::uint16_t *>(base +
            legacy_oem_encoder::kAlignProbs);
    const std::uint16_t root_prob = align_probs[1];
    auto *cache = reinterpret_cast<std::int32_t *>(base + legacy_oem_encoder::kAlignPriceCache);

    for (std::uint32_t index = 0U; index < 0x10U; ++index) {
        const std::uint32_t bit0 = index & 1U;
        const std::uint32_t bit1 = (index >> 1U) & 1U;
        const std::uint32_t bit2 = (index >> 2U) & 1U;
        const std::uint32_t bit3 = (index >> 3U) & 1U;
        const std::uint32_t node_mid = bit1 | ((bit0 | 2U) << 1U);
        const std::uint32_t prob_word = align_probs[bit2 | (node_mid << 1U)];
        const std::uint32_t prob_mid = align_probs[node_mid];
        const std::uint32_t prob_leaf = align_probs[bit0 | 2U];

        const auto masked_word = [&](std::uint32_t prob, std::uint32_t mask_bit) {
            return prices[(((mask_bit != 0U ? 0x7f0U : 0U) ^ prob) >> 2U) & 0x3ffcU];
        };
        const auto masked_nibble = [&](std::uint32_t prob, std::uint32_t mask_bit) {
            return prices[(((mask_bit != 0U ? 0x7f0U : 0U) ^ prob) >> 4U) & 0x3ffU];
        };

        cache[index] = masked_word(prob_word, bit3) + masked_nibble(prob_mid, bit2) +
                masked_nibble(prob_leaf, bit1) + masked_nibble(root_prob, bit0);
    }

    *reinterpret_cast<std::uint32_t *>(base + legacy_oem_encoder::kAlignPriceCounter) = 0U;
}

namespace {

using namespace legacy_oem_encoder;

std::int32_t prob_path_price(const std::int32_t *prices, std::uint16_t prob, std::uint32_t bit) {
    return prices[(((bit != 0U ? 0x7f0U : 0U) ^ prob) >> 4U) & 0x3ffU];
}

std::int32_t pos_slot_high_price(const std::int32_t *prices, const std::uint16_t *high_probs,
        std::uint8_t slot_code, std::uint32_t slot_index) {
    const std::uint32_t levels = (slot_code >> 1U) - 1U;
    if (levels == 0U) {
        return 0;
    }

    const std::uint32_t base_offset = (slot_code & 1U) | 2U;
    const std::uint32_t tree_base = base_offset << (levels & 0x1fU);
    std::int32_t total = 0;
    std::uint32_t path = 1U;
    std::uint32_t remainder = slot_index - tree_base;
    for (std::uint32_t level = levels; level != 0U; --level) {
        const std::uint32_t prob_index =
                tree_base - slot_code + path;
        const std::uint16_t prob = high_probs[prob_index];
        total += prob_path_price(prices, prob, remainder & 1U);
        path = (remainder & 1U) | (path << 1U);
        remainder >>= 1U;
    }
    return total;
}

std::int32_t pos_slot_tree_price(const std::int32_t *prices, const std::uint16_t *slot_probs,
        std::uint32_t slot) {
    std::int32_t total = 0;
    std::uint32_t index = slot | 0x40U;
    for (;;) {
        const std::uint32_t parent = index >> 1U;
        const std::uint16_t prob = slot_probs[parent << 1U];
        total += prob_path_price(prices, prob, index & 1U);
        if (parent == 1U) {
            break;
        }
        index = parent;
    }
    return total;
}

}  // namespace

void legacy_codec_encoder_refresh_match_prices(void *encoder) {
    if (encoder == nullptr) {
        return;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    const auto *prices = reinterpret_cast<const std::int32_t *>(base + kProbPrices);
    const auto *slot_table = reinterpret_cast<const std::uint8_t *>(base + kPosSlotTable);
    const auto *remap_table = reinterpret_cast<const std::uint8_t *>(base + kPosSlotRemapTable);
    const auto *high_probs = reinterpret_cast<const std::uint16_t *>(base + kPosSlotHighProbs);

    std::int32_t high_prices[0x7c] = {};
    for (std::uint32_t slot = 4U; slot < 0x80U; ++slot) {
        high_prices[slot - 4U] = pos_slot_high_price(prices, high_probs, slot_table[slot], slot);
    }

    const std::uint32_t slot_count =
            *reinterpret_cast<std::uint32_t *>(base + kPosSlotPriceCount);
    auto *high_adjust = reinterpret_cast<std::int32_t *>(base + kPosSlotHighPriceAdjust);

    for (std::uint32_t pos_state = 0U; pos_state < 4U; ++pos_state) {
        const auto *state_slot_probs = reinterpret_cast<const std::uint16_t *>(base +
                kPosSlotProbs + pos_state * 0x80U);
        auto *slot_prices = reinterpret_cast<std::int32_t *>(base + kMatchPosSlotPrice +
                pos_state * 0x100U);

        if (slot_count != 0U) {
            for (std::uint32_t slot = 0U; slot < slot_count; ++slot) {
                slot_prices[slot] = pos_slot_tree_price(prices, state_slot_probs, slot);
            }

            if (slot_count > 0xeU) {
                for (std::uint32_t high_slot = 0U; high_slot + 0xfU < slot_count; ++high_slot) {
                    high_adjust[high_slot] = static_cast<std::int32_t>(
                            ((high_slot * 8U + 0x70U) & 0xfffffff0U) +
                            static_cast<std::uint32_t>(high_adjust[high_slot]) - 0x50U);
                }
            }
        }

        std::uint8_t *slot_row = base + kMatchPosSlotPrice + pos_state * 0x100U;
        std::uint8_t *dist_row = base + pos_state * 0x200U;
        std::memcpy(dist_row + kMatchDistPriceShort, slot_row, sizeof(std::uint64_t));
        std::memcpy(dist_row + kMatchDistPriceShortAux, slot_row + 8U, sizeof(std::uint64_t));

        auto *dist_prices = reinterpret_cast<std::int32_t *>(dist_row + kMatchDistPrice);
        const auto *slot_price_row = reinterpret_cast<const std::int32_t *>(slot_row);
        for (std::uint32_t index = 0U; index < 0x7cU; ++index) {
            const std::uint8_t remap = remap_table[index];
            dist_prices[index] = high_prices[index] + slot_price_row[remap];
        }

        high_adjust += 0x100U / sizeof(std::int32_t);
    }

    *reinterpret_cast<std::uint32_t *>(base + kMatchEncodeCount) = 0U;
}

void legacy_codec_encoder_refresh_price_caches(void *encoder) {
    if (encoder == nullptr) {
        return;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    if (*reinterpret_cast<std::int32_t *>(base + kLiteralMode) == 0) {
        legacy_codec_encoder_refresh_match_prices(encoder);
        legacy_codec_encoder_refresh_align_prices(encoder);
    }

    const std::int32_t fast_bytes =
            *reinterpret_cast<std::int32_t *>(base + kFastBytes);
    const std::int32_t refresh_limit = fast_bytes - 1;
    *reinterpret_cast<std::int32_t *>(base + kLenPriceRefreshLimit) = refresh_limit;
    *reinterpret_cast<std::int32_t *>(base + kRepLenPriceRefreshLimit) = refresh_limit;

    const std::uint32_t pos_state_count =
            1U << (*reinterpret_cast<std::uint32_t *>(base + kPosStateBits) & 0x1fU);
    const auto *price_table = reinterpret_cast<const std::int32_t *>(base + kProbPrices);
    auto *len_encoder = reinterpret_cast<std::uint16_t *>(base + kLenEncBase);
    auto *rep_len_encoder = reinterpret_cast<std::uint16_t *>(base + kRepLenEncBase);

    for (std::uint32_t pos_state = 0U; pos_state < pos_state_count; ++pos_state) {
        legacy_codec_encoder_refresh_len_price_slot(len_encoder, pos_state, price_table);
        legacy_codec_encoder_refresh_len_price_slot(rep_len_encoder, pos_state, price_table);
    }
}

}  // namespace kksdk
