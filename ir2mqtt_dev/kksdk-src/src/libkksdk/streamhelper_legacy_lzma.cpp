#include "streamhelper_legacy_lzma.hpp"

#include "legacy_codec_encoder_blocks.hpp"
#include "legacy_codec_encoder_context.hpp"
#include "legacy_codec_encoder_core.hpp"
#include "legacy_codec_encoder_lifecycle.hpp"
#include "legacy_codec_encoder_parse.hpp"
#include "legacy_codec_encoder_stream_init.hpp"
#include "legacy_codec_range_encoder.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

#ifdef __ANDROID__
#include <android/log.h>
#define KKSDK_LOGE(tag, ...) __android_log_print(ANDROID_LOG_ERROR, tag, __VA_ARGS__)
#else
#define KKSDK_LOGE(tag, ...) ((void)0)
#endif

#ifndef KKSDK_ENABLE_OEM_LZMA_REFERENCE
#define KKSDK_ENABLE_OEM_LZMA_REFERENCE 0
#endif

#if KKSDK_ENABLE_OEM_LZMA_REFERENCE
#include "oem_lzma_codec_bridge.hpp"
#endif

namespace kksdk {
namespace {

constexpr const char *kTag = "kksdk_lzma_enc";
using CallbackCtx = std::intptr_t;

// FUN_0014e698 input descriptor (passed as callback context).
struct LzmaInputDesc {
    unsigned long (*read)(CallbackCtx ctx, void *dst, unsigned long *size);
    const std::uint8_t *data;
    int total;
    int pos;
};

// FUN_0014e714 output descriptor.
struct LzmaOutputDesc {
    unsigned long (*write)(CallbackCtx ctx, void *src, unsigned long size);
    std::uint8_t *data;
    int capacity;
    int size;
};

extern "C" unsigned long lzma_input_read(CallbackCtx ctx, void *dst, unsigned long *size) {
    auto *input = reinterpret_cast<LzmaInputDesc *>(ctx);
    if (input == nullptr || size == nullptr) {
        return 0;
    }

    if (input->pos < input->total) {
        const unsigned long available =
                static_cast<unsigned long>(input->total - input->pos);
        unsigned long to_copy = *size;
        if (available < to_copy) {
            to_copy = available;
            *size = to_copy;
        }
        std::memcpy(dst, input->data + input->pos, to_copy);
        input->pos += static_cast<int>(to_copy);
    } else {
        *size = 0;
    }
    return 0;
}

extern "C" unsigned long lzma_output_write(CallbackCtx ctx, void *src, unsigned long size) {
    auto *output = reinterpret_cast<LzmaOutputDesc *>(ctx);
    if (output == nullptr || src == nullptr || size == 0) {
        return 0;
    }

    const std::size_t used = static_cast<std::size_t>(output->size);
    if (used + size > static_cast<unsigned long>(output->capacity)) {
        unsigned long growth = size;
        if (growth < 0x1000U) {
            growth = 0x1000U;
        }
        const std::size_t new_capacity = used + growth;
        std::uint8_t *grown = static_cast<std::uint8_t *>(std::malloc(new_capacity));
        if (grown == nullptr) {
            return 0;
        }
        if (output->data != nullptr) {
            std::memcpy(grown, output->data, used);
            std::free(output->data);
        }
        output->data = grown;
        output->capacity = static_cast<int>(new_capacity);
    }

    std::memcpy(output->data + used, src, size);
    output->size += static_cast<int>(size);
    return size;
}

bool build_prefix_header(void *encoder, int input_length, std::uint8_t *out,
        std::size_t &out_size) {
    unsigned long header_props_size = 5;
    const int header_status = legacy_codec_encoder_write_header(
            encoder, reinterpret_cast<char *>(out + 1), &header_props_size);
    if (header_status != 0) {
        return false;
    }

    out[0] = static_cast<std::uint8_t>(static_cast<unsigned int>(input_length) >> 1U);
    std::memcpy(out + 6, &input_length, sizeof(int));
    out_size = header_props_size + 5U;
    return out_size == 10U;
}

bool grow_output_prefix(LzmaOutputDesc &output, const std::uint8_t *prefix,
        std::size_t prefix_size) {
    if (prefix_size > 0x1000U) {
        const int needed = static_cast<int>(prefix_size) + 0x1000;
        output.data = static_cast<std::uint8_t *>(std::malloc(static_cast<std::size_t>(needed)));
        if (output.data == nullptr) {
            return false;
        }
        output.capacity = needed;
    } else if (output.data == nullptr) {
        output.data = static_cast<std::uint8_t *>(std::malloc(0x1000U));
        if (output.data == nullptr) {
            return false;
        }
        output.capacity = 0x1000;
    }

    std::memcpy(output.data, prefix, prefix_size);
    output.size = static_cast<int>(prefix_size);
    return true;
}

bool run_lifted_native_until_finish(void *encoder, LzmaInputDesc &input_desc,
        LzmaOutputDesc &output_desc, void **allocator) {
    if (encoder == nullptr || allocator == nullptr) {
        return false;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    *reinterpret_cast<void **>(base + legacy_oem_encoder::kInputReadCallback) = &input_desc;
    *reinterpret_cast<void **>(base + legacy_oem_encoder::kOutputWriteCallback) = &output_desc;
    *reinterpret_cast<std::uint32_t *>(base + legacy_oem_encoder::kStreamInitFlag) = 1U;
    if (legacy_codec_encoder_stream_init(encoder, 0U, allocator, allocator) != 0U) {
        return false;
    }
    (void)legacy_codec_encoder_run_matchfinder_init_if_needed(encoder);

    const std::size_t input_total = input_desc.total > 0
            ? static_cast<std::size_t>(input_desc.total)
            : 0U;
    const std::size_t core_step_limit = std::max<std::size_t>(1024U, input_total * 16U + 1024U);
    for (std::size_t step = 0; step < core_step_limit; ++step) {
        if (legacy_codec_encoder_finish_flag(encoder) != 0U) {
            break;
        }
        const int status = legacy_codec_encoder_core_step(encoder, 0, 0U, 0U);
        if (status != 0 && status != 1) {
            return false;
        }
        if (legacy_codec_encoder_finish_flag(encoder) != 0U) {
            break;
        }
    }
    if (legacy_codec_encoder_finish_flag(encoder) == 0U) {
        return false;
    }

    legacy_codec_range_encoder_flush_buffer(*legacy_codec_range_encoder_from_native_state(encoder));
    return output_desc.size > 0;
}

bool input_has_repeated_pair(const std::uint8_t *input, std::size_t input_size) {
    if (input == nullptr || input_size < 3U) {
        return false;
    }

    bool seen_pair[0x10000U] = {};
    const auto pair_at = [](const std::uint8_t *bytes, std::size_t index) -> std::uint32_t {
        return (static_cast<std::uint32_t>(bytes[index]) << 8U) |
                static_cast<std::uint32_t>(bytes[index + 1U]);
    };

    seen_pair[pair_at(input, 0U)] = true;
    for (std::size_t pos = 1U; pos + 1U < input_size; ++pos) {
        const std::uint32_t pair = pair_at(input, pos);
        if (seen_pair[pair]) {
            return true;
        }
        seen_pair[pair] = true;
    }
    return false;
}

bool input_has_adjacent_repeat(const std::uint8_t *input, std::size_t input_size) {
    if (input == nullptr || input_size < 2U) {
        return false;
    }
    for (std::size_t pos = 1U; pos < input_size; ++pos) {
        if (input[pos] == input[pos - 1U]) {
            return true;
        }
    }
    return false;
}

std::size_t max_same_byte_run(const std::uint8_t *input, std::size_t input_size) {
    if (input == nullptr || input_size == 0U) {
        return 0U;
    }

    std::size_t best = 1U;
    std::size_t run = 1U;
    for (std::size_t pos = 1U; pos < input_size; ++pos) {
        if (input[pos] == input[pos - 1U]) {
            run += 1U;
            best = std::max(best, run);
        } else {
            run = 1U;
        }
    }
    return best;
}

bool repeated_pairs_are_only_short_same_byte_runs(const std::uint8_t *input,
        std::size_t input_size) {
    if (input == nullptr || input_size < 3U) {
        return true;
    }

    bool seen_pair[0x10000U] = {};
    const auto pair_at = [](const std::uint8_t *bytes, std::size_t index) -> std::uint32_t {
        return (static_cast<std::uint32_t>(bytes[index]) << 8U) |
                static_cast<std::uint32_t>(bytes[index + 1U]);
    };

    seen_pair[pair_at(input, 0U)] = true;
    for (std::size_t pos = 1U; pos + 1U < input_size; ++pos) {
        const std::uint32_t pair = pair_at(input, pos);
        if (seen_pair[pair] && input[pos] != input[pos + 1U]) {
            return false;
        }
        seen_pair[pair] = true;
    }
    return true;
}

bool run_lifted_default_sequential_shortrep_until_finish(void *encoder,
        LzmaInputDesc &input_desc, LzmaOutputDesc &output_desc, void **allocator) {
    if (encoder == nullptr || allocator == nullptr || input_desc.data == nullptr ||
            input_desc.total <= 0) {
        return false;
    }

    auto *base = static_cast<std::uint8_t *>(encoder);
    *reinterpret_cast<void **>(base + legacy_oem_encoder::kInputReadCallback) = &input_desc;
    *reinterpret_cast<void **>(base + legacy_oem_encoder::kOutputWriteCallback) = &output_desc;
    *reinterpret_cast<std::uint32_t *>(base + legacy_oem_encoder::kStreamInitFlag) = 1U;
    if (legacy_codec_encoder_stream_init(encoder, 0U, allocator, allocator) != 0U) {
        return false;
    }
    (void)legacy_codec_encoder_run_matchfinder_init_if_needed(encoder);

    if (!legacy_codec_encoder_encode_stream_entry_literal(encoder)) {
        return false;
    }

    LegacyCodecMatchFinderAccess match_finder{encoder};
    const std::size_t input_size = static_cast<std::size_t>(input_desc.total);
    for (std::size_t pos = 1U; pos < input_size;) {
        const bool short_rep0 = input_desc.data[pos] == input_desc.data[pos - 1U];
        std::uint32_t length = 1U;
        if (short_rep0) {
            while (pos + length < input_size &&
                    input_desc.data[pos + length] == input_desc.data[pos]) {
                length += 1U;
            }
        }
        const std::uint32_t tag = short_rep0 ? 0U : kEncoderTagLiteral;
        auto *processed = reinterpret_cast<std::int32_t *>(base +
                legacy_oem_encoder::kProcessedInBlock);
        *processed += static_cast<std::int32_t>(length);
        if (!legacy_codec_encoder_commit_symbol_ex(encoder, tag, length, input_desc.data + pos,
                    true)) {
            return false;
        }
        match_finder.skip(static_cast<std::int32_t>(length));
        if (legacy_codec_range_encoder_from_native_state(encoder)->status != 0) {
            return false;
        }
        pos += length;
    }

    legacy_codec_encoder_finalize_stream_block(encoder);
    if (legacy_codec_encoder_finish_flag(encoder) == 0U ||
            legacy_codec_range_encoder_from_native_state(encoder)->status != 0) {
        return false;
    }
    legacy_codec_range_encoder_flush_buffer(*legacy_codec_range_encoder_from_native_state(encoder));
    return output_desc.size > 0;
}

bool default_route_can_use_optimal_native(const std::uint8_t *input, std::size_t input_size) {
    if (input == nullptr || input_size < 3U) {
        return true;
    }

    return !input_has_repeated_pair(input, input_size);
}

bool default_route_can_use_sequential_shortrep_native(const std::uint8_t *input,
        std::size_t input_size) {
    return input_has_adjacent_repeat(input, input_size) &&
            max_same_byte_run(input, input_size) <= 3U &&
            repeated_pairs_are_only_short_same_byte_runs(input, input_size);
}

}  // namespace

bool streamhelper_lzma_encode_buffer_impl(const std::uint8_t *input, std::size_t input_size,
        std::vector<std::uint8_t> &output, int props_algo_override) {
    output.clear();
    if (input == nullptr || input_size == 0) {
        return false;
    }
    if (input_size > static_cast<std::size_t>(0x7fffffffU)) {
        return false;
    }

    void **allocator = legacy_codec_native_allocator_table();
    void *encoder = reinterpret_cast<void *>(legacy_codec_encoder_alloc(allocator));
    if (encoder == nullptr) {
        KKSDK_LOGE(kTag, "encoder alloc failed");
        return false;
    }

    bool ok = false;
    int props[12] = {};
    legacy_codec_encoder_default_props(props);
    props[10] = 0x1000;
    if (props_algo_override >= 0) {
        props[5] = props_algo_override;
    }

    const int input_length = static_cast<int>(input_size);
    LzmaOutputDesc output_desc{};
    output_desc.write = lzma_output_write;
    output_desc.data = static_cast<std::uint8_t *>(std::malloc(0x1000U));
    output_desc.capacity = output_desc.data != nullptr ? 0x1000 : 0;
    output_desc.size = 0;

    LzmaInputDesc input_desc{};
    input_desc.read = lzma_input_read;
    input_desc.data = input;
    input_desc.total = input_length;
    input_desc.pos = 0;

    do {
        if (output_desc.data == nullptr) {
            break;
        }
        if (legacy_codec_encoder_apply_props(encoder, props) != 0) {
            break;
        }

        std::uint8_t prefix[10] = {};
        std::size_t prefix_size = 0;
        if (!run_lifted_native_until_finish(encoder, input_desc, output_desc, allocator) ||
                !build_prefix_header(encoder, input_length, prefix, prefix_size)) {
            break;
        }
        const std::size_t body_size = static_cast<std::size_t>(output_desc.size);
        if (body_size == 0U) {
            break;
        }

        output.resize(prefix_size + body_size);
        std::memcpy(output.data(), prefix, prefix_size);
        std::memcpy(output.data() + prefix_size, output_desc.data, body_size);
        ok = true;
    } while (false);

    legacy_codec_encoder_release_blob(reinterpret_cast<std::intptr_t>(encoder), allocator,
            allocator);
    std::free(output_desc.data);
    return ok;
}

bool streamhelper_lzma_encode_buffer_default_sequential_shortrep_impl(
        const std::uint8_t *input, std::size_t input_size, std::vector<std::uint8_t> &output) {
    output.clear();
    if (input == nullptr || input_size == 0 ||
            input_size > static_cast<std::size_t>(0x7fffffffU)) {
        return false;
    }

    void **allocator = legacy_codec_native_allocator_table();
    void *encoder = reinterpret_cast<void *>(legacy_codec_encoder_alloc(allocator));
    if (encoder == nullptr) {
        KKSDK_LOGE(kTag, "encoder alloc failed");
        return false;
    }

    bool ok = false;
    int props[12] = {};
    legacy_codec_encoder_default_props(props);
    props[10] = 0x1000;

    const int input_length = static_cast<int>(input_size);
    LzmaOutputDesc output_desc{};
    output_desc.write = lzma_output_write;
    output_desc.data = static_cast<std::uint8_t *>(std::malloc(0x1000U));
    output_desc.capacity = output_desc.data != nullptr ? 0x1000 : 0;
    output_desc.size = 0;

    LzmaInputDesc input_desc{};
    input_desc.read = lzma_input_read;
    input_desc.data = input;
    input_desc.total = input_length;
    input_desc.pos = 0;

    do {
        if (output_desc.data == nullptr ||
                legacy_codec_encoder_apply_props(encoder, props) != 0) {
            break;
        }

        std::uint8_t prefix[10] = {};
        std::size_t prefix_size = 0;
        if (!run_lifted_default_sequential_shortrep_until_finish(encoder, input_desc,
                    output_desc, allocator) ||
                !build_prefix_header(encoder, input_length, prefix, prefix_size)) {
            break;
        }
        const std::size_t body_size = static_cast<std::size_t>(output_desc.size);
        if (body_size == 0U) {
            break;
        }

        output.resize(prefix_size + body_size);
        std::memcpy(output.data(), prefix, prefix_size);
        std::memcpy(output.data() + prefix_size, output_desc.data, body_size);
        ok = true;
    } while (false);

    legacy_codec_encoder_release_blob(reinterpret_cast<std::intptr_t>(encoder), allocator,
            allocator);
    std::free(output_desc.data);
    return ok;
}

#if KKSDK_ENABLE_OEM_LZMA_REFERENCE
bool streamhelper_lzma_encode_buffer_oem_impl(const std::uint8_t *input, std::size_t input_size,
        std::vector<std::uint8_t> &output, int props_algo_override) {
    output.clear();
    if (input == nullptr || input_size == 0 ||
            input_size > static_cast<std::size_t>(0x7fffffffU) ||
            !oem_lzma::ensure_loaded()) {
        return false;
    }

    void **allocator = static_cast<void **>(oem_lzma::allocator_table());
    const oem_lzma::EncoderHandle encoder = oem_lzma::encoder_alloc()(allocator);
    if (encoder == 0) {
        return false;
    }

    bool ok = false;
    int props[12] = {};
    oem_lzma::encoder_default_props()(props);
    props[10] = 0x1000;
    if (props_algo_override >= 0) {
        props[5] = props_algo_override;
    }

    const int input_length = static_cast<int>(input_size);
    LzmaOutputDesc output_desc{};
    output_desc.write = lzma_output_write;
    output_desc.data = static_cast<std::uint8_t *>(std::malloc(0x1000U));
    output_desc.capacity = output_desc.data != nullptr ? 0x1000 : 0;
    output_desc.size = 0;

    LzmaInputDesc input_desc{};
    input_desc.read = lzma_input_read;
    input_desc.data = input;
    input_desc.total = input_length;
    input_desc.pos = 0;

    do {
        if (output_desc.data == nullptr ||
                oem_lzma::encoder_apply_props()(encoder, props) != 0) {
            break;
        }
        std::uint8_t prefix[10] = {};
        std::size_t prefix_size = 0;
        if (!build_prefix_header(reinterpret_cast<void *>(encoder), input_length, prefix,
                    prefix_size) ||
                !grow_output_prefix(output_desc, prefix, prefix_size)) {
            break;
        }
        const unsigned long encode_status = oem_lzma::encoder_run_oem()(
                encoder, &output_desc, &input_desc, nullptr, allocator, allocator);
        if (encode_status != 0 || output_desc.size <= 0) {
            break;
        }
        output.assign(output_desc.data,
                output_desc.data + static_cast<std::size_t>(output_desc.size));
        ok = true;
    } while (false);

    oem_lzma::encoder_release()(encoder, allocator, allocator);
    std::free(output_desc.data);
    return ok;
}
#endif

bool streamhelper_lzma_encode_buffer(const std::uint8_t *input, std::size_t input_size,
        std::vector<std::uint8_t> &output) {
    if (default_route_can_use_sequential_shortrep_native(input, input_size) &&
            streamhelper_lzma_encode_buffer_default_sequential_shortrep_impl(input, input_size,
                    output)) {
        return true;
    }
    if (default_route_can_use_optimal_native(input, input_size) &&
            streamhelper_lzma_encode_buffer_impl(input, input_size, output, -1)) {
        return true;
    }
    return streamhelper_lzma_encode_buffer_fast_lifted_loop_native(input, input_size, output);
}

#if KKSDK_ENABLE_OEM_LZMA_REFERENCE
bool streamhelper_lzma_encode_buffer_oem(const std::uint8_t *input, std::size_t input_size,
        std::vector<std::uint8_t> &output) {
    return streamhelper_lzma_encode_buffer_oem_impl(input, input_size, output, -1);
}
#endif

bool streamhelper_lzma_encode_buffer_fast(const std::uint8_t *input, std::size_t input_size,
        std::vector<std::uint8_t> &output) {
    return streamhelper_lzma_encode_buffer_impl(input, input_size, output, 0);
}

bool streamhelper_lzma_encode_buffer_fast_lifted_init(const std::uint8_t *input,
        std::size_t input_size, std::vector<std::uint8_t> &output) {
    return streamhelper_lzma_encode_buffer_impl(input, input_size, output, 0);
}

bool streamhelper_lzma_encode_buffer_fast_lifted_loop(const std::uint8_t *input,
        std::size_t input_size, std::vector<std::uint8_t> &output) {
    const bool prior_fail_on_unhandled = legacy_codec_encoder_lift_fast_loop_fail_on_unhandled();
    const bool prior_stream_native =
            legacy_codec_encoder_lift_fast_stream_native_from_zero();
    const bool prior_full_native = legacy_codec_encoder_lift_fast_full_native();
    const bool prior_try_native = legacy_codec_encoder_lift_fast_try_native_non_uniform_open();
    const bool prior_oem_style = legacy_codec_encoder_fast_native_open_parse();

    const bool use_full_native = input_size > 1U;
    const bool try_native_non_uniform = true;

    legacy_codec_encoder_set_lift_fast_loop_fail_on_unhandled(true);
    legacy_codec_encoder_set_lift_fast_stream_native_from_zero(use_full_native);
    legacy_codec_encoder_set_lift_fast_full_native(use_full_native);
    legacy_codec_encoder_set_lift_fast_try_native_non_uniform_open(try_native_non_uniform);
    legacy_codec_encoder_set_fast_native_open_parse(try_native_non_uniform);
    const bool ok = streamhelper_lzma_encode_buffer_impl(input, input_size, output, 0);
    legacy_codec_encoder_set_fast_native_open_parse(prior_oem_style);
    legacy_codec_encoder_set_lift_fast_try_native_non_uniform_open(prior_try_native);
    legacy_codec_encoder_set_lift_fast_full_native(prior_full_native);
    legacy_codec_encoder_set_lift_fast_stream_native_from_zero(prior_stream_native);
    legacy_codec_encoder_set_lift_fast_loop_fail_on_unhandled(prior_fail_on_unhandled);
    return ok;
}

bool streamhelper_lzma_encode_buffer_fast_lifted_loop_native(const std::uint8_t *input,
        std::size_t input_size, std::vector<std::uint8_t> &output) {
    const bool prior_fail_on_unhandled = legacy_codec_encoder_lift_fast_loop_fail_on_unhandled();
    const bool prior_stream_native =
            legacy_codec_encoder_lift_fast_stream_native_from_zero();
    const bool prior_full_native = legacy_codec_encoder_lift_fast_full_native();
    const bool prior_try_native = legacy_codec_encoder_lift_fast_try_native_non_uniform_open();
    const bool prior_oem_style = legacy_codec_encoder_fast_native_open_parse();
    legacy_codec_encoder_set_lift_fast_loop_fail_on_unhandled(true);
    legacy_codec_encoder_set_lift_fast_stream_native_from_zero(true);
    legacy_codec_encoder_set_lift_fast_full_native(input_size > 1U);
    legacy_codec_encoder_set_lift_fast_try_native_non_uniform_open(true);
    legacy_codec_encoder_set_fast_native_open_parse(true);
    const bool ok = streamhelper_lzma_encode_buffer_impl(input, input_size, output, 0);
    legacy_codec_encoder_set_fast_native_open_parse(prior_oem_style);
    legacy_codec_encoder_set_lift_fast_try_native_non_uniform_open(prior_try_native);
    legacy_codec_encoder_set_lift_fast_full_native(prior_full_native);
    legacy_codec_encoder_set_lift_fast_stream_native_from_zero(prior_stream_native);
    legacy_codec_encoder_set_lift_fast_loop_fail_on_unhandled(prior_fail_on_unhandled);
    return ok;
}

#if KKSDK_ENABLE_OEM_LZMA_REFERENCE
bool streamhelper_lzma_encode_buffer_oem_fast(const std::uint8_t *input, std::size_t input_size,
        std::vector<std::uint8_t> &output) {
    return streamhelper_lzma_encode_buffer_oem_impl(input, input_size, output, 0);
}
#endif

}  // namespace kksdk
