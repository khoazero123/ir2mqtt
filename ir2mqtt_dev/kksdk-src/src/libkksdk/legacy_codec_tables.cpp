#include "legacy_codec_tables.hpp"

#include <algorithm>

namespace kksdk {

LegacyCodecLenSlotTable legacy_codec_make_len_slot_table() {
    LegacyCodecLenSlotTable table{};
    table[0] = 0x00;
    table[1] = 0x01;

    std::size_t offset = 2;
    for (std::uint8_t symbol = 2; symbol != 0x1a; ++symbol) {
        const std::size_t repeat_count =
                std::size_t{1} << (((symbol >> 1U) - 1U) & 0x1fU);
        std::fill_n(table.begin() + static_cast<std::ptrdiff_t>(offset),
                repeat_count, symbol);
        offset += repeat_count;
    }
    return table;
}

std::size_t legacy_codec_fill_len_slot_table(std::uint8_t *out, std::size_t out_size) {
    if (out == nullptr || out_size == 0) {
        return 0;
    }

    const LegacyCodecLenSlotTable table = legacy_codec_make_len_slot_table();
    const std::size_t amount = std::min(out_size, table.size());
    std::copy_n(table.data(), amount, out);
    return amount;
}

static std::uint32_t normalize_square(std::uint32_t &value, int &shift_count) {
    if (value > 0xffffU) {
        do {
            const std::uint32_t overflow = value >> 17U;
            value >>= 1U;
            ++shift_count;
            if (overflow == 0) {
                break;
            }
        } while (true);
    }
    value *= value;
    shift_count <<= 1;
    return value;
}

LegacyCodecPriceTable legacy_codec_make_price_table() {
    LegacyCodecPriceTable table{};
    for (std::uint32_t value = 8; value < 0x800U; value += 0x10U) {
        std::uint32_t squared = value * value;
        int shift_count = 0;
        normalize_square(squared, shift_count);
        normalize_square(squared, shift_count);
        normalize_square(squared, shift_count);
        if (squared > 0xffffU) {
            do {
                const std::uint32_t overflow = squared >> 17U;
                squared >>= 1U;
                ++shift_count;
                if (overflow == 0) {
                    break;
                }
            } while (true);
        }

        const std::size_t index = (value >> 4U) & 0x7fU;
        table[index] = 0xa1U - static_cast<std::uint32_t>(shift_count);
    }
    return table;
}

std::size_t legacy_codec_fill_price_table(std::uint32_t *out, std::size_t count) {
    if (out == nullptr || count == 0) {
        return 0;
    }

    const LegacyCodecPriceTable table = legacy_codec_make_price_table();
    const std::size_t amount = std::min(count, table.size());
    std::copy_n(table.data(), amount, out);
    return amount;
}

std::uint32_t legacy_codec_bit_price(std::uint16_t probability, bool bit,
        const LegacyCodecPriceTable &prices) {
    const std::uint32_t selector = bit ? 0x7f0U : 0U;
    const std::uint32_t index = ((selector ^ probability) >> 4U) & 0x7fU;
    return prices[index];
}

std::uint32_t legacy_codec_bit_tree_price(const std::uint16_t *probabilities,
        std::uint32_t symbol, std::uint32_t bit_count, const LegacyCodecPriceTable &prices) {
    if (probabilities == nullptr || bit_count == 0) {
        return 0;
    }

    std::uint32_t price = 0;
    std::uint32_t node = 1;
    for (std::uint32_t bit_index = bit_count; bit_index != 0; --bit_index) {
        const bool bit = ((symbol >> (bit_index - 1U)) & 1U) != 0;
        price += legacy_codec_bit_price(probabilities[node], bit, prices);
        node = (node << 1U) | static_cast<std::uint32_t>(bit);
    }
    return price;
}

std::uint32_t legacy_codec_reverse_bit_tree_price(const std::uint16_t *probabilities,
        std::uint32_t symbol, std::uint32_t bit_count, const LegacyCodecPriceTable &prices) {
    if (probabilities == nullptr || bit_count == 0) {
        return 0;
    }

    std::uint32_t price = 0;
    std::uint32_t node = 1;
    for (std::uint32_t bit_index = 0; bit_index < bit_count; ++bit_index) {
        const bool bit = ((symbol >> bit_index) & 1U) != 0;
        price += legacy_codec_bit_price(probabilities[node], bit, prices);
        node = (node << 1U) | static_cast<std::uint32_t>(bit);
    }
    return price;
}

std::uint32_t legacy_codec_direct_bits_price(std::uint32_t bit_count) {
    return bit_count << 4U;
}

std::uint32_t legacy_codec_literal_price(const std::uint16_t *probabilities,
        std::uint8_t symbol, const LegacyCodecPriceTable &prices) {
    if (probabilities == nullptr) {
        return 0;
    }

    std::uint32_t price = 0;
    std::uint32_t node = 1;
    for (int bit_index = 7; bit_index >= 0; --bit_index) {
        const bool bit = ((symbol >> static_cast<unsigned int>(bit_index)) & 1U) != 0;
        price += legacy_codec_bit_price(probabilities[node], bit, prices);
        node = (node << 1U) | static_cast<std::uint32_t>(bit);
    }
    return price;
}

std::uint32_t legacy_codec_matched_literal_price(const std::uint16_t *probabilities,
        std::uint8_t symbol, std::uint8_t match_byte, const LegacyCodecPriceTable &prices) {
    if (probabilities == nullptr) {
        return 0;
    }

    std::uint32_t price = 0;
    std::uint32_t node = 1;
    bool matched = true;
    for (int bit_index = 7; bit_index >= 0; --bit_index) {
        const bool bit = ((symbol >> static_cast<unsigned int>(bit_index)) & 1U) != 0;
        const bool match_bit =
                ((match_byte >> static_cast<unsigned int>(bit_index)) & 1U) != 0;
        const std::uint32_t probability_index =
                matched ? (0x100U + (static_cast<std::uint32_t>(match_bit) << 8U) + node) :
                node;
        price += legacy_codec_bit_price(probabilities[probability_index], bit, prices);
        node = (node << 1U) | static_cast<std::uint32_t>(bit);
        matched = matched && (bit == match_bit);
    }
    return price;
}

LegacyCodecAlignPriceTable legacy_codec_make_align_price_table(
        const std::uint16_t *probabilities, const LegacyCodecPriceTable &prices) {
    LegacyCodecAlignPriceTable table{};
    if (probabilities == nullptr) {
        return table;
    }

    for (std::uint32_t symbol = 0; symbol < table.size(); ++symbol) {
        table[symbol] = legacy_codec_reverse_bit_tree_price(probabilities, symbol, 4, prices);
    }
    return table;
}

std::size_t legacy_codec_fill_align_price_table(const std::uint16_t *probabilities,
        std::uint32_t *out, std::size_t count, const LegacyCodecPriceTable &prices) {
    if (out == nullptr || count == 0) {
        return 0;
    }

    const LegacyCodecAlignPriceTable table =
            legacy_codec_make_align_price_table(probabilities, prices);
    const std::size_t amount = std::min(count, table.size());
    std::copy_n(table.data(), amount, out);
    return amount;
}

LegacyCodecCrcTable legacy_codec_make_crc_table() {
    LegacyCodecCrcTable table{};
    for (std::uint32_t value = 0; value < table.size(); ++value) {
        std::uint32_t crc = value;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1U) ^ ((crc & 1U) != 0 ? 0xedb88320U : 0U);
        }
        table[value] = crc;
    }
    return table;
}

