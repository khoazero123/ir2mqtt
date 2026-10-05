#include "legacy_codec_options.hpp"

#include <cstddef>

namespace kksdk {

LegacyCodecOptions legacy_codec_default_options() {
    LegacyCodecOptions options;
    options.level = 5;
    options.dictionary_size = 0xffffffffU;
    options.lc = 3;
    options.lp = 0;
    options.pb = 2;
    options.bt_mode = -1;
    options.num_hash_bytes = -1;
    options.fast_bytes = -1;
    options.match_finder_type = -1;
    options.match_finder_cycles = 0;
    options.reduce_size = 0;
    options.write_end_mark = false;
    return options;
}

std::uint32_t legacy_codec_dictionary_bucket(std::uint32_t dictionary_size) {
    static constexpr std::uint32_t kBuckets[] = {
            0x00001000U, 0x00001800U, 0x00002000U, 0x00003000U,
            0x00004000U, 0x00006000U, 0x00008000U, 0x0000c000U,
            0x00010000U, 0x00018000U, 0x00020000U, 0x00030000U,
            0x00040000U, 0x00060000U, 0x00080000U, 0x000c0000U,
            0x00100000U, 0x00180000U, 0x00200000U, 0x00300000U,
            0x00400000U, 0x00600000U, 0x00800000U, 0x00c00000U,
            0x01000000U, 0x01800000U, 0x02000000U, 0x03000000U,
            0x04000000U, 0x06000000U, 0x08000000U, 0x0c000000U,
            0x10000000U, 0x18000000U, 0x20000000U, 0x30000000U,
            0x40000000U, 0x60000000U, 0x80000000U, 0xc0000000U,
    };

    for (std::uint32_t bucket : kBuckets) {
        if (dictionary_size <= bucket) {
            return bucket;
        }
    }
    return dictionary_size;
}

std::uint32_t legacy_codec_dictionary_position_slot(std::uint32_t dictionary_size) {
    if (dictionary_size < 2U) {
        return 0;
    }
    if (dictionary_size == 2U) {
        return 2;
    }

    std::uint32_t slot = 4;
    std::uint32_t limit = 4;
    while (limit < 0x40000000U) {
        if (dictionary_size < limit + 1U) {
            return slot;
        }
        limit <<= 1U;
        slot += 2U;
    }
    return dictionary_size < 0x40000001U ? 0x3cU : 0x3eU;
}

std::uint32_t legacy_codec_default_dictionary_size(int level) {
    const int resolved_level = level < 0 ? 5 : level;
    if (resolved_level < 6) {
        return 1U << ((resolved_level * 2) + 14U);
    }
    return resolved_level == 6 ? 0x02000000U : 0x04000000U;
}

void legacy_codec_normalize_options(LegacyCodecOptions &options) {
    if (options.level < 0) {
        options.level = 5;
    }
    if (options.dictionary_size == 0) {
        options.dictionary_size = legacy_codec_default_dictionary_size(options.level);
    }
    if (options.lc < 0) {
        options.lc = 3;
    }
    if (options.lp < 0) {
        options.lp = 0;
    }
    if (options.pb < 0) {
        options.pb = 2;
    }
    if (options.bt_mode < 0) {
        options.bt_mode = options.level > 4 ? 1 : 0;
    }
    if (options.fast_bytes < 0) {
        options.fast_bytes = options.level > 6 ? 0x40 : 0x20;
    }
    if (options.num_hash_bytes < 0) {
        options.num_hash_bytes = options.bt_mode != 0 ? 1 : 0;
    }
    if (options.match_finder_type < 0) {
        options.match_finder_type = 4;
    }
    if (options.match_finder_cycles == 0) {
        const int base = (options.fast_bytes >> 1) + 0x10;
        options.match_finder_cycles = options.num_hash_bytes == 0 ? base >> 1 : base;
    }
    if (!options.write_end_mark) {
        options.write_end_mark = true;
    }
}

bool legacy_codec_resolve_options(const LegacyCodecOptions &options,
        LegacyCodecResolvedOptions &out) {
    const int level = options.level < 0 ? 5 : options.level;
    std::uint32_t dictionary_size = options.dictionary_size;
    if (dictionary_size == 0) {
        dictionary_size = legacy_codec_default_dictionary_size(level);
    }

    const int lc = options.lc < 0 ? 3 : options.lc;
    const int lp = options.lp < 0 ? 0 : options.lp;
    const int pb = options.pb < 0 ? 2 : options.pb;
    const std::uint32_t bt_mode =
            options.bt_mode < 0 ? static_cast<std::uint32_t>(level > 4) :
            static_cast<std::uint32_t>(options.bt_mode);

    std::uint32_t fast_bytes = level > 6 ? 0x40U : 0x20U;
    if (options.fast_bytes >= 0) {
        fast_bytes = static_cast<std::uint32_t>(options.fast_bytes);
    }

    std::uint32_t num_hash_bytes = bt_mode != 0 ? 1U : 0U;
    if (options.num_hash_bytes >= 0) {
        num_hash_bytes = static_cast<std::uint32_t>(options.num_hash_bytes);
    }

    int match_finder_type = options.match_finder_type < 0 ? 4 : options.match_finder_type;
    std::uint32_t match_finder_cycles = static_cast<std::uint32_t>(options.match_finder_cycles);
    if (match_finder_cycles == 0) {
        match_finder_cycles = ((fast_bytes >> 1U) + 0x10U) >> (num_hash_bytes == 0 ? 1U : 0U);
    }

    if (lc >= 9 || lp < 0 || lp >= 5 || pb >= 5 || dictionary_size >= 0x40000001U) {
        return false;
    }

    if (fast_bytes < 6) {
        fast_bytes = 5;
    } else if (fast_bytes > 0x110U) {
        fast_bytes = 0x111U;
    }

    if (num_hash_bytes == 0) {
        match_finder_type = 4;
    } else if (match_finder_type < 2) {
        match_finder_type = 2;
    } else if (match_finder_type > 3) {
        match_finder_type = 4;
    }

    out.dictionary_size = dictionary_size;
    out.match_finder_cycles = match_finder_cycles;
    out.lc = lc;
    out.lp = lp < 0 ? 0 : lp;
    out.pb = pb;
    out.fast_bytes = fast_bytes;
    out.literal_context_mode = bt_mode == 0;
    out.num_hash_bytes = num_hash_bytes;
    out.match_finder_type = match_finder_type;
    out.reduce_size = options.reduce_size;
    return true;
}

bool legacy_codec_write_header(const LegacyCodecOptions &options,
        std::array<std::uint8_t, 5> &out) {
    if (options.lc < 0 || options.lc > 8 ||
            options.lp < 0 || options.lp > 4 ||
            options.pb < 0 || options.pb > 4) {
        return false;
    }

    const std::uint32_t bucket = legacy_codec_dictionary_bucket(options.dictionary_size);
    out[0] = static_cast<std::uint8_t>(((options.pb * 5 + options.lp) * 9) + options.lc);
    out[1] = static_cast<std::uint8_t>(bucket & 0xffU);
    out[2] = static_cast<std::uint8_t>((bucket >> 8) & 0xffU);
    out[3] = static_cast<std::uint8_t>((bucket >> 16) & 0xffU);
    out[4] = static_cast<std::uint8_t>((bucket >> 24) & 0xffU);
    return true;
}

bool legacy_codec_parse_properties(const std::uint8_t *header, std::uint32_t header_size,
        LegacyCodecOptions &out) {
    if (header == nullptr || header_size < 5) {
        return false;
    }

    const std::uint8_t props = header[0];
    if (props >= 0xe1U) {
        return false;
    }

    LegacyCodecOptions options = legacy_codec_default_options();
    options.lc = props % 9U;
    const std::uint8_t remainder = props / 9U;
    options.lp = remainder % 5U;
    options.pb = remainder / 5U;

    std::uint32_t dictionary_size =
            static_cast<std::uint32_t>(header[1]) |
            (static_cast<std::uint32_t>(header[2]) << 8U) |
            (static_cast<std::uint32_t>(header[3]) << 16U) |
            (static_cast<std::uint32_t>(header[4]) << 24U);
    if (dictionary_size < 0x1000U) {
        dictionary_size = 0x1000U;
    }
    options.dictionary_size = dictionary_size;

    out = options;
    return true;
}

bool legacy_codec_parse_header(const std::uint8_t *header, std::uint32_t header_size,
        LegacyCodecHeader &out) {
    LegacyCodecOptions options;
    if (!legacy_codec_parse_properties(header, header_size, options)) {
        return false;
    }

    out.options = options;
    out.probability_model_size =
            (0x300U << ((static_cast<std::uint32_t>(options.lp) + options.lc) & 0x1fU)) +
            0x736U;
    return true;
}

}  // namespace kksdk

