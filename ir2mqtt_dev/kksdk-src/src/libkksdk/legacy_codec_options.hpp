#pragma once

#include <array>
#include <cstdint>

namespace kksdk {

struct LegacyCodecOptions {
    int level = 5;
    std::uint32_t dictionary_size = 0xffffffffU;
    int lc = 3;
    int lp = 0;
    int pb = 2;
    int bt_mode = -1;
    int num_hash_bytes = -1;
    int fast_bytes = -1;
    int match_finder_type = -1;
    int match_finder_cycles = 0;
    int reduce_size = 0;
    bool write_end_mark = false;
};

struct LegacyCodecHeader {
    LegacyCodecOptions options;
    std::uint32_t probability_model_size = 0;
};

struct LegacyCodecResolvedOptions {
    std::uint32_t dictionary_size = 0;
    std::uint32_t match_finder_cycles = 0;
    int lc = 3;
    int lp = 0;
    int pb = 2;
    std::uint32_t fast_bytes = 0;
    bool literal_context_mode = false;
    std::uint32_t num_hash_bytes = 0;
    int match_finder_type = 4;
    int reduce_size = 0;
};

LegacyCodecOptions legacy_codec_default_options();
std::uint32_t legacy_codec_dictionary_bucket(std::uint32_t dictionary_size);
std::uint32_t legacy_codec_dictionary_position_slot(std::uint32_t dictionary_size);
std::uint32_t legacy_codec_default_dictionary_size(int level);
void legacy_codec_normalize_options(LegacyCodecOptions &options);
bool legacy_codec_resolve_options(const LegacyCodecOptions &options,
        LegacyCodecResolvedOptions &out);
bool legacy_codec_write_header(const LegacyCodecOptions &options,
        std::array<std::uint8_t, 5> &out);
bool legacy_codec_parse_properties(const std::uint8_t *header, std::uint32_t header_size,
        LegacyCodecOptions &out);
bool legacy_codec_parse_header(const std::uint8_t *header, std::uint32_t header_size,
        LegacyCodecHeader &out);

}  // namespace kksdk

extern "C" void kksdk_legacy_codec_default_options(void *options);
extern "C" unsigned int kksdk_legacy_codec_dictionary_bucket(unsigned int dictionary_size);
extern "C" unsigned int kksdk_legacy_codec_dictionary_position_slot(unsigned int dictionary_size);
extern "C" unsigned int kksdk_legacy_codec_default_dictionary_size(int level);
extern "C" void kksdk_legacy_codec_normalize_options(void *options);
extern "C" int kksdk_legacy_codec_resolve_options(const void *options, void *out);
extern "C" int kksdk_legacy_codec_write_header(const void *options, unsigned char *out,
        unsigned long long out_size);
extern "C" int kksdk_legacy_codec_parse_properties(const unsigned char *header,
        unsigned int header_size, void *out);
extern "C" int kksdk_legacy_codec_parse_header(const unsigned char *header,
        unsigned int header_size, void *out);
