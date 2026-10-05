#pragma once

#include "../include/ghidra_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kksdk_irdevice_remote_layout {
    unsigned short table_a_offset;
    unsigned short table_b_offset;
    unsigned short payload_end_offset;
} kksdk_irdevice_remote_layout;

typedef struct kksdk_irdevice_parse_result {
    int format;
    int bit_count;
} kksdk_irdevice_parse_result;

bool kksdk_irdevice_init(void *env, void *context, void *expected_hash);
unsigned short kksdk_irdevice_get_frequency();
unsigned long long kksdk_irdevice_create_remote_error(void);
unsigned long long kksdk_irdevice_create_remote(void *env, void *data);
void *kksdk_irdevice_encode(void *env, void *data, void *out_status);
void *kksdk_irdevice_parse(void *env, void *data);
unsigned int kksdk_irdevice_repeat_expanded_count(
        unsigned int duration_count, unsigned int repeat_count);
unsigned int kksdk_irdevice_repeat_expanded_index(
        unsigned int duration_count, unsigned int repeat, unsigned int index);
void kksdk_irdevice_reset_remote_view(void);
unsigned int kksdk_irdevice_set_remote_view(const unsigned char *data,
        unsigned int size);
unsigned int kksdk_irdevice_parse_remote_layout(const unsigned char *data,
        unsigned int size, kksdk_irdevice_remote_layout *layout);
unsigned int kksdk_irdevice_decode_frequency(const unsigned char *data,
        unsigned int size);
unsigned int kksdk_irdevice_build_frequency_frame(unsigned int packed_frequency,
        unsigned char *out, unsigned short out_capacity);
unsigned int kksdk_irdevice_duration_frame_size(unsigned short duration_count);
unsigned int kksdk_irdevice_carrier_from_packed(unsigned int packed_frequency);
unsigned int kksdk_irdevice_packed_frequency_carrier_mask(void);
unsigned int kksdk_irdevice_packed_frequency_valid_shift(void);
unsigned int kksdk_irdevice_packed_frequency_valid_mask(void);
unsigned int kksdk_irdevice_min_valid_carrier_field(void);
unsigned int kksdk_irdevice_microseconds_per_second(void);
int kksdk_irdevice_packed_frequency_is_valid(unsigned int packed_frequency);
unsigned int kksdk_irdevice_rounded_carrier(unsigned int packed_frequency);
unsigned int kksdk_irdevice_carrier_period_us(unsigned int carrier);
unsigned int kksdk_irdevice_duration_ticks(unsigned int duration, unsigned int period);
unsigned int kksdk_irdevice_encode_duration_frame(unsigned int packed_frequency,
        const unsigned int *durations, unsigned short duration_count, unsigned char *out,
        unsigned int out_capacity);
unsigned int kksdk_irdevice_command_size16(unsigned int command_size);
unsigned int kksdk_irdevice_command_payload_size(unsigned int command_size);
int kksdk_irdevice_encode_status_success(void);
int kksdk_irdevice_encode_status_remote_error(void);
int kksdk_irdevice_encode_status_capacity_error(void);
int kksdk_irdevice_encode_status_invalid_command(void);
unsigned int kksdk_irdevice_result_error(void);
unsigned int kksdk_irdevice_result_capacity_error(void);
unsigned int kksdk_irdevice_raw_payload_marker(void);
int kksdk_irdevice_payload_fits_template(
        unsigned int payload_size, unsigned int remote_payload_limit);
unsigned int kksdk_irdevice_raw_duration_pair_count(unsigned int payload_size);
int kksdk_irdevice_encode_command_frame(const unsigned char *command,
        unsigned int command_size, unsigned short *durations, unsigned short duration_capacity,
        unsigned short *duration_count, unsigned char *repeat_count);
int kksdk_irdevice_encode_template_command(const unsigned char *command,
        unsigned int command_size, unsigned short *durations, unsigned short duration_capacity,
        unsigned short *duration_count);
int kksdk_irdevice_encode_symbol_template_command(const unsigned char *command,
        unsigned int command_size, unsigned short *durations, unsigned short duration_capacity,
        unsigned short *duration_count);
unsigned int kksdk_irdevice_symbol_bits_for_count(unsigned int symbol_count);
unsigned int kksdk_irdevice_symbols_per_byte(unsigned int symbol_bits);
unsigned int kksdk_irdevice_symbol_from_byte(
        unsigned char value, unsigned int symbol_bits, unsigned int symbol_index);
unsigned int kksdk_irdevice_unknown_duration_flag(void);
unsigned int kksdk_irdevice_duration_flag_mask(void);
unsigned int kksdk_irdevice_duration_high_value_mask(void);
unsigned int kksdk_irdevice_duration_low_value_mask(void);
unsigned int kksdk_irdevice_duration_flag_from_high_byte(unsigned int high);
unsigned short kksdk_irdevice_duration_value_from_pair(
        unsigned int high, unsigned int low);
