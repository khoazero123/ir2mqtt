#pragma once

#include <cstdint>

namespace kksdk {

// Block map for FUN_001473d0:
//  A: range encoder stream       -> legacy_codec_range_encoder.*
//  B: length symbol encode       -> legacy_codec_encoder_encode_length (FUN_0014cbe0)
//  C: stream finish              -> legacy_codec_encoder_finish (FUN_0014c378, lifted)
//  D: optimal parse main loop    -> pending (LAB_0014796c body)
//  E: price refresh              -> legacy_codec_encoder_refresh_len_price_slot (FUN_0014c06c)
//  F: match/literal bit trees    -> legacy_codec_encoder_trees.cpp (encode_symbol)

// Block D: preamble + first literal fast path (FUN_001473d0 entry)
bool legacy_codec_encoder_preamble(void *encoder, int *early_status);
bool legacy_codec_encoder_try_first_literal(void *encoder);

bool legacy_codec_encoder_encode_stream_entry_literal(void *encoder);
bool legacy_codec_encoder_encode_fast_stream_entry_literal(void *encoder);
bool legacy_codec_encoder_encode_fast_stream_entry_after_warmup(void *encoder);

// FUN_001473d0 fast block==0 open: OEM prefetch + IsMatch + literal bits only (block stays 0, ldc=1).
bool legacy_codec_encoder_native_fast_block_zero_open(void *encoder);
// OEM-shaped open prep: sync MF (find_match side effects) then restore encoder blob from pre-sync snap.
bool legacy_codec_encoder_native_open_prepare_matchfinder(void *encoder);
// FUN_001473d0 block==0 open body: literal open + native match covering remainder.
bool legacy_codec_encoder_native_fast_block_zero_body(void *encoder);

bool legacy_codec_encoder_sync_matchfinder_when_idle(void *encoder,
        std::uint32_t *match_length_out, std::uint32_t *distance_index_out);
bool legacy_codec_encoder_try_encode_pending_eof_literal(void *encoder);

// Block D: parse choice commit (LAB_0014866c tail)
void legacy_codec_encoder_maybe_refresh_prices(void *encoder);
void legacy_codec_encoder_after_symbol(void *encoder, std::uint32_t encoded_length,
        std::uint32_t tag, bool native_rep_tail);
void legacy_codec_encoder_after_symbol_ex(void *encoder, std::uint32_t encoded_length,
        std::uint32_t tag, bool native_rep_tail, bool bump_match_count);
void legacy_codec_encoder_after_apply_batch(void *encoder);
bool legacy_codec_encoder_commit_symbol(void *encoder, std::uint32_t tag,
        std::uint32_t length, const std::uint8_t *literal_ptr);
bool legacy_codec_encoder_commit_symbol_ex(void *encoder, std::uint32_t tag,
        std::uint32_t length, const std::uint8_t *literal_ptr, bool bump_match_count);
bool legacy_codec_encoder_commit_parse_choice(void *encoder);
bool legacy_codec_encoder_commit_parse_choice_with_literal(void *encoder,
        const std::uint8_t *forced_literal_ptr);

// Block D: optimal-parse graph (LAB_0014796c / LAB_0014b7a4)
bool legacy_codec_encoder_parse_sync_matchfinder(void *encoder, std::uint32_t &match_length,
        std::uint32_t &distance_index);
bool legacy_codec_encoder_backtrack_prepare(void *encoder, std::uint32_t start_node);
bool legacy_codec_encoder_apply_parse_batch(void *encoder, std::uint32_t start_node);

int legacy_codec_encoder_core_step(void *encoder, int mode, unsigned long input_limit,
        unsigned int output_limit);

}  // namespace kksdk
