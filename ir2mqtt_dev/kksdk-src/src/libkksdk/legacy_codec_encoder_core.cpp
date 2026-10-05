#include "legacy_codec_encoder_core.hpp"

#include "legacy_codec_encoder_parse.hpp"

namespace kksdk {

int legacy_codec_encoder_core_step(void *encoder, int mode, unsigned long input_limit,
        unsigned int output_limit) {
    return legacy_codec_encoder_main_step(encoder, mode, input_limit, output_limit);
}

}  // namespace kksdk
