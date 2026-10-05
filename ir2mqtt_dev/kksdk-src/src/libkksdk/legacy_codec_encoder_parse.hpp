#pragma once

#include "legacy_codec_encoder_state.hpp"

#include <cstdint>

namespace kksdk {

// Optimal-parse graph nodes (FUN_001473d0 / LAB_0014796c).
namespace legacy_oem_parse {

constexpr std::size_t kNodeStride = 0x30U;
constexpr std::size_t kNodePriceBase = 0x4dcU;
constexpr std::size_t kNodeBase = 0x4e4U;
constexpr std::size_t kNodeFlag = 0x0U;
constexpr std::size_t kNodeParentLink = 0x8U;
constexpr std::size_t kNodeParentIndex = 0x10U;
constexpr std::size_t kNodeTag = 0x14U;
constexpr std::size_t kNodeAuxRep = 0x4U;
constexpr std::size_t kRootLengthSlot = 0x4f4U;
constexpr std::size_t kParseDistanceCode = 0x4d4U;

inline std::uint32_t *node_price(std::uint8_t *encoder, std::uint32_t node_index) {
    return reinterpret_cast<std::uint32_t *>(encoder + kNodePriceBase +
            static_cast<std::size_t>(node_index) * kNodeStride);
}

inline std::uint8_t *node_ptr(std::uint8_t *encoder, std::uint32_t node_index) {
    return encoder + kNodeBase + static_cast<std::size_t>(node_index) * kNodeStride;
}

inline std::uint32_t *node_flag(std::uint8_t *encoder, std::uint32_t node_index) {
    return reinterpret_cast<std::uint32_t *>(node_ptr(encoder, node_index) + kNodeFlag);
}

inline void **node_parent_link(std::uint8_t *encoder, std::uint32_t node_index) {
    return reinterpret_cast<void **>(node_ptr(encoder, node_index) + kNodeParentLink);
}

inline std::uint32_t *node_parent_index(std::uint8_t *encoder, std::uint32_t node_index) {
    return reinterpret_cast<std::uint32_t *>(node_ptr(encoder, node_index) + kNodeParentIndex);
}

inline std::uint32_t *node_tag(std::uint8_t *encoder, std::uint32_t node_index) {
    return reinterpret_cast<std::uint32_t *>(node_ptr(encoder, node_index) + kNodeTag);
}

inline std::uint32_t *node_aux_rep(std::uint8_t *encoder, std::uint32_t node_index) {
    return reinterpret_cast<std::uint32_t *>(node_ptr(encoder, node_index) + kNodeAuxRep);
}

}  // namespace legacy_oem_parse

bool legacy_codec_encoder_backtrack_prepare(void *encoder, std::uint32_t start_node);
bool legacy_codec_encoder_apply_parse_batch(void *encoder, std::uint32_t start_node);
bool legacy_codec_encoder_apply_literal_node_chain(void *encoder, std::uint32_t node_count);
bool legacy_codec_encoder_parse_47d10_stream_tail_batch(void *encoder,
        std::uint32_t node_count);

// LAB_0014796c: refresh matchfinder when literal_mode==0 and parse node catches up.
bool legacy_codec_encoder_parse_sync_matchfinder(void *encoder, std::uint32_t &match_length,
        std::uint32_t &distance_index);

enum class LegacyCodecEncoderParseStepStatus {
    kGraphStarted,
    kBacktrackReady,
    kApplyNow,
    kUnhandledNative,
    kError,
};

struct LegacyCodecEncoderParseStepResult {
    LegacyCodecEncoderParseStepStatus status = LegacyCodecEncoderParseStepStatus::kUnhandledNative;
    std::uint32_t path_length = 0U;
    std::uint32_t chosen_tag = 0U;
    bool native_tail_remainder = false;
};

// LAB_00148180..4851c: probe rep lengths at current byte.
bool legacy_codec_encoder_probe_rep_lengths(void *encoder, std::uint32_t avail_limit,
        std::uint32_t rep_lengths[4]);
// OEM LAB_00147b44 fast greedy rep scan (get_buffer dual-byte gate).
void legacy_codec_encoder_fast_probe_lab_47b44_rep_lengths(void *encoder, std::uint32_t avail,
        std::uint32_t rep_lengths[4]);
std::uint32_t legacy_codec_encoder_pick_best_rep_index(const std::uint32_t rep_lengths[4]);

struct LegacyCodecEncoderParsePricingContext {
    std::uint32_t pos_state = 0U;
    std::uint32_t lzma_state = 0U;
    std::int32_t lit_state_price = 0;
    std::int32_t is_match_xor_price = 0;
    std::uint32_t path_length = 1U;
    std::uint32_t rep_lengths[4] = {};
};

// joined_r0x00149730: literal/rep0-short price + first parse-graph node.
bool legacy_codec_encoder_parse_write_literal_node(void *encoder, std::uint32_t pos_state,
        std::uint32_t main_match_length, std::uint32_t match_distance_index,
        const std::uint32_t rep_lengths[4], std::uint32_t chosen_rep_length,
        std::uint32_t &chosen_tag, LegacyCodecEncoderParsePricingContext &pricing);

// LAB_0014a07c..4b7a4: rep/match graph pricing. Backtrack deferred when needs_greedy.
bool legacy_codec_encoder_parse_price_graph(void *encoder,
        const LegacyCodecEncoderParsePricingContext &pricing, std::uint32_t main_match_length,
        std::uint32_t match_distance_index, std::uint32_t &backtrack_node, bool &needs_greedy,
        std::uint32_t &frontier_limit);

// LAB_0014a35c: greedy graph extension before backtrack when path_length > 1.
bool legacy_codec_encoder_greedy_extend_graph(void *encoder, std::uint32_t frontier_limit,
        std::uint32_t pos_state, std::uint32_t &backtrack_node);

// literal_mode!=0 fast path (LAB_00147b44..4866c, no parse graph).
LegacyCodecEncoderParseStepResult legacy_codec_encoder_fast_parse_step(void *encoder,
        std::uint32_t pos_state);

// Partial LAB_0014796c (literal_mode==0 entry). Unlifted paths return kUnhandledNative.
LegacyCodecEncoderParseStepResult legacy_codec_encoder_optimal_parse_step(void *encoder,
        std::uint32_t pos_state);

// Lifted replacement for OEM 473d0 inner block (47860..4b920). Unhandled paths return status 2.
int legacy_codec_encoder_try_lifted_core_block(void *encoder, int mode, unsigned long input_limit,
        unsigned int output_limit);

// Return status 2 on old incomplete multi-byte paths so lift_fast_loop stays native.
void legacy_codec_encoder_set_lift_fast_loop_fail_on_unhandled(bool disabled);
bool legacy_codec_encoder_lift_fast_loop_fail_on_unhandled();

void legacy_codec_encoder_set_lift_fast_stream_native_from_zero(bool enabled);
bool legacy_codec_encoder_lift_fast_stream_native_from_zero();

void legacy_codec_encoder_set_lift_fast_full_native(bool enabled);
bool legacy_codec_encoder_lift_fast_full_native();

// Skip forced literal@block==0; OEM open body encodes match from byte 0 (no lifted warmup).
void legacy_codec_encoder_set_fast_native_open_parse(bool enabled);
bool legacy_codec_encoder_fast_native_open_parse();

// Experimental: lifted parse open for non-uniform (default off; still not bit-exact vs OEM).
void legacy_codec_encoder_set_lift_fast_try_native_non_uniform_open(bool enabled);
bool legacy_codec_encoder_lift_fast_try_native_non_uniform_open();

// OEM FUN_001473d0 loop entry block_bytes snapshot (native fast finish sync).
std::uint64_t legacy_codec_encoder_fast_loop_block_snapshot();

// True when the full input buffer is a single repeated byte (repeat_a open path).
bool legacy_codec_encoder_open_input_is_uniform(void *encoder);

// Input byte count from stream read callback (0 when unavailable).
int legacy_codec_encoder_open_input_size(void *encoder);

struct LegacyCodecEncoderContinueStatus {
    bool keep_encoding = false;
    int return_code = 0;
};

LegacyCodecEncoderContinueStatus legacy_codec_encoder_check_block_continue(void *encoder,
        int mode, unsigned long input_limit, unsigned int output_limit,
        std::uint32_t block_start_pos_state, std::uint32_t current_pos_state);

bool legacy_codec_encoder_extend_match_length(void *encoder, std::uint32_t match_index,
        std::uint32_t avail_limit, std::uint32_t &match_length);
void legacy_codec_encoder_finalize_stream_block(void *encoder);
bool legacy_codec_encoder_fast_native_47d10_apply_tail(void *encoder, std::uint32_t tail_node);
bool legacy_codec_encoder_fast_parse_should_fold_stream_tail(void *encoder);
int legacy_codec_encoder_main_step(void *encoder, int mode, unsigned long input_limit,
        unsigned int output_limit);

}  // namespace kksdk
