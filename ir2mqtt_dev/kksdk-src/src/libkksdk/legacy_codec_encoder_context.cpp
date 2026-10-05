#include "legacy_codec_encoder_context.hpp"

#include "legacy_codec_encoder_matchfinder_layout.hpp"
#include "legacy_codec_encoder_matchfinder_runtime.hpp"
#include "legacy_codec_encoder_parse.hpp"

namespace kksdk {
namespace {

using MatchFinderInitFn = void (*)(void *ctx);
using MatchFinderIntFn = int (*)(void *ctx);
using MatchFinderPtrFn = void *(*)(void *ctx);
using MatchFinderNormalizeFn = void *(*)(void *ctx);
using MatchFinderGetByteFn = std::uint32_t (*)(void *ctx, int offset);
using MatchFinderFindFn = int (*)(void *ctx, void *match_buffer);

using namespace legacy_oem_matchfinder;
using namespace legacy_oem_encoder;
using namespace legacy_oem_parse;

void **match_finder_slots(void *encoder) {
    return reinterpret_cast<void **>(encoder);
}

}  // namespace

bool legacy_codec_encoder_uses_lifted_matchfinder(void *encoder) {
    if (encoder == nullptr) {
        return false;
    }
    void **slots = match_finder_slots(encoder);
    return slots[0] == reinterpret_cast<void *>(&legacy_codec_encoder_lifted_mf_init);
}

void *LegacyCodecMatchFinderAccess::context() const {
    return match_finder_slots(encoder)[6];
}

int LegacyCodecMatchFinderAccess::has_data() const {
    const auto get_chunk = reinterpret_cast<MatchFinderIntFn>(match_finder_slots(encoder)[2]);
    if (get_chunk == nullptr) {
        return 0;
    }
    return get_chunk(context());
}

void *LegacyCodecMatchFinderAccess::buffer() const {
    const auto get_ptr = reinterpret_cast<MatchFinderPtrFn>(match_finder_slots(encoder)[3]);
    if (get_ptr == nullptr) {
        return nullptr;
    }
    return get_ptr(context());
}

int LegacyCodecMatchFinderAccess::skip(std::int32_t count) const {
    const auto skip_fn = reinterpret_cast<void (*)(void *, int)>(match_finder_slots(encoder)[5]);
    if (skip_fn == nullptr) {
        return 0;
    }
    skip_fn(context(), count);
    return 1;
}

bool LegacyCodecMatchFinderAccess::normalize() const {
    void *ctx = context();
    if (ctx == nullptr) {
        return false;
    }
    if (legacy_codec_encoder_uses_lifted_matchfinder(encoder)) {
        legacy_codec_encoder_matchfinder_runtime_normalize(ctx);
        return true;
    }
    // OEM FUN_001473d0 calls slot[3] (get_ptr) after the first literal warmup path.
    const auto get_ptr = reinterpret_cast<MatchFinderPtrFn>(match_finder_slots(encoder)[3]);
    if (get_ptr != nullptr) {
        (void)get_ptr(ctx);
    }
    return true;
}

const std::uint8_t *LegacyCodecMatchFinderAccess::current_byte_ptr() const {
    return current_literal_ptr();
}

const std::uint8_t *LegacyCodecMatchFinderAccess::current_literal_ptr() const {
    const auto *window = static_cast<const std::uint8_t *>(buffer());
    if (window == nullptr) {
        return nullptr;
    }
    // OEM LAB_0014796c uses (*(slot[3]))(ctx) - 1 as the literal pointer.
    return window - 1;
}

std::uint32_t legacy_codec_matchfinder_get_byte(void *encoder, int offset) {
    if (encoder == nullptr) {
        return 0U;
    }
    const auto get_byte =
            reinterpret_cast<MatchFinderGetByteFn>(match_finder_slots(encoder)[1]);
    if (get_byte == nullptr) {
        return 0U;
    }
    return get_byte(match_finder_slots(encoder)[6], offset);
}

bool legacy_codec_matchfinder_warmup(void *encoder) {
    if (encoder == nullptr) {
        return false;
    }

    LegacyCodecMatchFinderAccess match_finder{encoder};
    if (match_finder.has_data() == 0) {
        return false;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    void **slots = match_finder_slots(encoder);
    const auto get_chunk = reinterpret_cast<MatchFinderIntFn>(slots[2]);
    const std::uint32_t avail = static_cast<std::uint32_t>(get_chunk(match_finder.context()));
    *reinterpret_cast<std::uint32_t *>(base + legacy_oem_encoder::kMatchfinderAvail) = avail;

    const auto find_match = reinterpret_cast<MatchFinderFindFn>(slots[4]);
    const std::uint32_t min_match = legacy_oem_encoder::kLzmaMinMatchLength;
    const int match_len = find_match == nullptr
            ? 0
            : find_match(match_finder.context(), base + legacy_oem_encoder::kMatchBuffer);
    if (match_len > 0) {
        const auto *match_buffer = reinterpret_cast<const std::uint32_t *>(base +
                legacy_oem_encoder::kMatchBuffer);
        if (match_buffer[static_cast<std::size_t>(match_len) - 2U] == min_match) {
            match_finder.normalize();
        }
    }
    return true;
}

bool legacy_codec_matchfinder_native_open_prefetch(void *encoder, bool run_find_match,
        bool skip_on_min_match) {
    if (encoder == nullptr) {
        return false;
    }

    LegacyCodecMatchFinderAccess match_finder{encoder};
    if (match_finder.has_data() == 0) {
        return false;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    void **slots = match_finder_slots(encoder);
    const auto get_chunk = reinterpret_cast<MatchFinderIntFn>(slots[2]);
    if (get_chunk == nullptr) {
        return false;
    }

    void *const ctx = match_finder.context();
    if (get_chunk(ctx) == 0) {
        return false;
    }
    const std::uint32_t avail = static_cast<std::uint32_t>(get_chunk(ctx));
    *reinterpret_cast<std::uint32_t *>(base + legacy_oem_encoder::kMatchfinderAvail) = avail;

    if (!run_find_match) {
        return true;
    }

    const auto find_match = reinterpret_cast<MatchFinderFindFn>(slots[4]);
    if (find_match == nullptr) {
        return false;
    }

    const int distance_index = find_match(ctx, base + legacy_oem_encoder::kMatchBuffer);
    if (distance_index > 0) {
        const auto *match_buffer = reinterpret_cast<const std::uint32_t *>(base +
                legacy_oem_encoder::kMatchBuffer);
        const std::uint32_t match_length =
                match_buffer[static_cast<std::size_t>(distance_index) - 2U];
        *reinterpret_cast<std::uint32_t *>(base + kParseDistanceCode) =
                static_cast<std::uint32_t>(distance_index);
        *reinterpret_cast<std::uint32_t *>(base + kParseCachedAvailLength) = match_length;
        if (skip_on_min_match &&
                match_buffer[static_cast<std::size_t>(distance_index) - 2U] ==
                        legacy_oem_encoder::kLzmaMinMatchLength) {
            match_finder.skip(1);
        }
    }
    return true;
}

bool legacy_codec_matchfinder_entry_avail(void *encoder) {
    if (encoder == nullptr) {
        return false;
    }

    LegacyCodecMatchFinderAccess match_finder{encoder};
    if (match_finder.has_data() == 0) {
        return false;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    const auto get_chunk =
            reinterpret_cast<MatchFinderIntFn>(match_finder_slots(encoder)[2]);
    const std::uint32_t avail =
            static_cast<std::uint32_t>(get_chunk(match_finder.context()));
    *reinterpret_cast<std::uint32_t *>(base + legacy_oem_encoder::kMatchfinderAvail) = avail;
    return avail != 0U;
}

bool legacy_codec_encoder_run_matchfinder_init_if_needed(void *encoder) {
    if (encoder == nullptr) {
        return false;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    auto *init_flag = reinterpret_cast<std::int32_t *>(base + legacy_oem_encoder::kStreamInitFlag);
    if (*init_flag == 0) {
        return true;
    }

    void **slots = match_finder_slots(encoder);
    const auto init_fn = reinterpret_cast<MatchFinderInitFn>(slots[0]);
    void *ctx = slots[6];
    if (init_fn != nullptr && ctx != nullptr) {
        init_fn(ctx);
    }
    *init_flag = 0;
    return true;
}

bool legacy_codec_encoder_rep_distance_reachable(void *encoder, const std::uint8_t *cur,
        std::int32_t rep_distance) {
    if (encoder == nullptr || cur == nullptr || rep_distance < 0) {
        return false;
    }

    const std::uint64_t encoded = legacy_codec_encoder_block_bytes(encoder);
    // LZMA rep0==0 is distance 1; need at least rep_distance+1 bytes encoded in block.
    if (static_cast<std::uint64_t>(rep_distance) + 1U > encoded) {
        return false;
    }

    const auto *base = static_cast<const std::uint8_t *>(encoder);
    if (*reinterpret_cast<const std::int32_t *>(base + legacy_oem_encoder::kLiteralMode) != 0) {
        return true;
    }

    const std::uint8_t *ref = cur - static_cast<std::size_t>(rep_distance + 1);
    if (ref >= cur) {
        return false;
    }

    LegacyCodecMatchFinderAccess match_finder{encoder};
    const auto *mf_bytes = static_cast<const std::uint8_t *>(match_finder.context());
    if (mf_bytes == nullptr) {
        return false;
    }

    const auto *words = reinterpret_cast<const std::uint64_t *>(mf_bytes);
    const std::uint8_t *window_base = reinterpret_cast<const std::uint8_t *>(words[8]);
    if (window_base == nullptr) {
        return false;
    }

    const std::uint8_t *window_min =
            window_base + *reinterpret_cast<const std::uint32_t *>(mf_bytes + kKeepBefore);
    return ref >= window_min;
}

}  // namespace kksdk