extern "C" void kksdk_legacy_codec_default_options(void *options) {
    if (options == nullptr) {
        return;
    }
    *static_cast<kksdk::LegacyCodecOptions *>(options) = kksdk::legacy_codec_default_options();
}

extern "C" unsigned int kksdk_legacy_codec_dictionary_bucket(unsigned int dictionary_size) {
    return kksdk::legacy_codec_dictionary_bucket(dictionary_size);
}

extern "C" unsigned int kksdk_legacy_codec_dictionary_position_slot(unsigned int dictionary_size) {
    return kksdk::legacy_codec_dictionary_position_slot(dictionary_size);
}

extern "C" unsigned int kksdk_legacy_codec_default_dictionary_size(int level) {
    return kksdk::legacy_codec_default_dictionary_size(level);
}

extern "C" void kksdk_legacy_codec_normalize_options(void *options) {
    if (options == nullptr) {
        return;
    }
    kksdk::legacy_codec_normalize_options(*static_cast<kksdk::LegacyCodecOptions *>(options));
}

extern "C" int kksdk_legacy_codec_resolve_options(const void *options, void *out) {
    if (options == nullptr || out == nullptr) {
        return 5;
    }
    kksdk::LegacyCodecResolvedOptions resolved{};
    if (!kksdk::legacy_codec_resolve_options(
                *static_cast<const kksdk::LegacyCodecOptions *>(options), resolved)) {
        return 5;
    }
    *static_cast<kksdk::LegacyCodecResolvedOptions *>(out) = resolved;
    return 0;
}

