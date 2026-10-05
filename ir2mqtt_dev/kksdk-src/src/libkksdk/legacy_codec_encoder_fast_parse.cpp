#include "legacy_codec_encoder_matchfinder_layout.hpp"
#include "legacy_codec_encoder_matchfinder_runtime.hpp"
#include "legacy_codec_encoder_parse.hpp"
#include "legacy_codec_encoder_parse_price_shared.hpp"

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
using namespace legacy_oem_parse_detail;

constexpr std::uint32_t kMaxMatchProbe = 0x111U;

bool g_lift_fast_loop_fail_on_unhandled = true;
bool g_lift_fast_stream_native_from_zero = true;
bool g_lift_fast_full_native = true;
bool g_fast_native_open_parse = true;
bool g_lift_fast_try_native_non_uniform_open = true;
std::uint64_t g_fast_loop_block_snapshot = 0U;

std::uint32_t fast_matchfinder_unencoded_bytes(void *encoder);
std::uint32_t fast_open_input_unencoded_bytes(void *encoder);
void apply_fast_rep_choice(void *encoder, std::uint32_t rep_index, std::uint32_t rep_length);

std::uint32_t oem_probe_rep_length_guarded(void *encoder, const std::uint8_t *window,
        std::int32_t rep_distance, std::uint32_t avail_limit);

bool lifted_new_match_distance_allowed(const std::uint8_t *base, std::uint32_t distance) {
    if (base == nullptr || distance == 0U) {
        return false;
    }
    const std::uint64_t encoded =
            *reinterpret_cast<const std::uint64_t *>(base + kBlockBytesEncoded);
    return static_cast<std::uint64_t>(distance) <= encoded;
}

std::uint32_t clamp_avail(std::uint32_t avail) {
    return avail > kMaxMatchProbe ? kMaxMatchProbe : avail;
}

std::uint32_t fast_matchfinder_unencoded_bytes(void *encoder) {
    if (legacy_codec_encoder_fast_open_body_active(encoder)) {
        const int open_size = legacy_codec_encoder_open_input_size(encoder);
        const std::uint32_t ldc = legacy_codec_encoder_fast_local_dc(encoder);
        if (open_size > static_cast<int>(ldc)) {
            return static_cast<std::uint32_t>(open_size) - ldc;
        }
        return 0U;
    }

    LegacyCodecMatchFinderAccess match_finder{encoder};
    void *const ctx = match_finder.context();
    if (ctx == nullptr) {
        return 0U;
    }

    const auto *mf_bytes = static_cast<const std::uint8_t *>(ctx);
    const auto *words = reinterpret_cast<const std::uint64_t *>(ctx);
    const std::uint32_t history_size =
            *reinterpret_cast<const std::uint32_t *>(mf_bytes + kHistorySize);
    const std::uint32_t read_bytes =
            static_cast<std::uint32_t>(words[1]) - history_size;
    const std::uint32_t write_bytes =
            static_cast<std::uint32_t>(words[2]) - history_size;
    const std::uint64_t block_bytes = legacy_codec_encoder_block_bytes(encoder);
    const bool stream_end =
            *reinterpret_cast<const std::int32_t *>(mf_bytes + kStreamEnd) != 0;
    const std::uint32_t frontier = stream_end ? write_bytes : read_bytes;
    if (static_cast<std::uint64_t>(frontier) <= block_bytes) {
        return 0U;
    }
    return frontier - static_cast<std::uint32_t>(block_bytes);
}

std::uint32_t fast_open_input_unencoded_bytes(void *encoder) {
    std::uint32_t unencoded = fast_matchfinder_unencoded_bytes(encoder);
    const int open_size = legacy_codec_encoder_open_input_size(encoder);
    const auto block = static_cast<std::uint32_t>(legacy_codec_encoder_block_bytes(encoder));
    if (open_size > static_cast<int>(block)) {
        const std::uint32_t from_input =
                static_cast<std::uint32_t>(open_size) - block;
        if (from_input < unencoded) {
            unencoded = from_input;
        } else if (g_lift_fast_full_native && unencoded == 0U && from_input > 0U) {
            unencoded = from_input;
        }
    }
    return unencoded;
}

// Cap MF frontier by known open input only for lifted full-native (open_size-driven loop).
std::uint32_t fast_bounded_unencoded_bytes(void *encoder) {
    if (g_lift_fast_full_native) {
        return fast_open_input_unencoded_bytes(encoder);
    }
    return fast_matchfinder_unencoded_bytes(encoder);
}

bool fast_non_uniform_tail_rep_covers_remaining(void *encoder, std::uint32_t rep_len) {
    if (!g_lift_fast_full_native || rep_len == 0U ||
            legacy_codec_encoder_open_input_is_uniform(encoder)) {
        return false;
    }
    const std::uint32_t remaining = fast_bounded_unencoded_bytes(encoder);
    return remaining >= 3U && rep_len == remaining;
}

bool fast_current_input_repeats_previous(void *encoder) {
    if (encoder == nullptr) {
        return false;
    }
    struct FastInputView {
        unsigned long (*read)(long ctx, void *dst, unsigned long *size);
        const std::uint8_t *data;
        int total;
        int pos;
    };
    auto *base = static_cast<std::uint8_t *>(encoder);
    const auto *input = *reinterpret_cast<const FastInputView * const *>(
            base + legacy_oem_encoder::kInputReadCallback);
    const std::uint64_t block = legacy_codec_encoder_block_bytes(encoder);
    if (input == nullptr || input->data == nullptr || input->total <= 0 ||
            block == 0U || block >= static_cast<std::uint64_t>(input->total)) {
        return false;
    }
    const std::size_t current = static_cast<std::size_t>(block);
    return input->data[current] == input->data[current - 1U];
}

bool fast_sparse_pair_tail_rep_allowed(void *encoder, std::uint32_t rep_len) {
    if (encoder == nullptr || !g_lift_fast_full_native || rep_len != 2U ||
            legacy_codec_encoder_open_input_is_uniform(encoder) ||
            legacy_codec_encoder_fast_open_body_active(encoder)) {
        return false;
    }

    struct FastInputView {
        unsigned long (*read)(long ctx, void *dst, unsigned long *size);
        const std::uint8_t *data;
        int total;
        int pos;
    };

    auto *base = static_cast<std::uint8_t *>(encoder);
    const auto *input = *reinterpret_cast<const FastInputView * const *>(
            base + legacy_oem_encoder::kInputReadCallback);
    const std::uint64_t block64 = legacy_codec_encoder_block_bytes(encoder);
    const std::int32_t rep0 =
            *reinterpret_cast<const std::int32_t *>(base + kRep0Distance);
    if (input == nullptr || input->data == nullptr || input->total <= 0 || rep0 != 2 ||
            block64 < 3U || block64 + rep_len > static_cast<std::uint64_t>(input->total)) {
        return false;
    }

    const std::size_t block = static_cast<std::size_t>(block64);
    return input->data[block] == input->data[block - 3U] &&
            input->data[block + 1U] == input->data[block - 2U];
}

bool fast_non_uniform_tail_rep_needs_head_literal(void *encoder, std::uint32_t rep_len) {
    if (!g_lift_fast_full_native || rep_len == 0U ||
            legacy_codec_encoder_open_input_is_uniform(encoder)) {
        return false;
    }
    if (fast_sparse_pair_tail_rep_allowed(encoder, rep_len)) {
        return false;
    }
    const std::uint32_t remaining = fast_bounded_unencoded_bytes(encoder);
    return remaining >= 3U && rep_len + 1U == remaining &&
            !fast_current_input_repeats_previous(encoder);
}

bool fast_open_input_rep_choice_matches(void *encoder, std::uint32_t rep_index,
        std::uint32_t rep_length) {
    if (encoder == nullptr || !g_lift_fast_full_native ||
            legacy_codec_encoder_fast_open_body_active(encoder) ||
            rep_index >= 4U || rep_length == 0U) {
        return true;
    }

    struct FastInputView {
        unsigned long (*read)(long ctx, void *dst, unsigned long *size);
        const std::uint8_t *data;
        int total;
        int pos;
    };

    auto *base = static_cast<std::uint8_t *>(encoder);
    const auto *input = *reinterpret_cast<const FastInputView * const *>(
            base + legacy_oem_encoder::kInputReadCallback);
    const std::uint64_t block64 = legacy_codec_encoder_block_bytes(encoder);
    if (input == nullptr || input->data == nullptr || input->total <= 0 ||
            block64 >= static_cast<std::uint64_t>(input->total)) {
        return true;
    }

    constexpr std::size_t kRepOffsets[4] = {
            kRep0Distance, kRepDist3, kRep1Distance, kRepDist4,
    };
    const std::int32_t rep_distance =
            *reinterpret_cast<const std::int32_t *>(base + kRepOffsets[rep_index]);
    if (rep_distance < 0) {
        return false;
    }
    const std::size_t actual_distance = static_cast<std::size_t>(rep_distance) + 1U;
    const std::size_t block = static_cast<std::size_t>(block64);
    const std::size_t total = static_cast<std::size_t>(input->total);
    if (actual_distance == 0U || actual_distance > block || block + rep_length > total) {
        return false;
    }
    for (std::size_t i = 0U; i < rep_length; ++i) {
        if (input->data[block + i] != input->data[block + i - actual_distance]) {
            return false;
        }
    }
    return true;
}

bool fast_try_input_verified_history_match(void *encoder,
        LegacyCodecEncoderParseStepResult &result) {
    if (encoder == nullptr || !g_lift_fast_full_native ||
            legacy_codec_encoder_fast_open_body_active(encoder) ||
            legacy_codec_encoder_open_input_is_uniform(encoder)) {
        return false;
    }

    struct FastInputView {
        unsigned long (*read)(long ctx, void *dst, unsigned long *size);
        const std::uint8_t *data;
        int total;
        int pos;
    };

    auto *base = static_cast<std::uint8_t *>(encoder);
    const auto *input = *reinterpret_cast<const FastInputView * const *>(
            base + legacy_oem_encoder::kInputReadCallback);
    const std::uint64_t block64 = legacy_codec_encoder_block_bytes(encoder);
    if (input == nullptr || input->data == nullptr || input->total <= 0 ||
            block64 == 0U || block64 >= static_cast<std::uint64_t>(input->total)) {
        return false;
    }

    const std::size_t block = static_cast<std::size_t>(block64);
    const std::size_t total = static_cast<std::size_t>(input->total);
    const std::size_t remaining = total - block;
    if (remaining < 3U) {
        return false;
    }

    std::uint32_t best_distance = 0U;
    std::uint32_t best_length = 0U;
    std::uint32_t best_len2_distance = 0U;
    const std::size_t max_distance = block < kMaxMatchProbe ? block : kMaxMatchProbe;
    const std::size_t max_length = remaining < kMaxMatchProbe ? remaining : kMaxMatchProbe;
    for (std::size_t distance = 1U; distance <= max_distance; ++distance) {
        std::size_t length = 0U;
        const std::size_t previous = block - distance;
        while (length < max_length &&
                input->data[block + length] == input->data[previous + length]) {
            ++length;
        }
        if (length >= 3U && length > best_length) {
            best_length = static_cast<std::uint32_t>(length);
            best_distance = static_cast<std::uint32_t>(distance);
        } else if (length == 2U && best_len2_distance == 0U) {
            best_len2_distance = static_cast<std::uint32_t>(distance);
        }
    }
    if (best_length < 3U && best_len2_distance != 0U && remaining > 2U) {
        const std::size_t after_short = block + 2U;
        const std::size_t future_remaining = total - after_short;
        const std::size_t future_max_distance =
                after_short < kMaxMatchProbe ? after_short : kMaxMatchProbe;
        const std::size_t future_max_length =
                future_remaining < kMaxMatchProbe ? future_remaining : kMaxMatchProbe;
        for (std::size_t distance = 1U; distance <= future_max_distance; ++distance) {
            std::size_t length = 0U;
            const std::size_t previous = after_short - distance;
            while (length < future_max_length &&
                    input->data[after_short + length] == input->data[previous + length]) {
                ++length;
            }
            if (length >= 3U) {
                best_length = 2U;
                best_distance = best_len2_distance;
                break;
            }
        }
    }
    if (best_length < 3U && best_len2_distance == 3U && remaining >= 8U) {
        std::uint32_t repeated_pair_cycles = 0U;
        for (std::size_t probe = block + 3U;
                repeated_pair_cycles < 2U && probe + 1U < total; probe += 3U) {
            if (input->data[probe] != input->data[probe - best_len2_distance] ||
                    input->data[probe + 1U] !=
                            input->data[probe + 1U - best_len2_distance]) {
                break;
            }
            ++repeated_pair_cycles;
        }
        if (repeated_pair_cycles >= 2U) {
            best_length = 2U;
            best_distance = best_len2_distance;
        }
    }
    if (best_length < 2U || best_distance == 0U) {
        return false;
    }

    const std::int32_t rep0_distance =
            *reinterpret_cast<const std::int32_t *>(base + kRep0Distance);
    if (rep0_distance >= 0 &&
            best_distance == static_cast<std::uint32_t>(rep0_distance) + 1U &&
            fast_open_input_rep_choice_matches(encoder, 0U, best_length)) {
        apply_fast_rep_choice(encoder, 0U, best_length);
        result.status = LegacyCodecEncoderParseStepStatus::kApplyNow;
        result.path_length = best_length;
        result.chosen_tag = 0U;
        return true;
    }

    result.status = LegacyCodecEncoderParseStepStatus::kApplyNow;
    result.path_length = best_length;
    result.chosen_tag = best_distance + 3U;
    return true;
}

