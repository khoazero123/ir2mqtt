#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

using undefined = std::uint8_t;
using undefined1 = std::uint8_t;
using undefined2 = std::uint16_t;
using undefined4 = std::uint32_t;
using undefined8 = std::uint64_t;
using byte = unsigned char;
using uint = unsigned int;
using ulong = unsigned long;

extern "C" using ghidra_word = std::uint64_t;

template <typename... Args>
static inline ghidra_word ghidra_unimplemented(Args...) {
    return 0;
}

