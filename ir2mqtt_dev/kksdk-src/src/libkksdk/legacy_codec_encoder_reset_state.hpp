#pragma once

namespace kksdk {

// FUN_00146274 — reset LZMA probability models and encode cursor fields.
void legacy_codec_encoder_reset_state(void *encoder);

}  // namespace kksdk