std::size_t legacy_codec_fill_crc_table(std::uint32_t *out, std::size_t count) {
    if (out == nullptr || count == 0) {
        return 0;
    }

    const LegacyCodecCrcTable table = legacy_codec_make_crc_table();
    const std::size_t amount = std::min(count, table.size());
    std::copy_n(table.data(), amount, out);
    return amount;
}

}  // namespace kksdk

extern "C" unsigned long long kksdk_legacy_codec_fill_len_slot_table(
        unsigned char *out, unsigned long long out_size) {
    return static_cast<unsigned long long>(kksdk::legacy_codec_fill_len_slot_table(
            out, static_cast<std::size_t>(out_size)));
}

extern "C" unsigned long long kksdk_legacy_codec_fill_price_table(
        unsigned int *out, unsigned long long count) {
    return static_cast<unsigned long long>(kksdk::legacy_codec_fill_price_table(
            out, static_cast<std::size_t>(count)));
}

static kksdk::LegacyCodecPriceTable load_price_table(const unsigned int *prices,
        unsigned long long price_count) {
    if (prices == nullptr || price_count < 128) {
        return kksdk::legacy_codec_make_price_table();
    }

    kksdk::LegacyCodecPriceTable table{};
    std::copy_n(prices, table.size(), table.begin());
    return table;
}

