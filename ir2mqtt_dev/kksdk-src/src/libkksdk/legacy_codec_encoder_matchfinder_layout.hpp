#pragma once

#include <cstddef>
#include <cstdint>

namespace kksdk {
namespace legacy_oem_matchfinder {

// OEM match-finder blob layout (encoder + 0x38).
constexpr std::size_t kCurrentPtr = 0x00U;
constexpr std::size_t kReadPos = 0x08U;
constexpr std::size_t kPositionLimit = 0x0cU;
constexpr std::size_t kWritePos = 0x10U;
constexpr std::size_t kMatchLenLimit = 0x14U;
constexpr std::size_t kCyclicPos = 0x18U;
constexpr std::size_t kHistorySize = 0x1cU;
constexpr std::size_t kMatchMaxLen = 0x20U;
constexpr std::size_t kHashTables = 0x28U;
constexpr std::size_t kSonTablePtr = 0x30U;
constexpr std::size_t kHashMask = 0x38U;
constexpr std::size_t kWindowBuffer = 0x40U;
constexpr std::size_t kStreamCallback = 0x48U;
constexpr std::size_t kStreamEnd = 0x50U;
constexpr std::size_t kWindowBlockSize = 0x54U;
constexpr std::size_t kKeepBefore = 0x58U;
constexpr std::size_t kKeepAfter = 0x5cU;
constexpr std::size_t kHashBytes = 0x60U;
constexpr std::size_t kDirectInput = 0x64U;
constexpr std::size_t kDirectInputRemaining = 0x68U;
constexpr std::size_t kBinaryTreeMode = 0x70U;
constexpr std::size_t kHashTableWords = 0x80U;
constexpr std::size_t kSonTableWords = 0x84U;
constexpr std::size_t kStreamError = 0x88U;
constexpr std::size_t kMaxDepth = 0x3cU;
constexpr std::size_t kMfDictionarySize = 0x78U;
constexpr std::size_t kCrcTable = 0x8cU;
constexpr std::uint32_t kCrcTableEntries = 0x100U;

}  // namespace legacy_oem_matchfinder
}  // namespace kksdk
