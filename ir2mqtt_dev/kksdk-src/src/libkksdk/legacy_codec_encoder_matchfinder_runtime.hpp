#pragma once

#include <cstdint>

namespace kksdk {

// FUN_00141548 — reset hash tables, prime buffer, fill input, refresh limits.
void legacy_codec_encoder_matchfinder_runtime_init(void *matchfinder);

// FUN_001410ac — read from stream callback or consume direct-input remainder.
void legacy_codec_encoder_matchfinder_runtime_fill(void *matchfinder);

// FUN_00141a38 — normalize hash offsets, slide window, refill, refresh limits.
void legacy_codec_encoder_matchfinder_runtime_normalize(void *matchfinder);

extern "C" void legacy_codec_encoder_lifted_mf_init(void *matchfinder);
extern "C" std::uint8_t legacy_codec_encoder_lifted_mf_get_byte(void *matchfinder, int offset);
extern "C" int legacy_codec_encoder_lifted_mf_avail(void *matchfinder);
extern "C" void *legacy_codec_encoder_lifted_mf_get_ptr(void *matchfinder);
extern "C" int legacy_codec_encoder_lifted_mf_bt4_find(void *matchfinder, void *match_buffer);
extern "C" void legacy_codec_encoder_lifted_mf_bt4_skip(void *matchfinder, int count);
extern "C" int legacy_codec_encoder_lifted_mf_hc_find(void *matchfinder, void *match_buffer);
extern "C" void legacy_codec_encoder_lifted_mf_hc_skip(void *matchfinder, int count);
extern "C" int legacy_codec_encoder_lifted_mf_bt2_find(void *matchfinder, void *match_buffer);
extern "C" void legacy_codec_encoder_lifted_mf_bt2_skip(void *matchfinder, int count);
extern "C" int legacy_codec_encoder_lifted_mf_bt3_find(void *matchfinder, void *match_buffer);
extern "C" void legacy_codec_encoder_lifted_mf_bt3_skip(void *matchfinder, int count);

// OEM FUN_001420cc open-body sync: find_match must not advance the cursor.
void legacy_codec_encoder_set_matchfinder_find_hold_position(bool hold);
bool legacy_codec_encoder_matchfinder_find_hold_position();

}  // namespace kksdk
