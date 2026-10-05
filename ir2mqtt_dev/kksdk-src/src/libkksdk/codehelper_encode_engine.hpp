#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kksdk_remote_encoder kksdk_remote_encoder;

kksdk_remote_encoder *kksdk_remote_encoder_create(unsigned int remote_id);
void kksdk_remote_encoder_destroy(kksdk_remote_encoder *encoder);
int kksdk_remote_encoder_add_line(kksdk_remote_encoder *encoder, const char *line,
        unsigned long long line_size);
int kksdk_remote_encoder_encode(kksdk_remote_encoder *encoder, unsigned int power,
        unsigned int mode, unsigned int temperature, unsigned int wind_speed,
        unsigned int lr_wind_mode, unsigned int ud_wind_mode, unsigned int function_id,
        const unsigned char *ext_bytes, unsigned long long ext_bytes_len,
        const char *ext_string, unsigned char ***out_frames,
        unsigned long long *out_frame_count, unsigned long long **out_frame_sizes);

void kksdk_remote_encoder_free_output(unsigned char **frames,
        unsigned long long *frame_sizes, unsigned long long frame_count);

#ifdef __cplusplus
}
#endif