unsigned int kksdk_irdevice_zero_mark_template_offset(void);
unsigned int kksdk_irdevice_zero_space_template_offset(void);
unsigned int kksdk_irdevice_one_mark_template_offset(void);
unsigned int kksdk_irdevice_one_space_template_offset(void);
unsigned int kksdk_irdevice_append_binary_template_bits(const unsigned char *remote,
        unsigned char value, unsigned int bit_count, int lsb_first,
        unsigned short *durations, unsigned short capacity, unsigned short *count);
unsigned int kksdk_irdevice_append_duration(unsigned short *durations,
        unsigned short capacity, unsigned short *count, unsigned short value);
unsigned int kksdk_irdevice_sum_durations(const unsigned short *durations,
        unsigned short count);
unsigned short kksdk_irdevice_read_be16(const unsigned char *data);
unsigned int kksdk_irdevice_append_be16_duration(unsigned short *durations,
        unsigned short capacity, unsigned short *count, const unsigned char *data);
unsigned int kksdk_irdevice_append_be16_range(unsigned short *durations,
        unsigned short capacity, unsigned short *count, const unsigned char *data,
        unsigned short start, unsigned short end);
unsigned int kksdk_irdevice_find_template_segment(const unsigned char *remote,
        unsigned short table_start, unsigned short table_end, unsigned char segment_code,
        unsigned short *payload_start, unsigned short *payload_end);
unsigned int kksdk_irdevice_apply_template_segment(const unsigned char *remote,
        unsigned short table_start, unsigned short table_end, unsigned char segment_code,
        unsigned short *durations, unsigned short capacity, unsigned short *count);
unsigned int kksdk_irdevice_apply_duration_gap(unsigned short *durations,
        unsigned short capacity, unsigned short *count, unsigned short target_total,
        int merge_with_previous);
unsigned int kksdk_irdevice_apply_direct_gap(unsigned short *durations,
        unsigned short capacity, unsigned short *count, unsigned short gap,
        int merge_with_previous);
int kksdk_irdevice_value_in_window(int value, unsigned int base, unsigned int width);
unsigned int kksdk_irdevice_unknown_pulse_state(void);
int kksdk_irdevice_pulse_state_is_unknown(unsigned int state);
unsigned int kksdk_irdevice_mid_sequence_pause_min(void);
unsigned int kksdk_irdevice_terminal_pause_min(void);
unsigned int kksdk_irdevice_trailer_gap_min(void);
int kksdk_irdevice_accepts_terminal_pause(
        unsigned int count, unsigned int index, int value);
int kksdk_irdevice_accepts_trailer_gap(
        unsigned int count, unsigned int exact_count, int trailer_value);
unsigned int kksdk_irdevice_next_pulse_kind(unsigned int index);
int kksdk_irdevice_decode_fixed_32_bits(const int *durations, int count,
        char *out_bits, int *out_format);
int kksdk_irdevice_decode_alternating_14_bits(const int *durations,
        unsigned int count, char *out_bits);
int kksdk_irdevice_decode_alternating_bits(const int *durations,
        unsigned int count, char *out_bits, unsigned int target_bits);
int kksdk_irdevice_decode_alternating_15_bits(const int *durations,
        unsigned int count, char *out_bits);
int kksdk_irdevice_decode_odd_index_bits(const int *durations, int count,
        char *out_bits, unsigned int terminal_index, unsigned int header_base,
        unsigned int header_width, unsigned int zero_base, unsigned int zero_width,
        unsigned int one_base, unsigned int one_width, int exact_count);
int kksdk_irdevice_decode_23_odd_bits(const int *durations, int count,
        char *out_bits);
int kksdk_irdevice_decode_25_odd_bits(const int *durations, int count,
        char *out_bits);
int kksdk_irdevice_decode_23_high_odd_bits(const int *durations, int count,
        char *out_bits);
int kksdk_irdevice_classify_binary_pulse(int value, unsigned int zero_base,
        unsigned int zero_width, unsigned int one_base, unsigned int one_width);
int kksdk_irdevice_decode_variable_pulse_bits(const int *durations,
        unsigned int count, char *out_bits, unsigned int target_bits);
unsigned int kksdk_irdevice_variable_pulse_long_min(void);
unsigned int kksdk_irdevice_variable_pulse_default_split(void);
unsigned int kksdk_irdevice_variable_pulse_bit4_split(void);
int kksdk_irdevice_decode_21_variable_bits(const int *durations,
        unsigned int count, char *out_bits);
int kksdk_irdevice_decode_37_variable_bits(const int *durations,
        unsigned int count, char *out_bits);
int kksdk_irdevice_decode_4level_17_symbols(const int *durations, int count,
        char *out_symbols);
unsigned int kksdk_irdevice_special_decoder_target_bits(void);
int kksdk_irdevice_decode_20_special_bits(const int *durations,
        unsigned int count, char *out_bits);
int kksdk_irdevice_parse_pulse_protocol(const int *durations, unsigned int count,
        char *out_bits, kksdk_irdevice_parse_result *result);

#ifdef __cplusplus
}
#endif
