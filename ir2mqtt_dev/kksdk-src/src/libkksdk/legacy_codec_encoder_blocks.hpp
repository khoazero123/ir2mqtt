#pragma once

#include "legacy_codec_range_encoder.hpp"

#include <cstdint>

namespace kksdk {

// FUN_0014c06c
void legacy_codec_encoder_refresh_len_price_slot(std::uint16_t *len_encoder,
        std::uint32_t pos_state, const std::int32_t *price_table);

// FUN_00146b84
void legacy_codec_encoder_refresh_align_prices(void *encoder);

// FUN_00146928
void legacy_codec_encoder_refresh_match_prices(void *encoder);

// FUN_0014682c
void legacy_codec_encoder_refresh_price_caches(void *encoder);

// Block F: bit-tree encoders used by FUN_001473d0 / LAB_0014866c
bool legacy_codec_encoder_encode_is_match(void *encoder, std::uint32_t pos_state,
        std::uint32_t lzma_state, bool is_match);

bool legacy_codec_encoder_encode_literal(void *encoder, std::uint32_t pos_state,
        const std::uint8_t *literal_ptr);

bool legacy_codec_encoder_encode_simple_literal_root(void *encoder, std::uint32_t literal_byte);

const std::uint8_t *native_apply_literal_ptr(void *encoder);

bool legacy_codec_encoder_encode_stream_entry_literal(void *encoder);

// Block F: match/rep/literal symbol encode (LAB_0014866c)
bool legacy_codec_encoder_encode_rep_match(void *encoder, std::uint32_t rep_index,
        std::uint32_t length, std::uint32_t pos_state);
// IsMatch..IsRep0Long only (no RepLen, no state/rep tail). For rep bisect probes.
bool legacy_codec_encoder_encode_rep_preamble(void *encoder, std::uint32_t rep_index,
        std::uint32_t length, std::uint32_t pos_state);
bool legacy_codec_encoder_encode_new_match(void *encoder, std::uint32_t distance,
        std::uint32_t length, std::uint32_t pos_state);
bool legacy_codec_encoder_encode_match_distance(void *encoder, std::uint32_t pos_state,
        std::uint32_t distance, std::uint32_t match_length);

// tag: 0xffffffff = literal; 0..3 = rep index; >=4 = new-match distance
bool legacy_codec_encoder_encode_symbol(void *encoder, std::uint32_t tag,
        std::uint32_t length, std::uint32_t pos_state, const std::uint8_t *literal_ptr);

constexpr std::uint32_t kEncoderTagLiteral = 0xffffffffU;
void legacy_codec_encoder_encode_length(std::uint16_t *len_encoder,
        LegacyCodecRangeEncoderStream &stream, std::uint32_t length_symbol,
        std::uint32_t pos_state, bool decrement_counter, void *oem_encoder);

// Terminal match body only (no flush tail). FUN_0014c378 when finish_mode != 0.
bool legacy_codec_encoder_encode_finish_terminal_match(void *encoder,
        std::uint32_t processed_pos_state);

bool legacy_codec_encoder_encode_finish_pos_slot(void *encoder, std::uint32_t pos_slot);

// FUN_0014c378 terminal + flush tail (honours finish_mode).
void legacy_codec_encoder_finish(void *encoder, std::uint32_t processed_pos_state);

// FUN_0014c378 flush tail only (after encode_finish_terminal_match).
void legacy_codec_encoder_finish_flush_only(void *encoder);

}  // namespace kksdk
