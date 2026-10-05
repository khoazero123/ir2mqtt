#pragma once

#include "../include/ghidra_types.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*kksdk_codehelper_stream_read_byte_fn)(void *stream);
typedef int (*kksdk_codehelper_stream_write_byte_fn)(void *stream, unsigned int value);

typedef struct kksdk_codehelper_input_stream {
    const unsigned char *cursor;
    const unsigned char *end;
    void *stream;
    kksdk_codehelper_stream_read_byte_fn read_byte;
} kksdk_codehelper_input_stream;

typedef struct kksdk_codehelper_putback_stream {
    unsigned char *begin;
    unsigned char *cursor;
    unsigned char *high_water;
    int allow_mismatch;
} kksdk_codehelper_putback_stream;

typedef struct kksdk_codehelper_output_stream {
    unsigned char *cursor;
    unsigned char *end;
    void *stream;
    kksdk_codehelper_stream_write_byte_fn write_byte;
} kksdk_codehelper_output_stream;

typedef struct kksdk_codehelper_hash_node {
    struct kksdk_codehelper_hash_node *next;
    unsigned long long key;
    unsigned int tag;
    void *payload;
} kksdk_codehelper_hash_node;

typedef struct kksdk_codehelper_byte_buffer {
    unsigned char *data;
    unsigned long long size;
} kksdk_codehelper_byte_buffer;

typedef struct kksdk_codehelper_patch_record {
    unsigned int tag;
    const unsigned char *data;
    unsigned long long size;
} kksdk_codehelper_patch_record;

typedef struct kksdk_codehelper_config_line {
    unsigned int tag;
    const char *payload;
    unsigned long long payload_size;
} kksdk_codehelper_config_line;

typedef struct kksdk_codehelper_remote_config kksdk_codehelper_remote_config;

void *kksdk_codehelper_encode(void *env, unsigned int remote_type, long long remote_handle,
        unsigned int power, unsigned int mode, unsigned int temperature, unsigned int wind_speed,
        unsigned int lr_wind_mode, unsigned int ud_wind_mode, unsigned int function_id,
        void *ext_bytes, void *ext_string);
unsigned long long kksdk_codehelper_init_remote(void *env, unsigned int remote_id,
        unsigned int remote_type, void *names, void *out_remote);
void kksdk_codehelper_release_remote(unsigned int remote_id, void *remote);
unsigned int kksdk_codehelper_byte_value(unsigned int value);
unsigned int kksdk_codehelper_shift5(unsigned int value);
unsigned int kksdk_codehelper_merge_byte_window(unsigned int current_byte,
        unsigned int bit_count, unsigned int bit_offset, unsigned int previous_byte);
unsigned int kksdk_codehelper_extract_shifted_byte_bits(unsigned int value,
        unsigned int bit_offset, int bit_count);
unsigned long long kksdk_codehelper_bounded_substring(const char *input,
        unsigned long long input_size, unsigned long long offset, unsigned long long max_count,
        char *out, unsigned long long out_capacity);
long long kksdk_codehelper_find_substring(const char *haystack,
        unsigned long long haystack_size, const char *needle, unsigned long long needle_size,
        unsigned long long start_offset);
long long kksdk_codehelper_invalid_index_result(void);
int kksdk_codehelper_byte_from_hex_nibbles(int high, int low);
long long kksdk_codehelper_parse_hex_record(const char *input,
        unsigned long long input_size, unsigned long long offset, unsigned char *out,
        unsigned long long out_capacity, unsigned long long *decoded_size);
unsigned long long kksdk_codehelper_hex_record_payload_chars(unsigned long long byte_count);
unsigned long long kksdk_codehelper_hex_record_total_chars(unsigned long long byte_count);
void kksdk_codehelper_noop_callback(void);
unsigned int kksdk_codehelper_zero_status(void);
unsigned int kksdk_codehelper_eof_status(void);
int kksdk_codehelper_stream_error(void);
unsigned int kksdk_codehelper_putback_any_value(void);
unsigned int kksdk_codehelper_stream_copy_chunk_limit(void);
unsigned long long kksdk_codehelper_stream_read(kksdk_codehelper_input_stream *input,
        unsigned char *out, unsigned long long requested);
int kksdk_codehelper_stream_get_byte(kksdk_codehelper_input_stream *input);
int kksdk_codehelper_stream_peek_byte(const kksdk_codehelper_input_stream *input);
int kksdk_codehelper_stream_putback_byte(kksdk_codehelper_putback_stream *input,
        unsigned int value);
unsigned long long kksdk_codehelper_stream_write(kksdk_codehelper_output_stream *output,
        const unsigned char *data, unsigned long long requested);
int kksdk_codehelper_stream_put_byte(kksdk_codehelper_output_stream *output,
        unsigned int value);
unsigned long long kksdk_codehelper_append_fill(char *buffer, unsigned long long size,
        unsigned long long capacity, unsigned long long count, unsigned int value);
unsigned long long kksdk_codehelper_append_bytes(char *buffer, unsigned long long size,
        unsigned long long capacity, const char *data, unsigned long long count);
