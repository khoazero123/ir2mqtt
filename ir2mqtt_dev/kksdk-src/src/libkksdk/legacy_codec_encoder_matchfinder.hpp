#pragma once

#include <cstdint>

namespace kksdk {

// FUN_00141304 — plan and allocate match-finder window/hash tables.
// Returns 1 on success, 0 on failure (OEM convention).
int legacy_codec_encoder_matchfinder_plan_alloc(void *matchfinder, std::uint32_t dictionary_size,
        int keep_before, int fast_bytes, int match_limit, void **allocator_pair);

// FUN_00142018 — bind match-finder vtable slots on the encoder blob.
void legacy_codec_encoder_bind_matchfinder_callbacks(void *matchfinder, void *encoder);

}  // namespace kksdk
