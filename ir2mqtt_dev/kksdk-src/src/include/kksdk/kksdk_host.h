#ifndef KKSDK_HOST_H
#define KKSDK_HOST_H

#include <stddef.h>
#include <stdint.h>

#include "kk_crypto.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kksdk_remote_encoder kksdk_remote_encoder;

kksdk_remote_encoder *kksdk_remote_encoder_create(unsigned int remote_id);
void kksdk_remote_encoder_destroy(kksdk_remote_encoder *encoder);
int kksdk_remote_encoder_add_line(kksdk_remote_encoder *encoder, const char *line,
                                  unsigned long long line_size);
int kksdk_remote_encoder_encode(kksdk_remote_encoder *encoder, unsigned int power,
                                unsigned int mode, unsigned int temperature,
                                unsigned int wind_speed, unsigned int lr_wind_mode,
                                unsigned int ud_wind_mode, unsigned int function_id,
                                const unsigned char *ext_bytes,
                                unsigned long long ext_bytes_len, const char *ext_string,
                                unsigned char ***out_frames,
                                unsigned long long *out_frame_count,
                                unsigned long long **out_frame_sizes);
void kksdk_remote_encoder_free_output(unsigned char **frames,
                                      unsigned long long *frame_sizes,
                                      unsigned long long frame_count);

int kksdk_irdevice_encode_pulse(const uint8_t *remote_data, size_t remote_size,
                                const uint8_t *command, size_t command_size,
                                uint32_t **out_durations,
                                size_t *out_duration_count);
void kksdk_irdevice_free_pulse(uint32_t *durations);

void streamhelper_transform_encrypt(uint8_t *buffer, int length, int key);
void streamhelper_transform_decrypt(uint8_t *buffer, int length, int key);

int kksdk_streamhelper_lzma_encode(const uint8_t *input, size_t input_size,
                                   uint8_t **out, size_t *out_size);
void kksdk_streamhelper_lzma_free(void *ptr);

const char *kksdk_host_version(void);

#ifdef __cplusplus
}
#endif

#endif
