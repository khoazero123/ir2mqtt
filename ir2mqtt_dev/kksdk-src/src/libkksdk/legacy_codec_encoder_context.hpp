#pragma once

#include "legacy_codec_encoder_matchfinder_layout.hpp"

#include "legacy_codec_encoder_state.hpp"

#include <cstdint>

#include "legacy_codec_encoder_state.hpp"

namespace kksdk {

struct LegacyCodecMatchFinderAccess {
    void *encoder;

    void *context() const;
    int has_data() const;
    void *buffer() const;
    int skip(std::int32_t count) const;
    bool normalize() const;
    const std::uint8_t *current_byte_ptr() const;
    const std::uint8_t *current_literal_ptr() const;
};

std::uint32_t legacy_codec_matchfinder_get_byte(void *encoder, int offset);
bool legacy_codec_matchfinder_warmup(void *encoder);
// FUN_001473d0 block==0: find_match; optional skip(1) when match len==2 (warmup uses normalize).
bool legacy_codec_matchfinder_native_open_prefetch(void *encoder, bool run_find_match = true,
        bool skip_on_min_match = true);
bool legacy_codec_matchfinder_entry_avail(void *encoder);
bool legacy_codec_encoder_uses_lifted_matchfinder(void *encoder);
bool legacy_codec_encoder_run_matchfinder_init_if_needed(void *encoder);
bool legacy_codec_encoder_rep_distance_reachable(void *encoder, const std::uint8_t *cur,
        std::int32_t rep_distance);

inline std::uint32_t legacy_codec_encoder_status_code(void *encoder) {
    return *reinterpret_cast<std::uint32_t *>(static_cast<std::uint8_t *>(encoder) +
            legacy_oem_encoder::kStatusCode);
}

inline std::uint32_t legacy_codec_encoder_finish_flag(void *encoder) {
    return *reinterpret_cast<std::uint32_t *>(static_cast<std::uint8_t *>(encoder) +
            legacy_oem_encoder::kFinishFlag);
}

inline std::uint64_t legacy_codec_encoder_block_bytes(void *encoder) {
    return *reinterpret_cast<std::uint64_t *>(static_cast<std::uint8_t *>(encoder) +
            legacy_oem_encoder::kBlockBytesEncoded);
}

inline std::uint32_t legacy_codec_encoder_pos_state(void *encoder) {
    const auto *base = static_cast<const std::uint8_t *>(encoder);
    const std::uint32_t mask =
            *reinterpret_cast<const std::uint32_t *>(base + legacy_oem_encoder::kPosStateMask);
    return static_cast<std::uint32_t>(legacy_codec_encoder_block_bytes(encoder)) & mask;
}

// OEM FUN_0014c378(param_2): local_dc accumulated in apply loop; for fast one-shot
// blocks this is block_bytes-1 at finish (e.g. 32 encoded bytes -> pos_state 3).
inline std::uint32_t legacy_codec_encoder_fast_local_dc(void *encoder) {
    return *reinterpret_cast<std::uint32_t *>(static_cast<std::uint8_t *>(encoder) +
            legacy_oem_encoder::kFastLocalDc);
}

inline void legacy_codec_encoder_reset_fast_local_dc(void *encoder) {
    *reinterpret_cast<std::uint32_t *>(static_cast<std::uint8_t *>(encoder) +
            legacy_oem_encoder::kFastLocalDc) = 0U;
}

inline bool legacy_codec_encoder_fast_open_body_active(void *encoder) {
    return legacy_codec_encoder_block_bytes(encoder) == 0U &&
            legacy_codec_encoder_fast_local_dc(encoder) > 0U;
}

inline std::uint32_t legacy_codec_encoder_fast_pos_state(void *encoder) {
    const auto *base = static_cast<const std::uint8_t *>(encoder);
    const std::uint32_t mask =
            *reinterpret_cast<const std::uint32_t *>(base + legacy_oem_encoder::kPosStateMask);
    const std::uint64_t block_bytes = legacy_codec_encoder_block_bytes(encoder);
    const std::uint32_t local_dc = legacy_codec_encoder_fast_local_dc(encoder);
    // OEM block==0 open literal: ldc=1 but pos_state stays 0 until block_bytes advances.
    if (local_dc != 0U && block_bytes != 0U) {
        return local_dc & mask;
    }
    return static_cast<std::uint32_t>(block_bytes) & mask;
}

// OEM LAB_0014866c: is_match/length use (kPosStateMask & local_dc), not block_bytes when ldc==0.
inline std::uint32_t legacy_codec_encoder_fast_match_pos_state(void *encoder) {
    const auto *base = static_cast<const std::uint8_t *>(encoder);
    const std::uint32_t mask =
            *reinterpret_cast<const std::uint32_t *>(base + legacy_oem_encoder::kPosStateMask);
    return legacy_codec_encoder_fast_local_dc(encoder) & mask;
}

// OEM LAB_0014866c apply: is_match/len use (local_dc & pos_mask), not block_bytes.
inline std::uint32_t legacy_codec_encoder_fast_apply_pos_state(void *encoder) {
    const auto *base = static_cast<const std::uint8_t *>(encoder);
    const std::uint32_t mask =
            *reinterpret_cast<const std::uint32_t *>(base + legacy_oem_encoder::kPosStateMask);
    const std::uint32_t local_dc = legacy_codec_encoder_fast_local_dc(encoder);
    if (local_dc != 0U) {
        return local_dc & mask;
    }
    return static_cast<std::uint32_t>(legacy_codec_encoder_block_bytes(encoder)) & mask;
}

inline std::uint32_t legacy_codec_encoder_finish_pos_state(void *encoder) {
    const auto *base = static_cast<const std::uint8_t *>(encoder);
    if (*reinterpret_cast<const std::int32_t *>(base + legacy_oem_encoder::kLiteralMode) != 0) {
        return legacy_codec_encoder_fast_local_dc(encoder);
    }
    return static_cast<std::uint32_t>(legacy_codec_encoder_block_bytes(encoder));
}

// Lifted/OEM match records store the distance symbol base (LZ distance minus one).
// LAB_0014866c encodes new-match tags as stored_distance + 4.
inline std::uint32_t legacy_codec_match_buffer_lz_distance(const std::uint32_t *match_buffer,
        std::uint32_t distance_index) {
    if (match_buffer == nullptr || distance_index == 0U) {
        return 0U;
    }
    return match_buffer[distance_index - 1U];
}

}  // namespace kksdk
