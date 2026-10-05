#include "legacy_codec_encoder_lifecycle.hpp"

#include "legacy_codec_encoder_matchfinder_layout.hpp"
#include "legacy_codec_encoder_matchfinder.hpp"
#include "legacy_codec_encoder_matchfinder_layout.hpp"
#include "legacy_codec_encoder_reset_state.hpp"
#include "legacy_codec_encoder_state.hpp"
#include "legacy_codec_options.hpp"
#include "legacy_codec_tables.hpp"

#include <cstdlib>
#include <cstring>
#include <array>

namespace kksdk {
namespace {

using namespace legacy_oem_encoder;
using namespace legacy_oem_matchfinder;

using AllocPairFn = void *(*)(void **allocator, std::uint64_t size);
using FreePairFn = void (*)(void **allocator, void *ptr);

void *native_alloc_pair(void **allocator, std::uint64_t size) {
    (void)allocator;
    return std::calloc(1U, static_cast<std::size_t>(size));
}

void native_free_pair(void **allocator, void *ptr) {
    (void)allocator;
    std::free(ptr);
}

void **g_native_allocator_table() {
    static void *table[2] = {
            reinterpret_cast<void *>(native_alloc_pair),
            reinterpret_cast<void *>(native_free_pair),
    };
    return table;
}

void matchfinder_init_defaults(std::uint8_t *matchfinder) {
    if (matchfinder == nullptr) {
        return;
    }

    // FUN_001411b0 — MF defaults before FUN_00145ca4 overwrites typed fields.
    *reinterpret_cast<std::uint32_t *>(matchfinder + kMaxDepth) = 0x20U;
    *reinterpret_cast<std::uint32_t *>(matchfinder + kHashBytes) = 4U;
    *reinterpret_cast<std::int32_t *>(matchfinder + kBinaryTreeMode) = 1;
    *reinterpret_cast<void **>(matchfinder + kWindowBuffer) = nullptr;
    *reinterpret_cast<void **>(matchfinder + kHashTables) = nullptr;
    legacy_codec_fill_crc_table(
            reinterpret_cast<std::uint32_t *>(matchfinder + kCrcTable),
            kCrcTableEntries);
}

void free_owned_pointer(void **allocator, void *&ptr) {
    if (ptr == nullptr) {
        return;
    }
    const auto free_fn = reinterpret_cast<FreePairFn>(allocator[1]);
    free_fn(allocator, ptr);
    ptr = nullptr;
}

void release_matchfinder_storage(std::uint8_t *matchfinder, void **allocator) {
    if (matchfinder == nullptr || allocator == nullptr) {
        return;
    }

    free_owned_pointer(allocator, *reinterpret_cast<void **>(matchfinder + kHashTables));
    if (*reinterpret_cast<std::int32_t *>(matchfinder + kDirectInput) == 0) {
        free_owned_pointer(allocator, *reinterpret_cast<void **>(matchfinder + kWindowBuffer));
    }
}

void release_encoder_sidecars(std::uint8_t *base, void **allocator) {
    free_owned_pointer(allocator, *reinterpret_cast<void **>(base + kLiteralTreePrimary));
    free_owned_pointer(allocator, *reinterpret_cast<void **>(base + kLiteralTreeSecondary));
    free_owned_pointer(allocator, *reinterpret_cast<void **>(base + kProgressReadPos));
}

void fill_oem_pos_slot_prefix(std::uint8_t *base) {
    *reinterpret_cast<std::uint16_t *>(base + kPosSlotTable) = 0x100U;
    std::size_t write_offset = 2U;
    for (std::uint8_t symbol = 2; symbol != 0x1aU; ++symbol) {
        const std::size_t repeat_count =
                std::size_t{1} << (((symbol >> 1U) - 1U) & 0x1fU);
        for (std::size_t index = 0; index < repeat_count; ++index) {
            base[kPosSlotTable + write_offset + index] = symbol;
        }
        write_offset += repeat_count;
    }
}

int resolve_level(const int *props) {
    return props[0] < 0 ? 5 : props[0];
}

std::uint32_t resolve_dictionary_size(const int *props) {
    const int level = resolve_level(props);
    if (props[1] <= 0) {
        return legacy_codec_default_dictionary_size(level);
    }
    return static_cast<std::uint32_t>(props[1]);
}

}  // namespace

void **legacy_codec_native_allocator_table() {
    return g_native_allocator_table();
}

void legacy_codec_encoder_default_props(int *props) {
    if (props == nullptr) {
        return;
    }

    const LegacyCodecOptions defaults = legacy_codec_default_options();
    props[0] = defaults.level;
    props[1] = 0;
    props[2] = defaults.lc;
    props[3] = defaults.lp;
    props[4] = defaults.pb;
    props[5] = defaults.bt_mode;
    props[6] = defaults.fast_bytes;
    props[7] = defaults.num_hash_bytes;
    props[8] = defaults.match_finder_type;
    props[9] = static_cast<int>(defaults.match_finder_cycles);
    props[10] = static_cast<int>(defaults.reduce_size);
    props[11] = defaults.write_end_mark ? 1 : 0;
}

void legacy_codec_encoder_init_new_blob(void *encoder) {
    if (encoder == nullptr) {
        return;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);

    // FUN_00145ef8 — only clears owned pointers; blob is zeroed by calloc in alloc.
    *reinterpret_cast<void **>(base + kOutputWriteCallback) = nullptr;
    *reinterpret_cast<void **>(base + kProgressReadPos) = nullptr;

    matchfinder_init_defaults(base + kMatchFinder);

    int default_props[12] = {};
    legacy_codec_encoder_default_props(default_props);
    legacy_codec_encoder_apply_props(encoder, default_props);

    fill_oem_pos_slot_prefix(base);
    legacy_codec_fill_price_table(reinterpret_cast<std::uint32_t *>(base + kProbPrices), 0x80U);

    *reinterpret_cast<void **>(base + kLiteralTreePrimary) = nullptr;
    *reinterpret_cast<void **>(base + kLiteralTreeSecondary) = nullptr;
}

int legacy_codec_encoder_apply_props(void *encoder, int *props) {
    if (encoder == nullptr || props == nullptr) {
        return 5;
    }

    const int level = resolve_level(props);
    std::uint32_t dictionary_size = resolve_dictionary_size(props);
    int lc = props[2] < 0 ? 3 : props[2];
    const int lp_raw = props[3];
    const int lp = lp_raw < 0 ? 0 : (lp_raw & (lp_raw >> 31 ^ 0xffffffff));
    int pb = props[4] < 0 ? 2 : props[4];
    std::uint32_t bt_mode = props[5] < 0 ? static_cast<std::uint32_t>(level > 4) :
            static_cast<std::uint32_t>(props[5]);

    std::uint32_t fast_bytes = level > 6 ? 0x40U : 0x20U;
    if (props[6] >= 0) {
        fast_bytes = static_cast<std::uint32_t>(props[6]);
    }

    std::uint32_t num_hash_bytes = bt_mode != 0U ? 1U : 0U;
    if (props[7] >= 0) {
        num_hash_bytes = static_cast<std::uint32_t>(props[7]);
    }

    int match_finder_type = props[8] < 0 ? 4 : props[8];
    std::uint32_t match_finder_cycles = props[9] <= 0
            ? ((fast_bytes >> 1U) + 0x10U) >> (num_hash_bytes == 0U ? 1U : 0U)
            : static_cast<std::uint32_t>(props[9]);
    const int reduce_size = props[10];

    if (lc >= 9 || lp < 0 || lp >= 5 || pb >= 5 || dictionary_size >= 0x40000001U) {
        return 5;
    }
    if (fast_bytes < 6U) {
        fast_bytes = 5U;
    } else if (fast_bytes > 0x110U) {
        fast_bytes = 0x111U;
    }

    if (num_hash_bytes == 0U) {
        match_finder_type = 4;
    } else if (match_finder_type < 2) {
        match_finder_type = 2;
    } else if (match_finder_type > 3) {
        match_finder_type = 4;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    auto *matchfinder = base + kMatchFinder;

    *reinterpret_cast<std::uint32_t *>(base + kDictionarySize) = dictionary_size;
    *reinterpret_cast<std::uint32_t *>(base + kMatchFinderCycles) = match_finder_cycles;
    *reinterpret_cast<std::uint32_t *>(base + kFastBytes) = fast_bytes;
    *reinterpret_cast<std::int32_t *>(base + kLiteralCtxBits) = lc;
    *reinterpret_cast<std::uint32_t *>(base + kLiteralPosBits) = static_cast<std::uint32_t>(lp);
    *reinterpret_cast<std::int32_t *>(base + kPosStateBits) = pb;
    *reinterpret_cast<std::uint32_t *>(base + kLiteralMode) = static_cast<std::uint32_t>(bt_mode == 0U);
    *reinterpret_cast<std::uint32_t *>(base + kMatchfinderLargeDictFlag) =
            dictionary_size > 0x1000000U ? 1U : 0U;
    *reinterpret_cast<std::int32_t *>(base + kFinishMode) = reduce_size;

    *reinterpret_cast<std::uint32_t *>(matchfinder + kHashBytes) =
            static_cast<std::uint32_t>(match_finder_type);
    *reinterpret_cast<std::int32_t *>(matchfinder + kBinaryTreeMode) = bt_mode != 0U ? 1 : 0;
    *reinterpret_cast<std::uint32_t *>(matchfinder + kMaxDepth) = match_finder_cycles;

    return 0;
}

int legacy_codec_encoder_write_header(void *encoder, char *out, unsigned long *in_out_size) {
    if (encoder == nullptr || out == nullptr || in_out_size == nullptr || *in_out_size < 5U) {
        return 5;
    }

    const auto *base = static_cast<const std::uint8_t *>(encoder);
    LegacyCodecOptions options = legacy_codec_default_options();
    options.dictionary_size = *reinterpret_cast<const std::uint32_t *>(base + kDictionarySize);
    options.lc = *reinterpret_cast<const std::int32_t *>(base + kLiteralCtxBits);
    options.lp = static_cast<int>(*reinterpret_cast<const std::uint32_t *>(base + kLiteralPosBits));
    options.pb = *reinterpret_cast<const std::int32_t *>(base + kPosStateBits);

    std::array<std::uint8_t, 5> header{};
    if (!legacy_codec_write_header(options, header)) {
        return 5;
    }

    std::memcpy(out, header.data(), header.size());
    *in_out_size = header.size();
    return 0;
}

std::intptr_t legacy_codec_encoder_alloc(void **allocator) {
    if (allocator == nullptr || allocator[0] == nullptr) {
        return 0;
    }

    const auto alloc = reinterpret_cast<AllocPairFn>(allocator[0]);
    void *encoder = alloc(allocator, static_cast<std::uint64_t>(kStateSize));
    if (encoder == nullptr) {
        return 0;
    }

    legacy_codec_encoder_init_new_blob(encoder);
    return reinterpret_cast<std::intptr_t>(encoder);
}

void legacy_codec_encoder_release_blob(std::intptr_t encoder, void **allocator,
        void *allocator_ctx) {
    if (encoder == 0 || allocator == nullptr || allocator[1] == nullptr) {
        return;
    }

    auto *base = reinterpret_cast<std::uint8_t *>(encoder);
    (void)allocator_ctx;

    release_matchfinder_storage(base + kMatchFinder, allocator);
    release_encoder_sidecars(base, allocator);

    const auto free_fn = reinterpret_cast<FreePairFn>(allocator[1]);
    free_fn(allocator, base);
}

}  // namespace kksdk

int kksdk::legacy_codec_encoder_apply_props_bridge(std::intptr_t encoder, int *props) {
    return kksdk::legacy_codec_encoder_apply_props(reinterpret_cast<void *>(encoder), props);
}

int kksdk::legacy_codec_encoder_write_header_bridge(std::intptr_t encoder, char *out,
        unsigned long *in_out_size) {
    return kksdk::legacy_codec_encoder_write_header(reinterpret_cast<void *>(encoder), out,
            in_out_size);
}

void kksdk::legacy_codec_encoder_release_bridge(std::intptr_t encoder, void *allocator_a,
        void *allocator_b) {
    (void)allocator_b;
    kksdk::legacy_codec_encoder_release_blob(encoder, static_cast<void **>(allocator_a),
            allocator_b);
}
