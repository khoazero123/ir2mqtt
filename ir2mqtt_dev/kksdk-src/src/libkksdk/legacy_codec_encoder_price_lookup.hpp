#pragma once

#include <cstdint>

namespace kksdk {
namespace legacy_oem_price {

inline std::int32_t lookup_nibble(const std::int32_t *table, std::uint32_t prob) {
    return table[(prob >> 4U) & 0x3ffU];
}

inline std::int32_t lookup_nibble_xor(const std::int32_t *table, std::uint32_t prob) {
    return table[((prob >> 4U) ^ 0x7fU) & 0x3ffU];
}

inline std::int32_t lookup_word(const std::int32_t *table, std::uint32_t index) {
    return table[index & 0x3ffcU];
}

inline std::int32_t lookup_word_xor(const std::int32_t *table, std::uint32_t index) {
    return table[(index & 0x3ffcU) ^ 0x1fcU];
}

}  // namespace legacy_oem_price
}  // namespace kksdk
