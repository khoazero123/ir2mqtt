#pragma once

#include <cstddef>
#include <cstdint>

namespace kksdk {

// OEM LZMA encoder blob layout (FUN_001460c4 allocates 0x46a78 bytes).
namespace legacy_oem_encoder {

constexpr std::size_t kStateSize = 0x46a78U;

constexpr std::size_t kMatchFinder = 0x38U;
// Ghidra index 0x65f1 * 8 == kLzmaStateFinish (active LZMA state dword).
constexpr std::size_t kLzmaState = 0x32f88U;
constexpr std::size_t kLzmaStateFinish = 0x32f88U;
constexpr std::size_t kPendingLiteral = 0x4ccU;
constexpr std::size_t kProcessedInBlock = 0x32f74U;
constexpr std::size_t kPosStateMask = 0x33be4U;
constexpr std::size_t kPosStateBits = 0x33bdcU;
constexpr std::size_t kFastBytes = 0x32f70U;
constexpr std::size_t kLenPriceRefreshLimit = 0x3d2a0U;
constexpr std::size_t kRepLenPriceRefreshLimit = 0x38a58U;
constexpr std::size_t kDictionarySize = 0x3d358U;
constexpr std::size_t kMatchFinderCycles = 0x3d35cU;
constexpr std::size_t kProgressBufferEnd = 0x3d310U;
constexpr std::size_t kLiteralTreePrimary = 0x33be8U;
constexpr std::size_t kLiteralTreeSecondary = 0x3d368U;
// Ghidra index 0x677d * 8 == kLiteralTreePrimary (active literal prob tree pointer).
constexpr std::size_t kLiteralTreeRoot = kLiteralTreePrimary;
constexpr std::size_t kLiteralCtxKey = 0x3d2e4U;
constexpr std::size_t kLiteralPosBits = 0x33bd8U;
constexpr std::size_t kEncoderMatchfinderBackref = 0x30U;
constexpr std::size_t kMatchfinderLargeDictFlag = 0xacU;
constexpr std::size_t kFinishFlag = 0x3d34cU;
constexpr std::size_t kFinishMode = 0x3d338U;
constexpr std::size_t kLiteralMode = 0x3d2e8U;
constexpr std::size_t kExtraInputFlag = 0xc0U;
constexpr std::size_t kStatusCode = 0x3d354U;
constexpr std::size_t kRangeStream = 0x3d2f0U;
constexpr std::size_t kOutputWriteCallback = 0x3d320U;
constexpr std::size_t kInputReadCallback = 0x80U;
constexpr std::size_t kLenEncBase = 0x34254U;
constexpr std::size_t kRepLenEncBase = 0x38a9cU;
constexpr std::size_t kProbPrices = 0x324dcU;
constexpr std::size_t kLiteralPosMask = 0x33be0U;
constexpr std::size_t kLiteralCtxBits = 0x33bd4U;
// Ghidra index 0x65ef/0x65f0 * 8 — active rep0/rep1 distance dwords.
constexpr std::size_t kRep0Distance = 0x32f78U;
constexpr std::size_t kRep1Distance = 0x32f80U;
constexpr std::size_t kRepDist2 = kRep0Distance;
constexpr std::size_t kRepDist3 = 0x32f7cU;
constexpr std::size_t kRepDist4 = 0x32f84U;
constexpr std::size_t kPosSlotTable = 0x304dcU;
constexpr std::size_t kPosSlotRemapTable = 0x304e0U;
constexpr std::size_t kPosSlotPriceCount = 0x33bd0U;
constexpr std::size_t kPosSlotHighPriceAdjust = 0x32fc4U;
constexpr std::size_t kPosSlotHighProbs = 0x3414eU;
constexpr std::size_t kIsMatchProbs = 0x33bf0U;
constexpr std::size_t kLitStateProbs = 0x33d70U;
constexpr std::size_t kRep0ShortProbs = 0x33d88U;
constexpr std::size_t kRep1ShortProbs = 0x33da0U;
constexpr std::size_t kRep2ShortProbs = 0x33db8U;
constexpr std::size_t kRep3ShortProbs = 0x33dd0U;
constexpr std::size_t kPosSlotProbs = 0x33f50U;
constexpr std::size_t kAlignProbs = 0x34234U;
constexpr std::size_t kLenPriceSlotCount = 0x2404U;
constexpr std::size_t kBlockBytesEncoded = 0x3d340U;
constexpr std::size_t kMatchEncodeCount = 0x3d348U;
constexpr std::size_t kFastFinishDcAdjust = 0x3d33cU;  // 1=rep/match tail dc; 2=47d10 eof ptr
constexpr std::size_t kFastLocalDc = 0x3d350U;
constexpr std::size_t kStreamOutStatus = 0x3d330U;
constexpr std::size_t kProgressBase = 0x3d300U;
constexpr std::size_t kProgressReadPos = 0x3d318U;
constexpr std::size_t kProgressWritePos = 0x3d308U;
constexpr std::size_t kProgressWriteEnd = 0x3d328U;
constexpr std::size_t kStreamInitFlag = 0x3d360U;
constexpr std::size_t kAlignPriceCounter = 0x33bccU;
constexpr std::size_t kMatchDistPriceShort = 0x3338cU;
constexpr std::size_t kMatchDistPriceShortAux = 0x33394U;
constexpr std::size_t kMatchDistPrice = 0x3339cU;
constexpr std::size_t kMatchPosSlotPriceAux = 0x32f94U;
constexpr std::size_t kMatchPosSlotPrice = 0x32f8cU;
constexpr std::size_t kAlignPriceCache = 0x33b8cU;
constexpr std::size_t kLenPriceCache = 0x34658U;
constexpr std::size_t kRepLenPriceCache = 0x38ea0U;
constexpr std::size_t kLenPriceSlotBytes = 0x404U;
// LZMA1 minimum match length is fixed at 2; OEM compares against this in the parse path.
constexpr std::uint32_t kLzmaMinMatchLength = 2U;
constexpr std::size_t kParseChoiceTag = 0x4f8U;
// Ghidra indexes param_1 as undefined8*; byte offset = index * 8.
constexpr std::size_t kParseNodeIndex = 0x4c8U;
constexpr std::size_t kParseCachedAvailLength = 0x4d0U;
// Sentinel: short-path sync deferred skip (match_len_limit == 1).
constexpr std::uint32_t kParseSyncDeferSkipFlag = 0xffffffffU;
constexpr std::size_t kMatchfinderAvail = 0x4d8U;
constexpr std::size_t kMatchBuffer = 0x326dcU;

constexpr std::uint32_t kStatusOk = 0U;
constexpr std::uint32_t kStatusInputEof = 8U;
constexpr std::uint32_t kStatusOutputError = 9U;

static constexpr std::uint32_t kLiteralStateTable[12] = {
        0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 4, 5,
};

static constexpr std::uint32_t kShortRepStateTable[12] = {
        9, 9, 9, 9, 9, 9, 9, 11, 11, 11, 11, 11,
};

static constexpr std::uint32_t kRepStateTable[12] = {
        8, 8, 8, 8, 8, 8, 8, 11, 11, 11, 11, 11,
};

static constexpr std::uint32_t kMatchStateTable[12] = {
        7, 7, 7, 7, 7, 7, 7, 10, 10, 10, 10, 10,
};

inline const std::uint32_t *literal_state_table() {
    return kLiteralStateTable;
}

inline const std::uint32_t *short_rep_state_table() {
    return kShortRepStateTable;
}

inline const std::uint32_t *rep_state_table() {
    return kRepStateTable;
}

inline const std::uint32_t *match_state_table() {
    return kMatchStateTable;
}

// Kept for older probes/price code; this is the OEM DAT_001b7cf0 new-match transition.
inline const std::uint32_t *rep0_long_state_table() {
    return match_state_table();
}

}  // namespace legacy_oem_encoder

}  // namespace kksdk
