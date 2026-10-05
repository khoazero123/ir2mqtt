#pragma once

#include <cstdint>

namespace kksdk {

void legacy_codec_encoder_default_props(int *props);
int legacy_codec_encoder_apply_props(void *encoder, int *props);
int legacy_codec_encoder_write_header(void *encoder, char *out, unsigned long *in_out_size);
void legacy_codec_encoder_release_blob(std::intptr_t encoder, void **allocator,
        void *allocator_ctx);
void legacy_codec_encoder_init_new_blob(void *encoder);
std::intptr_t legacy_codec_encoder_alloc(void **allocator);

int legacy_codec_encoder_apply_props_bridge(std::intptr_t encoder, int *props);
int legacy_codec_encoder_write_header_bridge(std::intptr_t encoder, char *out,
        unsigned long *in_out_size);
void legacy_codec_encoder_release_bridge(std::intptr_t encoder, void *allocator_a,
        void *allocator_b);

void **legacy_codec_native_allocator_table();

}  // namespace kksdk
