#pragma once

#include <cstdint>

namespace kksdk {

// FUN_00146c8c — encoder stream preparation (matchfinder + price-cache init).
std::uint64_t legacy_codec_encoder_stream_init(void *encoder, std::uint32_t mode,
        void **allocator, void *allocator_ctx);

// Eager MF buffer fill for probes; does not clear kStreamInitFlag.
void legacy_codec_encoder_stream_init_warm_matchfinder(void *encoder);

}  // namespace kksdk