void apply_fast_rep_choice(void *encoder, std::uint32_t rep_index, std::uint32_t rep_length);
bool fast_parse_should_fold_stream_tail(void *encoder);
bool fast_parse_try_lab_47d10_tail(void *encoder, LegacyCodecEncoderParseStepResult &result);
bool fast_try_native_remainder_literal(void *encoder, LegacyCodecEncoderParseStepResult &result);
bool fast_rep_probe_allowed(void *encoder, std::int32_t rep_distance);

bool fast_tail_short_rep0_match(void *encoder, std::uint32_t avail,
        std::uint32_t unencoded) {
    if (encoder == nullptr || unencoded != 2U) {
        return false;
    }
    auto *base = static_cast<std::uint8_t *>(encoder);
    const std::int32_t rep0 =
            *reinterpret_cast<const std::int32_t *>(base + kRep0Distance);
    if (!fast_rep_probe_allowed(encoder, rep0)) {
        return false;
    }
    const std::uint8_t tail_a =
            avail == 0U
            ? static_cast<std::uint8_t>(legacy_codec_matchfinder_get_byte(encoder, -2))
            : static_cast<std::uint8_t>(legacy_codec_matchfinder_get_byte(encoder, 0));
    const std::uint8_t tail_b =
            avail == 0U
            ? static_cast<std::uint8_t>(legacy_codec_matchfinder_get_byte(encoder, -3))
            : static_cast<std::uint8_t>(legacy_codec_matchfinder_get_byte(encoder, -1));
    return tail_a == tail_b && tail_a != 0U;
}

bool fast_try_tail_rep0_step(void *encoder, LegacyCodecEncoderParseStepResult &result) {
    const std::uint32_t unencoded = fast_matchfinder_unencoded_bytes(encoder);
    if (unencoded != 2U) {
        return false;
    }
    if (!fast_parse_should_fold_stream_tail(encoder)) {
        return false;
    }

    LegacyCodecMatchFinderAccess match_finder{encoder};
    const std::uint32_t avail = static_cast<std::uint32_t>(match_finder.has_data());
    const std::uint8_t *window = static_cast<const std::uint8_t *>(match_finder.buffer());

    auto *base = static_cast<std::uint8_t *>(encoder);
    const std::int32_t rep0 =
            *reinterpret_cast<const std::int32_t *>(base + kRep0Distance);
    const std::uint32_t probe_avail = avail > 0U ? avail : unencoded;
    const std::uint32_t rep0_len =
            window != nullptr
            ? oem_probe_rep_length_guarded(encoder, window, rep0, probe_avail)
            : 0U;
    const bool short_rep0 =
            rep0_len < 1U &&
            fast_tail_short_rep0_match(encoder, avail, unencoded);
    if (rep0_len < 1U && !short_rep0) {
        return false;
    }

    apply_fast_rep_choice(encoder, 0U, 1U);
    result.status = LegacyCodecEncoderParseStepStatus::kApplyNow;
    result.path_length = 1U;
    result.chosen_tag = 0U;
    return true;
}

