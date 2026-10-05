#include "legacy_codec_encoder_parse.hpp"

#include "legacy_codec_encoder_blocks.hpp"
#include "legacy_codec_encoder_context.hpp"
#include "legacy_codec_encoder_core.hpp"
#include "legacy_codec_encoder_state.hpp"
#include "legacy_codec_range_encoder.hpp"

namespace kksdk {
namespace {

using namespace legacy_oem_encoder;

constexpr std::uint32_t kMaxMatchProbe = 0x111U;

}  // namespace

bool legacy_codec_encoder_extend_match_length(void *encoder, std::uint32_t match_index,
        std::uint32_t avail_limit, std::uint32_t &match_length) {
    if (encoder == nullptr || match_index == 0U) {
        return false;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    const std::uint32_t min_match = kLzmaMinMatchLength;
    const auto *match_buffer = reinterpret_cast<const std::uint32_t *>(base + kMatchBuffer);
    const std::uint32_t start_length = match_buffer[match_index - 2U];
    if (start_length != min_match) {
        return false;
    }

    LegacyCodecMatchFinderAccess match_finder{encoder};
    const auto *window = static_cast<const std::uint8_t *>(match_finder.buffer());
    if (window == nullptr) {
        return false;
    }

    std::uint32_t limit = avail_limit;
    if (limit > kMaxMatchProbe) {
        limit = kMaxMatchProbe;
    }

    const std::int32_t rep_distance =
            *reinterpret_cast<const std::int32_t *>(base + kMatchBuffer +
                    static_cast<std::size_t>(match_index - 1U) * sizeof(std::uint32_t));
    const std::uint8_t *window_end = window - 1;
    std::uint32_t length = start_length;
    while (length < limit) {
        const std::uint8_t *cur = window_end + static_cast<std::ptrdiff_t>(length);
        const std::uint8_t *ref = cur - static_cast<std::ptrdiff_t>(rep_distance + 1);
        if (*cur != *ref) {
            break;
        }
        length += 1U;
    }

    match_length = length;
    return true;
}

void legacy_codec_encoder_finalize_stream_block(void *encoder) {
    if (encoder == nullptr) {
        return;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    auto *finish_dc_adjust = reinterpret_cast<std::uint32_t *>(base +
            legacy_oem_encoder::kFastFinishDcAdjust);
    if (*reinterpret_cast<const std::int32_t *>(base + legacy_oem_encoder::kLiteralMode) != 0) {
        const std::uint32_t local_dc = legacy_codec_encoder_fast_local_dc(encoder);
        const std::uint64_t block = legacy_codec_encoder_block_bytes(encoder);
        const int open_size = legacy_codec_encoder_open_input_size(encoder);
        if (open_size > 0 && block >= static_cast<std::uint64_t>(open_size) && local_dc > 0U &&
                static_cast<std::uint64_t>(local_dc) + 1U == block &&
                *finish_dc_adjust != 3U) {
            legacy_codec_encoder_reset_fast_local_dc(encoder);
        } else if (local_dc > 0U && !legacy_codec_encoder_lift_fast_full_native() &&
                (open_size <= 0 || block < static_cast<std::uint64_t>(open_size))) {
            auto *block_slot = reinterpret_cast<std::uint64_t *>(base +
                    legacy_oem_encoder::kBlockBytesEncoded);
            if (block > static_cast<std::uint64_t>(local_dc)) {
                *block_slot = static_cast<std::uint64_t>(local_dc);
            } else if (block < static_cast<std::uint64_t>(local_dc)) {
                *block_slot = static_cast<std::uint64_t>(local_dc);
            }
        }
    }

    const std::uint32_t pos_state = legacy_codec_encoder_finish_pos_state(encoder);
    if (legacy_codec_encoder_lift_fast_full_native()) {
        std::uint32_t finish_dc = pos_state;
        if (*reinterpret_cast<const std::int32_t *>(base + legacy_oem_encoder::kLiteralMode) != 0) {
            if (*finish_dc_adjust == 3U) {
                const std::uint32_t mask =
                        *reinterpret_cast<const std::uint32_t *>(base +
                                legacy_oem_encoder::kPosStateMask);
                finish_dc = static_cast<std::uint32_t>(
                        legacy_codec_encoder_block_bytes(encoder)) & mask;
            } else if (legacy_codec_encoder_open_input_is_uniform(encoder)) {
                finish_dc = 1U;
            } else if (legacy_codec_encoder_open_input_size(encoder) > 0) {
                const std::uint32_t local_dc = legacy_codec_encoder_fast_local_dc(encoder);
                if (local_dc > 0U) {
                    auto *block_slot = reinterpret_cast<std::uint64_t *>(base +
                            legacy_oem_encoder::kBlockBytesEncoded);
                    const std::uint64_t block = *block_slot;
                    if (block > static_cast<std::uint64_t>(local_dc)) {
                        *block_slot = static_cast<std::uint64_t>(local_dc);
                    }
                }
                finish_dc = local_dc > 0U ? local_dc : 0U;
            } else {
                finish_dc = legacy_codec_encoder_fast_local_dc(encoder);
            }
        }
        *finish_dc_adjust = 0U;
        legacy_codec_encoder_finish(encoder, finish_dc);
        return;
    }

    legacy_codec_encoder_finish(encoder, pos_state);
}

int legacy_codec_encoder_main_step(void *encoder, int mode, unsigned long input_limit,
        unsigned int output_limit) {
    if (encoder == nullptr) {
        return 2;
    }

    int early_status = 0;
    if (legacy_codec_encoder_preamble(encoder, &early_status)) {
        return early_status;
    }

    const int lifted_status = legacy_codec_encoder_try_lifted_core_block(encoder, mode,
            input_limit, output_limit);
    if (lifted_status == 0 || lifted_status == 1) {
        return lifted_status;
    }
    if (lifted_status == 2) {
        return 2;
    }
    if (lifted_status == -1) {
        return 2;
    }
    return 2;
}

}  // namespace kksdk
