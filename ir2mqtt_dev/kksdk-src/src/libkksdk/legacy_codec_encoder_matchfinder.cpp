#include "legacy_codec_encoder_matchfinder.hpp"

#include "legacy_codec_encoder_matchfinder_layout.hpp"
#include "legacy_codec_encoder_matchfinder_runtime.hpp"
#include "legacy_codec_encoder_matchfinder_runtime.hpp"
#include "legacy_codec_encoder_state.hpp"
#include "legacy_codec_state.hpp"

namespace kksdk {
namespace {

using namespace legacy_oem_matchfinder;

constexpr std::size_t kMfSonOffset = 0x7cU;
constexpr std::size_t kMfFastBytesStore = kMatchMaxLen;

using OemAllocPairFn = void *(*)(void **allocator, std::uint64_t size);
using OemFreePairFn = void (*)(void **allocator, void *ptr);

void *match_finder_alloc_bridge(void *allocator, std::size_t size) {
    auto *pair = static_cast<void **>(allocator);
    const auto alloc = reinterpret_cast<OemAllocPairFn>(*pair);
    return alloc(pair, static_cast<std::uint64_t>(size));
}

void match_finder_free_bridge(void *allocator, void *ptr) {
    auto *pair = static_cast<void **>(allocator);
    const auto free_fn = reinterpret_cast<OemFreePairFn>(pair[1]);
    free_fn(pair, ptr);
}

void matchfinder_release_on_failure(std::uint8_t *matchfinder, void **allocator_pair) {
    const auto free_fn = reinterpret_cast<OemFreePairFn>(allocator_pair[1]);
    auto *tables = *reinterpret_cast<void **>(matchfinder + kHashTables);
    if (tables != nullptr) {
        free_fn(allocator_pair, tables);
        *reinterpret_cast<void **>(matchfinder + kHashTables) = nullptr;
    }
    auto *window = *reinterpret_cast<void **>(matchfinder + kWindowBuffer);
    if (window != nullptr && *reinterpret_cast<std::int32_t *>(matchfinder + kDirectInput) == 0) {
        free_fn(allocator_pair, window);
        *reinterpret_cast<void **>(matchfinder + kWindowBuffer) = nullptr;
    }
}

LegacyCodecMatchFinderAllocation load_matchfinder_allocation(std::uint8_t *matchfinder) {
    LegacyCodecMatchFinderAllocation allocation{};
    allocation.window_buffer =
            *reinterpret_cast<std::uint8_t **>(matchfinder + kWindowBuffer);
    allocation.window_size =
            *reinterpret_cast<std::uint32_t *>(matchfinder + kWindowBlockSize);
    allocation.hash_table =
            *reinterpret_cast<std::uint32_t **>(matchfinder + kHashTables);
    allocation.hash_table_words =
            *reinterpret_cast<std::uint32_t *>(matchfinder + kHashTableWords);
    allocation.son_table =
            *reinterpret_cast<std::uint32_t **>(matchfinder + kSonTablePtr);
    allocation.son_table_words =
            *reinterpret_cast<std::uint32_t *>(matchfinder + kSonTableWords);
    return allocation;
}

void store_matchfinder_allocation(std::uint8_t *matchfinder,
        const LegacyCodecMatchFinderAllocation &allocation,
        const LegacyCodecMatchFinderMemoryPlan &plan) {
    *reinterpret_cast<std::uint32_t *>(matchfinder + kMfSonOffset) = plan.son_offset;
    *reinterpret_cast<std::uint32_t *>(matchfinder + kHashTableWords) =
            legacy_codec_match_finder_hash_table_words(plan);
    *reinterpret_cast<std::uint32_t *>(matchfinder + kSonTableWords) =
            legacy_codec_match_finder_son_table_words(plan);
    *reinterpret_cast<std::uint32_t **>(matchfinder + kHashTables) = allocation.hash_table;
    *reinterpret_cast<std::uint32_t **>(matchfinder + kSonTablePtr) = allocation.son_table;
    if (*reinterpret_cast<std::int32_t *>(matchfinder + kDirectInput) == 0) {
        *reinterpret_cast<std::uint8_t **>(matchfinder + kWindowBuffer) =
                allocation.window_buffer;
        *reinterpret_cast<std::uint32_t *>(matchfinder + kWindowBlockSize) = plan.block_size;
    }
}

}  // namespace

int legacy_codec_encoder_matchfinder_plan_alloc(void *matchfinder, std::uint32_t dictionary_size,
        int keep_before, int fast_bytes, int match_limit, void **allocator_pair) {
    if (matchfinder == nullptr || allocator_pair == nullptr) {
        return 0;
    }

    auto *mf = static_cast<std::uint8_t *>(matchfinder);
    if (dictionary_size > 0xc0000000U) {
        matchfinder_release_on_failure(mf, allocator_pair);
        return 0;
    }

    const std::uint32_t history_size = dictionary_size + 1U;
    const int keep_after = match_limit + fast_bytes;
    *reinterpret_cast<std::int32_t *>(mf + kKeepBefore) =
            static_cast<std::int32_t>(history_size + static_cast<std::uint32_t>(keep_before));
    *reinterpret_cast<std::int32_t *>(mf + kKeepAfter) = keep_after;

    LegacyCodecMatchFinderMemoryPlanRequest plan_request{};
    plan_request.dictionary_size = dictionary_size;
    plan_request.keep_before = static_cast<std::uint32_t>(keep_before);
    plan_request.fast_bytes = static_cast<std::uint32_t>(fast_bytes);
    plan_request.match_max_len = static_cast<std::uint32_t>(match_limit);
    plan_request.hash_bytes = *reinterpret_cast<std::uint32_t *>(mf + kHashBytes);
    plan_request.binary_tree_mode =
            *reinterpret_cast<std::int32_t *>(mf + kBinaryTreeMode) != 0;

    const LegacyCodecMatchFinderMemoryPlan plan =
            legacy_codec_make_match_finder_memory_plan(plan_request);
    if (!plan.valid) {
        matchfinder_release_on_failure(mf, allocator_pair);
        return 0;
    }

    const bool direct_input = *reinterpret_cast<std::int32_t *>(mf + kDirectInput) != 0;
    LegacyCodecMatchFinderAllocation allocation = load_matchfinder_allocation(mf);

    if (!direct_input) {
        if (allocation.window_buffer == nullptr ||
                allocation.window_size != plan.block_size) {
            LegacyCodecMatchFinderAllocationRequest window_release{};
            window_release.allocation = &allocation;
            window_release.allocator = allocator_pair;
            window_release.free = match_finder_free_bridge;
            legacy_codec_free_match_finder_window(window_release);

            void *window = match_finder_alloc_bridge(allocator_pair, plan.block_size);
            if (window == nullptr) {
                matchfinder_release_on_failure(mf, allocator_pair);
                return 0;
            }
            allocation.window_buffer = static_cast<std::uint8_t *>(window);
            allocation.window_size = plan.block_size;
        }
    } else {
        allocation.window_size = plan.block_size;
        *reinterpret_cast<std::uint32_t *>(mf + kWindowBlockSize) = plan.block_size;
    }

    *reinterpret_cast<std::uint32_t *>(mf + kMfDictionarySize) = dictionary_size;
    *reinterpret_cast<std::uint32_t *>(mf + kHashMask) = plan.hash_mask;
    *reinterpret_cast<std::int32_t *>(mf + kHistorySize) = static_cast<std::int32_t>(history_size);
    *reinterpret_cast<std::int32_t *>(mf + kMfFastBytesStore) = fast_bytes;

    LegacyCodecMatchFinderAllocationRequest alloc_request{};
    alloc_request.plan = &plan;
    alloc_request.allocation = &allocation;
    alloc_request.allocator = allocator_pair;
    alloc_request.alloc = match_finder_alloc_bridge;
    alloc_request.free = match_finder_free_bridge;
    alloc_request.direct_input = direct_input;

    if (!legacy_codec_allocate_match_finder_memory(alloc_request)) {
        matchfinder_release_on_failure(mf, allocator_pair);
        return 0;
    }

    store_matchfinder_allocation(mf, allocation, plan);
    return 1;
}

void legacy_codec_encoder_bind_matchfinder_callbacks(void *matchfinder, void *encoder) {
    if (matchfinder == nullptr || encoder == nullptr) {
        return;
    }

    auto *mf = static_cast<const std::uint8_t *>(matchfinder);
    auto **slots = reinterpret_cast<void **>(encoder);

    slots[0] = reinterpret_cast<void *>(&legacy_codec_encoder_lifted_mf_init);
    slots[1] = reinterpret_cast<void *>(&legacy_codec_encoder_lifted_mf_get_byte);
    slots[2] = reinterpret_cast<void *>(&legacy_codec_encoder_lifted_mf_avail);
    slots[3] = reinterpret_cast<void *>(&legacy_codec_encoder_lifted_mf_get_ptr);
    slots[6] = matchfinder;

    if (*reinterpret_cast<const std::int32_t *>(mf + kBinaryTreeMode) == 0) {
        slots[4] = reinterpret_cast<void *>(&legacy_codec_encoder_lifted_mf_hc_find);
        slots[5] = reinterpret_cast<void *>(&legacy_codec_encoder_lifted_mf_hc_skip);
        return;
    }

    const std::uint32_t hash_bytes = *reinterpret_cast<const std::uint32_t *>(mf + kHashBytes);
    if (hash_bytes == 2U) {
        slots[4] = reinterpret_cast<void *>(&legacy_codec_encoder_lifted_mf_bt2_find);
        slots[5] = reinterpret_cast<void *>(&legacy_codec_encoder_lifted_mf_bt2_skip);
        return;
    }
    if (hash_bytes == 3U) {
        slots[4] = reinterpret_cast<void *>(&legacy_codec_encoder_lifted_mf_bt3_find);
        slots[5] = reinterpret_cast<void *>(&legacy_codec_encoder_lifted_mf_bt3_skip);
        return;
    }

    slots[4] = reinterpret_cast<void *>(&legacy_codec_encoder_lifted_mf_bt4_find);
    slots[5] = reinterpret_cast<void *>(&legacy_codec_encoder_lifted_mf_bt4_skip);
}

}  // namespace kksdk