extern "C" int kksdk_legacy_codec_write_header(const void *options, unsigned char *out,
        unsigned long long out_size) {
    if (options == nullptr || out == nullptr || out_size < 5) {
        return 5;
    }

    std::array<std::uint8_t, 5> header{};
    if (!kksdk::legacy_codec_write_header(
                *static_cast<const kksdk::LegacyCodecOptions *>(options), header)) {
        return 5;
    }

    for (std::size_t i = 0; i < header.size(); ++i) {
        out[i] = header[i];
    }
    return 0;
}

extern "C" int kksdk_legacy_codec_parse_properties(const unsigned char *header,
        unsigned int header_size, void *out) {
    if (out == nullptr) {
        return 4;
    }
    kksdk::LegacyCodecOptions options{};
    if (!kksdk::legacy_codec_parse_properties(header, header_size, options)) {
        return 4;
    }
    *static_cast<kksdk::LegacyCodecOptions *>(out) = options;
    return 0;
}

extern "C" int kksdk_legacy_codec_parse_header(const unsigned char *header,
        unsigned int header_size, void *out) {
    if (out == nullptr) {
        return 4;
    }
    kksdk::LegacyCodecHeader parsed{};
    if (!kksdk::legacy_codec_parse_header(header, header_size, parsed)) {
        return 4;
    }
    *static_cast<kksdk::LegacyCodecHeader *>(out) = parsed;
    return 0;
}
