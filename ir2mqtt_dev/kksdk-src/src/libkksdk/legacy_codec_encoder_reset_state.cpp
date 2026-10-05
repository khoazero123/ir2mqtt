#include "legacy_codec_encoder_reset_state.hpp"

#include "legacy_codec_encoder_state.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace kksdk {
namespace {

using namespace legacy_oem_encoder;

constexpr std::uint64_t kProbU64Pack = 0x400040004000400ULL;
constexpr std::uint16_t kProbHalf = 0x400U;
constexpr std::size_t kRangeStreamLow = 0x3d2f8U;
constexpr std::size_t kRangeStreamFlag = 0x3d2f4U;
constexpr std::size_t kEncoderLiteralPosMask = 0x33be0U;

static void fill_prob_u16(std::uint8_t *base, std::size_t offset, std::size_t byte_count) {
    if (byte_count == 0U) {
        return;
    }
    std::fill_n(reinterpret_cast<std::uint16_t *>(base + offset), byte_count / sizeof(std::uint16_t),
            kProbHalf);
}

static void fill_prob_u64_pattern(std::uint8_t *base, std::size_t offset, std::size_t byte_count) {
    auto *words = reinterpret_cast<std::uint64_t *>(base + offset);
    const std::size_t count = byte_count / sizeof(std::uint64_t);
    for (std::size_t index = 0U; index < count; ++index) {
        words[index] = kProbU64Pack;
    }
}

static void reset_is_match_and_rep_short(std::uint8_t *base) {
    std::int64_t rep_short_offset = -0x18;
    auto *match_words = reinterpret_cast<std::uint64_t *>(base + kIsMatchProbs);
    do {
        match_words[0] = kProbU64Pack;
        match_words[1] = kProbU64Pack;
        match_words[2] = kProbU64Pack;
        match_words[3] = kProbU64Pack;
        match_words[4] = kProbU64Pack;
        match_words[5] = kProbU64Pack;
        match_words[6] = kProbU64Pack;
        match_words[7] = kProbU64Pack;

        auto *rep_short =
                reinterpret_cast<std::uint16_t *>(base + kRep1ShortProbs + rep_short_offset);
        rep_short_offset += 2;
        match_words += 4;
        rep_short[-0xc] = kProbHalf;
        *rep_short = kProbHalf;
        rep_short[0xc] = kProbHalf;
        rep_short[0x18] = kProbHalf;
    } while (rep_short_offset != 0);
}

static void reset_literal_primary_tree(std::uint8_t *base) {
    const std::uint32_t literal_pos_bits =
            *reinterpret_cast<const std::uint32_t *>(base + kLiteralPosBits);
    std::uint32_t literal_key_bits =
            static_cast<std::uint32_t>(*reinterpret_cast<const std::int32_t *>(base + kLiteralCtxBits)) +
            literal_pos_bits;
    if (literal_key_bits >= 0x18U) {
        return;
    }

    std::uint32_t literal_count = 0x300U << (literal_key_bits & 0x1fU);
    auto *literal_tree =
            *reinterpret_cast<std::uint16_t **>(base + kLiteralTreePrimary);
    if (literal_tree == nullptr) {
        return;
    }

    std::size_t count = literal_count;
    if (count < 2U) {
        count = 1U;
    }

    std::size_t vectorized = count & ~static_cast<std::size_t>(0xfU);
    if (vectorized >= 0x10U) {
        auto *packed = reinterpret_cast<std::uint64_t *>(literal_tree + 0x8);
        std::size_t remaining = vectorized;
        do {
            packed[-1] = kProbU64Pack;
            packed[-2] = kProbU64Pack;
            packed[1] = kProbU64Pack;
            *packed = kProbU64Pack;
            remaining -= 0x10U;
            packed += 4;
        } while (remaining != 0U);
        if (vectorized == count) {
            return;
        }
    }

    for (std::size_t index = vectorized; index < count; ++index) {
        literal_tree[index] = kProbHalf;
    }
}

static std::int32_t mask_for_bits(std::uint32_t bits) {
    const std::uint32_t clamped = bits & 0x1fU;
    if (clamped >= 31U) {
        return -1;
    }
    return static_cast<std::int32_t>(~(0xffffffffU << clamped));
}

}  // namespace

void legacy_codec_encoder_reset_state(void *encoder) {
    auto *base = reinterpret_cast<std::uint8_t *>(encoder);

    *reinterpret_cast<std::uint64_t *>(base + kRangeStreamLow) = 0U;
    *reinterpret_cast<std::uint64_t *>(base + kProgressBase) = 1U;
    *reinterpret_cast<std::uint64_t *>(base + kRepDist2) = 0U;
    *reinterpret_cast<std::uint64_t *>(base + kRepDist3) = 0U;
    *reinterpret_cast<std::uint32_t *>(base + kLzmaStateFinish) = 0U;
    *reinterpret_cast<std::uint32_t *>(base + kLzmaState) = 0U;
    *reinterpret_cast<std::uint32_t *>(base + kRangeStream) = 0xffffffffU;
    *reinterpret_cast<std::uint64_t *>(base + kProgressWritePos) =
            *reinterpret_cast<std::uint64_t *>(base + kProgressReadPos);
    base[kRangeStreamFlag] = 0U;
    *reinterpret_cast<std::uint64_t *>(base + kProgressWriteEnd) = 0U;
    *reinterpret_cast<std::uint32_t *>(base + kStreamOutStatus) = 0U;

    reset_is_match_and_rep_short(base);
    reset_literal_primary_tree(base);

    // Tail of rep-short / pre-pos-slot models not touched by the interleaved loop.
    fill_prob_u16(base, kRep3ShortProbs, kPosSlotProbs - kRep3ShortProbs);

    fill_prob_u64_pattern(base, kPosSlotProbs, 0x34230U - kPosSlotProbs);
    *reinterpret_cast<std::uint32_t *>(base + kAlignProbs - 4U) = 0x4000400U;
    fill_prob_u64_pattern(base, kAlignProbs, 0x34254U - kAlignProbs);
    *reinterpret_cast<std::uint16_t *>(base + kLenEncBase) = kProbHalf;
    *reinterpret_cast<std::uint16_t *>(base + kLenEncBase + 2U) = kProbHalf;
    fill_prob_u64_pattern(base, kLenEncBase + 4U, 0x34658U - (kLenEncBase + 4U));
    *reinterpret_cast<std::uint32_t *>(base + kRepLenEncBase) = 0x4000400U;
    fill_prob_u64_pattern(base, kRepLenEncBase + 4U, 0x38e9aU - (kRepLenEncBase + 4U));

    *reinterpret_cast<std::uint64_t *>(base + kParseNodeIndex) = 0U;
    *reinterpret_cast<std::uint32_t *>(base + kProcessedInBlock) = 0U;
    *reinterpret_cast<std::uint32_t *>(base + kPendingLiteral) = 0U;

    const std::uint32_t literal_pos_bits =
            *reinterpret_cast<const std::uint32_t *>(base + kLiteralPosBits) & 0x1fU;
    const std::uint32_t pos_state_bits =
            *reinterpret_cast<const std::uint32_t *>(base + kPosStateBits) & 0x1fU;
    *reinterpret_cast<std::int32_t *>(base + kEncoderLiteralPosMask) =
            mask_for_bits(literal_pos_bits);
    *reinterpret_cast<std::int32_t *>(base + kPosStateMask) = mask_for_bits(pos_state_bits);
}

}  // namespace kksdk