unsigned long long kksdk_codehelper_string_growth_capacity(
        unsigned long long current_capacity, unsigned long long extra_needed);
unsigned long long kksdk_codehelper_string_min_capacity(void);
unsigned long long kksdk_codehelper_string_capacity_alignment(void);
unsigned long long kksdk_codehelper_string_capacity_alignment_mask(void);
unsigned long long kksdk_codehelper_string_max_capacity(void);
unsigned long long kksdk_codehelper_string_double_growth_limit(void);
unsigned long long kksdk_codehelper_rebuild_with_gap(char *out,
        unsigned long long out_capacity, const char *input, unsigned long long input_size,
        unsigned long long prefix_size, unsigned long long remove_count,
        unsigned long long gap_size);
unsigned long long kksdk_codehelper_rebuild_with_insert(char *out,
        unsigned long long out_capacity, const char *input, unsigned long long input_size,
        unsigned long long prefix_size, unsigned long long remove_count,
        const char *insert, unsigned long long insert_size);
unsigned long long kksdk_codehelper_clamped_remove_count(
        unsigned long long size, unsigned long long offset, unsigned long long max_remove);
unsigned long long kksdk_codehelper_replace_size_after(
        unsigned long long size, unsigned long long removed, unsigned long long insert_size);
unsigned long long kksdk_codehelper_replace_bytes(char *buffer,
        unsigned long long size, unsigned long long capacity, unsigned long long offset,
        unsigned long long max_remove, const char *insert, unsigned long long insert_size);
int kksdk_codehelper_is_power_of_two_bucket_count(unsigned long long bucket_count);
unsigned long long kksdk_codehelper_hash_bucket_index(
        unsigned long long bucket_count, unsigned long long key);
int kksdk_codehelper_hash_node_matches(
        const kksdk_codehelper_hash_node *node, unsigned long long key, unsigned int tag);
kksdk_codehelper_hash_node *kksdk_codehelper_find_hash_node(
        kksdk_codehelper_hash_node **buckets, unsigned long long bucket_count,
        unsigned long long key, unsigned int tag);
int kksdk_codehelper_select_patch_index(unsigned int tag, int requested_index,
        unsigned long long record_count);
int kksdk_codehelper_patch_tag_uses_adjusted_index(unsigned int tag);
int kksdk_codehelper_patch_tag_requires_single_record(unsigned int tag);
int kksdk_codehelper_patch_tag_is_adjusted_byte_pairs(unsigned int tag);
int kksdk_codehelper_patch_tag_is_adjusted_bit_triplets(unsigned int tag);
int kksdk_codehelper_patch_tag_is_single_byte_pairs(unsigned int tag);
int kksdk_codehelper_patch_tag_is_single_bit_triplets(unsigned int tag);
unsigned int kksdk_codehelper_patch_tag_adjusted_byte_pairs_id(void);
unsigned int kksdk_codehelper_patch_tag_adjusted_bit_triplets_id(void);
unsigned int kksdk_codehelper_patch_tag_single_byte_pairs_id(void);
unsigned int kksdk_codehelper_patch_tag_single_bit_triplets_id(void);
unsigned int kksdk_codehelper_result_error(void);
unsigned int kksdk_codehelper_apply_byte_pairs(kksdk_codehelper_byte_buffer *buffer,
        const unsigned char *records, unsigned long long record_size);
unsigned int kksdk_codehelper_apply_repeat_add_pairs(kksdk_codehelper_byte_buffer *buffer,
        const unsigned char *records, unsigned long long record_size, unsigned int repeat_count);
unsigned long long kksdk_codehelper_bit_capacity(unsigned long long byte_size);
unsigned int kksdk_codehelper_patch_triplet_max_bits(void);
int kksdk_codehelper_is_valid_bit_range(
        unsigned int start_bit, unsigned int end_bit, unsigned long long bit_capacity);
unsigned int kksdk_codehelper_apply_bit_triplets(kksdk_codehelper_byte_buffer *buffer,
        const unsigned char *records, unsigned long long record_size, unsigned int add_count);
unsigned int kksdk_codehelper_apply_literal_bit_triplets(kksdk_codehelper_byte_buffer *buffer,
        const unsigned char *records, unsigned long long record_size);
unsigned int kksdk_codehelper_apply_oem_ext_bit_triplets(kksdk_codehelper_byte_buffer *buffer,
        const unsigned char *records, unsigned long long record_size);
void kksdk_codehelper_apply_type2_compact_bits(unsigned char *buffer,
        unsigned long long buffer_size, const unsigned char *body,
        unsigned long long body_size, int repeat, int byte_offset);
unsigned int kksdk_codehelper_apply_oem_bit_triplets_with_add(
        kksdk_codehelper_byte_buffer *buffer, const unsigned char *records,
        unsigned long long record_size, unsigned int add_count);
unsigned int kksdk_codehelper_apply_patch_record(kksdk_codehelper_byte_buffer *buffer,
        const kksdk_codehelper_patch_record *record, unsigned int parameter);