extern "C" unsigned int kksdk_legacy_codec_bit_price(unsigned short probability,
        int bit, const unsigned int *prices, unsigned long long price_count) {
    const kksdk::LegacyCodecPriceTable table = load_price_table(prices, price_count);
    return kksdk::legacy_codec_bit_price(probability, bit != 0, table);
}

extern "C" unsigned int kksdk_legacy_codec_bit_tree_price(const unsigned short *probabilities,
        unsigned int symbol, unsigned int bit_count, const unsigned int *prices,
        unsigned long long price_count) {
    const kksdk::LegacyCodecPriceTable table = load_price_table(prices, price_count);
    return kksdk::legacy_codec_bit_tree_price(probabilities, symbol, bit_count, table);
}

extern "C" unsigned int kksdk_legacy_codec_reverse_bit_tree_price(
        const unsigned short *probabilities, unsigned int symbol, unsigned int bit_count,
        const unsigned int *prices, unsigned long long price_count) {
    const kksdk::LegacyCodecPriceTable table = load_price_table(prices, price_count);
    return kksdk::legacy_codec_reverse_bit_tree_price(probabilities, symbol, bit_count, table);
}

extern "C" unsigned int kksdk_legacy_codec_direct_bits_price(unsigned int bit_count) {
    return kksdk::legacy_codec_direct_bits_price(bit_count);
}

extern "C" unsigned int kksdk_legacy_codec_literal_price(const unsigned short *probabilities,
        unsigned int symbol, const unsigned int *prices, unsigned long long price_count) {
    const kksdk::LegacyCodecPriceTable table = load_price_table(prices, price_count);
    return kksdk::legacy_codec_literal_price(probabilities,
            static_cast<std::uint8_t>(symbol & 0xffU), table);
}

extern "C" unsigned int kksdk_legacy_codec_matched_literal_price(
        const unsigned short *probabilities, unsigned int symbol, unsigned int match_byte,
        const unsigned int *prices, unsigned long long price_count) {
    const kksdk::LegacyCodecPriceTable table = load_price_table(prices, price_count);
    return kksdk::legacy_codec_matched_literal_price(probabilities,
            static_cast<std::uint8_t>(symbol & 0xffU),
            static_cast<std::uint8_t>(match_byte & 0xffU), table);
}

extern "C" unsigned long long kksdk_legacy_codec_fill_align_price_table(
        const unsigned short *probabilities, unsigned int *out, unsigned long long count,
        const unsigned int *prices, unsigned long long price_count) {
    const kksdk::LegacyCodecPriceTable table = load_price_table(prices, price_count);
    return static_cast<unsigned long long>(kksdk::legacy_codec_fill_align_price_table(
            probabilities, out, static_cast<std::size_t>(count), table));
}

extern "C" unsigned long long kksdk_legacy_codec_fill_crc_table(
        unsigned int *out, unsigned long long count) {
    return static_cast<unsigned long long>(kksdk::legacy_codec_fill_crc_table(
            out, static_cast<std::size_t>(count)));
}
