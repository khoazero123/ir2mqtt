#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#ifndef KKSDK_ENABLE_OEM_LZMA_REFERENCE
#define KKSDK_ENABLE_OEM_LZMA_REFERENCE 0
#endif

namespace kksdk {

// Lifted FUN_0014e0e0 codec path (LZMA compress + optional transform is applied by caller).
bool streamhelper_lzma_encode_buffer(const std::uint8_t *input, std::size_t input_size,
        std::vector<std::uint8_t> &output);
#if KKSDK_ENABLE_OEM_LZMA_REFERENCE
bool streamhelper_lzma_encode_buffer_oem(const std::uint8_t *input, std::size_t input_size,
        std::vector<std::uint8_t> &output);
#endif
bool streamhelper_lzma_encode_buffer_fast(const std::uint8_t *input, std::size_t input_size,
        std::vector<std::uint8_t> &output);
bool streamhelper_lzma_encode_buffer_fast_lifted_init(const std::uint8_t *input,
        std::size_t input_size, std::vector<std::uint8_t> &output);
bool streamhelper_lzma_encode_buffer_fast_lifted_loop(const std::uint8_t *input,
        std::size_t input_size, std::vector<std::uint8_t> &output);
bool streamhelper_lzma_encode_buffer_fast_lifted_loop_native(const std::uint8_t *input,
        std::size_t input_size, std::vector<std::uint8_t> &output);
#if KKSDK_ENABLE_OEM_LZMA_REFERENCE
bool streamhelper_lzma_encode_buffer_oem_fast(const std::uint8_t *input, std::size_t input_size,
        std::vector<std::uint8_t> &output);
#endif

}  // namespace kksdk
