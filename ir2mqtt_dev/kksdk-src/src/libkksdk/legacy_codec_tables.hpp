#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace kksdk {

using LegacyCodecLenSlotTable = std::array<std::uint8_t, 4096>;
using LegacyCodecPriceTable = std::array<std::uint32_t, 128>;
using LegacyCodecAlignPriceTable = std::array<std::uint32_t, 16>;
using LegacyCodecCrcTable = std::array<std::uint32_t, 256>;

LegacyCodecLenSlotTable legacy_codec_make_len_slot_table();
std::size_t legacy_codec_fill_len_slot_table(std::uint8_t *out, std::size_t out_size);
LegacyCodecPriceTable legacy_codec_make_price_table();
std::size_t legacy_codec_fill_price_table(std::uint32_t *out, std::size_t count);
std::uint32_t legacy_codec_bit_price(std::uint16_t probability, bool bit,
        const LegacyCodecPriceTable &prices);
std::uint32_t legacy_codec_bit_tree_price(const std::uint16_t *probabilities,
        std::uint32_t symbol, std::uint32_t bit_count, const LegacyCodecPriceTable &prices);
std::uint32_t legacy_codec_reverse_bit_tree_price(const std::uint16_t *probabilities,
        std::uint32_t symbol, std::uint32_t bit_count, const LegacyCodecPriceTable &prices);
std::uint32_t legacy_codec_direct_bits_price(std::uint32_t bit_count);
std::uint32_t legacy_codec_literal_price(const std::uint16_t *probabilities,
        std::uint8_t symbol, const LegacyCodecPriceTable &prices);
std::uint32_t legacy_codec_matched_literal_price(const std::uint16_t *probabilities,
        std::uint8_t symbol, std::uint8_t match_byte, const LegacyCodecPriceTable &prices);
LegacyCodecAlignPriceTable legacy_codec_make_align_price_table(
        const std::uint16_t *probabilities, const LegacyCodecPriceTable &prices);
std::size_t legacy_codec_fill_align_price_table(const std::uint16_t *probabilities,
        std::uint32_t *out, std::size_t count, const LegacyCodecPriceTable &prices);
LegacyCodecCrcTable legacy_codec_make_crc_table();
std::size_t legacy_codec_fill_crc_table(std::uint32_t *out, std::size_t count);

}  // namespace kksdk

extern "C" unsigned long long kksdk_legacy_codec_fill_len_slot_table(
        unsigned char *out, unsigned long long out_size);
extern "C" unsigned long long kksdk_legacy_codec_fill_price_table(
        unsigned int *out, unsigned long long count);
extern "C" unsigned int kksdk_legacy_codec_bit_price(unsigned short probability,
        int bit, const unsigned int *prices, unsigned long long price_count);
extern "C" unsigned int kksdk_legacy_codec_bit_tree_price(const unsigned short *probabilities,
        unsigned int symbol, unsigned int bit_count, const unsigned int *prices,
        unsigned long long price_count);
extern "C" unsigned int kksdk_legacy_codec_reverse_bit_tree_price(
        const unsigned short *probabilities, unsigned int symbol, unsigned int bit_count,
        const unsigned int *prices, unsigned long long price_count);
extern "C" unsigned int kksdk_legacy_codec_direct_bits_price(unsigned int bit_count);
extern "C" unsigned int kksdk_legacy_codec_literal_price(const unsigned short *probabilities,
        unsigned int symbol, const unsigned int *prices, unsigned long long price_count);
extern "C" unsigned int kksdk_legacy_codec_matched_literal_price(
        const unsigned short *probabilities, unsigned int symbol, unsigned int match_byte,
        const unsigned int *prices, unsigned long long price_count);
extern "C" unsigned long long kksdk_legacy_codec_fill_align_price_table(
        const unsigned short *probabilities, unsigned int *out, unsigned long long count,
        const unsigned int *prices, unsigned long long price_count);
extern "C" unsigned long long kksdk_legacy_codec_fill_crc_table(
        unsigned int *out, unsigned long long count);