// OEM LAB_00147d10: apply parse-node chain one symbol at a time (goto LAB_0014866c), not
// legacy_codec_encoder_apply_parse_batch.
bool fast_native_47d10_apply_tail(void *encoder, std::uint32_t tail_node) {
    if (encoder == nullptr || tail_node == 0U) {
        return false;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    LegacyCodecMatchFinderAccess match_finder{encoder};
    const std::uint8_t *cur = match_finder.current_literal_ptr();
    const std::uint8_t *get_ptr = static_cast<const std::uint8_t *>(match_finder.buffer());
    const bool fold_at_get_ptr =
            get_ptr != nullptr && cur != nullptr && get_ptr == cur + 1;
    const std::uint32_t unencoded_tail = fast_open_input_unencoded_bytes(encoder);
    const bool eof_single_byte_tail =
            g_lift_fast_full_native && tail_node == 1U && unencoded_tail == 1U &&
            match_finder.has_data() <= 0 && fast_parse_should_fold_stream_tail(encoder);
    const bool stream_tail =
            legacy_codec_encoder_parse_47d10_stream_tail_batch(encoder, tail_node) ||
            (g_lift_fast_full_native && tail_node > 1U &&
                    fast_parse_should_fold_stream_tail(encoder) && fold_at_get_ptr) ||
            eof_single_byte_tail;

    if (stream_tail) {
        auto *processed = reinterpret_cast<std::int32_t *>(base + kProcessedInBlock);
        auto *finish_dc_adjust =
                reinterpret_cast<std::uint32_t *>(base + kFastFinishDcAdjust);
        const int open_size = legacy_codec_encoder_open_input_size(encoder);
        const std::uint64_t block_before_tail = legacy_codec_encoder_block_bytes(encoder);
        const bool pending_folded_tail_finish = *finish_dc_adjust == 3U;
        const bool folded_multi_tail = fold_at_get_ptr && tail_node > 1U;
        bool eof_apply_from_processed = false;

        if (tail_node > 1U) {
            if (folded_multi_tail) {
                *processed = static_cast<std::int32_t>(tail_node);
            } else {
                *processed += static_cast<std::int32_t>(tail_node - 1U);
                match_finder.skip(static_cast<std::int32_t>(tail_node - 1U));
            }
            if (folded_multi_tail) {
                *finish_dc_adjust = 0U;
            } else if (!g_lift_fast_full_native || open_size <= 0 ||
                    block_before_tail < static_cast<std::uint64_t>(open_size)) {
                *finish_dc_adjust = 2U;
            } else {
                *finish_dc_adjust = 0U;
            }
        } else if (eof_single_byte_tail) {
            eof_apply_from_processed = *processed > 0;
            if (eof_apply_from_processed) {
                *finish_dc_adjust = 0U;
            } else {
                *processed = 0;
                *finish_dc_adjust = 2U;
            }
        } else if (*processed == 0) {
            *processed += 1;
        }

        const std::uint8_t *literal_ptr = nullptr;
        if (folded_multi_tail || eof_apply_from_processed) {
            literal_ptr = native_apply_literal_ptr(encoder);
        } else if (fold_at_get_ptr || eof_single_byte_tail) {
            literal_ptr = match_finder.has_data() > 0 ? get_ptr : cur;
        } else {
            literal_ptr = cur;
        }
        if (literal_ptr == nullptr ||
                !legacy_codec_encoder_commit_symbol_ex(encoder, kEncoderTagLiteral, 1U,
                        literal_ptr, false)) {
            return false;
        }
        if (legacy_codec_range_encoder_from_native_state(encoder)->status != 0) {
            return false;
        }
        if ((g_lift_fast_full_native && tail_node > 1U) || pending_folded_tail_finish) {
            *finish_dc_adjust = 3U;
        }
        if ((fold_at_get_ptr && !folded_multi_tail && !eof_apply_from_processed) ||
                (eof_single_byte_tail && !eof_apply_from_processed)) {
            match_finder.skip(1);
        }
    } else {
        LegacyCodecMatchFinderAccess match_finder{encoder};
        for (std::uint32_t node_index = 1U; node_index <= tail_node; ++node_index) {
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
            if (g_lift_fast_full_native) {
                match_finder.skip(1);
            }
        }
    }

tail_graph_reset:
    *reinterpret_cast<std::int32_t *>(base + kProcessedInBlock) = 0;
    *reinterpret_cast<std::uint32_t *>(base + kParseNodeIndex) = 0U;
    *reinterpret_cast<std::uint32_t *>(base + kPendingLiteral) = 0U;
    *reinterpret_cast<std::uint32_t *>(base + kRootLengthSlot) = 0U;
    *reinterpret_cast<std::uint32_t *>(base + kParseCachedAvailLength) = 0U;
    *reinterpret_cast<std::uint32_t *>(base + kParseDistanceCode) = 0U;
    return true;
}

// OEM LAB_00147d10 reads parse-node chain at kRootLengthSlot / kPendingLiteral.
bool fast_seed_tail_rep_literal_graph(void *encoder, std::uint32_t &tail_node) {
    if (encoder == nullptr) {
        return false;
    }

    const std::uint32_t tail_count = fast_bounded_unencoded_bytes(encoder);
    if (tail_count != 2U) {
        return false;
    }

    LegacyCodecMatchFinderAccess match_finder{encoder};
    const std::uint32_t avail = static_cast<std::uint32_t>(match_finder.has_data());
    const std::uint8_t *window = static_cast<const std::uint8_t *>(match_finder.buffer());
    if (window == nullptr && !fast_tail_short_rep0_match(encoder, avail, tail_count)) {
        return false;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    const std::int32_t rep0 =
            *reinterpret_cast<const std::int32_t *>(base + kRep0Distance);
    const std::uint32_t probe_avail = avail > 0U ? avail : tail_count;
    const std::uint32_t rep0_len =
            window != nullptr
            ? oem_probe_rep_length_guarded(encoder, window, rep0, probe_avail)
            : 0U;
    const bool first_is_rep0 =
            rep0_len >= 1U || fast_tail_short_rep0_match(encoder, avail, tail_count);

    *node_price(base, 0U) = 0U;
    *node_tag(base, 0U) = kEncoderTagLiteral;
    *node_parent_index(base, 0U) = 0U;
    *node_flag(base, 0U) = 0U;

    *node_price(base, 1U) = 0U;
    *node_tag(base, 1U) = first_is_rep0 ? 0U : kEncoderTagLiteral;
    *node_parent_index(base, 1U) = 0U;
    *node_flag(base, 1U) = 0U;
    *node_aux_rep(base, 1U) = 0U;

    *node_price(base, 2U) = 0U;
    *node_tag(base, 2U) = kEncoderTagLiteral;
    *node_parent_index(base, 2U) = 1U;
    *node_flag(base, 2U) = 0U;
    *node_aux_rep(base, 2U) = 0U;

    *reinterpret_cast<std::uint32_t *>(base + kRootLengthSlot) = 2U;
    *reinterpret_cast<std::uint32_t *>(base + kPendingLiteral) = 2U;
    *reinterpret_cast<std::uint32_t *>(base + kParseNodeIndex) = 2U;
    *reinterpret_cast<std::uint32_t *>(base + kParseCachedAvailLength) = 2U;
    tail_node = 2U;
    return true;
}

void fast_seed_tail_parse_graph_for_oem(void *encoder) {
    if (encoder == nullptr) {
        return;
    }

    const std::uint32_t tail_count = fast_bounded_unencoded_bytes(encoder);
    if (tail_count == 0U) {
        return;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    if (tail_count > 0x100U) {
        return;
    }

    *node_price(base, 0U) = 0U;
    *node_tag(base, 0U) = kEncoderTagLiteral;
    *node_parent_index(base, 0U) = 0U;
    *node_flag(base, 0U) = 0U;
    for (std::uint32_t i = 1U; i <= tail_count; ++i) {
        *node_price(base, i) = 0U;
        *node_tag(base, i) = kEncoderTagLiteral;
        *node_parent_index(base, i) = i - 1U;
        *node_flag(base, i) = 0U;
        *node_aux_rep(base, i) = 0U;
    }

    *reinterpret_cast<std::uint32_t *>(base + kRootLengthSlot) = tail_count;
    *reinterpret_cast<std::uint32_t *>(base + kPendingLiteral) = tail_count;
    *reinterpret_cast<std::uint32_t *>(base + kParseNodeIndex) = tail_count;
    *reinterpret_cast<std::uint32_t *>(base + kParseCachedAvailLength) = tail_count;
}

bool fast_rep_probe_allowed(void *encoder, std::int32_t rep_distance) {
    if (encoder == nullptr || rep_distance < 0) {
        return false;
    }
    const std::uint64_t encoded = legacy_codec_encoder_block_bytes(encoder);
    return static_cast<std::uint64_t>(rep_distance) + 1U <= encoded;
}

bool fast_parse_should_fold_stream_tail(void *encoder) {
    if (fast_matchfinder_unencoded_bytes(encoder) == 0U) {
        return false;
    }
    LegacyCodecMatchFinderAccess match_finder{encoder};
    void *const ctx = match_finder.context();
    if (ctx == nullptr) {
        return false;
    }
    const auto *mf_bytes = static_cast<const std::uint8_t *>(ctx);
    return *reinterpret_cast<const std::int32_t *>(mf_bytes + kStreamEnd) != 0;
}

bool fast_full_native_prefers_stepped_literal(void *encoder, std::uint32_t avail) {
    if (!g_lift_fast_full_native ||
            legacy_codec_encoder_open_input_is_uniform(encoder)) {
        return false;
    }
    const int open_size = legacy_codec_encoder_open_input_size(encoder);
    if (open_size <= 1) {
        return false;
    }
    const auto block = legacy_codec_encoder_block_bytes(encoder);
    if (block >= static_cast<std::uint64_t>(open_size)) {
        return false;
    }
    if (block != 1U) {
        return false;
    }
    if (fast_open_input_unencoded_bytes(encoder) <= 2U) {
        return false;
    }
    return avail >= 2U;
}

bool fast_full_native_step_literal_before_47d10(void *encoder,
        std::uint32_t unencoded_tail) {
    if (!g_lift_fast_full_native ||
            legacy_codec_encoder_open_input_is_uniform(encoder)) {
        return false;
    }
    if (fast_sparse_pair_tail_rep_allowed(encoder, 2U)) {
        return false;
    }
    return unencoded_tail > 2U && fast_parse_should_fold_stream_tail(encoder);
}

bool fast_full_native_handle_stream_tail(void *encoder, std::uint32_t avail,
        LegacyCodecEncoderParseStepResult &result) {
    if (!g_lift_fast_full_native ||
            legacy_codec_encoder_open_input_is_uniform(encoder) ||
            !fast_parse_should_fold_stream_tail(encoder)) {
        return false;
    }
    const std::uint32_t unencoded = fast_open_input_unencoded_bytes(encoder);
    if (unencoded == 0U || unencoded > 3U) {
        return false;
    }
    if (avail >= 2U && fast_sparse_pair_tail_rep_allowed(encoder, 2U)) {
        return false;
    }
    if (unencoded == 3U) {
        result.status = LegacyCodecEncoderParseStepStatus::kApplyNow;
        result.path_length = 1U;
        result.chosen_tag = kEncoderTagLiteral;
        return true;
    }
    if (unencoded > 2U && avail >= 2U) {
        result.status = LegacyCodecEncoderParseStepStatus::kApplyNow;
        result.path_length = 1U;
        result.chosen_tag = kEncoderTagLiteral;
        return true;
    }
    if (fast_full_native_step_literal_before_47d10(encoder, unencoded)) {
        result.status = LegacyCodecEncoderParseStepStatus::kApplyNow;
        result.path_length = 1U;
        result.chosen_tag = kEncoderTagLiteral;
        return true;
    }
    if (fast_try_native_remainder_literal(encoder, result)) {
        return true;
    }
    if (fast_parse_try_lab_47d10_tail(encoder, result)) {
        return true;
    }
    result.status = LegacyCodecEncoderParseStepStatus::kUnhandledNative;
    result.native_tail_remainder = true;
    return true;
}

// OEM LAB_00147d10: stream tail with avail<2 folds parse-node chain into apply (not stepped literal).
bool fast_parse_try_lab_47d10_tail(void *encoder, LegacyCodecEncoderParseStepResult &result) {
    const std::uint32_t unencoded = fast_open_input_unencoded_bytes(encoder);
    if (unencoded == 0U) {
        return false;
    }

    LegacyCodecMatchFinderAccess match_finder{encoder};
    const std::uint32_t avail = static_cast<std::uint32_t>(match_finder.has_data());
    if (!fast_parse_should_fold_stream_tail(encoder)) {
        return false;
    }
    if (unencoded > 2U) {
        return false;
    }
    // OEM: avail==0 with 3+ bytes left uses stepped literal first (avail==0 branch).
    if (avail == 0U && unencoded > 2U) {
        return false;
    }
    if (g_lift_fast_full_native && unencoded == 2U &&
            fast_tail_short_rep0_match(encoder, avail, unencoded)) {
        apply_fast_rep_choice(encoder, 0U, 2U);
        result.status = LegacyCodecEncoderParseStepStatus::kApplyNow;
        result.path_length = 2U;
        result.chosen_tag = 0U;
        return true;
    }
    if (unencoded == 2U && fast_tail_short_rep0_match(encoder, avail, unencoded)) {
        std::uint32_t rep_tail_node = 0U;
        if (fast_seed_tail_rep_literal_graph(encoder, rep_tail_node)) {
            result.status = LegacyCodecEncoderParseStepStatus::kBacktrackReady;
            result.path_length = rep_tail_node;
            return true;
        }
    }
    if (fast_try_tail_rep0_step(encoder, result)) {
        return true;
    }

    fast_seed_tail_parse_graph_for_oem(encoder);
    auto *base = static_cast<std::uint8_t *>(encoder);
    const std::uint32_t tail_node =
            *reinterpret_cast<std::uint32_t *>(base + kPendingLiteral);
    if (tail_node == 0U) {
        return false;
    }

    result.status = LegacyCodecEncoderParseStepStatus::kBacktrackReady;
    result.path_length = tail_node;
    return true;
}

bool fast_try_native_remainder_literal(void *encoder, LegacyCodecEncoderParseStepResult &result) {
    if (!g_lift_fast_loop_fail_on_unhandled) {
        return false;
    }
    std::uint32_t unencoded = fast_matchfinder_unencoded_bytes(encoder);
    const int open_size = legacy_codec_encoder_open_input_size(encoder);
    const auto block = static_cast<std::uint32_t>(legacy_codec_encoder_block_bytes(encoder));
    if (open_size > static_cast<int>(block)) {
        const std::uint32_t from_input =
                static_cast<std::uint32_t>(open_size) - block;
        if (from_input < unencoded || unencoded == 0U) {
            unencoded = from_input;
        }
    }
    if (unencoded == 0U) {
        return false;
    }
    if (fast_parse_try_lab_47d10_tail(encoder, result)) {
        return true;
    }
    result.status = LegacyCodecEncoderParseStepStatus::kApplyNow;
    result.path_length = 1U;
    result.chosen_tag = kEncoderTagLiteral;
    return true;
}

// OEM-shaped rep0 tail (LAB_00147dd0 remaining bump): fold before lifted rep apply when
// rep0+1 covers all unencoded bytes (lift_fast_loop + lift_fast hybrid).
bool fast_should_use_rep_remainder_tail(void *encoder) {
    const std::uint32_t remaining = fast_matchfinder_unencoded_bytes(encoder);
    if (remaining == 0U) {
        return false;
    }

    LegacyCodecMatchFinderAccess match_finder{encoder};
    const std::uint32_t avail = static_cast<std::uint32_t>(match_finder.has_data());
    const std::uint32_t probe_avail = avail >= 2U ? avail : remaining;
    if (probe_avail < 2U) {
        return false;
    }

    const std::uint8_t *window = static_cast<const std::uint8_t *>(match_finder.buffer());
    if (window == nullptr) {
        return false;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    const std::int32_t rep0 =
            *reinterpret_cast<const std::int32_t *>(base + kRep0Distance);
    const std::uint32_t rep0_len =
            oem_probe_rep_length_guarded(encoder, window, rep0, probe_avail);
    return rep0_len + 1U == remaining;
}

// OEM-shaped rep0 tail (LAB_00147dd0 remaining bump): use folded tail before
// check_block_continue normalizes matchfinder.
bool fast_should_use_body_tail(void *encoder) {
    return fast_should_use_rep_remainder_tail(encoder);
}

// OEM LAB_00147b44 / LAB_00147dd0: probe on get_buffer() with dual-byte gate (no block_bytes check).
std::uint32_t oem_probe_rep_length_at_window(const std::uint8_t *window, std::int32_t rep_distance,
        std::uint32_t avail_limit) {
    if (window == nullptr || rep_distance < 0 || avail_limit == 0U) {
        return 0U;
    }
    if (static_cast<std::uint32_t>(rep_distance) > kMaxMatchProbe) {
        return 0U;
    }

    const std::uint8_t *literal = window - 1;
    const std::ptrdiff_t back = -(static_cast<std::ptrdiff_t>(rep_distance) + 1);
    if (literal[0] != literal[back] || window[0] != window[back]) {
        return 0U;
    }

    if (avail_limit == 1U) {
        return 1U;
    }

    const std::uint32_t limit = clamp_avail(avail_limit);
    if (limit < 3U) {
        return 2U;
    }

    // OEM LAB_00147dd0: loop while (lVar37 + 2) < uVar61; return lVar81 + 1 on full match.
    std::uint32_t probe = 1U;
    while (probe + 1U < limit) {
        if (window[probe] != window[probe + back]) {
            return probe + 1U;
        }
        probe += 1U;
    }
    return probe + 1U;
}

std::uint32_t oem_probe_rep_length_guarded(void *encoder, const std::uint8_t *window,
        std::int32_t rep_distance, std::uint32_t avail_limit) {
    if (!fast_rep_probe_allowed(encoder, rep_distance)) {
        return 0U;
    }
    return oem_probe_rep_length_at_window(window, rep_distance, avail_limit);
}

void fast_probe_lab_47b44_rep_lengths(void *encoder, std::uint32_t avail,
        std::uint32_t rep_lengths[4]);

// Fast greedy rep scan: OEM get_buffer() dual-byte gate (LAB_00147b44 / 00147dd0).
void fast_probe_oem_rep_lengths(void *encoder, std::uint32_t avail,
        std::uint32_t rep_lengths[4]) {
    fast_probe_lab_47b44_rep_lengths(encoder, avail, rep_lengths);

    LegacyCodecMatchFinderAccess match_finder{encoder};
    const std::uint8_t *window = static_cast<const std::uint8_t *>(match_finder.buffer());
    if (window == nullptr) {
        return;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    const std::int32_t rep1 =
            *reinterpret_cast<const std::int32_t *>(base + kRep1Distance);
    const std::uint32_t rep1_len =
            oem_probe_rep_length_guarded(encoder, window, rep1, avail);
    if (rep1_len > rep_lengths[2]) {
        rep_lengths[2] = rep1_len;
    }
}

void fast_probe_lab_47b44_rep_lengths(void *encoder, std::uint32_t avail,
        std::uint32_t rep_lengths[4]) {
    rep_lengths[0] = rep_lengths[1] = rep_lengths[2] = rep_lengths[3] = 0U;
    if (encoder == nullptr || rep_lengths == nullptr || avail == 0U) {
        return;
    }

    LegacyCodecMatchFinderAccess match_finder{encoder};
    const std::uint8_t *window = static_cast<const std::uint8_t *>(match_finder.buffer());
    if (window == nullptr) {
        return;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    constexpr std::size_t kOem47b44Offsets[3] = {kRepDist3, kRep0Distance, kRepDist4};
    constexpr std::uint32_t kOem47b44Indices[3] = {1U, 0U, 3U};
    for (std::size_t slot = 0U; slot < 3U; ++slot) {
        const std::int32_t rep_distance =
                *reinterpret_cast<const std::int32_t *>(base + kOem47b44Offsets[slot]);
        rep_lengths[kOem47b44Indices[slot]] = oem_probe_rep_length_guarded(encoder, window,
                rep_distance, avail);
    }
}

bool fast_emit_rep_choice(void *encoder, std::uint32_t rep_index, std::uint32_t rep_length,
        LegacyCodecEncoderParseStepResult &result) {
    if (rep_length <= 1U) {
        return false;
    }
    if (!fast_open_input_rep_choice_matches(encoder, rep_index, rep_length)) {
        return false;
    }
    apply_fast_rep_choice(encoder, rep_index, rep_length);
    result.status = LegacyCodecEncoderParseStepStatus::kApplyNow;
    result.path_length = rep_length;
    result.chosen_tag = rep_index;
    return true;
}

std::uint32_t fast_maybe_bump_rep_to_remaining(void *encoder, std::uint32_t rep_len) {
    if (encoder == nullptr || rep_len == 0U) {
        return rep_len;
    }
    std::uint32_t remaining = fast_bounded_unencoded_bytes(encoder);
    if (legacy_codec_encoder_open_input_is_uniform(encoder)) {
        const int open_size = legacy_codec_encoder_open_input_size(encoder);
        const auto block = static_cast<std::uint32_t>(legacy_codec_encoder_block_bytes(encoder));
        if (open_size > 0 && static_cast<std::uint32_t>(open_size) > block) {
            const std::uint32_t from_input =
                    static_cast<std::uint32_t>(open_size) - block;
            if (from_input > remaining) {
                remaining = from_input;
            }
        }
    }
    if (remaining > 0U &&
            (rep_len + 1U == remaining ||
                    (legacy_codec_encoder_open_input_is_uniform(encoder) &&
                            rep_len + 2U == remaining))) {
        auto *base = static_cast<std::uint8_t *>(encoder);
        *reinterpret_cast<std::uint32_t *>(base + kFastFinishDcAdjust) = 1U;
        return remaining;
    }
    return rep_len;
}

bool fast_parse_try_lab_47dd0_rep0(void *encoder, std::uint32_t avail,
        std::uint32_t main_match_len, LegacyCodecEncoderParseStepResult &result) {
    if (avail < 2U) {
        return false;
    }

    LegacyCodecMatchFinderAccess match_finder{encoder};
    const std::uint8_t *window = static_cast<const std::uint8_t *>(match_finder.buffer());
    if (window == nullptr) {
        return false;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    const std::int32_t rep0 =
            *reinterpret_cast<const std::int32_t *>(base + kRep0Distance);
    // OEM get_chunk avail at sync entry (pre_sync before processed++).
    std::uint32_t rep0_len =
            oem_probe_rep_length_guarded(encoder, window, rep0, avail);
    const std::uint32_t raw_rep0_len = rep0_len;
    rep0_len = fast_maybe_bump_rep_to_remaining(encoder, rep0_len);
    if (fast_non_uniform_tail_rep_covers_remaining(encoder, raw_rep0_len) ||
            fast_non_uniform_tail_rep_needs_head_literal(encoder, raw_rep0_len)) {
        return false;
    }
    // OEM LAB_00147dd0: rep0 when probe length >= main match (not strictly greater).
    if (rep0_len < 2U) {
        return false;
    }
    if (rep0_len < main_match_len) {
        if (!legacy_codec_encoder_open_input_is_uniform(encoder)) {
            return false;
        }
        const int open_size = legacy_codec_encoder_open_input_size(encoder);
        const auto block = static_cast<std::uint32_t>(legacy_codec_encoder_block_bytes(encoder));
        if (open_size <= 0 ||
                static_cast<std::uint32_t>(open_size) != block + rep0_len) {
            return false;
        }
    }
    return fast_emit_rep_choice(encoder, 0U, rep0_len, result);
}

bool fast_parse_try_lab_47b44(void *encoder, std::uint32_t avail, std::uint32_t main_match_len,
        std::uint32_t &best_rep_index_out, std::uint32_t &best_rep_len_out) {
    best_rep_index_out = 0U;
    best_rep_len_out = 0U;
    if (avail < 2U) {
        return false;
    }

    std::uint32_t rep_lengths[4] = {};
    fast_probe_lab_47b44_rep_lengths(encoder, avail, rep_lengths);

    std::uint32_t best_len = 0U;
    std::uint32_t best_index = 0U;
    for (std::uint32_t index = 0U; index < 4U; ++index) {
        if (rep_lengths[index] >= main_match_len && rep_lengths[index] > best_len) {
            best_len = rep_lengths[index];
            best_index = index;
        }
    }
    if (best_len >= main_match_len && best_len >= 2U) {
        best_rep_index_out = best_index;
        best_rep_len_out = best_len;
        return true;
    }

    best_len = 0U;
    best_index = 0U;
    for (std::uint32_t index = 0U; index < 4U; ++index) {
        if (rep_lengths[index] > best_len) {
            best_len = rep_lengths[index];
            best_index = index;
        }
    }
    if (best_len <= 1U) {
        return false;
    }

    best_rep_index_out = best_index;
    best_rep_len_out = best_len;
    return true;
}

std::uint32_t normalize_cached_match_distance(const std::uint8_t *base,
        std::uint32_t distance_index, std::uint32_t match_length, std::uint32_t &distance_out) {
    distance_out = 0U;
    if (distance_index == 0U || match_length == 0U) {
        return match_length;
    }

    const auto *match_buffer = reinterpret_cast<const std::uint32_t *>(base + kMatchBuffer);
    std::uint32_t effective_length = match_length;
    std::uint32_t distance = match_buffer[distance_index - 1U];
    std::uint32_t index = distance_index;
    while (index > 2U) {
        const std::uint32_t prev_distance = match_buffer[index - 2U];
        const std::uint32_t prev_length = match_buffer[index - 3U];
        if (effective_length != prev_distance + 1U || (distance >> 7U) <= prev_length) {
            break;
        }
        index -= 2U;
        distance = prev_distance;
        effective_length = prev_distance;
    }

    if (distance > 0x7fU && effective_length == 2U) {
        effective_length = 1U;
    }

    distance_out = distance;
    return effective_length;
}

bool fast_pick_fallback_rep(void *encoder, std::uint32_t avail, std::uint32_t &rep_index_out,
        std::uint32_t &rep_len_out) {
    std::uint32_t rep_lengths[4] = {};
    fast_probe_oem_rep_lengths(encoder, avail, rep_lengths);

    std::uint32_t best_len = 0U;
    std::uint32_t best_index = 1U;
    for (std::uint32_t index = 1U; index < 4U; ++index) {
        if (rep_lengths[index] > best_len) {
            best_len = rep_lengths[index];
            best_index = index;
        }
    }

    if (best_len <= 1U) {
        return false;
    }

    rep_index_out = best_index;
    rep_len_out = best_len;
    return true;
}

bool fast_pick_match_heuristic(std::uint32_t effective_match_length, std::uint32_t match_distance,
        std::uint32_t fallback_rep_len, std::uint32_t fallback_rep_index,
        std::uint32_t &tag_out, std::uint32_t &length_out) {
    if (fallback_rep_len <= 1U) {
        return false;
    }

    if (effective_match_length <= fallback_rep_len + 1U ||
            (match_distance > 0x1ffU && effective_match_length <= fallback_rep_len + 2U) ||
            (match_distance > 0x7fffU && effective_match_length <= fallback_rep_len + 3U)) {
        tag_out = fallback_rep_index;
        length_out = fallback_rep_len;
        return true;
    }
    return false;
}

bool fast_pick_rep0_short(void *encoder, std::uint32_t &tag_out, std::uint32_t &len_out) {
    auto *base = static_cast<std::uint8_t *>(encoder);
    LegacyCodecMatchFinderAccess match_finder{encoder};
    const std::uint8_t *cur = match_finder.current_literal_ptr();
    if (cur == nullptr) {
        return false;
    }

    const std::int32_t rep0 =
            *reinterpret_cast<const std::int32_t *>(base + kRep0Distance);
    if (rep0 < 0) {
        return false;
    }
    const std::uint8_t match_byte = cur[-static_cast<std::size_t>(rep0 + 1)];
    if (cur[0] != match_byte) {
        return false;
    }

    // OEM LAB_00148214 rep-chain walk ends in a single-byte literal, not rep0-short rep.
    tag_out = kEncoderTagLiteral;
    len_out = 1U;
    return true;
}

void apply_fast_rep_choice(void *encoder, std::uint32_t rep_index, std::uint32_t rep_length) {
    auto *base = static_cast<std::uint8_t *>(encoder);
    const std::uint32_t unencoded = fast_matchfinder_unencoded_bytes(encoder);
    LegacyCodecMatchFinderAccess match_finder{encoder};
    const std::uint32_t avail = static_cast<std::uint32_t>(match_finder.has_data());
    if (rep_length == unencoded || rep_length == avail) {
        *reinterpret_cast<std::uint32_t *>(base + kFastFinishDcAdjust) = 1U;
    }
    auto *processed = reinterpret_cast<std::int32_t *>(base + kProcessedInBlock);
    (void)rep_index;
    if (rep_length > 1U) {
        *processed += static_cast<std::int32_t>(rep_length - 1U);
    }
}

bool rep_chain_walk_matches(const std::uint8_t *cur, std::int32_t rep_distance,
        std::uint32_t effective_match_len) {
    if (cur == nullptr || rep_distance < 0 || effective_match_len < 2U) {
        return false;
    }

    const std::uint32_t probe_limit = effective_match_len - 1U;
    const std::ptrdiff_t back = -(static_cast<std::ptrdiff_t>(rep_distance) + 1);
    if (cur[0] != cur[back]) {
        return false;
    }
    if (probe_limit < 2U) {
        return true;
    }
    if (cur[1] != cur[-rep_distance]) {
        return false;
    }

    std::uint32_t steps = probe_limit - 2U;
    std::uint32_t probe = 1U;
    while (steps > 0U) {
        probe += 1U;
        if (cur[probe] != cur[probe + back]) {
            return false;
        }
        steps -= 1U;
    }
    return true;
}

bool fast_try_lab_48214_decision(void *encoder, std::uint32_t avail,
        std::uint32_t effective_match_len, std::uint32_t match_distance,
        std::uint32_t distance_index, std::uint32_t &tag_out, std::uint32_t &len_out,
        bool skip_rep_chain_walk) {
    const std::uint32_t min_avail =
            legacy_codec_encoder_fast_open_body_active(encoder) ? 2U : 3U;
    if (avail < min_avail || effective_match_len < 2U || distance_index == 0U) {
        return false;
    }

    LegacyCodecMatchFinderAccess match_finder{encoder};
    const std::uint8_t *cur = match_finder.current_literal_ptr();
    if (cur == nullptr) {
        return false;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    if (!skip_rep_chain_walk) {
        constexpr std::size_t kRepChainOffsets[4] = {
                kRep0Distance, kRepDist3, kRep1Distance, kRepDist4,
        };
        for (const std::size_t offset : kRepChainOffsets) {
            const std::int32_t rep_distance =
                    *reinterpret_cast<const std::int32_t *>(base + offset);
            if (rep_chain_walk_matches(cur, rep_distance, effective_match_len)) {
                tag_out = kEncoderTagLiteral;
                len_out = 1U;
                return true;
            }
        }
    }

    if (!lifted_new_match_distance_allowed(base, match_distance) && !skip_rep_chain_walk) {
        return false;
    }

    std::uint32_t encode_distance = match_distance;
    if (distance_index != 0U) {
        const auto *match_buffer =
                reinterpret_cast<const std::uint32_t *>(base + kMatchBuffer);
        encode_distance = legacy_codec_match_buffer_lz_distance(match_buffer, distance_index);
    }
    if (legacy_codec_encoder_lift_fast_full_native() &&
            !legacy_codec_encoder_fast_open_body_active(encoder) &&
            static_cast<std::uint64_t>(encode_distance) + 1U >
                    legacy_codec_encoder_block_bytes(encoder)) {
        return false;
    }
    if (legacy_codec_encoder_lift_fast_full_native() &&
            !legacy_codec_encoder_fast_open_body_active(encoder)) {
        const std::uint32_t actual_distance = encode_distance + 1U;
        if (actual_distance == 0U || cur[0] != cur[-static_cast<std::int32_t>(actual_distance)]) {
            return false;
        }
        struct FastInputView {
            unsigned long (*read)(long ctx, void *dst, unsigned long *size);
            const std::uint8_t *data;
            int total;
            int pos;
        };
        const auto *input = *reinterpret_cast<const FastInputView * const *>(
                base + legacy_oem_encoder::kInputReadCallback);
        const std::uint64_t block = legacy_codec_encoder_block_bytes(encoder);
        if (input != nullptr && input->data != nullptr && input->total > 0 &&
                block < static_cast<std::uint64_t>(input->total) &&
                static_cast<std::uint64_t>(actual_distance) <= block) {
            const std::size_t current = static_cast<std::size_t>(block);
            const std::size_t previous = current - static_cast<std::size_t>(actual_distance);
            const std::size_t remaining =
                    static_cast<std::size_t>(input->total) - current;
            const std::size_t requested_len = static_cast<std::size_t>(effective_match_len);
            const std::size_t compare_len = requested_len < remaining ? requested_len : remaining;
            for (std::size_t i = 0; i < compare_len; ++i) {
                if (input->data[current + i] != input->data[previous + i]) {
                    return false;
                }
            }
        }
    }

    tag_out = encode_distance + 4U;
    len_out = effective_match_len;
    if (len_out < 2U) {
        len_out = 2U;
    }
    return true;
}

}  // namespace

struct MatchfinderFindHoldGuard {
    explicit MatchfinderFindHoldGuard(bool enabled) : enabled_(enabled) {
        if (enabled_) {
            legacy_codec_encoder_set_matchfinder_find_hold_position(true);
        }
    }
    ~MatchfinderFindHoldGuard() {
        if (enabled_) {
            legacy_codec_encoder_set_matchfinder_find_hold_position(false);
        }
    }

    MatchfinderFindHoldGuard(const MatchfinderFindHoldGuard &) = delete;
    MatchfinderFindHoldGuard &operator=(const MatchfinderFindHoldGuard &) = delete;

private:
    bool enabled_ = false;
};

bool legacy_codec_encoder_sync_matchfinder_when_idle(void *encoder,
        std::uint32_t *match_length_out, std::uint32_t *distance_index_out) {
    if (encoder == nullptr) {
        return false;
    }

    using namespace legacy_oem_encoder;
    using namespace legacy_oem_parse;

    auto *base = static_cast<std::uint8_t *>(encoder);
    auto *processed = reinterpret_cast<std::int32_t *>(base + kProcessedInBlock);
    std::uint32_t match_length = 0U;
    std::uint32_t distance_index = 0U;

    if (*processed == 0) {
        const bool oem_open_body = legacy_codec_encoder_fast_open_body_active(encoder);
        const std::uint32_t cached_length =
                *reinterpret_cast<std::uint32_t *>(base + kParseCachedAvailLength);
        const std::uint32_t cached_distance =
                *reinterpret_cast<std::uint32_t *>(base + kParseDistanceCode);
        if (oem_open_body &&
                (cached_length != 0U || cached_distance != 0U ||
                        cached_length == kParseSyncDeferSkipFlag)) {
            match_length = cached_length;
            if (match_length == kParseSyncDeferSkipFlag) {
                match_length = 0U;
            }
            distance_index = cached_distance;
        } else {
        const MatchfinderFindHoldGuard find_hold_guard(oem_open_body);
        LegacyCodecMatchFinderAccess match_finder{encoder};
        void **slots = reinterpret_cast<void **>(encoder);
        const auto get_chunk = reinterpret_cast<int (*)(void *)>(slots[2]);
        const auto find_match = reinterpret_cast<int (*)(void *, void *)>(slots[4]);
        const auto get_buffer = reinterpret_cast<std::uint8_t *(*)(void *)>(slots[3]);
        if (get_chunk == nullptr || find_match == nullptr || get_buffer == nullptr) {
            return false;
        }

        void *const ctx = match_finder.context();
        const std::uint32_t avail = static_cast<std::uint32_t>(get_chunk(ctx));
        *reinterpret_cast<std::uint32_t *>(base + kMatchfinderAvail) = avail;

        if (avail == 0U) {
            match_length = 0U;
            distance_index = 0U;
            *reinterpret_cast<std::uint32_t *>(base + kParseCachedAvailLength) = match_length;
            *reinterpret_cast<std::uint32_t *>(base + kParseDistanceCode) = distance_index;
            if (match_length_out != nullptr) {
                *match_length_out = match_length;
            }
            if (distance_index_out != nullptr) {
                *distance_index_out = distance_index;
            }
            return true;
        }

        const auto *mf_bytes = static_cast<const std::uint8_t *>(ctx);
        const std::uint32_t match_len_limit =
                *reinterpret_cast<const std::uint32_t *>(mf_bytes + kMatchLenLimit);
        if (match_len_limit < 4U) {
            // OEM FUN_001420cc short path when *(mf + 0x14) < 4.
            match_length = 0U;
            distance_index = 0U;
            if (match_len_limit == 1U) {
                *reinterpret_cast<std::uint32_t *>(base + kParseCachedAvailLength) =
                        kParseSyncDeferSkipFlag;
            } else {
                *reinterpret_cast<std::uint32_t *>(base + kParseCachedAvailLength) = match_length;
            }
        } else {
            distance_index = static_cast<std::uint32_t>(find_match(ctx, base + kMatchBuffer));
            if (distance_index == 0U) {
                match_length = 0U;
            } else {
                const auto *match_buffer = reinterpret_cast<const std::uint32_t *>(base + kMatchBuffer);
                match_length = match_buffer[distance_index - 2U];
                const std::uint32_t min_match = kLzmaMinMatchLength;
                if (!oem_open_body) {
                    if (match_length == min_match) {
                        std::uint32_t extended = match_length;
                        if (!legacy_codec_encoder_extend_match_length(encoder, distance_index, avail,
                                    extended)) {
                            extended = match_length;
                        }
                        match_length = extended;
                    } else if (match_length > min_match) {
                        const std::uint32_t dist = match_buffer[distance_index - 1U];
                        if (dist == min_match) {
                            const std::uint8_t *window = get_buffer(ctx);
                            const std::uint8_t *cur = window - 1U;
                            const std::uint32_t limit = avail > 0x111U ? 0x111U : avail;
                            std::uint32_t probe = match_length;
                            while (probe < limit) {
                                const std::int32_t rep0 =
                                        *reinterpret_cast<const std::int32_t *>(base + kRep0Distance);
                                const std::uint8_t *ref = cur - static_cast<std::size_t>(rep0 + 1);
                                if (cur[probe - 1U] != ref[probe - 1U]) {
                                    break;
                                }
                                probe += 1U;
                            }
                            match_length = probe;
                        }
                    }
                }
            }

            *reinterpret_cast<std::uint32_t *>(base + kParseCachedAvailLength) = match_length;
        }
        *reinterpret_cast<std::uint32_t *>(base + kParseDistanceCode) = distance_index;
        if (!oem_open_body) {
            *processed += 1;
        }
        }
    } else {
        match_length = *reinterpret_cast<std::uint32_t *>(base + kParseCachedAvailLength);
        if (match_length == kParseSyncDeferSkipFlag) {
            match_length = 0U;
        }
        distance_index = *reinterpret_cast<std::uint32_t *>(base + kParseDistanceCode);
    }

    if (match_length_out != nullptr) {
        *match_length_out = match_length;
    }
    if (distance_index_out != nullptr) {
        *distance_index_out = distance_index;
    }
    return true;
}

namespace {

bool fast_sync_matchfinder(void *encoder, std::uint32_t &match_length,
        std::uint32_t &distance_index) {
    return legacy_codec_encoder_sync_matchfinder_when_idle(encoder, &match_length,
            &distance_index);
}

bool fast_try_finalize_full_native_open_input(void *encoder) {
    if (!g_lift_fast_full_native) {
        return false;
    }
    const int open_size = legacy_codec_encoder_open_input_size(encoder);
    if (open_size <= 0) {
        return false;
    }
    if (legacy_codec_encoder_block_bytes(encoder) <
            static_cast<std::uint64_t>(open_size)) {
        return false;
    }
    if (legacy_codec_encoder_finish_flag(encoder) != 0U) {
        return true;
    }
    if (legacy_codec_encoder_try_encode_pending_eof_literal(encoder)) {
        legacy_codec_encoder_after_apply_batch(encoder);
    }
    legacy_codec_encoder_finalize_stream_block(encoder);
    return true;
}

int lifted_loop_return_status(void *encoder, int oem_return_code) {
    if (legacy_codec_encoder_finish_flag(encoder) != 0U) {
        return oem_return_code == 1 ? 0 : oem_return_code;
    }
    if (g_lift_fast_full_native && fast_try_finalize_full_native_open_input(encoder)) {
        return 0;
    }
    if (oem_return_code == 1) {
        return 0;
    }
    return oem_return_code;
}

namespace {

struct EncoderInputView {
    unsigned long (*read)(long ctx, void *dst, unsigned long *size);
    const std::uint8_t *data;
    int total;
    int pos;
};

bool fast_input_span_uniform(const std::uint8_t *data, std::uint32_t count) {
    if (data == nullptr || count < 2U) {
        return false;
    }
    const std::uint8_t lead = data[0];
    for (std::uint32_t i = 1U; i < count; ++i) {
        if (data[i] != lead) {
            return false;
        }
    }
    return true;
}

}  // namespace

// True when all remaining bytes are the same (repeat_a native rep tail).
bool fast_uniform_repeat_remainder(void *encoder) {
    std::uint32_t unencoded = fast_matchfinder_unencoded_bytes(encoder);
    const int open_size = legacy_codec_encoder_open_input_size(encoder);
    const std::uint64_t block = legacy_codec_encoder_block_bytes(encoder);
    if (open_size > 0 && static_cast<std::uint64_t>(open_size) > block) {
        const std::uint32_t from_input =
                static_cast<std::uint32_t>(open_size) - static_cast<std::uint32_t>(block);
        if (from_input > unencoded) {
            unencoded = from_input;
        }
    }
    if (unencoded < 2U) {
        return false;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    const auto *input = *reinterpret_cast<const EncoderInputView * const *>(
            base + legacy_oem_encoder::kInputReadCallback);
    if (input != nullptr && input->data != nullptr) {
        const std::size_t start = static_cast<std::size_t>(block);
        const std::size_t end = start + static_cast<std::size_t>(unencoded);
        if (end <= static_cast<std::size_t>(input->total)) {
            return fast_input_span_uniform(input->data + start, unencoded);
        }
    }

    LegacyCodecMatchFinderAccess match_finder{encoder};
    const std::uint32_t avail = static_cast<std::uint32_t>(match_finder.has_data());
    if (avail < 2U) {
        return false;
    }
    const std::uint8_t *window = static_cast<const std::uint8_t *>(match_finder.buffer());
    if (window == nullptr) {
        return false;
    }
    const std::uint32_t limit = avail < unencoded ? avail : unencoded;
    return fast_input_span_uniform(window, limit);
}

namespace {

bool fast_non_uniform_open_body(void *encoder) {
    return legacy_codec_encoder_open_input_size(encoder) > 1 &&
            !fast_uniform_repeat_remainder(encoder);
}

bool fast_skip_forced_literal_at_block_zero(void *encoder) {
    return g_fast_native_open_parse &&
            legacy_codec_encoder_block_bytes(encoder) == 0U;
}

struct FastOemStyleOpenGuard {
    explicit FastOemStyleOpenGuard(bool enabled) : enabled_(enabled) {
        if (enabled_) {
            g_fast_native_open_parse = true;
        }
    }
    ~FastOemStyleOpenGuard() {
        if (enabled_) {
            g_fast_native_open_parse = false;
        }
    }

    FastOemStyleOpenGuard(const FastOemStyleOpenGuard &) = delete;
    FastOemStyleOpenGuard &operator=(const FastOemStyleOpenGuard &) = delete;

private:
    bool enabled_ = false;
};

int fast_run_non_uniform_unhandled(void *encoder, int mode, unsigned long input_limit,
        unsigned int output_limit) {
    (void)mode;
    (void)input_limit;
    (void)output_limit;
    if (legacy_codec_encoder_uses_lifted_matchfinder(encoder) &&
            !legacy_codec_encoder_run_matchfinder_init_if_needed(encoder)) {
        return 2;
    }
    return 2;
}

}  // namespace

int run_lifted_encode_loop(void *encoder, int mode, unsigned long input_limit,
        unsigned int output_limit, bool fast_literal_mode) {
    auto *base = static_cast<std::uint8_t *>(encoder);

    if (fast_try_finalize_full_native_open_input(encoder)) {
        return lifted_loop_return_status(encoder, 0);
    }

    bool try_native_non_uniform_open = false;
    if (fast_literal_mode &&
            legacy_codec_encoder_block_bytes(encoder) == 0U &&
            fast_matchfinder_unencoded_bytes(encoder) > 1U) {
        const bool stream_native_oem =
                g_lift_fast_stream_native_from_zero &&
                legacy_codec_encoder_open_input_size(encoder) > 1;
        const bool non_uniform_open_body = fast_non_uniform_open_body(encoder);
        try_native_non_uniform_open =
                g_lift_fast_try_native_non_uniform_open &&
                g_lift_fast_loop_fail_on_unhandled && non_uniform_open_body &&
                !g_lift_fast_full_native;
        if (!try_native_non_uniform_open && !g_lift_fast_full_native &&
                (!g_lift_fast_loop_fail_on_unhandled || stream_native_oem ||
                 non_uniform_open_body)) {
            return fast_run_non_uniform_unhandled(encoder, mode, input_limit, output_limit);
        }
    }

    if (try_native_non_uniform_open) {
        return fast_run_non_uniform_unhandled(encoder, mode, input_limit, output_limit);
    }

    const FastOemStyleOpenGuard oem_style_open_guard(false);

    if (legacy_codec_encoder_uses_lifted_matchfinder(encoder) &&
            !legacy_codec_encoder_run_matchfinder_init_if_needed(encoder)) {
        return 2;
    }

    if (fast_literal_mode) {
        if (legacy_codec_encoder_block_bytes(encoder) == 0U) {
            const std::uint32_t open_local_dc = legacy_codec_encoder_fast_local_dc(encoder);
            if (!try_native_non_uniform_open &&
                    open_local_dc == 0U &&
                    !legacy_codec_matchfinder_warmup(encoder)) {
                return 2;
            }
            if (!try_native_non_uniform_open &&
                    !g_lift_fast_loop_fail_on_unhandled &&
                    legacy_codec_encoder_open_input_size(encoder) == 1) {
                if (!legacy_codec_encoder_encode_fast_stream_entry_after_warmup(encoder)) {
                    return 2;
                }
                if (legacy_codec_encoder_try_encode_pending_eof_literal(encoder)) {
                    legacy_codec_encoder_after_apply_batch(encoder);
                }
                legacy_codec_encoder_finalize_stream_block(encoder);
                return lifted_loop_return_status(encoder,
                        static_cast<int>(legacy_codec_encoder_status_code(encoder)));
            }
            if (fast_matchfinder_unencoded_bytes(encoder) > 1U &&
                    !fast_uniform_repeat_remainder(encoder) &&
                    !g_lift_fast_full_native &&
                    !try_native_non_uniform_open &&
                    open_local_dc == 0U &&
                    !legacy_codec_encoder_encode_fast_stream_entry_after_warmup(encoder)) {
                return 2;
            }
            if (g_lift_fast_full_native &&
                    legacy_codec_encoder_open_input_size(encoder) > 1 &&
                    !legacy_codec_encoder_encode_fast_stream_entry_after_warmup(encoder)) {
                return 2;
            }
        }
    } else if (legacy_codec_encoder_block_bytes(encoder) == 0U &&
            !legacy_codec_encoder_try_first_literal(encoder)) {
        return 2;
    }

    if (fast_literal_mode && g_lift_fast_loop_fail_on_unhandled) {
        g_fast_loop_block_snapshot = legacy_codec_encoder_block_bytes(encoder);
    }

    const std::uint32_t block_start_pos = legacy_codec_encoder_pos_state(encoder);
    bool first_iteration = true;

    const auto stream_fully_encoded = [&]() -> bool {
        LegacyCodecMatchFinderAccess mf{encoder};
        void *const ctx = mf.context();
        if (ctx == nullptr) {
            return false;
        }
        const auto *mf_bytes = static_cast<const std::uint8_t *>(ctx);
        const auto *words = reinterpret_cast<const std::uint64_t *>(ctx);
        const std::uint32_t history_size =
                *reinterpret_cast<const std::uint32_t *>(mf_bytes + kHistorySize);
        const std::uint64_t block_bytes = legacy_codec_encoder_block_bytes(encoder);
        const std::uint32_t read_bytes =
                static_cast<std::uint32_t>(words[1]) - history_size;
        const std::uint32_t write_bytes =
                static_cast<std::uint32_t>(words[2]) - history_size;
        if (*reinterpret_cast<const std::int32_t *>(mf_bytes + kStreamEnd) != 0) {
            return static_cast<std::uint64_t>(write_bytes) <= block_bytes;
        }
        return static_cast<std::uint64_t>(read_bytes) <= block_bytes;
    };

    while (true) {
        LegacyCodecMatchFinderAccess match_finder{encoder};
        auto *processed_slot = reinterpret_cast<std::int32_t *>(base + kProcessedInBlock);
        const int open_size = legacy_codec_encoder_open_input_size(encoder);
        const auto block_bytes = legacy_codec_encoder_block_bytes(encoder);
        if (fast_try_finalize_full_native_open_input(encoder)) {
            return lifted_loop_return_status(encoder, 0);
        }
        const bool lifted_input_remainder = [&]() -> bool {
            return open_size > static_cast<int>(block_bytes);
        }();
        const bool input_fully_encoded = [&]() -> bool {
            return open_size > 0 && block_bytes >= static_cast<std::uint64_t>(open_size);
        }();
        if (*processed_slot == 0 && input_fully_encoded) {
            legacy_codec_encoder_finalize_stream_block(encoder);
            return lifted_loop_return_status(encoder, 0);
        }
        if (*processed_slot == 0 && !input_fully_encoded &&
                (match_finder.has_data() <= 0 || stream_fully_encoded() ||
                        (fast_parse_should_fold_stream_tail(encoder) &&
                                fast_matchfinder_unencoded_bytes(encoder) == 0U))) {
            void *const mf_ctx = match_finder.context();
            if (mf_ctx != nullptr) {
                auto *mf_bytes = static_cast<std::uint8_t *>(mf_ctx);
                if (*reinterpret_cast<std::int32_t *>(mf_bytes + kStreamEnd) == 0) {
                    legacy_codec_encoder_matchfinder_runtime_fill(mf_ctx);
                }
            }
            if (match_finder.normalize() && match_finder.has_data() > 0 &&
                    !stream_fully_encoded()) {
                continue;
            }
            if (legacy_codec_encoder_try_encode_pending_eof_literal(encoder)) {
                legacy_codec_encoder_after_apply_batch(encoder);
            }
            legacy_codec_encoder_finalize_stream_block(encoder);
            return lifted_loop_return_status(encoder,
                    static_cast<int>(legacy_codec_encoder_status_code(encoder)));
        }

        const std::uint32_t pos_state = legacy_codec_encoder_fast_pos_state(encoder);
        LegacyCodecMatchFinderAccess pre_parse_match_finder{encoder};
        const std::uint8_t *pre_parse_literal_ptr =
                pre_parse_match_finder.current_literal_ptr();
        const LegacyCodecEncoderParseStepResult parse_result = fast_literal_mode
                ? legacy_codec_encoder_fast_parse_step(encoder, pos_state)
                : legacy_codec_encoder_optimal_parse_step(encoder, pos_state);

        if (parse_result.status == LegacyCodecEncoderParseStepStatus::kUnhandledNative) {
            if (parse_result.native_tail_remainder) {
                LegacyCodecEncoderParseStepResult tail_result{};
                if (fast_parse_try_lab_47d10_tail(encoder, tail_result) &&
                        tail_result.status ==
                                LegacyCodecEncoderParseStepStatus::kBacktrackReady) {
                    const bool applied = fast_native_47d10_apply_tail(encoder,
                            tail_result.path_length);
                    if (!applied) {
                        return 2;
                    }
                    legacy_codec_encoder_after_apply_batch(encoder);
                    continue;
                }
            }
            if (!g_lift_fast_loop_fail_on_unhandled) {
                return 2;
            }
            if (!g_lift_fast_full_native &&
                    legacy_codec_encoder_open_input_is_uniform(encoder) &&
                    legacy_codec_encoder_open_input_size(encoder) > 1) {
                return 2;
            }
            if (g_lift_fast_full_native) {
                LegacyCodecEncoderParseStepResult tail_result{};
                if (fast_parse_try_lab_47d10_tail(encoder, tail_result) &&
                        tail_result.status ==
                                LegacyCodecEncoderParseStepStatus::kBacktrackReady) {
                    const bool applied = fast_native_47d10_apply_tail(encoder,
                            tail_result.path_length);
                    if (!applied) {
                        return 2;
                    }
                    legacy_codec_encoder_after_apply_batch(encoder);
                    if (fast_try_finalize_full_native_open_input(encoder)) {
                        return lifted_loop_return_status(encoder, 0);
                    }
                    continue;
                }
            }
            {
                const int open_size = legacy_codec_encoder_open_input_size(encoder);
                const std::uint64_t block_done = legacy_codec_encoder_block_bytes(encoder);
                if (open_size > 0 &&
                        block_done >= static_cast<std::uint64_t>(open_size)) {
                    legacy_codec_encoder_finalize_stream_block(encoder);
                    return lifted_loop_return_status(encoder, 0);
                }
            }
            return 2;
        }
        if (parse_result.status == LegacyCodecEncoderParseStepStatus::kError) {
            return 2;
        }

        first_iteration = false;

        if (parse_result.status == LegacyCodecEncoderParseStepStatus::kApplyNow) {
            // Uniform-tail / rep0-tail must be decided before commit_parse_choice advances MF
            // (post-literal skip makes fast_matchfinder_unencoded_bytes() read 0).
            const std::uint64_t block_before_commit =
                    legacy_codec_encoder_block_bytes(encoder);
            const int open_input_bytes = legacy_codec_encoder_open_input_size(encoder);
            const bool first_literal_multi_byte_unhandled = block_before_commit == 0U &&
                    open_input_bytes > 1;
            const bool unhandled_after_apply = fast_literal_mode &&
                    parse_result.chosen_tag == kEncoderTagLiteral &&
                    (first_literal_multi_byte_unhandled ||
                            fast_uniform_repeat_remainder(encoder) ||
                            fast_should_use_rep_remainder_tail(encoder));
            const bool uniform_literal_open_unhandled = block_before_commit == 0U &&
                    parse_result.chosen_tag == kEncoderTagLiteral &&
                    (fast_uniform_repeat_remainder(encoder) ||
                            (legacy_codec_encoder_open_input_is_uniform(encoder) &&
                                    open_input_bytes > 1));
            const bool non_uniform_open_unhandled = block_before_commit == 0U &&
                    parse_result.chosen_tag == kEncoderTagLiteral &&
                    open_input_bytes > 1 &&
                    !legacy_codec_encoder_open_input_is_uniform(encoder);
            *reinterpret_cast<std::uint32_t *>(base + kParseChoiceTag) =
                    parse_result.chosen_tag;
            *reinterpret_cast<std::uint32_t *>(base + kPendingLiteral) =
                    parse_result.path_length;
            if (!legacy_codec_encoder_commit_parse_choice_with_literal(encoder,
                        pre_parse_literal_ptr)) {
                return 2;
            }
            *reinterpret_cast<std::uint32_t *>(base + kParseNodeIndex) = 0U;
            *reinterpret_cast<std::uint32_t *>(base + kPendingLiteral) = 0U;
            *reinterpret_cast<std::uint32_t *>(base + kParseCachedAvailLength) = 0U;
            *reinterpret_cast<std::uint32_t *>(base + legacy_oem_parse::kParseDistanceCode) = 0U;
            legacy_codec_encoder_after_apply_batch(encoder);
            const bool will_return_unhandled =
                    !g_lift_fast_loop_fail_on_unhandled &&
                    !g_lift_fast_full_native &&
                    unhandled_after_apply &&
                    (uniform_literal_open_unhandled || non_uniform_open_unhandled);
            if (fast_literal_mode && parse_result.chosen_tag == kEncoderTagLiteral &&
                    parse_result.path_length == 1U && !will_return_unhandled &&
                    !uniform_literal_open_unhandled) {
                LegacyCodecMatchFinderAccess post_literal_mf{encoder};
                if (post_literal_mf.current_literal_ptr() == pre_parse_literal_ptr) {
                    post_literal_mf.skip(1);
                }
            }
            if (will_return_unhandled) {
                return 2;
            }
            if (fast_try_finalize_full_native_open_input(encoder)) {
                return lifted_loop_return_status(encoder, 0);
            }
        } else if (parse_result.status == LegacyCodecEncoderParseStepStatus::kBacktrackReady) {
            const std::uint32_t tail_left = fast_open_input_unencoded_bytes(encoder);
            const bool full_native_eof_tail = g_lift_fast_full_native &&
                    fast_parse_should_fold_stream_tail(encoder) &&
                    tail_left == 1U && parse_result.path_length == 1U;
            const bool oem_47d10_tail =
                    fast_literal_mode &&
                    parse_result.path_length >= 1U &&
                    parse_result.path_length <= 2U &&
                    tail_left > 0U &&
                    tail_left <= 2U &&
                    (legacy_codec_encoder_parse_47d10_stream_tail_batch(encoder,
                            parse_result.path_length) ||
                            full_native_eof_tail ||
                            (g_lift_fast_full_native &&
                                    parse_result.path_length > 1U &&
                                    fast_parse_should_fold_stream_tail(encoder)));
            const bool applied = oem_47d10_tail
                    ? fast_native_47d10_apply_tail(encoder, parse_result.path_length)
                    : legacy_codec_encoder_apply_parse_batch(encoder, parse_result.path_length);
            if (!applied) {
                return 2;
            }
            legacy_codec_encoder_after_apply_batch(encoder);
            if (fast_try_finalize_full_native_open_input(encoder)) {
                return lifted_loop_return_status(encoder, 0);
            }
        }

        const auto *stream = legacy_codec_range_encoder_from_native_state(encoder);
        if (stream != nullptr && stream->status != 0) {
            return stream->status;
        }

        const auto continue_status = legacy_codec_encoder_check_block_continue(encoder, mode,
                input_limit, output_limit, block_start_pos,
                legacy_codec_encoder_pos_state(encoder));
        if (!continue_status.keep_encoding) {
            // Re-check EOF via continue_status, not match_finder from loop head (stale after encode).
            if (continue_status.return_code == 0) {
                if (legacy_codec_encoder_try_encode_pending_eof_literal(encoder)) {
                    legacy_codec_encoder_after_apply_batch(encoder);
                }
                legacy_codec_encoder_finalize_stream_block(encoder);
            }
            return lifted_loop_return_status(encoder, continue_status.return_code);
        }
    }
}

}  // namespace

namespace {

bool fast_parse_trailing_byte_after_find(void *encoder, std::uint8_t *base,
        LegacyCodecEncoderParseStepResult &result) {
    const std::int32_t processed =
            *reinterpret_cast<std::int32_t *>(base + legacy_oem_encoder::kProcessedInBlock);
    if (processed == 0) {
        return false;
    }

    const std::uint32_t sync_cached =
            *reinterpret_cast<std::uint32_t *>(base + legacy_oem_encoder::kParseCachedAvailLength);
    if (sync_cached != legacy_oem_encoder::kParseSyncDeferSkipFlag) {
        return false;
    }

    // OEM FUN_001420cc defer-skip: literal for cursor advanced without find.
    result.status = LegacyCodecEncoderParseStepStatus::kApplyNow;
    result.path_length = 1U;
    result.chosen_tag = kEncoderTagLiteral;
    return true;
}

bool fast_parse_try_rep_choice(void *encoder, std::uint32_t avail, std::uint32_t pre_sync_avail,
        const std::uint32_t rep_lengths[4], LegacyCodecEncoderParseStepResult &result) {
    const std::uint32_t best_rep_index =
            legacy_codec_encoder_pick_best_rep_index(rep_lengths);
    const std::uint32_t best_rep_len = rep_lengths[best_rep_index];
    if (best_rep_len == 0U) {
        return false;
    }
    const std::uint32_t emit_len = fast_maybe_bump_rep_to_remaining(encoder, best_rep_len);
    if (fast_non_uniform_tail_rep_covers_remaining(encoder, best_rep_len) ||
            fast_non_uniform_tail_rep_needs_head_literal(encoder, best_rep_len)) {
        return false;
    }
    if (emit_len < 2U) {
        return false;
    }
    const std::uint32_t remaining = fast_bounded_unencoded_bytes(encoder);
    const std::uint32_t avail_required = emit_len > best_rep_len ? emit_len : best_rep_len;
    if (avail < avail_required && remaining < emit_len) {
        return false;
    }
    if (emit_len == 1U && pre_sync_avail >= 2U) {
        return false;
    }
    if (!fast_open_input_rep_choice_matches(encoder, best_rep_index, emit_len)) {
        return false;
    }

    apply_fast_rep_choice(encoder, best_rep_index, emit_len);
    result.status = LegacyCodecEncoderParseStepStatus::kApplyNow;
    result.path_length = emit_len;
    result.chosen_tag = best_rep_index;
    return true;
}

}  // namespace

bool legacy_codec_encoder_open_input_is_uniform(void *encoder) {
    if (encoder == nullptr) {
        return false;
    }
    struct EncoderInputView {
        unsigned long (*read)(long ctx, void *dst, unsigned long *size);
        const std::uint8_t *data;
        int total;
        int pos;
    };
    auto *base = static_cast<std::uint8_t *>(encoder);
    const auto *input = *reinterpret_cast<const EncoderInputView * const *>(
            base + legacy_oem_encoder::kInputReadCallback);
    if (input == nullptr || input->data == nullptr || input->total < 2) {
        return false;
    }
    const std::uint8_t lead = input->data[0];
    for (int i = 1; i < input->total; ++i) {
        if (input->data[i] != lead) {
            return false;
        }
    }
    return true;
}

int legacy_codec_encoder_open_input_size(void *encoder) {
    if (encoder == nullptr) {
        return 0;
    }
    struct EncoderInputView {
        unsigned long (*read)(long ctx, void *dst, unsigned long *size);
        const std::uint8_t *data;
        int total;
        int pos;
    };
    auto *base = static_cast<std::uint8_t *>(encoder);
    const auto *input = *reinterpret_cast<const EncoderInputView * const *>(
            base + legacy_oem_encoder::kInputReadCallback);
    if (input == nullptr) {
        return 0;
    }
    return input->total;
}

LegacyCodecEncoderParseStepResult legacy_codec_encoder_fast_parse_step(void *encoder,
        std::uint32_t pos_state) {
    LegacyCodecEncoderParseStepResult result{};
    if (encoder == nullptr) {
        result.status = LegacyCodecEncoderParseStepStatus::kError;
        return result;
    }

    (void)pos_state;

    auto *base = static_cast<std::uint8_t *>(encoder);
    std::uint32_t match_length = 0U;
    std::uint32_t distance_index = 0U;

    LegacyCodecMatchFinderAccess pre_sync_match_finder{encoder};
    const std::uint32_t pre_sync_avail =
            static_cast<std::uint32_t>(pre_sync_match_finder.has_data());

    std::uint32_t pre_sync_rep_lengths[4] = {};
    fast_probe_oem_rep_lengths(encoder, pre_sync_avail, pre_sync_rep_lengths);
    if (legacy_codec_encoder_block_bytes(encoder) >= 35U &&
            fast_sparse_pair_tail_rep_allowed(encoder, 2U)) {
        result.status = LegacyCodecEncoderParseStepStatus::kApplyNow;
        result.path_length = 2U;
        result.chosen_tag = 0U;
        return result;
    }

    // Lifted stepped literal at block==1 was forcing lit(b)+match(30); OEM uses one match(31)
    // from block==1 (see probe_fast_loop_repeat_ab nostep vs stepped bisect).
    if (g_lift_fast_full_native && !legacy_codec_encoder_open_input_is_uniform(encoder)) {
        const auto block = legacy_codec_encoder_block_bytes(encoder);
        const std::uint32_t unencoded = fast_open_input_unencoded_bytes(encoder);
        if (block == 1U && fast_full_native_prefers_stepped_literal(encoder, pre_sync_avail)) {
            result.status = LegacyCodecEncoderParseStepStatus::kApplyNow;
            result.path_length = 1U;
            result.chosen_tag = kEncoderTagLiteral;
            return result;
        }
        if (block == 2U && unencoded == 3U && pre_sync_avail >= 1U) {
            result.status = LegacyCodecEncoderParseStepStatus::kApplyNow;
            result.path_length = 1U;
            result.chosen_tag = kEncoderTagLiteral;
            return result;
        }
        if (block == 3U && unencoded == 2U && pre_sync_avail >= 1U) {
            result.status = LegacyCodecEncoderParseStepStatus::kApplyNow;
            result.path_length = 1U;
            result.chosen_tag = kEncoderTagLiteral;
            return result;
        }
        if (block >= 4U && unencoded == 1U &&
                fast_parse_should_fold_stream_tail(encoder) &&
                fast_parse_try_lab_47d10_tail(encoder, result)) {
            return result;
        }
    }

    if (!fast_sync_matchfinder(encoder, match_length, distance_index)) {
        if (fast_try_native_remainder_literal(encoder, result)) {
            return result;
        }
        result.status = LegacyCodecEncoderParseStepStatus::kUnhandledNative;
        return result;
    }

    LegacyCodecMatchFinderAccess avail_match_finder{encoder};
    const std::int32_t avail_signed = avail_match_finder.has_data();
    std::uint32_t avail = avail_signed > 0 ? static_cast<std::uint32_t>(avail_signed) : 0U;
    if (legacy_codec_encoder_fast_open_body_active(encoder)) {
        const std::uint32_t open_remaining = fast_matchfinder_unencoded_bytes(encoder);
        if (open_remaining >= 2U && avail < open_remaining) {
            avail = open_remaining;
        }
    }
    *reinterpret_cast<std::uint32_t *>(base + kMatchfinderAvail) = avail;

    if (fast_full_native_handle_stream_tail(encoder, avail, result)) {
        return result;
    }

    std::uint32_t rep_lengths[4] = {};
    fast_probe_oem_rep_lengths(encoder, avail, rep_lengths);

    const bool rep_stale_after_short_advance =
            avail == 0U && rep_lengths[0] == 0U && rep_lengths[1] == 0U &&
            rep_lengths[2] == 0U && rep_lengths[3] == 0U &&
            (pre_sync_rep_lengths[0] | pre_sync_rep_lengths[1] | pre_sync_rep_lengths[2] |
                    pre_sync_rep_lengths[3]) != 0U;
    const std::uint32_t *rep_probe = rep_stale_after_short_advance ? pre_sync_rep_lengths
                                                                 : rep_lengths;
    const std::uint32_t rep_avail = rep_stale_after_short_advance ? pre_sync_avail : avail;

    const std::uint32_t main_match_len =
            match_length == kParseSyncDeferSkipFlag ? 0U : match_length;
    const bool has_main_find_match = distance_index != 0U && main_match_len >= 2U;

    // Lifted loop: OEM fast opens with a literal before rep/match unless oem-style open parse.
    if (legacy_codec_encoder_block_bytes(encoder) == 0U &&
            !legacy_codec_encoder_lift_fast_full_native() &&
            !fast_skip_forced_literal_at_block_zero(encoder) &&
            !legacy_codec_encoder_fast_open_body_active(encoder)) {
        result.status = LegacyCodecEncoderParseStepStatus::kApplyNow;
        result.path_length = 1U;
        result.chosen_tag = kEncoderTagLiteral;
        return result;
    }

    const bool uniform_open_input = legacy_codec_encoder_open_input_is_uniform(encoder) &&
            legacy_codec_encoder_open_input_size(encoder) > 1;
    if (uniform_open_input) {
        if (g_lift_fast_loop_fail_on_unhandled && !g_lift_fast_full_native &&
                legacy_codec_encoder_block_bytes(encoder) > 0U && uniform_open_input) {
            result.status = LegacyCodecEncoderParseStepStatus::kUnhandledNative;
            return result;
        }
        const int open_size = legacy_codec_encoder_open_input_size(encoder);
        const auto block = static_cast<std::uint32_t>(legacy_codec_encoder_block_bytes(encoder));
        if (open_size > static_cast<int>(block)) {
            std::uint32_t tail =
                    static_cast<std::uint32_t>(open_size) - block;
            // OEM repeat_a: one literal then a single rep0 covering the uniform tail.
            if (tail >= 2U) {
                *reinterpret_cast<std::uint32_t *>(base + kFastFinishDcAdjust) = 1U;
                if (fast_emit_rep_choice(encoder, 0U, tail, result)) {
                    return result;
                }
            }
        }
        if (fast_parse_try_lab_47dd0_rep0(encoder, pre_sync_avail, main_match_len, result)) {
            return result;
        }
        if (fast_parse_try_rep_choice(encoder, pre_sync_avail, pre_sync_avail,
                pre_sync_rep_lengths, result)) {
            return result;
        }
    }

    if (!has_main_find_match &&
            fast_parse_try_rep_choice(encoder, rep_avail, pre_sync_avail, rep_probe, result)) {
        return result;
    }

    if (avail == 0U) {
        if (fast_parse_trailing_byte_after_find(encoder, base, result)) {
            return result;
        }
        if (fast_try_native_remainder_literal(encoder, result)) {
            return result;
        }
        const std::uint32_t unencoded = fast_open_input_unencoded_bytes(encoder);
        if (fast_full_native_step_literal_before_47d10(encoder, unencoded)) {
            result.status = LegacyCodecEncoderParseStepStatus::kApplyNow;
            result.path_length = 1U;
            result.chosen_tag = kEncoderTagLiteral;
            return result;
        }
        if (fast_parse_try_lab_47d10_tail(encoder, result)) {
            return result;
        }
        // OEM LAB_00147d10: tail bytes are folded into finish, not a stepped literal.
        if (unencoded > 0U) {
            result.status = LegacyCodecEncoderParseStepStatus::kUnhandledNative;
            result.native_tail_remainder = true;
            return result;
        }
        result.status = LegacyCodecEncoderParseStepStatus::kUnhandledNative;
        return result;
    }
    if (avail < 2U) {
        const std::uint32_t unencoded_tail = fast_open_input_unencoded_bytes(encoder);
        if (fast_full_native_step_literal_before_47d10(encoder, unencoded_tail)) {
            result.status = LegacyCodecEncoderParseStepStatus::kApplyNow;
            result.path_length = 1U;
            result.chosen_tag = kEncoderTagLiteral;
            return result;
        }
        if (fast_try_native_remainder_literal(encoder, result)) {
            return result;
        }
        if (!fast_parse_should_fold_stream_tail(encoder) &&
                unencoded_tail > 2U) {
            result.status = LegacyCodecEncoderParseStepStatus::kApplyNow;
            result.path_length = 1U;
            result.chosen_tag = kEncoderTagLiteral;
            return result;
        }
        if (fast_parse_try_lab_47d10_tail(encoder, result)) {
            return result;
        }
        if (fast_parse_should_fold_stream_tail(encoder)) {
            result.status = LegacyCodecEncoderParseStepStatus::kUnhandledNative;
            result.native_tail_remainder = true;
            return result;
        }
        result.status = LegacyCodecEncoderParseStepStatus::kApplyNow;
        result.path_length = 1U;
        result.chosen_tag = kEncoderTagLiteral;
        return result;
    }

    if (avail == 2U && fast_parse_should_fold_stream_tail(encoder)) {
        const std::uint32_t remaining = fast_matchfinder_unencoded_bytes(encoder);
        if (remaining == 2U && fast_parse_try_lab_47d10_tail(encoder, result)) {
            return result;
        }
    }

    if (legacy_codec_encoder_block_bytes(encoder) == 0U) {
        const std::uint32_t remaining = fast_matchfinder_unencoded_bytes(encoder);
        if (remaining <= 1U) {
            result.status = LegacyCodecEncoderParseStepStatus::kApplyNow;
            result.path_length = 1U;
            result.chosen_tag = kEncoderTagLiteral;
            return result;
        }
        if (!legacy_codec_encoder_fast_open_body_active(encoder) &&
                !fast_skip_forced_literal_at_block_zero(encoder)) {
            // Lifted loop: OEM fast always opens with a literal (rep probe needs block_bytes > 0).
            result.status = LegacyCodecEncoderParseStepStatus::kApplyNow;
            result.path_length = 1U;
            result.chosen_tag = kEncoderTagLiteral;
            return result;
        }
    }

    if (fast_parse_try_lab_47dd0_rep0(encoder, pre_sync_avail, main_match_len, result)) {
        return result;
    }

    std::uint32_t lab47b44_rep_index = 0U;
    std::uint32_t lab47b44_rep_len = 0U;
    fast_parse_try_lab_47b44(encoder, avail, main_match_len, lab47b44_rep_index,
            lab47b44_rep_len);
    if (!has_main_find_match && lab47b44_rep_len >= main_match_len && lab47b44_rep_len >= 2U) {
        if (!fast_non_uniform_tail_rep_covers_remaining(encoder, lab47b44_rep_len) &&
                !fast_non_uniform_tail_rep_needs_head_literal(encoder, lab47b44_rep_len)) {
            if (fast_emit_rep_choice(encoder, lab47b44_rep_index, lab47b44_rep_len, result)) {
                return result;
            }
        }
    }

    std::uint32_t match_distance = 0U;
    const std::uint32_t effective_match_length = normalize_cached_match_distance(base,
            distance_index, match_length, match_distance);

    const bool oem_open_body = legacy_codec_encoder_fast_open_body_active(encoder);
    std::uint32_t remaining = fast_bounded_unencoded_bytes(encoder);
    if (oem_open_body) {
        const int open_size = legacy_codec_encoder_open_input_size(encoder);
        const std::uint32_t ldc = legacy_codec_encoder_fast_local_dc(encoder);
        if (open_size > static_cast<int>(ldc)) {
            remaining = static_cast<std::uint32_t>(open_size) - ldc;
        }
    }
    if (has_main_find_match && lab47b44_rep_index == 0U && lab47b44_rep_len >= 2U &&
            g_lift_fast_full_native && !legacy_codec_encoder_open_input_is_uniform(encoder) &&
            remaining >= 3U && lab47b44_rep_len + 1U == remaining) {
        const std::uint32_t tail_rep_len = fast_maybe_bump_rep_to_remaining(encoder,
                lab47b44_rep_len);
        if (tail_rep_len == remaining &&
                fast_emit_rep_choice(encoder, lab47b44_rep_index, tail_rep_len, result)) {
            return result;
        }
    }
    std::uint32_t encode_match_len =
            effective_match_length >= main_match_len ? effective_match_length : main_match_len;
    // OEM block==0 open body: match length comes from find/sync, not open_size-ldc override.
    if (!oem_open_body && remaining > 0U && encode_match_len + 1U >= remaining &&
            remaining > encode_match_len) {
        encode_match_len = remaining;
        *reinterpret_cast<std::uint32_t *>(base + kFastFinishDcAdjust) = 1U;
    }
    if (oem_open_body && has_main_find_match) {
        const int open_size = legacy_codec_encoder_open_input_size(encoder);
        const std::uint32_t ldc = legacy_codec_encoder_fast_local_dc(encoder);
        if (open_size > static_cast<int>(ldc)) {
            const std::uint32_t oem_open_match =
                    static_cast<std::uint32_t>(open_size) - ldc;
            if (oem_open_match >= 2U) {
                encode_match_len = oem_open_match;
                *reinterpret_cast<std::uint32_t *>(base + kFastFinishDcAdjust) = 1U;
            }
        }
    }

    if (legacy_codec_encoder_fast_open_body_active(encoder) &&
            encode_match_len >= 2U && distance_index != 0U) {
        std::uint32_t chain_tag = 0U;
        std::uint32_t chain_len = 0U;
        if (fast_try_lab_48214_decision(encoder, avail, encode_match_len, match_distance,
                    distance_index, chain_tag, chain_len, true)) {
            result.status = LegacyCodecEncoderParseStepStatus::kApplyNow;
            result.path_length = chain_len;
            result.chosen_tag = chain_tag;
            return result;
        }
    }

    const bool skip_rep_chain_primary =
            has_main_find_match ||
            (encode_match_len >= lab47b44_rep_len && lab47b44_rep_len > 0U);
    const bool skip_rep_chain_fallback = has_main_find_match;

    const std::uint32_t match_avail_gate =
            legacy_codec_encoder_fast_open_body_active(encoder) ? 2U : 3U;
    if (encode_match_len >= 2U && avail >= match_avail_gate && distance_index != 0U &&
            (has_main_find_match ||
                    (encode_match_len >= lab47b44_rep_len && lab47b44_rep_len > 0U))) {
        std::uint32_t chain_tag = 0U;
        std::uint32_t chain_len = 0U;
        if (fast_try_lab_48214_decision(encoder, avail, encode_match_len, match_distance,
                    distance_index, chain_tag, chain_len, skip_rep_chain_primary)) {
            result.status = LegacyCodecEncoderParseStepStatus::kApplyNow;
            result.path_length = chain_len;
            result.chosen_tag = chain_tag;
            return result;
        }
    }

    if (!has_main_find_match && lab47b44_rep_len > 1U &&
            encode_match_len < lab47b44_rep_len + 2U) {
        std::uint32_t heuristic_tag = 0U;
        std::uint32_t heuristic_len = 0U;
        if (fast_pick_match_heuristic(encode_match_len, match_distance, lab47b44_rep_len,
                    lab47b44_rep_index, heuristic_tag, heuristic_len) &&
                fast_open_input_rep_choice_matches(encoder, heuristic_tag, heuristic_len)) {
            apply_fast_rep_choice(encoder, heuristic_tag, heuristic_len);
            result.status = LegacyCodecEncoderParseStepStatus::kApplyNow;
            result.path_length = heuristic_len;
            result.chosen_tag = heuristic_tag;
            return result;
        }
    }

    if (encode_match_len >= 2U && avail >= 3U && distance_index != 0U) {
        std::uint32_t chain_tag = 0U;
        std::uint32_t chain_len = 0U;
        if (fast_try_lab_48214_decision(encoder, avail, encode_match_len, match_distance,
                    distance_index, chain_tag, chain_len, skip_rep_chain_fallback)) {
            result.status = LegacyCodecEncoderParseStepStatus::kApplyNow;
            result.path_length = chain_len;
            result.chosen_tag = chain_tag;
            return result;
        }
    }

    if (fast_parse_should_fold_stream_tail(encoder)) {
        const std::uint32_t tail_remaining = fast_matchfinder_unencoded_bytes(encoder);
        if (tail_remaining <= 2U &&
                fast_parse_try_lab_47d10_tail(encoder, result)) {
            return result;
        }
    }

    if (fast_try_input_verified_history_match(encoder, result)) {
        return result;
    }

    result.status = LegacyCodecEncoderParseStepStatus::kApplyNow;
    result.path_length = 1U;
    result.chosen_tag = kEncoderTagLiteral;
    return result;
}

bool legacy_codec_encoder_try_encode_pending_eof_literal(void *encoder) {
    if (encoder == nullptr) {
        return false;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    if (*reinterpret_cast<std::int32_t *>(base + kProcessedInBlock) != 0) {
        return false;
    }

    LegacyCodecMatchFinderAccess match_finder{encoder};
    if (match_finder.has_data() > 0) {
        return false;
    }

    void *const ctx = match_finder.context();
    if (ctx == nullptr) {
        return false;
    }

    auto *mf_bytes = static_cast<std::uint8_t *>(ctx);
    if (*reinterpret_cast<std::int32_t *>(mf_bytes + kStreamEnd) == 0) {
        return false;
    }

    const auto *words = reinterpret_cast<const std::uint64_t *>(ctx);
    const std::uint32_t history_size =
            *reinterpret_cast<const std::uint32_t *>(mf_bytes + kHistorySize);
    const std::uint32_t write_bytes =
            static_cast<std::uint32_t>(words[2]) - history_size;
    if (write_bytes == 0U) {
        return false;
    }

    const std::uint64_t block_bytes = legacy_codec_encoder_block_bytes(encoder);
    if (static_cast<std::uint64_t>(write_bytes) <= block_bytes) {
        return false;
    }

    if (legacy_codec_encoder_lift_fast_full_native()) {
        const int open_size = legacy_codec_encoder_open_input_size(encoder);
        if (open_size > 0 &&
                block_bytes >= static_cast<std::uint64_t>(open_size)) {
            return false;
        }
    }

    const bool fast_literal_mode =
            *reinterpret_cast<const std::int32_t *>(base + kLiteralMode) != 0;
    auto *finish_dc_adjust =
            reinterpret_cast<std::uint32_t *>(base + kFastFinishDcAdjust);
    const bool eof_ptr_at_get =
            fast_literal_mode && *finish_dc_adjust == 2U;
    if (eof_ptr_at_get) {
        *finish_dc_adjust = 0U;
    }

    const std::uint8_t *literal_ptr = nullptr;
    if (eof_ptr_at_get) {
        const std::uint8_t *get_ptr =
                static_cast<const std::uint8_t *>(match_finder.buffer());
        const std::uint8_t *cur = match_finder.current_literal_ptr();
        literal_ptr = (match_finder.has_data() > 0 && get_ptr != nullptr) ? get_ptr : cur;
    } else {
        literal_ptr = match_finder.current_literal_ptr();
    }
    if (literal_ptr == nullptr && !eof_ptr_at_get) {
        return false;
    }
    if (eof_ptr_at_get && literal_ptr == nullptr) {
        return false;
    }

    auto *processed = reinterpret_cast<std::int32_t *>(base + kProcessedInBlock);
    // OEM pending EOF: processed=1 → encode at get_ptr-1. After 47d10 stream-tail presync,
    // get_ptr already targets the final byte; processed must stay 0 (native_apply_literal_ptr).
    *processed = eof_ptr_at_get ? 0 : 1;
    if (!legacy_codec_encoder_commit_symbol(encoder, kEncoderTagLiteral, 1U, literal_ptr)) {
        *processed = 0;
        return false;
    }
    *processed = 0;

    match_finder.skip(1);
    return true;
}

bool legacy_codec_encoder_fast_native_47d10_apply_tail(void *encoder, std::uint32_t tail_node) {
    return fast_native_47d10_apply_tail(encoder, tail_node);
}

int legacy_codec_encoder_try_lifted_core_block(void *encoder, int mode, unsigned long input_limit,
        unsigned int output_limit) {
    if (encoder == nullptr) {
        return 2;
    }

    if (!legacy_codec_encoder_uses_lifted_matchfinder(encoder)) {
        return 2;
    }

    const bool fast_literal_mode =
            *reinterpret_cast<std::int32_t *>(static_cast<std::uint8_t *>(encoder) + kLiteralMode) !=
            0;
    return run_lifted_encode_loop(encoder, mode, input_limit, output_limit, fast_literal_mode);
}

void legacy_codec_encoder_fast_probe_lab_47b44_rep_lengths(void *encoder, std::uint32_t avail,
        std::uint32_t rep_lengths[4]) {
    fast_probe_lab_47b44_rep_lengths(encoder, avail, rep_lengths);
}

void legacy_codec_encoder_set_lift_fast_loop_fail_on_unhandled(bool disabled) {
    g_lift_fast_loop_fail_on_unhandled = disabled;
}

bool legacy_codec_encoder_lift_fast_loop_fail_on_unhandled() {
    return g_lift_fast_loop_fail_on_unhandled;
}

void legacy_codec_encoder_set_lift_fast_stream_native_from_zero(bool enabled) {
    g_lift_fast_stream_native_from_zero = enabled;
}

bool legacy_codec_encoder_lift_fast_stream_native_from_zero() {
    return g_lift_fast_stream_native_from_zero;
}

void legacy_codec_encoder_set_lift_fast_full_native(bool enabled) {
    g_lift_fast_full_native = enabled;
}

bool legacy_codec_encoder_lift_fast_full_native() {
    return g_lift_fast_full_native;
}

void legacy_codec_encoder_set_fast_native_open_parse(bool enabled) {
    g_fast_native_open_parse = enabled;
}

bool legacy_codec_encoder_fast_native_open_parse() {
    return g_fast_native_open_parse;
}

void legacy_codec_encoder_set_lift_fast_try_native_non_uniform_open(bool enabled) {
    g_lift_fast_try_native_non_uniform_open = enabled;
}

bool legacy_codec_encoder_lift_fast_try_native_non_uniform_open() {
    return g_lift_fast_try_native_non_uniform_open;
}

bool legacy_codec_encoder_fast_parse_should_fold_stream_tail(void *encoder) {
    return fast_parse_should_fold_stream_tail(encoder);
}

std::uint64_t legacy_codec_encoder_fast_loop_block_snapshot() {
    return g_fast_loop_block_snapshot;
}

}  // namespace kksdk
