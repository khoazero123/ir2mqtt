#include "legacy_codec_encoder_matchfinder_layout.hpp"
#include "legacy_codec_encoder_matchfinder_runtime.hpp"
#include "legacy_codec_encoder_core.hpp"
#include "legacy_codec_encoder_parse.hpp"

#include "legacy_codec_encoder_blocks.hpp"
#include "legacy_codec_encoder_context.hpp"
#include "legacy_codec_encoder_core.hpp"
#include "legacy_codec_encoder_state.hpp"
#include "legacy_codec_range_encoder.hpp"

namespace kksdk {
namespace {

using namespace legacy_oem_encoder;
using namespace legacy_oem_matchfinder;
using namespace legacy_oem_parse;

bool parse_47d10_stream_tail_batch(void *encoder, std::uint32_t node_count) {
    if (encoder == nullptr || node_count <= 1U || node_count > 2U) {
        return false;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    if (*reinterpret_cast<const std::int32_t *>(base + kLiteralMode) == 0) {
        return false;
    }

    LegacyCodecMatchFinderAccess match_finder{encoder};
    void *const ctx = match_finder.context();
    if (ctx == nullptr) {
        return false;
    }

    const auto *mf_bytes = static_cast<const std::uint8_t *>(ctx);
    if (*reinterpret_cast<const std::int32_t *>(mf_bytes + kStreamEnd) == 0) {
        return false;
    }
    if (match_finder.has_data() > 0) {
        return false;
    }

    const auto *words = reinterpret_cast<const std::uint64_t *>(ctx);
    const std::uint32_t history_size =
            *reinterpret_cast<const std::uint32_t *>(mf_bytes + kHistorySize);
    const std::uint32_t write_bytes =
            static_cast<std::uint32_t>(words[2]) - history_size;
    const std::uint64_t block_bytes = legacy_codec_encoder_block_bytes(encoder);
    if (static_cast<std::uint64_t>(write_bytes) <= block_bytes) {
        return false;
    }
    return (write_bytes - static_cast<std::uint32_t>(block_bytes)) <= 2U;
}

void parse_oem_47d10_batch_presync(void *encoder, std::uint32_t node_count) {
    if (!parse_47d10_stream_tail_batch(encoder, node_count)) {
        return;
    }

    LegacyCodecMatchFinderAccess match_finder{encoder};
    match_finder.skip(static_cast<std::int32_t>(node_count - 1U));
}

}  // namespace

bool legacy_codec_encoder_parse_47d10_stream_tail_batch(void *encoder,
        std::uint32_t node_count) {
    return parse_47d10_stream_tail_batch(encoder, node_count);
}

bool legacy_codec_encoder_parse_sync_matchfinder(void *encoder, std::uint32_t &match_length,
        std::uint32_t &distance_index) {
    if (encoder == nullptr) {
        return false;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    if (*reinterpret_cast<std::int32_t *>(base + legacy_oem_encoder::kLiteralMode) != 0) {
        return false;
    }

    const std::uint32_t pending_literal =
            *reinterpret_cast<std::uint32_t *>(base + legacy_oem_encoder::kPendingLiteral);
    const std::uint32_t parse_node =
            *reinterpret_cast<std::uint32_t *>(base + legacy_oem_encoder::kParseNodeIndex);
    if (parse_node != pending_literal) {
        return false;
    }

    *reinterpret_cast<std::uint32_t *>(base + legacy_oem_encoder::kParseNodeIndex) = 0U;

    return legacy_codec_encoder_sync_matchfinder_when_idle(encoder, &match_length,
            &distance_index);
}

bool legacy_codec_encoder_backtrack_prepare(void *encoder, std::uint32_t start_node) {
    if (encoder == nullptr) {
        return false;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    std::uint32_t node_index = start_node;
    std::uint32_t walk_parent = *node_parent_index(base, node_index);
    std::uint32_t carry_tag = *node_tag(base, node_index);

    *reinterpret_cast<std::int32_t *>(base + kParseNodeIndex) = static_cast<std::int32_t>(node_index);

    while (true) {
        const std::uint32_t parent = walk_parent;
        std::uint32_t *parent_tag_slot = node_tag(base, parent);
        std::uint32_t next_parent = 0U;
        std::uint32_t next_carry = 0U;

        if (*node_flag(base, node_index) == 0U) {
            next_carry = *parent_tag_slot;
            next_parent = *node_parent_index(base, parent);
        } else {
            next_carry = kEncoderTagLiteral;
            next_parent = parent - 1U;
            *parent_tag_slot = kEncoderTagLiteral;
            *node_flag(base, parent) = 0U;
            *node_parent_index(base, parent) = next_parent;
            if (*node_aux_rep(base, node_index) != 0U) {
                *node_flag(base, next_parent) = 0U;
                *node_parent_link(base, next_parent) = *node_parent_link(base, node_index);
                next_carry = kEncoderTagLiteral;
            }
        }

        *parent_tag_slot = carry_tag;
        *node_parent_index(base, parent) = node_index;
        const bool continue_walk = parent != 0U;
        node_index = parent;
        walk_parent = next_parent;
        carry_tag = next_carry;
        if (!continue_walk) {
            break;
        }
    }

    const std::uint32_t length =
            *reinterpret_cast<std::uint32_t *>(base + kRootLengthSlot);
    *reinterpret_cast<std::uint32_t *>(base + kPendingLiteral) = length;
    return true;
}

bool legacy_codec_encoder_apply_literal_node_chain(void *encoder, std::uint32_t node_count) {
    if (encoder == nullptr || node_count == 0U) {
        return false;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    for (std::uint32_t node_index = 1U; node_index <= node_count; ++node_index) {
        if (!legacy_codec_encoder_backtrack_prepare(encoder, node_index)) {
            return false;
        }
        *reinterpret_cast<std::uint32_t *>(base + kPendingLiteral) = 1U;
        if (!legacy_codec_encoder_commit_parse_choice(encoder)) {
            return false;
        }
        if (legacy_codec_range_encoder_from_native_state(encoder)->status != 0) {
            return false;
        }
    }

    *reinterpret_cast<std::int32_t *>(base + kProcessedInBlock) = 0;
    *reinterpret_cast<std::uint32_t *>(base + kParseNodeIndex) = 0U;
    *reinterpret_cast<std::uint32_t *>(base + kPendingLiteral) = 0U;
    *reinterpret_cast<std::uint32_t *>(base + kRootLengthSlot) = 0U;
    *reinterpret_cast<std::uint32_t *>(base + kParseCachedAvailLength) = 0U;
    *reinterpret_cast<std::uint32_t *>(base + kParseDistanceCode) = 0U;
    return true;
}

bool legacy_codec_encoder_apply_parse_batch(void *encoder, std::uint32_t start_node) {
    if (encoder == nullptr) {
        return false;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    const std::uint32_t node_count =
            *reinterpret_cast<std::uint32_t *>(base + kRootLengthSlot);
    if (!legacy_codec_encoder_backtrack_prepare(encoder, start_node)) {
        return false;
    }

    while (true) {
        if (!legacy_codec_encoder_commit_parse_choice(encoder)) {
            return false;
        }
        if (legacy_codec_range_encoder_from_native_state(encoder)->status != 0) {
            return false;
        }

        const std::int32_t processed =
                *reinterpret_cast<std::int32_t *>(base + kProcessedInBlock);
        if (processed == 0) {
            legacy_codec_encoder_maybe_refresh_prices(encoder);
            return true;
        }

        if (!legacy_codec_encoder_backtrack_prepare(encoder, start_node)) {
            return false;
        }
    }
}

LegacyCodecEncoderContinueStatus legacy_codec_encoder_check_block_continue(void *encoder,
        int mode, unsigned long input_limit, unsigned int output_limit,
        std::uint32_t block_start_pos_state, std::uint32_t current_pos_state) {
    LegacyCodecEncoderContinueStatus result{};
    if (encoder == nullptr) {
        result.return_code = 2;
        return result;
    }

    const int open_size = legacy_codec_encoder_open_input_size(encoder);
    const std::uint64_t block = legacy_codec_encoder_block_bytes(encoder);
    if (open_size > 0 && block >= static_cast<std::uint64_t>(open_size)) {
        result.return_code = 0;
        return result;
    }

    LegacyCodecMatchFinderAccess match_finder{encoder};
    if (match_finder.has_data() == 0) {
        void *const mf_ctx = match_finder.context();
        if (mf_ctx != nullptr) {
            auto *mf_bytes = static_cast<std::uint8_t *>(mf_ctx);
            if (*reinterpret_cast<std::int32_t *>(mf_bytes + kStreamEnd) == 0) {
                legacy_codec_encoder_matchfinder_runtime_fill(mf_ctx);
            }
        }
        if (!match_finder.normalize() || match_finder.has_data() == 0) {
            if (open_size > static_cast<int>(block)) {
                result.keep_encoding = true;
                result.return_code = 0;
                return result;
            }
            result.return_code = 0;
            return result;
        }
    }

    const std::uint32_t block_progress = current_pos_state - block_start_pos_state;
    if (mode != 0) {
        if (output_limit <= block_progress + 0x112cU) {
            result.return_code = 2;
            return result;
        }

        const auto *base = static_cast<const std::uint8_t *>(encoder);
        const auto *range = legacy_codec_range_encoder_from_native_state(const_cast<void *>(encoder));
        if (range == nullptr) {
            result.return_code = 2;
            return result;
        }

        const unsigned long out_used =
                static_cast<unsigned long>(range->out_ptr - range->out_start);
        const unsigned long out_capacity =
                static_cast<unsigned long>(range->out_end - range->out_start);
        if ((input_limit & 0xffffffffUL) <= out_used + out_capacity + 0x2000UL) {
            result.return_code = 2;
            return result;
        }
    }

    if (block_progress < 0x8000U) {
        result.keep_encoding = true;
        result.return_code = 0;
        return result;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    const std::uint32_t status = legacy_codec_encoder_status_code(encoder);
    if (status == kStatusOk &&
            *reinterpret_cast<std::int32_t *>(base + kStreamOutStatus) == 0) {
        if (*reinterpret_cast<std::int32_t *>(base + kExtraInputFlag) == 0) {
            result.return_code = 1;
            return result;
        }
        *reinterpret_cast<std::uint32_t *>(base + kStatusCode) = kStatusInputEof;
        result.return_code = 1;
        return result;
    }

    result.return_code = 1;
    return result;
}

}  // namespace kksdk
