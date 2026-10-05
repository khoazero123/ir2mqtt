#include "legacy_codec_encoder_stream_init.hpp"

#include "legacy_codec_encoder_blocks.hpp"
#include "legacy_codec_encoder_context.hpp"
#include "legacy_codec_encoder_matchfinder.hpp"
#include "legacy_codec_encoder_matchfinder_runtime.hpp"
#include "legacy_codec_encoder_reset_state.hpp"
#include "legacy_codec_encoder_state.hpp"

namespace kksdk {
namespace {

using namespace legacy_oem_encoder;

constexpr std::uint32_t kMatchProbeLimit = 0x111U;
constexpr std::uint32_t kProgressScratchBytes = 0x10000U;
constexpr std::uint32_t kDefaultKeepSize = 0x1000U;
constexpr std::uint64_t kStatusAllocError = 2U;

using OemAllocFn = void *(*)(void *allocator, std::uint64_t size);
using OemFreePtrFn = void (*)(void *allocator, void *ptr);

std::uint32_t pos_slot_price_count_for_dictionary(std::uint32_t dict_size) {
    if (dict_size < 2U) {
        return 0U;
    }
    if (dict_size == 2U) {
        return 2U;
    }
    if (dict_size < 5U) {
        return 4U;
    }
    if (dict_size < 9U) {
        return 6U;
    }
    if (dict_size < 0x11U) {
        return 8U;
    }
    if (dict_size < 0x21U) {
        return 10U;
    }
    if (dict_size < 0x41U) {
        return 0xcU;
    }
    if (dict_size < 0x81U) {
        return 0xeU;
    }
    if (dict_size < 0x101U) {
        return 0x10U;
    }
    if (dict_size < 0x201U) {
        return 0x12U;
    }
    if (dict_size < 0x401U) {
        return 0x14U;
    }
    if (dict_size < 0x801U) {
        return 0x16U;
    }
    if (dict_size < 0x1001U) {
        return 0x18U;
    }
    if (dict_size < 0x2001U) {
        return 0x1aU;
    }
    if (dict_size < 0x4001U) {
        return 0x1cU;
    }
    if (dict_size < 0x8001U) {
        return 0x1eU;
    }
    if (dict_size < 0x10001U) {
        return 0x20U;
    }
    if (dict_size < 0x20001U) {
        return 0x22U;
    }
    if (dict_size < 0x40001U) {
        return 0x24U;
    }
    if (dict_size < 0x80001U) {
        return 0x26U;
    }
    if (dict_size < 0x100001U) {
        return 0x28U;
    }
    if (dict_size < 0x200001U) {
        return 0x2aU;
    }
    if (dict_size < 0x400001U) {
        return 0x2cU;
    }
    if (dict_size < 0x800001U) {
        return 0x2eU;
    }
    if (dict_size < 0x1000001U) {
        return 0x30U;
    }
    if (dict_size < 0x2000001U) {
        return 0x32U;
    }
    if (dict_size < 0x4000001U) {
        return 0x34U;
    }
    if (dict_size < 0x8000001U) {
        return 0x36U;
    }
    if (dict_size < 0x10000001U) {
        return 0x38U;
    }
    if (dict_size < 0x20000001U) {
        return 0x3aU;
    }
    if (dict_size < 0x40000001U) {
        return 0x3cU;
    }
    return 0x3eU;
}

bool ensure_progress_scratch(std::uint8_t *base, void **allocator) {
    auto *read_pos = reinterpret_cast<std::uint8_t **>(base + kProgressReadPos);
    if (*read_pos != nullptr) {
        return true;
    }

    const auto alloc = reinterpret_cast<OemAllocFn>(*allocator);
    void *scratch = alloc(allocator, kProgressScratchBytes);
    if (scratch == nullptr) {
        return false;
    }

    *read_pos = static_cast<std::uint8_t *>(scratch);
    *reinterpret_cast<std::uint8_t **>(base + kProgressBufferEnd) =
            static_cast<std::uint8_t *>(scratch) + kProgressScratchBytes;
    return true;
}

void free_literal_tree(void **allocator, void *tree) {
    if (tree == nullptr) {
        return;
    }
    const auto free_fn = reinterpret_cast<OemFreePtrFn>(allocator[1]);
    free_fn(allocator, tree);
}

bool ensure_literal_trees(std::uint8_t *base, void **allocator) {
    const std::uint32_t pos_bits =
            *reinterpret_cast<std::uint32_t *>(base + kLiteralPosBits);
    const std::uint32_t ctx_bits =
            *reinterpret_cast<std::uint32_t *>(base + kLiteralCtxBits);
    const std::uint32_t ctx_key = pos_bits + ctx_bits;

    auto *primary = reinterpret_cast<void **>(base + kLiteralTreePrimary);
    auto *secondary = reinterpret_cast<void **>(base + kLiteralTreeSecondary);
    auto *cached_key = reinterpret_cast<std::uint32_t *>(base + kLiteralCtxKey);

    if (*primary != nullptr && *secondary != nullptr && *cached_key == ctx_key) {
        return true;
    }

    free_literal_tree(allocator, *primary);
    free_literal_tree(allocator, *secondary);
    *primary = nullptr;
    *secondary = nullptr;

    const std::uint64_t tree_bytes =
            (static_cast<std::uint64_t>(0x300U) << (ctx_key & 0x1fU)) * 2U;
    const auto alloc = reinterpret_cast<OemAllocFn>(*allocator);

    void *tree_a = alloc(allocator, tree_bytes);
    void *tree_b = alloc(allocator, tree_bytes);
    *primary = tree_a;
    *secondary = tree_b;
    if (tree_a == nullptr || tree_b == nullptr) {
        free_literal_tree(allocator, tree_a);
        free_literal_tree(allocator, tree_b);
        *primary = nullptr;
        *secondary = nullptr;
        return false;
    }

    *cached_key = ctx_key;
    return true;
}

int matchfinder_keep_size(std::uint32_t mode, std::uint32_t dict_size) {
    int keep = static_cast<int>(mode) - static_cast<int>(dict_size);
    if (mode <= dict_size + 0x1000U) {
        keep = static_cast<int>(kDefaultKeepSize);
    }
    return keep;
}

}  // namespace

std::uint64_t legacy_codec_encoder_stream_init(void *encoder, std::uint32_t mode,
        void **allocator, void *allocator_ctx) {
    if (encoder == nullptr) {
        return kStatusAllocError;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);

    const std::uint32_t dict_size =
            *reinterpret_cast<std::uint32_t *>(base + kDictionarySize);
    *reinterpret_cast<std::uint32_t *>(base + kPosSlotPriceCount) =
            pos_slot_price_count_for_dictionary(dict_size);
    *reinterpret_cast<std::uint32_t *>(base + kFinishFlag) = 0U;
    *reinterpret_cast<std::uint32_t *>(base + kStatusCode) = 0U;

    if (!ensure_progress_scratch(base, allocator)) {
        return kStatusAllocError;
    }
    if (!ensure_literal_trees(base, allocator)) {
        return kStatusAllocError;
    }

    void *matchfinder = base + kMatchFinder;
    *reinterpret_cast<std::uint32_t *>(base + kMatchfinderLargeDictFlag) =
            dict_size > 0x1000000U ? 1U : 0U;

    const int keep_size = matchfinder_keep_size(mode, dict_size);
    const int fast_bytes = *reinterpret_cast<std::int32_t *>(base + kFastBytes);
    auto *mf_allocator = reinterpret_cast<void **>(allocator_ctx);
    if (legacy_codec_encoder_matchfinder_plan_alloc(matchfinder, dict_size, keep_size, fast_bytes,
                static_cast<int>(kMatchProbeLimit), mf_allocator) == 0) {
        return kStatusAllocError;
    }

    *reinterpret_cast<void **>(base + kEncoderMatchfinderBackref) = matchfinder;

    legacy_codec_encoder_bind_matchfinder_callbacks(matchfinder, encoder);

    legacy_codec_encoder_reset_state(encoder);
    *reinterpret_cast<std::uint64_t *>(base + kBlockBytesEncoded) = 0U;
    *reinterpret_cast<std::uint32_t *>(base + kFastFinishDcAdjust) = 0U;
    legacy_codec_encoder_reset_fast_local_dc(encoder);

    // OEM FUN_00146c8c primes MF buffer before first core step (FUN_00141548 + fill).
    legacy_codec_encoder_stream_init_warm_matchfinder(encoder);

    return 0U;
}

void legacy_codec_encoder_stream_init_warm_matchfinder(void *encoder) {
    if (encoder == nullptr) {
        return;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    void *matchfinder = base + kMatchFinder;
    legacy_codec_encoder_matchfinder_runtime_init(matchfinder);
    *reinterpret_cast<std::int32_t *>(base + kStreamInitFlag) = 0;
}

}  // namespace kksdk