int kksdk_codehelper_patch_tag_is_byte_pairs(unsigned int tag);
int kksdk_codehelper_patch_tag_is_repeat_add(unsigned int tag);
int kksdk_codehelper_patch_tag_is_bit_triplets_with_add(unsigned int tag);
int kksdk_codehelper_patch_tag_is_bit_triplets(unsigned int tag);
unsigned long long kksdk_codehelper_bit_byte_index(unsigned long long bit_index);
unsigned int kksdk_codehelper_bit_shift(unsigned long long bit_index);
unsigned int kksdk_codehelper_get_bit(const unsigned char *data, unsigned long long bit_index);
int kksdk_codehelper_checksum_mode_is_byte_sum(unsigned int mode);
int kksdk_codehelper_checksum_mode_is_nibble_sum(unsigned int mode);
int kksdk_codehelper_checksum_mode_is_output_nibble(unsigned int mode);
int kksdk_codehelper_checksum_mode_is_complement(unsigned int mode);
int kksdk_codehelper_checksum_mode_is_byte_sum_plain(unsigned int mode);
int kksdk_codehelper_checksum_mode_is_byte_sum_complement(unsigned int mode);
int kksdk_codehelper_checksum_mode_is_nibble_sum_plain(unsigned int mode);
int kksdk_codehelper_checksum_mode_is_nibble_sum_complement(unsigned int mode);
int kksdk_codehelper_checksum_mode_is_output_nibble_plain(unsigned int mode);
int kksdk_codehelper_checksum_mode_is_output_nibble_complement(unsigned int mode);
unsigned int kksdk_codehelper_checksum_mode_byte_sum_plain_id(void);
unsigned int kksdk_codehelper_checksum_mode_byte_sum_complement_id(void);
unsigned int kksdk_codehelper_checksum_mode_nibble_sum_plain_id(void);
unsigned int kksdk_codehelper_checksum_mode_nibble_sum_complement_id(void);
unsigned int kksdk_codehelper_checksum_mode_output_nibble_plain_id(void);
unsigned int kksdk_codehelper_checksum_mode_output_nibble_complement_id(void);
unsigned int kksdk_codehelper_nibble_byte_index(unsigned int nibble_index);
int kksdk_codehelper_nibble_in_bounds(unsigned long long size, unsigned int nibble_index);
int kksdk_codehelper_nibble_is_low(unsigned int nibble_index);
unsigned int kksdk_codehelper_low_nibble(unsigned int value);
unsigned int kksdk_codehelper_high_nibble(unsigned int value);
unsigned int kksdk_codehelper_read_nibble(const unsigned char *data,
        unsigned long long size, unsigned int nibble_index);
void kksdk_codehelper_write_nibble(unsigned char *data,
        unsigned long long size, unsigned int nibble_index, unsigned int value);
unsigned int kksdk_codehelper_sum_byte_range(const unsigned char *data,
        unsigned long long size, unsigned int start, unsigned int end, unsigned int seed);
unsigned int kksdk_codehelper_sum_nibble_range(const unsigned char *data,
        unsigned long long size, unsigned int start, unsigned int end, unsigned int seed);
unsigned int kksdk_codehelper_checksum_seed_from_record(
        const unsigned char *record, unsigned long long record_size);
unsigned int kksdk_codehelper_apply_checksum_record(kksdk_codehelper_byte_buffer *buffer,
        const unsigned char *record, unsigned long long record_size);
int kksdk_codehelper_parse_config_line(const char *input,
        unsigned long long input_size, kksdk_codehelper_config_line *out_line);
unsigned long long kksdk_codehelper_split_payload(const char *input,
        unsigned long long input_size, char delimiter, kksdk_codehelper_config_line *out_parts,
        unsigned long long out_capacity);
int kksdk_codehelper_config_tag_is_patch(unsigned int tag);
int kksdk_codehelper_config_tag_is_checksum(unsigned int tag);
int kksdk_codehelper_config_tag_is_key(unsigned int tag);
unsigned int kksdk_codehelper_config_tag_checksum_id(void);
unsigned int kksdk_codehelper_config_tag_key_id(void);
kksdk_codehelper_remote_config *kksdk_codehelper_create_remote_config(unsigned int remote_id);
void kksdk_codehelper_destroy_remote_config(kksdk_codehelper_remote_config *config);
unsigned int kksdk_codehelper_add_config_line(kksdk_codehelper_remote_config *config,
        const char *line, unsigned long long line_size);
int kksdk_codehelper_config_group_is_fixed(unsigned int group);
int kksdk_codehelper_config_group_is_patch(unsigned int group);
int kksdk_codehelper_config_group_is_checksum(unsigned int group);
int kksdk_codehelper_config_group_is_key(unsigned int group);
unsigned int kksdk_codehelper_config_group_fixed_id(void);
unsigned int kksdk_codehelper_config_group_patch_id(void);
unsigned int kksdk_codehelper_config_group_checksum_id(void);
unsigned int kksdk_codehelper_config_group_key_id(void);
unsigned long long kksdk_codehelper_count_config_records(
        const kksdk_codehelper_remote_config *config, unsigned int group);

#ifdef __cplusplus
}
#endif
