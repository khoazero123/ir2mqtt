#include "legacy_codec_state.hpp"

#include <algorithm>
#include <cstring>
#include <new>

namespace kksdk {

constexpr unsigned long long kLegacyCodecHeaderEncodedSize = 5ULL;

int legacy_codec_status_value(LegacyCodecStatus status) {
    return static_cast<int>(status);
}

int legacy_codec_decode_status_value(LegacyCodecDecodeStatus status) {
    return static_cast<int>(status);
}

void legacy_codec_init_state(LegacyCodecState &state) {
    state.options = legacy_codec_default_options();
    legacy_codec_resolve_options(state.options, state.resolved_options);
    state.len_slot_table = legacy_codec_make_len_slot_table();
    state.price_table = legacy_codec_make_price_table();
    state.workspace.probability_models = nullptr;
    state.workspace.probability_models_mirror = nullptr;
    state.workspace.output_buffer = nullptr;
    state.workspace.range_state = nullptr;
    state.stream_context = nullptr;
    state.progress_context = nullptr;
}

LegacyCodecState *legacy_codec_allocate_state(void *allocator, LegacyCodecAllocFn alloc_fn) {
    if (alloc_fn == nullptr) {
        return nullptr;
    }

    void *memory = alloc_fn(allocator, sizeof(LegacyCodecState));
    if (memory == nullptr) {
        return nullptr;
    }

    auto *state = new (memory) LegacyCodecState();
    legacy_codec_init_state(*state);
    return state;
}

static void copy_region_to_snapshot(const LegacyCodecSnapshotRegion &region) {
    if (region.active != nullptr && region.snapshot != nullptr && region.size != 0) {
        std::memcpy(region.snapshot, region.active, region.size);
    }
}

static void copy_region_to_active(const LegacyCodecSnapshotRegion &region) {
    if (region.active != nullptr && region.snapshot != nullptr && region.size != 0) {
        std::memcpy(region.active, region.snapshot, region.size);
    }
}

void legacy_codec_save_snapshot(const LegacyCodecSnapshotView &view) {
    copy_region_to_snapshot(view.literal_low);
    copy_region_to_snapshot(view.literal_high);
    copy_region_to_snapshot(view.probability_models);
    if (view.active_processed_flag != nullptr && view.snapshot_processed_flag != nullptr) {
        *view.snapshot_processed_flag = *view.active_processed_flag;
    }
}

void legacy_codec_restore_snapshot(const LegacyCodecSnapshotView &view) {
    copy_region_to_active(view.literal_low);
    copy_region_to_active(view.literal_high);
    copy_region_to_active(view.probability_models);
    if (view.active_processed_flag != nullptr && view.snapshot_processed_flag != nullptr) {
        *view.active_processed_flag = *view.snapshot_processed_flag;
    }
}

static std::uint32_t mask_for_bits(int bits) {
    if (bits <= 0) {
        return 0;
    }
    if (bits >= 32) {
        return 0xffffffffU;
    }
    return (1U << static_cast<unsigned int>(bits)) - 1U;
}

static std::uint32_t legacy_codec_mask16(std::uint32_t value) {
    return value & 0xffffU;
}

static std::uint32_t legacy_codec_match_finder_hash2_mask(std::uint32_t value) {
    return value & 0x3ffU;
}

static std::uint32_t legacy_codec_shift_count5(std::uint32_t value) {
    return value & 0x1fU;
}

static std::uint32_t legacy_codec_match_finder_default_hash_mask() {
    return 0xffffU;
}

static std::uint32_t legacy_codec_match_finder_default_hash_size() {
    return legacy_codec_match_finder_default_hash_mask() + 1U;
}

static std::uint32_t legacy_codec_match_finder_hash_mask_cap() {
    return 0x1000000U;
}

static void reset_probability_array(std::uint16_t *values, std::size_t count) {
    if (values != nullptr && count != 0) {
        std::fill_n(values, count, static_cast<std::uint16_t>(0x400U));
    }
}

void legacy_codec_reset_probability_state(const LegacyCodecResolvedOptions &options,
        const LegacyCodecProbabilityResetView &view) {
    if (view.processed_position != nullptr) {
        *view.processed_position = 0;
    }
    if (view.range_limit != nullptr) {
        *view.range_limit = view.range_start;
    }

    reset_probability_array(view.primary_probabilities, view.primary_probability_count);
    reset_probability_array(view.secondary_probabilities, view.secondary_probability_count);

    if (view.lc_mask != nullptr) {
        *view.lc_mask = mask_for_bits(options.lc);
    }
    if (view.pb_mask != nullptr) {
        *view.pb_mask = mask_for_bits(options.pb);
    }
}

LegacyCodecBoundedEncodeResult legacy_codec_run_bounded_encode(
        const LegacyCodecBoundedEncodeRequest &request) {
    LegacyCodecBoundedEncodeResult result{};
    if (!legacy_codec_bounded_encode_has_required_callbacks(request)) {
        result.status = legacy_codec_status_value(LegacyCodecStatus::memory_error);
        return result;
    }

    legacy_codec_bounded_encode_reset_if_requested(request);

    const int status = request.core_encode(request.state, request.core_user,
            request.finish_mode, request.input_limit);
    result.status = status;

    const unsigned long long output_after =
            legacy_codec_bounded_encode_output_position(*request.state, request.output_before);
    result.output_delta = legacy_codec_bounded_encode_output_delta(
            request.output_before, output_after);

    const unsigned int processed_after =
            legacy_codec_bounded_encode_processed_position(
                    *request.state, request.processed_before);
    result.processed_delta = legacy_codec_bounded_encode_processed_delta(
            request.processed_before, processed_after);
    return result;
}

bool legacy_codec_bounded_encode_has_required_callbacks(
        const LegacyCodecBoundedEncodeRequest &request) {
    return request.state != nullptr && request.core_encode != nullptr;
}

void legacy_codec_bounded_encode_reset_if_requested(
        const LegacyCodecBoundedEncodeRequest &request) {
    if (request.reset_before_encode) {
        legacy_codec_reset_probability_state(request.state->resolved_options, request.reset_view);
    }
}

unsigned long long legacy_codec_bounded_encode_output_position(
        const LegacyCodecState &state, unsigned long long fallback_position) {
    return state.progress_context != nullptr ?
            *static_cast<unsigned long long *>(state.progress_context) : fallback_position;
}

unsigned int legacy_codec_bounded_encode_processed_position(
        const LegacyCodecState &state, unsigned int fallback_position) {
    return state.stream_context != nullptr ?
            *static_cast<unsigned int *>(state.stream_context) : fallback_position;
}

unsigned long long legacy_codec_bounded_encode_output_delta(
        unsigned long long before, unsigned long long after) {
    return after >= before ? after - before : 0;
}

unsigned int legacy_codec_bounded_encode_processed_delta(
        unsigned int before, unsigned int after) {
    return after >= before ? after - before : 0;
}

int legacy_codec_prepare_stream_common(const LegacyCodecPrepareRequest &request) {
    if (!legacy_codec_prepare_stream_has_state(request)) {
        return legacy_codec_status_value(LegacyCodecStatus::memory_error);
    }
    legacy_codec_prepare_stream_bind_contexts(request);
    return legacy_codec_status_value(LegacyCodecStatus::ok);
}

bool legacy_codec_prepare_stream_has_state(const LegacyCodecPrepareRequest &request) {
    return request.state != nullptr;
}

void legacy_codec_prepare_stream_bind_contexts(const LegacyCodecPrepareRequest &request) {
    request.state->stream_context = request.stream_context;
    if (request.has_progress_context) {
        request.state->progress_context = request.progress_context;
    }
}

int legacy_codec_prepare_output_stream(const LegacyCodecPrepareRequest &request) {
    return legacy_codec_prepare_stream_common(request);
}

int legacy_codec_prepare_input_progress_stream(const LegacyCodecPrepareRequest &request) {
    return legacy_codec_prepare_stream_common(request);
}

void legacy_codec_noop_callback() {}

int legacy_codec_run_encode_loop(const LegacyCodecEncodeLoopRequest &request) {
    if (!legacy_codec_encode_loop_has_required_callbacks(request)) {
        return legacy_codec_status_value(LegacyCodecStatus::memory_error);
    }

    if (request.prepare != nullptr) {
        const int prepare_status =
                request.prepare(request.state, request.prepare_input, request.prepare_output);
        if (prepare_status != 0) {
            return prepare_status;
        }
    }

    int status = request.encode_step(request.state, request.core_user,
            request.finish_mode, request.input_limit);
    if (status != 0) {
        return status;
    }

    while (legacy_codec_encode_loop_should_continue(request.finished)) {
        if (request.progress != nullptr) {
            const auto adjusted_position =
                    legacy_codec_encode_loop_progress_position(*request.state);
            if (request.progress(request.progress_user, request.state->stream_context,
                        adjusted_position) != 0) {
                return request.progress_error_status;
            }
        }

        status = request.encode_step(request.state, request.core_user,
                request.finish_mode, request.input_limit);
        if (status != 0) {
            return status;
        }
    }

    return 0;
}

bool legacy_codec_encode_loop_has_required_callbacks(
        const LegacyCodecEncodeLoopRequest &request) {
    return request.state != nullptr && request.encode_step != nullptr;
}

bool legacy_codec_encode_loop_should_continue(const bool *finished) {
    return finished != nullptr && !*finished;
}

unsigned long long legacy_codec_encode_loop_progress_position(
        const LegacyCodecState &state) {
    return state.progress_context != nullptr ?
            *static_cast<unsigned long long *>(state.progress_context) : 0ULL;
}

int legacy_codec_run_owned_encode(const LegacyCodecOwnedEncodeRequest &request) {
    if (request.alloc == nullptr || request.free == nullptr) {
        return 2;
    }

    auto *state = legacy_codec_allocate_state(request.allocator, request.alloc);
    if (state == nullptr) {
        return 2;
    }

    int status = legacy_codec_owned_encode_apply_options(*state, request.options);
    if (status == legacy_codec_status_value(LegacyCodecStatus::ok)) {
        status = legacy_codec_owned_encode_apply_header(*state, request);
    }
    if (status == legacy_codec_status_value(LegacyCodecStatus::ok)) {
        LegacyCodecEncodeLoopRequest loop = request.loop;
        loop.state = state;
        status = legacy_codec_run_encode_loop(loop);
    }

    LegacyCodecAllocator allocator{request.allocator, request.free};
    legacy_codec_release_workspace_and_self(state->workspace, allocator, nullptr, nullptr, state);
    return status;
}

bool legacy_codec_owned_encode_has_header(const LegacyCodecOwnedEncodeRequest &request) {
    return request.header != nullptr && request.header_size != 0;
}

int legacy_codec_owned_encode_apply_options(
        LegacyCodecState &state, const LegacyCodecOptions *options) {
    if (options == nullptr) {
        return legacy_codec_status_value(LegacyCodecStatus::ok);
    }
    state.options = *options;
    return legacy_codec_resolve_options(state.options, state.resolved_options) ?
            legacy_codec_status_value(LegacyCodecStatus::ok) :
            legacy_codec_status_value(LegacyCodecStatus::option_error);
}

int legacy_codec_owned_encode_apply_header(
        LegacyCodecState &state, const LegacyCodecOwnedEncodeRequest &request) {
    if (!legacy_codec_owned_encode_has_header(request)) {
        return legacy_codec_status_value(LegacyCodecStatus::ok);
    }
    if (!legacy_codec_parse_properties(request.header, request.header_size, state.options)) {
        return legacy_codec_status_value(LegacyCodecStatus::invalid_properties);
    }
    return legacy_codec_resolve_options(state.options, state.resolved_options) ?
            legacy_codec_status_value(LegacyCodecStatus::ok) :
            legacy_codec_status_value(LegacyCodecStatus::option_error);
}

void legacy_codec_refresh_price_caches(const LegacyCodecPriceCacheRefreshRequest &request) {
    legacy_codec_price_cache_refresh_distance_tables(request);

    const std::uint32_t max_len_price =
            legacy_codec_price_cache_max_len_price(request.fast_bytes);
    legacy_codec_price_cache_store_limit(request.len_price_limit, max_len_price);
    legacy_codec_price_cache_store_limit(request.rep_len_price_limit, max_len_price);

    const std::uint32_t pos_state_count =
            legacy_codec_price_cache_pos_state_count(request.pos_state_bits);
    for (std::uint32_t pos_state = 0; pos_state < pos_state_count; ++pos_state) {
        if (request.refresh_len_prices != nullptr) {
            request.refresh_len_prices(request.len_context, pos_state, request.prices);
        }
        if (request.refresh_rep_len_prices != nullptr) {
            request.refresh_rep_len_prices(request.rep_len_context, pos_state, request.prices);
        }
    }
}

void legacy_codec_price_cache_refresh_distance_tables(
        const LegacyCodecPriceCacheRefreshRequest &request) {
    if (!request.distance_prices_dirty) {
        return;
    }
    legacy_codec_price_cache_refresh_optional(
            request.refresh_distance_prices, request.distance_context);
    legacy_codec_price_cache_refresh_optional(
            request.refresh_align_prices, request.align_context);
}

void legacy_codec_price_cache_refresh_optional(
        LegacyCodecRefreshFn refresh, void *context) {
    if (refresh != nullptr) {
        refresh(context);
    }
}

std::uint32_t legacy_codec_price_cache_max_len_price(std::uint32_t fast_bytes) {
    return fast_bytes == 0 ? 0 : fast_bytes - 1U;
}

std::uint32_t legacy_codec_price_cache_pos_state_count(std::uint32_t pos_state_bits) {
    return pos_state_bits >= 31U ? 0x80000000U : (1U << pos_state_bits);
}

void legacy_codec_price_cache_store_limit(
        std::uint32_t *limit_slot, std::uint32_t limit) {
    if (limit_slot != nullptr) {
        *limit_slot = limit;
    }
}

int legacy_codec_run_header_decode(const LegacyCodecHeaderDecodeRequest &request) {
    legacy_codec_header_decode_store_output(request.output_position, nullptr);
    if (!legacy_codec_header_decode_has_minimum_input(request.input_size)) {
        legacy_codec_header_decode_reset_input_size(request.input_size);
        return legacy_codec_status_value(LegacyCodecStatus::input_error);
    }

    const unsigned long long available_input = *request.input_size;
    legacy_codec_header_decode_reset_input_size(request.input_size);

    LegacyCodecHeader header{};
    if (!legacy_codec_parse_header(request.header, request.header_size, header)) {
        return legacy_codec_status_value(LegacyCodecStatus::invalid_properties);
    }
    if (request.alloc == nullptr || request.free == nullptr || request.core_decode == nullptr) {
        return legacy_codec_status_value(LegacyCodecStatus::memory_error);
    }

    void *probabilities = request.alloc(request.allocator,
            static_cast<std::size_t>(header.probability_model_size) * sizeof(std::uint16_t));
    if (probabilities == nullptr) {
        return legacy_codec_status_value(LegacyCodecStatus::memory_error);
    }

    LegacyCodecOneShotDecodeState state{};
    state.options = header.options;
    state.probability_models = probabilities;
    state.output_context = request.output_context;
    state.output_position = legacy_codec_header_decode_initial_output(
            request.output_position);
    state.probability_model_size = header.probability_model_size;
    state.initialized = true;

    *request.input_size = available_input;
    int status_value = 0;
    int result = request.core_decode(&state, request.output_limit, request.input,
            request.input_size, request.finish_mode,
            legacy_codec_header_decode_status_slot(request.decode_status, &status_value));
    const int final_status = legacy_codec_header_decode_final_status(
            request.decode_status, status_value);
    result = legacy_codec_header_decode_result_from_status(result, final_status);
    legacy_codec_header_decode_store_output(
            request.output_position, state.output_position);

    request.free(request.allocator, probabilities);
    return result;
}

int legacy_codec_header_decode_result_from_status(int core_result, int decode_status) {
    if (core_result != 0) {
        return core_result;
    }
    return decode_status == legacy_codec_decode_status_value(
            LegacyCodecDecodeStatus::finished_with_marker) ?
            legacy_codec_status_value(LegacyCodecStatus::ok) :
            legacy_codec_status_value(LegacyCodecStatus::input_error);
}

bool legacy_codec_header_decode_has_minimum_input(const unsigned long long *input_size) {
    return input_size != nullptr && *input_size >= kLegacyCodecHeaderEncodedSize;
}

void legacy_codec_header_decode_reset_input_size(unsigned long long *input_size) {
    if (input_size != nullptr) {
        *input_size = 0;
    }
}

void *legacy_codec_header_decode_initial_output(void **output_position) {
    return output_position == nullptr ? nullptr : *output_position;
}

void legacy_codec_header_decode_store_output(
        void **output_position, void *decoded_output_position) {
    if (output_position != nullptr) {
        *output_position = decoded_output_position;
    }
}

int *legacy_codec_header_decode_status_slot(
        int *request_decode_status, int *local_decode_status) {
    return request_decode_status == nullptr ? local_decode_status : request_decode_status;
}

int legacy_codec_header_decode_final_status(
        const int *request_decode_status, int local_decode_status) {
    return request_decode_status == nullptr ? local_decode_status : *request_decode_status;
}

int legacy_codec_run_streaming_decode(const LegacyCodecStreamingDecodeRequest &request) {
    if (request.state == nullptr || request.state->core_decode == nullptr ||
            request.output_size == nullptr || request.input_size == nullptr) {
        return legacy_codec_status_value(LegacyCodecStatus::memory_error);
    }

    unsigned long long output_remaining = *request.output_size;
    unsigned long long input_remaining = *request.input_size;
    *request.output_size = 0;
    *request.input_size = 0;

    auto *output_cursor = request.output;
    auto *input_cursor = static_cast<const std::uint8_t *>(request.input);
    while (output_remaining != 0) {
        unsigned long long read_before = request.state->read_pos;
        const unsigned long long write_pos = request.state->write_pos;
        if (read_before == write_pos) {
            read_before = 0;
            request.state->read_pos = 0;
        }

        const unsigned long long output_limit = legacy_codec_streaming_output_limit(
                read_before, write_pos, output_remaining);
        const int finish_mode = legacy_codec_streaming_finish_mode(
                output_limit, read_before, output_remaining, request.finish_mode);

        unsigned long long input_used = input_remaining;
        const int status = request.state->core_decode(request.state->decoder, output_limit,
                input_cursor, &input_used, finish_mode, request.decode_status);
        *request.input_size += input_used;

        const unsigned long long read_after = request.state->read_pos;
        const unsigned long long produced =
                legacy_codec_streaming_produced_size(read_before, read_after);
        if (produced != 0) {
            if (output_cursor == nullptr || request.state->output_buffer == nullptr) {
                return legacy_codec_status_value(LegacyCodecStatus::memory_error);
            }
            std::memcpy(output_cursor, request.state->output_buffer + read_before,
                    static_cast<std::size_t>(produced));
            output_cursor += produced;
            *request.output_size += produced;
        }

        if (legacy_codec_streaming_decode_finished(status, produced)) {
            return status;
        }

        if (input_cursor != nullptr) {
            input_cursor += input_used;
        }
        input_remaining = legacy_codec_streaming_remaining_after(
                input_remaining, input_used);
        output_remaining = legacy_codec_streaming_remaining_after(
                output_remaining, produced);
    }

    return 0;
}

unsigned long long legacy_codec_streaming_output_limit(
        unsigned long long read_position, unsigned long long write_position,
        unsigned long long output_remaining) {
    const unsigned long long buffered = write_position - read_position;
    return output_remaining <= buffered ? read_position + output_remaining : write_position;
}

unsigned long long legacy_codec_streaming_produced_size(
        unsigned long long read_before, unsigned long long read_after) {
    return read_after >= read_before ? read_after - read_before : 0;
}

int legacy_codec_streaming_finish_mode(
        unsigned long long output_limit, unsigned long long read_position,
        unsigned long long output_remaining, int requested_finish_mode) {
    return output_limit == read_position + output_remaining ? requested_finish_mode : 0;
}

bool legacy_codec_streaming_decode_finished(
        int status, unsigned long long produced_size) {
    return status != 0 || produced_size == 0;
}

unsigned long long legacy_codec_streaming_remaining_after(
        unsigned long long remaining, unsigned long long consumed) {
    return consumed <= remaining ? remaining - consumed : 0;
}

unsigned long long legacy_codec_range_decoder_initial_limit() {
    return 0x100000000ULL;
}

void legacy_codec_prepare_decoder_range(LegacyCodecDecoderRangeState &state,
        bool reset_buffered_input, bool finish_input) {
    state.status = 0;
    state.range_limit = legacy_codec_range_decoder_initial_limit();
    if (reset_buffered_input) {
        state.buffered_input = 0;
        state.input_finished = true;
    }
    if (finish_input) {
        state.input_finished = true;
    }
}

void legacy_codec_reset_decoder_range(LegacyCodecDecoderRangeState &state) {
    state.read_pos = 0;
    state.status = 0;
    state.buffered_input = 0;
    state.range_limit = legacy_codec_range_decoder_initial_limit();
    state.input_finished = true;
}

bool legacy_codec_range_decoder_normalize(LegacyCodecRangeDecoderCursor &cursor) {
    if ((cursor.range >> 24U) != 0) {
        return true;
    }
    if (cursor.input == nullptr || cursor.input >= cursor.input_end) {
        return false;
    }

    cursor.range <<= 8U;
    cursor.code = (cursor.code << 8U) | *cursor.input++;
    ++cursor.consumed;
    return true;
}

LegacyCodecRangeBitResult legacy_codec_range_decode_bit(
        LegacyCodecRangeDecoderCursor &cursor, std::uint16_t &probability) {
    LegacyCodecRangeBitResult result{};
    if (!legacy_codec_range_decoder_normalize(cursor)) {
        return result;
    }

    const std::uint32_t bound =
            (cursor.range >> 11U) * static_cast<std::uint32_t>(probability);
    if (cursor.code < bound) {
        cursor.range = bound;
        probability = static_cast<std::uint16_t>(
                probability + ((0x800U - probability) >> 5U));
        result.bit = 0;
    } else {
        cursor.range -= bound;
        cursor.code -= bound;
        probability = static_cast<std::uint16_t>(probability - (probability >> 5U));
        result.bit = 1;
    }

    result.ok = true;
    return result;
}

LegacyCodecRangeBitTreeResult legacy_codec_range_decode_bit_tree(
        LegacyCodecRangeDecoderCursor &cursor, std::uint16_t *probabilities,
        std::uint32_t symbol_limit) {
    LegacyCodecRangeBitTreeResult result{};
    if (probabilities == nullptr || symbol_limit <= 1U) {
        return result;
    }

    std::uint32_t symbol = 1U;
    while (symbol < symbol_limit) {
        LegacyCodecRangeBitResult bit =
                legacy_codec_range_decode_bit(cursor, probabilities[symbol]);
        if (!bit.ok) {
            return result;
        }
        symbol = (symbol << 1U) | bit.bit;
    }

    result.ok = true;
    result.symbol = symbol - symbol_limit;
    return result;
}

LegacyCodecRangeBitTreeResult legacy_codec_range_decode_reverse_bit_tree(
        LegacyCodecRangeDecoderCursor &cursor, std::uint16_t *probabilities,
        std::uint32_t bit_count) {
    LegacyCodecRangeBitTreeResult result{};
    if (probabilities == nullptr || bit_count > 30U) {
        return result;
    }

    std::uint32_t symbol = 1U;
    std::uint32_t value = 0;
    for (std::uint32_t bit_index = 0; bit_index < bit_count; ++bit_index) {
        LegacyCodecRangeBitResult bit =
                legacy_codec_range_decode_bit(cursor, probabilities[symbol]);
        if (!bit.ok) {
            return result;
        }
        symbol = (symbol << 1U) | bit.bit;
        value |= bit.bit << bit_index;
    }

    result.ok = true;
    result.symbol = value;
    return result;
}

LegacyCodecRangeBitTreeResult legacy_codec_range_decode_direct_bits(
        LegacyCodecRangeDecoderCursor &cursor, std::uint32_t bit_count) {
    LegacyCodecRangeBitTreeResult result{};
    if (bit_count > 30U) {
        return result;
    }

    std::uint32_t value = 0;
    for (std::uint32_t index = 0; index < bit_count; ++index) {
        if (!legacy_codec_range_decoder_normalize(cursor)) {
            return result;
        }
        cursor.range >>= 1U;
        const std::uint32_t bit = cursor.code >= cursor.range ? 1U : 0U;
        if (bit != 0) {
            cursor.code -= cursor.range;
        }
        value = (value << 1U) | bit;
    }

    result.ok = true;
    result.symbol = value;
    return result;
}

LegacyCodecRangeLengthResult legacy_codec_range_decode_length(
        LegacyCodecRangeDecoderCursor &cursor, std::uint16_t *probabilities,
        std::uint32_t pos_state) {
    LegacyCodecRangeLengthResult result{};
    if (probabilities == nullptr || pos_state >= 16U) {
        return result;
    }

    LegacyCodecRangeBitResult choice =
            legacy_codec_range_decode_bit(cursor, probabilities[0]);
    if (!choice.ok) {
        return result;
    }

    if (choice.bit == 0) {
        LegacyCodecRangeBitTreeResult low = legacy_codec_range_decode_bit_tree(
                cursor, probabilities + 2U + pos_state * 8U, 8U);
        result.ok = low.ok;
        result.symbol = low.symbol;
        return result;
    }

    choice = legacy_codec_range_decode_bit(cursor, probabilities[1]);
    if (!choice.ok) {
        return result;
    }

    if (choice.bit == 0) {
        LegacyCodecRangeBitTreeResult mid = legacy_codec_range_decode_bit_tree(
                cursor, probabilities + 0x82U + pos_state * 8U, 8U);
        result.ok = mid.ok;
        result.symbol = mid.symbol + 8U;
        return result;
    }

    LegacyCodecRangeBitTreeResult high = legacy_codec_range_decode_bit_tree(
            cursor, probabilities + 0x102U, 0x100U);
    result.ok = high.ok;
    result.symbol = high.symbol + 0x10U;
    return result;
}

LegacyCodecRangeBitTreeResult legacy_codec_range_decode_pos_slot(
        LegacyCodecRangeDecoderCursor &cursor, std::uint16_t *probabilities,
        std::uint32_t length_symbol) {
    if (probabilities == nullptr) {
        return LegacyCodecRangeBitTreeResult{};
    }

    const std::uint32_t length_state = length_symbol < 4U ? length_symbol : 3U;
    return legacy_codec_range_decode_bit_tree(
            cursor, probabilities + length_state * 0x40U, 0x40U);
}

LegacyCodecRangeDistanceResult legacy_codec_range_decode_reverse_distance(
        LegacyCodecRangeDecoderCursor &cursor, std::uint16_t *probabilities,
        std::uint32_t pos_slot) {
    LegacyCodecRangeDistanceResult result{};
    const LegacyCodecDistanceSlotInfo slot = legacy_codec_distance_slot_info(pos_slot);
    if (!slot.valid || slot.direct_footer) {
        return result;
    }

    std::uint32_t footer = 0;
    if (slot.footer_bit_count != 0) {
        if (probabilities == nullptr || slot.distance_minus_one_base < pos_slot) {
            return result;
        }
        const std::uint32_t probability_offset = slot.distance_minus_one_base - pos_slot;
        LegacyCodecRangeBitTreeResult decoded_footer =
                legacy_codec_range_decode_reverse_bit_tree(
                        cursor, probabilities + probability_offset, slot.footer_bit_count);
        if (!decoded_footer.ok) {
            return result;
        }
        footer = decoded_footer.symbol;
    }

    result.distance_minus_one = slot.distance_minus_one_base + footer;
    result.distance = result.distance_minus_one + 1U;
    result.ok = true;
    return result;
}

LegacyCodecRangeDistanceResult legacy_codec_range_decode_direct_distance(
        LegacyCodecRangeDecoderCursor &cursor, std::uint16_t *align_probabilities,
        std::uint32_t pos_slot) {
    LegacyCodecRangeDistanceResult result{};
    const LegacyCodecDistanceSlotInfo slot = legacy_codec_distance_slot_info(pos_slot);
    if (align_probabilities == nullptr || !slot.valid || !slot.direct_footer) {
        return result;
    }

    LegacyCodecRangeBitTreeResult direct =
            legacy_codec_range_decode_direct_bits(cursor, slot.footer_bit_count);
    if (!direct.ok) {
        return result;
    }

    LegacyCodecRangeBitTreeResult align = legacy_codec_range_decode_reverse_bit_tree(
            cursor, align_probabilities, slot.align_bit_count);
    if (!align.ok) {
        return result;
    }

    result.distance_minus_one =
            slot.distance_minus_one_base + (direct.symbol << slot.align_bit_count) +
            align.symbol;
    result.distance = result.distance_minus_one + 1U;
    result.ok = true;
    return result;
}

LegacyCodecRangeLiteralResult legacy_codec_range_decode_literal(
        LegacyCodecRangeDecoderCursor &cursor, const LegacyCodecRangeLiteralRequest &request) {
    LegacyCodecRangeLiteralResult result{};
    if (request.probabilities == nullptr) {
        return result;
    }

    std::uint32_t symbol = 1U;
    if (!request.use_match_byte) {
        while (symbol < 0x100U) {
            LegacyCodecRangeBitResult bit =
                    legacy_codec_range_decode_bit(cursor, request.probabilities[symbol]);
            if (!bit.ok) {
                return result;
            }
            symbol = (symbol << 1U) | bit.bit;
        }
    } else {
        std::uint32_t match_byte = request.match_byte;
        while (symbol < 0x100U) {
            match_byte <<= 1U;
            const std::uint32_t match_bit = match_byte & 0x100U;
            const std::uint32_t probability_index = match_bit + 0x100U + symbol;
            LegacyCodecRangeBitResult bit = legacy_codec_range_decode_bit(
                    cursor, request.probabilities[probability_index]);
            if (!bit.ok) {
                return result;
            }
            symbol = (symbol << 1U) | bit.bit;
            if ((match_bit != 0) != (bit.bit != 0)) {
                while (symbol < 0x100U) {
                    LegacyCodecRangeBitResult tail_bit =
                            legacy_codec_range_decode_bit(cursor, request.probabilities[symbol]);
                    if (!tail_bit.ok) {
                        return result;
                    }
                    symbol = (symbol << 1U) | tail_bit.bit;
                }
                break;
            }
        }
    }

    result.ok = true;
    result.value = static_cast<std::uint8_t>(symbol);
    return result;
}

LegacyCodecOutputWindowCopyResult legacy_codec_copy_match_to_output_window(
        const LegacyCodecOutputWindowCopyRequest &request) {
    LegacyCodecOutputWindowCopyResult result{};
    result.new_position = request.position;
    result.remaining_length = request.length;
    if (request.buffer == nullptr || request.cyclic_size == 0 ||
            request.distance == 0 || request.length == 0 ||
            request.position >= request.output_limit) {
        return result;
    }

    const std::uint32_t bytes_to_copy = legacy_codec_output_window_copy_length(
            request.position, request.output_limit, request.length);
    if (bytes_to_copy == 0) {
        return result;
    }

    std::uint64_t source_position = legacy_codec_output_window_source_position(
            request.position, request.cyclic_size, request.distance);
    std::uint64_t write_position = request.position;
    for (std::uint32_t index = 0; index < bytes_to_copy; ++index) {
        request.buffer[write_position % request.cyclic_size] =
                request.buffer[source_position % request.cyclic_size];
        ++write_position;
        ++source_position;
    }

    result.copied = true;
    result.bytes_copied = bytes_to_copy;
    result.remaining_length = request.length - bytes_to_copy;
    result.new_position = write_position;
    return result;
}

LegacyCodecOutputWindowCopyResult legacy_codec_copy_pending_match_to_output_window(
        const LegacyCodecPendingMatchCopyRequest &request) {
    LegacyCodecOutputWindowCopyRequest copy{};
    copy.buffer = request.buffer;
    copy.position = request.position;
    copy.output_limit = request.output_limit;
    copy.cyclic_size = request.cyclic_size;
    copy.distance = request.distance;
    copy.length = request.remaining_length;
    return legacy_codec_copy_match_to_output_window(copy);
}

std::uint32_t legacy_codec_output_window_copy_length(
        std::uint64_t position, std::uint64_t output_limit, std::uint32_t requested_length) {
    if (output_limit <= position) {
        return 0;
    }
    return static_cast<std::uint32_t>(
            std::min<std::uint64_t>(requested_length, output_limit - position));
}

std::uint64_t legacy_codec_output_window_source_position(
        std::uint64_t position, std::uint32_t cyclic_size, std::uint32_t distance) {
    if (cyclic_size == 0 || distance == 0) {
        return position;
    }
    return position >= distance ? position - distance : position + cyclic_size - distance;
}

LegacyCodecOutputWindowByteResult legacy_codec_output_window_byte_at_distance(
        const LegacyCodecOutputWindowByteRequest &request) {
    LegacyCodecOutputWindowByteResult result{};
    if (request.buffer == nullptr || request.cyclic_size == 0 || request.distance == 0) {
        return result;
    }

    const std::uint64_t source_position = legacy_codec_output_window_source_position(
            request.position, request.cyclic_size, request.distance);
    result.value = request.buffer[source_position % request.cyclic_size];
    result.ok = true;
    return result;
}

LegacyCodecOutputWindowByteResult legacy_codec_output_window_previous_byte(
        const std::uint8_t *buffer, std::uint64_t position, std::uint32_t cyclic_size) {
    LegacyCodecOutputWindowByteRequest request{};
    request.buffer = buffer;
    request.position = position;
    request.cyclic_size = cyclic_size;
    request.distance = 1U;
    return legacy_codec_output_window_byte_at_distance(request);
}

LegacyCodecOutputWindowWriteByteResult legacy_codec_write_byte_to_output_window(
        const LegacyCodecOutputWindowWriteByteRequest &request) {
    LegacyCodecOutputWindowWriteByteResult result{};
    result.new_position = request.position;
    if (request.buffer == nullptr || request.cyclic_size == 0 ||
            request.output_limit <= request.position) {
        return result;
    }

    request.buffer[request.position % request.cyclic_size] = request.value;
    result.written = true;
    result.new_position = request.position + 1U;
    return result;
}

LegacyCodecDistanceSlotInfo legacy_codec_distance_slot_info(std::uint32_t pos_slot) {
    LegacyCodecDistanceSlotInfo result{};
    if (pos_slot >= 0x40U) {
        return result;
    }

    result.valid = true;
    if (pos_slot < 4U) {
        result.distance_minus_one_base = pos_slot;
        return result;
    }

    const std::uint32_t footer_bits = (pos_slot >> 1U) - 1U;
    result.distance_minus_one_base = ((pos_slot & 1U) | 2U) << footer_bits;
    if (pos_slot < 0xeU) {
        result.footer_bit_count = footer_bits;
        return result;
    }

    result.footer_bit_count = footer_bits - 4U;
    result.direct_footer = true;
    result.align_bit_count = 4U;
    return result;
}

std::uint32_t legacy_codec_decoder_position_state(
        std::uint32_t processed_size, std::uint32_t position_bits) {
    const std::uint32_t shift = legacy_codec_shift_count5(position_bits);
    if (shift == 0) {
        return 0;
    }
    const std::uint32_t mask = (1U << shift) - 1U;
    return processed_size & mask;
}

std::uint64_t legacy_codec_decoder_iteration_output_limit(
        const LegacyCodecDecoderOutputLimitRequest &request) {
    if (request.pending_length != 0 || request.available_size <= request.processed_size) {
        return request.requested_limit;
    }

    const std::uint64_t buffered_limit =
            request.position + (request.available_size - request.processed_size);
    return std::min(request.requested_limit, buffered_limit);
}

bool legacy_codec_decoder_should_save_iteration(
        const std::uint8_t *input, const std::uint8_t *input_end,
        std::uint64_t position, std::uint64_t output_limit) {
    return input_end <= input || output_limit <= position;
}

bool legacy_codec_decoder_has_history(
        std::uint32_t processed_size, std::uint32_t pending_limit) {
    return processed_size != 0 || pending_limit != 0;
}

bool legacy_codec_decoder_distance_available(
        std::uint32_t distance_minus_one, std::uint32_t processed_size,
        std::uint32_t pending_limit) {
    const std::uint32_t available = pending_limit != 0 ? pending_limit : processed_size;
    return distance_minus_one < available;
}

std::uint32_t legacy_codec_decoder_match_length_from_symbol(std::uint32_t length_symbol) {
    return length_symbol + 2U;
}

bool legacy_codec_decoder_is_end_marker_distance(std::uint32_t distance_minus_one) {
    return distance_minus_one == 0xffffffffU;
}

std::uint32_t legacy_codec_clamp_pending_match_length(std::uint32_t length) {
    return length > 0x112U ? 0x112U : length;
}

bool legacy_codec_decoder_has_pending_match(std::uint32_t length) {
    return length != 0 && length < 0x112U;
}

void legacy_codec_reps_after_new_match(
        LegacyCodecRepDistances &reps, std::uint32_t distance) {
    reps.rep3 = reps.rep2;
    reps.rep2 = reps.rep1;
    reps.rep1 = reps.rep0;
    reps.rep0 = distance;
}

std::uint32_t legacy_codec_reps_after_repeated_match(
        LegacyCodecRepDistances &reps, std::uint32_t rep_index) {
    if (rep_index == 0) {
        return reps.rep0;
    }
    if (rep_index > 3U) {
        rep_index = 3U;
    }

    std::uint32_t selected = reps.rep0;
    if (rep_index == 1U) {
        selected = reps.rep1;
        reps.rep1 = reps.rep0;
    } else if (rep_index == 2U) {
        selected = reps.rep2;
        reps.rep2 = reps.rep1;
        reps.rep1 = reps.rep0;
    } else {
        selected = reps.rep3;
        reps.rep3 = reps.rep2;
        reps.rep2 = reps.rep1;
        reps.rep1 = reps.rep0;
    }
    reps.rep0 = selected;
    return selected;
}

std::uint32_t legacy_codec_state_after_literal(std::uint32_t state) {
    if (state < 4U) {
        return 0;
    }
    if (state < 10U) {
        return state - 3U;
    }
    return state - 6U;
}

std::uint32_t legacy_codec_state_after_match(std::uint32_t state) {
    return state < 7U ? 7U : 10U;
}

std::uint32_t legacy_codec_state_after_rep(std::uint32_t state) {
    return state < 7U ? 8U : 11U;
}

std::uint32_t legacy_codec_state_after_short_rep(std::uint32_t state) {
    return state < 7U ? 9U : 11U;
}

std::uint32_t legacy_codec_literal_context_index(std::uint64_t processed_position,
        std::uint8_t previous_byte, std::uint32_t lc, std::uint32_t lp) {
    const std::uint32_t literal_context =
            lc >= 8U ? previous_byte : previous_byte >> (8U - lc);
    const std::uint32_t position_context =
            static_cast<std::uint32_t>(processed_position) & mask_for_bits(static_cast<int>(lp));
    return (literal_context + (position_context << lc)) * 0x300U;
}

std::uint16_t *legacy_codec_literal_probabilities_for_context(
        std::uint16_t *literal_probabilities, std::uint32_t context_index) {
    if (literal_probabilities == nullptr) {
        return nullptr;
    }
    return literal_probabilities + context_index;
}

std::uint16_t *legacy_codec_match_probability_for_state(
        std::uint16_t *probabilities, std::uint32_t state, std::uint32_t pos_state) {
    if (probabilities == nullptr) {
        return nullptr;
    }
    return probabilities + state * 16U + pos_state;
}

std::uint16_t *legacy_codec_state_probability_at(
        std::uint16_t *probabilities, std::uint32_t base_offset, std::uint32_t state) {
    if (probabilities == nullptr) {
        return nullptr;
    }
    return probabilities + base_offset + state;
}

std::uint16_t *legacy_codec_state_pos_probability_at(
        std::uint16_t *probabilities, std::uint32_t base_offset,
        std::uint32_t state, std::uint32_t pos_state) {
    if (probabilities == nullptr) {
        return nullptr;
    }
    return probabilities + base_offset + state * 16U + pos_state;
}

std::uint32_t legacy_codec_probability_bank_offset(LegacyCodecProbabilityBank bank) {
    return static_cast<std::uint32_t>(bank);
}

std::uint16_t *legacy_codec_probability_at_bank(
        std::uint16_t *probabilities, LegacyCodecProbabilityBank bank, std::uint32_t state) {
    return legacy_codec_state_probability_at(
            probabilities, legacy_codec_probability_bank_offset(bank), state);
}

std::uint16_t *legacy_codec_pos_probability_at_bank(
        std::uint16_t *probabilities, LegacyCodecProbabilityBank bank,
        std::uint32_t state, std::uint32_t pos_state) {
    return legacy_codec_state_pos_probability_at(
            probabilities, legacy_codec_probability_bank_offset(bank), state, pos_state);
}

std::uint16_t *legacy_codec_is_rep_probability(
        std::uint16_t *probabilities, std::uint32_t state) {
    return legacy_codec_probability_at_bank(probabilities, LegacyCodecProbabilityBank::is_rep, state);
}

std::uint16_t *legacy_codec_is_rep_g0_probability(
        std::uint16_t *probabilities, std::uint32_t state) {
    return legacy_codec_probability_at_bank(
            probabilities, LegacyCodecProbabilityBank::is_rep_g0, state);
}

std::uint16_t *legacy_codec_is_rep_g1_probability(
        std::uint16_t *probabilities, std::uint32_t state) {
    return legacy_codec_probability_at_bank(
            probabilities, LegacyCodecProbabilityBank::is_rep_g1, state);
}

std::uint16_t *legacy_codec_is_rep_g2_probability(
        std::uint16_t *probabilities, std::uint32_t state) {
    return legacy_codec_probability_at_bank(
            probabilities, LegacyCodecProbabilityBank::is_rep_g2, state);
}

std::uint16_t *legacy_codec_is_rep0_long_probability(
        std::uint16_t *probabilities, std::uint32_t state, std::uint32_t pos_state) {
    return legacy_codec_pos_probability_at_bank(
            probabilities, LegacyCodecProbabilityBank::is_rep0_long, state, pos_state);
}

static std::uint16_t *legacy_codec_probability_bank_base(
        std::uint16_t *probabilities, LegacyCodecProbabilityBank bank) {
    if (probabilities == nullptr) {
        return nullptr;
    }
    return probabilities + legacy_codec_probability_bank_offset(bank);
}

std::uint16_t *legacy_codec_pos_slot_probabilities(
        std::uint16_t *probabilities) {
    return legacy_codec_probability_bank_base(probabilities, LegacyCodecProbabilityBank::pos_slot);
}

std::uint16_t *legacy_codec_align_probabilities(
        std::uint16_t *probabilities) {
    return legacy_codec_probability_bank_base(probabilities, LegacyCodecProbabilityBank::align);
}

std::uint16_t *legacy_codec_len_probabilities(
        std::uint16_t *probabilities) {
    return legacy_codec_probability_bank_base(probabilities, LegacyCodecProbabilityBank::len_choice);
}

std::uint16_t *legacy_codec_rep_len_probabilities(
        std::uint16_t *probabilities) {
    return legacy_codec_probability_bank_base(
            probabilities, LegacyCodecProbabilityBank::rep_len_choice);
}

std::uint16_t *legacy_codec_literal_probabilities(
        std::uint16_t *probabilities) {
    return legacy_codec_probability_bank_base(probabilities, LegacyCodecProbabilityBank::literal);
}

LegacyCodecMatchFinderCallbacks legacy_codec_select_match_finder_callbacks(
        const LegacyCodecMatchFinderSelection &selection) {
    LegacyCodecMatchFinderCallbacks callbacks{};
    if (selection.catalog == nullptr) {
        return callbacks;
    }

    callbacks.init = selection.catalog->init;
    callbacks.get_byte = selection.catalog->get_byte;
    callbacks.available_bytes = selection.catalog->available_bytes;
    callbacks.current_pointer = selection.catalog->current_pointer;

    if (!selection.binary_tree_mode) {
        callbacks.find_matches = selection.catalog->hash_chain_find;
        callbacks.skip = selection.catalog->hash_chain_skip;
    } else if (selection.hash_bytes == 2) {
        callbacks.find_matches = selection.catalog->binary_tree_2_find;
        callbacks.skip = selection.catalog->binary_tree_2_skip;
    } else if (selection.hash_bytes == 3) {
        callbacks.find_matches = selection.catalog->binary_tree_3_find;
        callbacks.skip = selection.catalog->binary_tree_3_skip;
    } else {
        callbacks.find_matches = selection.catalog->binary_tree_4_find;
        callbacks.skip = selection.catalog->binary_tree_4_skip;
    }

    return callbacks;
}

std::uint8_t legacy_codec_match_finder_get_byte(const LegacyCodecMatchFinderView &view,
        int offset) {
    if (view.current == nullptr || offset < 0) {
        return 0;
    }
    return view.current[offset];
}

std::uint32_t legacy_codec_match_finder_available_bytes(
        const LegacyCodecMatchFinderView &view) {
    return view.limit >= view.position ? view.limit - view.position : 0;
}

const std::uint8_t *legacy_codec_match_finder_current_pointer(
        const LegacyCodecMatchFinderView &view) {
    return view.current;
}

void legacy_codec_match_finder_rewind_cursor(LegacyCodecMatchFinderCursor &cursor,
        std::uint32_t amount) {
    cursor.cyclic_pos -= amount;
    cursor.buffer_pos -= amount;
    cursor.available_bytes -= amount;
}

void legacy_codec_match_finder_move_window(LegacyCodecMatchFinderWindow &window) {
    if (window.buffer == nullptr || window.current == nullptr) {
        return;
    }

    const std::uint32_t byte_count =
            window.write_pos + window.keep_size_before - window.read_pos;
    std::memmove(window.buffer, window.current - window.keep_size_before, byte_count);
    window.current = window.buffer + window.keep_size_before;
}

bool legacy_codec_match_finder_should_move_window(
        const LegacyCodecMatchFinderWindow &window) {
    if (window.direct_input || window.buffer == nullptr || window.current == nullptr) {
        return false;
    }
    return static_cast<std::uint64_t>(window.buffer + window.block_size - window.current) <=
            window.keep_size_after;
}

bool legacy_codec_match_finder_should_fill_input(
        const LegacyCodecMatchFinderFillState &state) {
    if (state.stream_error) {
        return false;
    }
    const std::uint32_t buffered =
            state.write_pos >= state.read_pos ? state.write_pos - state.read_pos : 0;
    return buffered <= state.keep_size_after;
}

void legacy_codec_match_finder_fill_input(LegacyCodecMatchFinderInputBuffer &state) {
    if (state.stream_end || state.stream_error != 0) {
        return;
    }

    if (state.direct_input) {
        const std::uint32_t consumed =
                legacy_codec_match_finder_direct_input_consumed(state);
        state.direct_input_remaining -= consumed;
        state.write_pos += consumed;
        if (state.direct_input_remaining == 0) {
            state.stream_end = true;
        }
        return;
    }

    if (state.buffer == nullptr || state.read == nullptr) {
        return;
    }

    while (true) {
        const std::uint32_t buffered =
                legacy_codec_match_finder_input_buffered_size(state);
        const std::uint64_t end_offset =
                legacy_codec_match_finder_input_target_offset(state);
        if (buffered >= end_offset) {
            break;
        }

        std::uint64_t read_size =
                legacy_codec_match_finder_input_read_size(buffered, end_offset);
        state.stream_error = state.read(state.stream, state.buffer + buffered, &read_size);
        if (state.stream_error != 0) {
            break;
        }
        if (read_size == 0) {
            state.stream_end = true;
            break;
        }

        state.write_pos += static_cast<std::uint32_t>(read_size);
        if (state.write_pos - state.read_pos > state.keep_size_after) {
            break;
        }
    }
}

std::uint32_t legacy_codec_match_finder_direct_input_writable(
        const LegacyCodecMatchFinderInputBuffer &state) {
    return static_cast<std::uint32_t>(~state.write_pos);
}

std::uint32_t legacy_codec_match_finder_direct_input_consumed(
        const LegacyCodecMatchFinderInputBuffer &state) {
    const std::uint32_t writable =
            legacy_codec_match_finder_direct_input_writable(state);
    return state.direct_input_remaining < writable ?
            static_cast<std::uint32_t>(state.direct_input_remaining) : writable;
}

std::uint32_t legacy_codec_match_finder_input_buffered_size(
        const LegacyCodecMatchFinderInputBuffer &state) {
    return state.write_pos - state.read_pos;
}

std::uint64_t legacy_codec_match_finder_input_target_offset(
        const LegacyCodecMatchFinderInputBuffer &state) {
    return static_cast<std::uint64_t>(state.block_size) + state.keep_size_after;
}

std::uint64_t legacy_codec_match_finder_input_read_size(
        std::uint32_t buffered, std::uint64_t end_offset) {
    return end_offset - buffered;
}

void legacy_codec_match_finder_refresh_limits(LegacyCodecMatchFinderLimitState &state) {
    if (state.cyclic_pos == state.cyclic_size) {
        state.cyclic_pos = 0;
    }

    const std::uint32_t position_cap =
            legacy_codec_match_finder_position_cap(state);

    const std::uint32_t available = legacy_codec_match_finder_limit_available(state);
    const std::uint32_t position_delta =
            legacy_codec_match_finder_position_delta(
                    available, state.keep_size_after, position_cap);

    state.match_len_limit = legacy_codec_match_finder_match_len_limit(
            available, state.match_max_len);
    state.position_limit = state.read_pos + position_delta;
}

std::uint32_t legacy_codec_match_finder_position_cap(
        const LegacyCodecMatchFinderLimitState &state) {
    std::uint32_t position_cap = state.cyclic_size - state.cyclic_pos;
    if (~state.read_pos <= position_cap) {
        position_cap = ~state.read_pos;
    }
    return position_cap;
}

std::uint32_t legacy_codec_match_finder_limit_available(
        const LegacyCodecMatchFinderLimitState &state) {
    return state.write_pos - state.read_pos;
}

std::uint32_t legacy_codec_match_finder_position_delta(
        std::uint32_t available, std::uint32_t keep_size_after,
        std::uint32_t position_cap) {
    std::uint32_t position_delta = available - keep_size_after;
    if (available < keep_size_after || position_delta == 0) {
        position_delta = available != 0 ? 1U : 0U;
    }
    return position_cap <= position_delta ? position_cap : position_delta;
}

std::uint32_t legacy_codec_match_finder_match_len_limit(
        std::uint32_t available, std::uint32_t match_max_len) {
    return available <= match_max_len ? available : match_max_len;
}

std::uint32_t legacy_codec_match_finder_hash3(const std::uint8_t *current,
        const std::uint32_t *crc_table, std::uint32_t crc_count) {
    if (current == nullptr || crc_table == nullptr ||
            crc_count <= static_cast<std::uint32_t>(current[1])) {
        return 0;
    }

    const std::uint32_t byte_pair =
            (static_cast<std::uint32_t>(current[0]) << 8U) |
            static_cast<std::uint32_t>(current[2]);
    return legacy_codec_mask16(byte_pair ^ legacy_codec_mask16(crc_table[current[1]]));
}

LegacyCodecMatchFinderHash4 legacy_codec_match_finder_hash4(
        const std::uint8_t *current, const std::uint32_t *crc_table,
        std::uint32_t crc_count, std::uint32_t hash_mask) {
    LegacyCodecMatchFinderHash4 hashes{};
    if (current == nullptr || crc_table == nullptr ||
            crc_count <= static_cast<std::uint32_t>(current[0]) ||
            crc_count <= static_cast<std::uint32_t>(current[3])) {
        return hashes;
    }

    const std::uint32_t hash2_source =
            crc_table[current[0]] ^ static_cast<std::uint32_t>(current[1]);
    const std::uint32_t hash3_source =
            hash2_source ^ (static_cast<std::uint32_t>(current[2]) << 8U);
    hashes.hash2 = legacy_codec_match_finder_hash2_mask(hash2_source);
    hashes.hash3 = legacy_codec_mask16(hash3_source) + 0x400U;
    hashes.hash4 =
            ((hash3_source ^ (crc_table[current[3]] << 5U)) & hash_mask) + 0x10400U;
    return hashes;
}

LegacyCodecMatchFinderHash4Update legacy_codec_match_finder_update_hash4_buckets(
        std::uint32_t *hash_table, std::uint32_t hash_count,
        const LegacyCodecMatchFinderHash4 &hashes, std::uint32_t position) {
    LegacyCodecMatchFinderHash4Update update{};
    if (hash_table == nullptr || hashes.hash2 >= hash_count ||
            hashes.hash3 >= hash_count || hashes.hash4 >= hash_count) {
        return update;
    }

    update.previous_hash2 = hash_table[hashes.hash2];
    update.previous_hash3 = hash_table[hashes.hash3];
    update.previous_hash4 = hash_table[hashes.hash4];
    hash_table[hashes.hash2] = position;
    hash_table[hashes.hash3] = position;
    hash_table[hashes.hash4] = position;
    update.updated = true;
    return update;
}

LegacyCodecMatchFinderHash3Masked legacy_codec_match_finder_hash3_masked(
        const std::uint8_t *current, const std::uint32_t *crc_table,
        std::uint32_t crc_count, std::uint32_t hash_mask) {
    LegacyCodecMatchFinderHash3Masked hashes{};
    if (current == nullptr || crc_table == nullptr ||
            crc_count <= static_cast<std::uint32_t>(current[0])) {
        return hashes;
    }

    const std::uint32_t hash2_source =
            crc_table[current[0]] ^ static_cast<std::uint32_t>(current[1]);
    hashes.hash2 = legacy_codec_match_finder_hash2_mask(hash2_source);
    hashes.hash3 =
            ((hash2_source ^ (static_cast<std::uint32_t>(current[2]) << 8U)) & hash_mask) +
            0x400U;
    return hashes;
}

LegacyCodecMatchFinderHash3Update legacy_codec_match_finder_update_hash3_buckets(
        std::uint32_t *hash_table, std::uint32_t hash_count,
        const LegacyCodecMatchFinderHash3Masked &hashes, std::uint32_t position) {
    LegacyCodecMatchFinderHash3Update update{};
    if (hash_table == nullptr || hashes.hash2 >= hash_count || hashes.hash3 >= hash_count) {
        return update;
    }

    update.previous_hash2 = hash_table[hashes.hash2];
    update.previous_hash3 = hash_table[hashes.hash3];
    hash_table[hashes.hash2] = position;
    hash_table[hashes.hash3] = position;
    update.updated = true;
    return update;
}

std::uint32_t legacy_codec_match_finder_hash2_direct(const std::uint8_t *current) {
    if (current == nullptr) {
        return 0;
    }
    return static_cast<std::uint32_t>(current[0]) |
            (static_cast<std::uint32_t>(current[1]) << 8U);
}

LegacyCodecMatchFinderHash2Update legacy_codec_match_finder_update_hash2_bucket(
        std::uint32_t *hash_table, std::uint32_t hash_count,
        std::uint32_t hash2, std::uint32_t position) {
    LegacyCodecMatchFinderHash2Update update{};
    if (hash_table == nullptr || hash2 >= hash_count) {
        return update;
    }

    update.previous_hash2 = hash_table[hash2];
    hash_table[hash2] = position;
    update.updated = true;
    return update;
}

std::uint32_t legacy_codec_match_finder_count_match(const std::uint8_t *current,
        std::uint32_t distance, std::uint32_t start_length, std::uint32_t max_length) {
    if (current == nullptr || distance == 0 || start_length >= max_length) {
        return start_length;
    }

    const std::uint8_t *candidate = current - distance;
    std::uint32_t length = start_length;
    while (length < max_length && candidate[length] == current[length]) {
        ++length;
    }
    return length;
}

int legacy_codec_match_finder_compare_at(const std::uint8_t *current,
        std::uint32_t distance, std::uint32_t length) {
    if (current == nullptr || distance == 0) {
        return 0;
    }

    const auto candidate_offset =
            static_cast<std::ptrdiff_t>(length) - static_cast<std::ptrdiff_t>(distance);
    const std::uint8_t candidate = *(current + candidate_offset);
    const std::uint8_t value = current[length];
    if (candidate < value) {
        return -1;
    }
    return candidate > value ? 1 : 0;
}

void legacy_codec_match_finder_update_tree_branch(
        LegacyCodecMatchFinderTreeBranch &branch, int comparison,
        std::uint32_t matched_length) {
    if (branch.candidate_links == nullptr) {
        return;
    }

    if (comparison < 0) {
        if (branch.lower_slot != nullptr) {
            *branch.lower_slot = branch.candidate_position;
        }
        branch.lower_slot = branch.candidate_links + 1;
        branch.lower_match_length = matched_length;
    } else {
        if (branch.upper_slot != nullptr) {
            *branch.upper_slot = branch.candidate_position;
        }
        branch.upper_slot = branch.candidate_links;
        branch.upper_match_length = matched_length;
    }
}

void legacy_codec_match_finder_splice_tree_children(
        std::uint32_t *lower_slot, std::uint32_t *upper_slot,
        const std::uint32_t *candidate_links) {
    if (candidate_links == nullptr) {
        return;
    }
    if (lower_slot != nullptr) {
        *lower_slot = candidate_links[0];
    }
    if (upper_slot != nullptr) {
        *upper_slot = candidate_links[1];
    }
}

std::uint32_t *legacy_codec_match_finder_tree_links_for_distance(
        std::uint32_t *tree_links, std::uint32_t link_count,
        std::uint32_t cyclic_pos, std::uint32_t cyclic_size, std::uint32_t distance) {
    if (tree_links == nullptr || !legacy_codec_match_finder_distance_in_history(
                distance, cyclic_size)) {
        return nullptr;
    }

    const std::uint32_t wrapped = legacy_codec_match_finder_wrapped_position(
            cyclic_pos, cyclic_size, distance);
    const std::uint32_t link_index = legacy_codec_match_finder_tree_link_index(wrapped);
    if (link_index >= link_count || link_count - link_index < 2U) {
        return nullptr;
    }
    return tree_links + link_index;
}

std::uint32_t *legacy_codec_match_finder_chain_link_for_distance(
        std::uint32_t *chain_links, std::uint32_t link_count,
        std::uint32_t cyclic_pos, std::uint32_t cyclic_size, std::uint32_t distance) {
    if (chain_links == nullptr || !legacy_codec_match_finder_distance_in_history(
                distance, cyclic_size)) {
        return nullptr;
    }

    const std::uint32_t wrapped = legacy_codec_match_finder_wrapped_position(
            cyclic_pos, cyclic_size, distance);
    if (wrapped >= link_count) {
        return nullptr;
    }
    return chain_links + wrapped;
}

bool legacy_codec_match_finder_distance_in_history(
        std::uint32_t distance, std::uint32_t history_size) {
    return distance != 0 && distance < history_size;
}

std::uint32_t legacy_codec_match_finder_wrapped_position(
        std::uint32_t cyclic_pos, std::uint32_t cyclic_size, std::uint32_t distance) {
    return distance <= cyclic_pos ? cyclic_pos - distance :
            cyclic_pos + cyclic_size - distance;
}

std::uint32_t legacy_codec_match_finder_tree_link_index(std::uint32_t wrapped_position) {
    return wrapped_position << 1U;
}

std::uint32_t legacy_codec_match_finder_record_count_before(
        const LegacyCodecMatchFinderMatchOutput *output) {
    return output != nullptr ? output->count : 0U;
}

std::uint32_t legacy_codec_match_finder_records_added(
        const LegacyCodecMatchFinderMatchOutput *output, std::uint32_t records_before) {
    return output != nullptr && output->count >= records_before ?
            output->count - records_before : 0U;
}

std::uint32_t legacy_codec_match_finder_next_tree_candidate(
        int comparison, const std::uint32_t *candidate_links) {
    if (candidate_links == nullptr) {
        return 0;
    }
    return comparison < 0 ? candidate_links[1] : candidate_links[0];
}

std::uint32_t legacy_codec_match_finder_start_length_at_least(
        std::uint32_t length, std::uint32_t minimum) {
    return length < minimum ? minimum : length;
}

std::ptrdiff_t legacy_codec_match_finder_candidate_start_offset(
        std::uint32_t start_length, std::uint32_t distance) {
    return static_cast<std::ptrdiff_t>(start_length) -
            static_cast<std::ptrdiff_t>(distance);
}

bool legacy_codec_match_finder_candidate_start_matches(
        const std::uint8_t *current, std::uint32_t distance,
        std::uint32_t start_length) {
    if (current == nullptr) {
        return false;
    }
    const auto candidate_start_offset =
            legacy_codec_match_finder_candidate_start_offset(start_length, distance);
    return current[candidate_start_offset] == current[start_length] &&
            current[-static_cast<std::ptrdiff_t>(distance)] == current[0];
}

std::uint32_t legacy_codec_match_finder_best_length_after_match(
        std::uint32_t best_length, std::uint32_t matched_length) {
    return std::max(best_length, matched_length);
}

LegacyCodecMatchFinderTreeProbeResult legacy_codec_match_finder_probe_tree(
        LegacyCodecMatchFinderTreeProbe &probe) {
    LegacyCodecMatchFinderTreeProbeResult result{};
    if (probe.current == nullptr || probe.distance == 0 || probe.branch == nullptr) {
        return result;
    }

    const std::uint32_t matched_length = legacy_codec_match_finder_count_match(
            probe.current, probe.distance, probe.start_length, probe.max_length);
    result.valid = true;
    result.matched_length = matched_length;

    if (probe.output != nullptr) {
        legacy_codec_match_finder_record_if_better(
                *probe.output, matched_length, probe.distance);
    }

    if (matched_length == probe.max_length) {
        legacy_codec_match_finder_splice_tree_children(
                probe.branch->lower_slot, probe.branch->upper_slot,
                probe.branch->candidate_links);
        result.terminal_match = true;
        return result;
    }

    result.comparison = legacy_codec_match_finder_compare_at(
            probe.current, probe.distance, matched_length);
    legacy_codec_match_finder_update_tree_branch(
            *probe.branch, result.comparison, matched_length);
    return result;
}

LegacyCodecMatchFinderTreeSearchResult legacy_codec_match_finder_search_binary_tree(
        LegacyCodecMatchFinderTreeSearch &search) {
    LegacyCodecMatchFinderTreeSearchResult result{};
    if (search.current == nullptr || search.tree_links == nullptr ||
            search.match_len_limit == 0 || search.initial_candidate >= search.position) {
        return result;
    }

    const std::uint32_t root_index = search.cyclic_pos << 1U;
    if (root_index >= search.tree_link_count || search.tree_link_count - root_index < 2U) {
        return result;
    }

    LegacyCodecMatchFinderTreeBranch branch{};
    branch.lower_slot = search.tree_links + root_index;
    branch.upper_slot = branch.lower_slot + 1;

    std::uint32_t candidate_position = search.initial_candidate;
    std::uint32_t remaining_depth = search.max_depth;
    const std::uint32_t records_before =
            legacy_codec_match_finder_record_count_before(search.output);
    while (remaining_depth != 0 && candidate_position < search.position) {
        const std::uint32_t distance = search.position - candidate_position;
        std::uint32_t *candidate_links = legacy_codec_match_finder_tree_links_for_distance(
                search.tree_links, search.tree_link_count, search.cyclic_pos,
                search.cyclic_size, distance);
        if (candidate_links == nullptr) {
            break;
        }

        const std::uint32_t start_length =
                std::max({search.start_length, branch.lower_match_length,
                        branch.upper_match_length});
        branch.candidate_position = candidate_position;
        branch.candidate_links = candidate_links;

        LegacyCodecMatchFinderTreeProbe probe{};
        probe.current = search.current;
        probe.distance = distance;
        probe.start_length = start_length;
        probe.max_length = search.match_len_limit;
        probe.branch = &branch;
        probe.output = search.output;
        const LegacyCodecMatchFinderTreeProbeResult probe_result =
                legacy_codec_match_finder_probe_tree(probe);
        result.best_length = legacy_codec_match_finder_best_length_after_match(
                result.best_length, probe_result.matched_length);
        if (!probe_result.valid) {
            break;
        }
        if (probe_result.terminal_match) {
            result.terminal_match = true;
            break;
        }

        candidate_position = legacy_codec_match_finder_next_tree_candidate(
                probe_result.comparison, candidate_links);
        --remaining_depth;
    }

    if (!result.terminal_match) {
        if (branch.lower_slot != nullptr) {
            *branch.lower_slot = 0;
        }
        if (branch.upper_slot != nullptr) {
            *branch.upper_slot = 0;
        }
    }
    result.records_added = legacy_codec_match_finder_records_added(
            search.output, records_before);
    return result;
}

LegacyCodecMatchFinderShortMatchResult legacy_codec_match_finder_try_short_match(
        LegacyCodecMatchFinderShortMatch &match) {
    LegacyCodecMatchFinderShortMatchResult result{};
    if (match.current == nullptr || !legacy_codec_match_finder_distance_in_history(
                match.distance, match.history_size)) {
        return result;
    }

    const std::uint8_t *candidate = match.current - match.distance;
    if (candidate[0] != match.current[0]) {
        return result;
    }

    const std::uint32_t start_length =
            match.start_length <= match.max_length ? match.start_length : match.max_length;
    result.matched = true;
    result.matched_length = legacy_codec_match_finder_count_match(
            match.current, match.distance, start_length, match.max_length);
    if (match.output != nullptr) {
        legacy_codec_match_finder_record_if_better(
                *match.output, result.matched_length, match.distance);
    }
    return result;
}

LegacyCodecMatchFinderShortMatchPairResult
legacy_codec_match_finder_try_short_match_pair(
        LegacyCodecMatchFinderShortMatchPair &match) {
    LegacyCodecMatchFinderShortMatchPairResult result{};

    LegacyCodecMatchFinderShortMatch first{};
    first.current = match.current;
    first.distance = match.first_distance;
    first.history_size = match.history_size;
    first.start_length = 2U;
    first.max_length = match.max_length;
    first.output = match.output;
    const LegacyCodecMatchFinderShortMatchResult first_result =
            legacy_codec_match_finder_try_short_match(first);
    result.first_matched = first_result.matched;
    if (first_result.matched) {
        result.best_length = first_result.matched_length;
        result.best_distance = match.first_distance;
    }

    if (match.second_distance != match.first_distance) {
        LegacyCodecMatchFinderShortMatch second{};
        second.current = match.current;
        second.distance = match.second_distance;
        second.history_size = match.history_size;
        second.start_length = 3U;
        second.max_length = match.max_length;
        second.output = match.output;
        const LegacyCodecMatchFinderShortMatchResult second_result =
                legacy_codec_match_finder_try_short_match(second);
        result.second_matched = second_result.matched;
        if (second_result.matched && result.best_length <= second_result.matched_length) {
            result.best_length = second_result.matched_length;
            result.best_distance = match.second_distance;
        }
    }
    return result;
}

bool legacy_codec_match_finder_record_if_better(
        LegacyCodecMatchFinderMatchOutput &output, std::uint32_t length,
        std::uint32_t distance) {
    if (length <= output.best_length || output.records == nullptr ||
            output.count >= output.capacity || distance == 0) {
        return false;
    }

    output.records[output.count++] = LegacyCodecMatchFinderMatchRecord{length, distance - 1U};
    output.best_length = length;
    return true;
}

void legacy_codec_match_finder_advance_position(
        LegacyCodecMatchFinderAdvanceState &state) {
    if (state.current != nullptr) {
        ++state.current;
    }
    ++state.position;
    ++state.cyclic_pos;
    if (state.position == state.position_limit && state.refresh != nullptr) {
        state.refresh(state.refresh_state);
    }
}

void legacy_codec_match_finder_hash_chain_skip(
        LegacyCodecMatchFinderHashChainSkipState &state, std::uint32_t count) {
    while (count != 0) {
        if (state.match_len_limit >= 3U) {
            const std::uint32_t hash = legacy_codec_match_finder_hash3(
                    state.current, state.crc_table, state.crc_count);
            if (state.hash_table != nullptr && hash < state.hash_count &&
                    state.chain_table != nullptr && state.cyclic_pos < state.chain_count) {
                const std::uint32_t previous = state.hash_table[hash];
                state.hash_table[hash] = state.position;
                state.chain_table[state.cyclic_pos] = previous;
            }
        }

        LegacyCodecMatchFinderAdvanceState advance{};
        advance.current = state.current;
        advance.position = state.position;
        advance.cyclic_pos = state.cyclic_pos;
        advance.position_limit = state.position_limit;
        advance.refresh = state.refresh;
        advance.refresh_state = state.refresh_state;
        legacy_codec_match_finder_advance_position(advance);
        state.current = advance.current;
        state.position = advance.position;
        state.cyclic_pos = advance.cyclic_pos;
        --count;
    }
}

LegacyCodecMatchFinderHashChainFindResult legacy_codec_match_finder_hash_chain_find(
        LegacyCodecMatchFinderHashChainFindState &state) {
    LegacyCodecMatchFinderHashChainFindResult result{};
    const std::uint32_t records_before =
            legacy_codec_match_finder_record_count_before(state.output);

    std::uint32_t candidate_position = 0;
    bool have_candidate = false;
    if (state.match_len_limit >= 3U) {
        const std::uint32_t hash = legacy_codec_match_finder_hash3(
                state.current, state.crc_table, state.crc_count);
        if (state.hash_table != nullptr && hash < state.hash_count &&
                state.chain_table != nullptr && state.cyclic_pos < state.chain_count) {
            candidate_position = state.hash_table[hash];
            state.hash_table[hash] = state.position;
            state.chain_table[state.cyclic_pos] = candidate_position;
            have_candidate = true;
        }
    }

    std::uint32_t remaining_depth = state.max_depth;
    while (have_candidate && remaining_depth != 0 && candidate_position < state.position) {
        const std::uint32_t distance = state.position - candidate_position;
        std::uint32_t *next_link = legacy_codec_match_finder_chain_link_for_distance(
                state.chain_table, state.chain_count, state.cyclic_pos,
                state.chain_count, distance);
        if (next_link == nullptr ||
                !legacy_codec_match_finder_distance_in_history(distance, state.chain_count)) {
            break;
        }

        const std::uint32_t start_length =
                legacy_codec_match_finder_start_length_at_least(result.best_length, 2U);
        if (start_length < state.match_len_limit &&
                legacy_codec_match_finder_candidate_start_matches(
                        state.current, distance, start_length)) {
            const std::uint32_t matched_length = legacy_codec_match_finder_count_match(
                    state.current, distance, 0U, state.match_len_limit);
            if (state.output != nullptr &&
                    legacy_codec_match_finder_record_if_better(
                            *state.output, matched_length, distance)) {
                result.best_length = matched_length;
            } else {
                result.best_length = legacy_codec_match_finder_best_length_after_match(
                        result.best_length, matched_length);
            }
            if (matched_length == state.match_len_limit) {
                break;
            }
        }

        candidate_position = *next_link;
        --remaining_depth;
    }

    LegacyCodecMatchFinderAdvanceState advance{};
    advance.current = state.current;
    advance.position = state.position;
    advance.cyclic_pos = state.cyclic_pos;
    advance.position_limit = state.position_limit;
    advance.refresh = state.refresh;
    advance.refresh_state = state.refresh_state;
    legacy_codec_match_finder_advance_position(advance);
    state.current = advance.current;
    state.position = advance.position;
    state.cyclic_pos = advance.cyclic_pos;

    result.records_added = legacy_codec_match_finder_records_added(
            state.output, records_before);
    return result;
}

void legacy_codec_match_finder_init(LegacyCodecMatchFinderInitState &state) {
    if (state.hash_table != nullptr && state.hash_count != 0) {
        std::fill_n(state.hash_table, state.hash_count, 0U);
    }

    state.current_position = 0;
    state.input.buffer = state.buffer;
    state.input.read_pos = state.base_position;
    state.input.write_pos = state.base_position;
    state.input.stream_end = false;
    state.input.stream_error = 0;

    legacy_codec_match_finder_fill_input(state.input);

    LegacyCodecMatchFinderLimitState limits{};
    limits.cyclic_pos = state.current_position;
    limits.cyclic_size = state.base_position;
    limits.read_pos = state.input.read_pos;
    limits.write_pos = state.input.write_pos;
    limits.keep_size_after = state.input.keep_size_after;
    limits.match_max_len = state.match_max_len;
    legacy_codec_match_finder_refresh_limits(limits);
    state.current_position = limits.cyclic_pos;
    state.match_len_limit = limits.match_len_limit;
    state.position_limit = limits.position_limit;
}

void legacy_codec_match_finder_normalize_offsets(std::uint32_t base,
        std::uint32_t *values, std::uint32_t count) {
    if (values == nullptr) {
        return;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        values[index] = legacy_codec_match_finder_normalized_offset(values[index], base);
    }
}

std::uint32_t legacy_codec_match_finder_normalized_offset(
        std::uint32_t value, std::uint32_t base) {
    return value >= base ? value - base : 0;
}

void legacy_codec_match_finder_binary_tree4_skip(
        LegacyCodecMatchFinderBinaryTree4SkipState &state, std::uint32_t count) {
    while (count != 0) {
        if (state.match_len_limit >= 4U) {
            const LegacyCodecMatchFinderHash4 hashes = legacy_codec_match_finder_hash4(
                    state.current, state.crc_table, state.crc_count, state.hash_mask);
            const LegacyCodecMatchFinderHash4Update update =
                    legacy_codec_match_finder_update_hash4_buckets(
                            state.hash_table, state.hash_count, hashes, state.position);
            if (update.updated) {
                if (state.max_depth != 0) {
                    LegacyCodecMatchFinderTreeSearch search{};
                    search.current = state.current;
                    search.position = state.position;
                    search.cyclic_pos = state.cyclic_pos;
                    search.cyclic_size = state.cyclic_size;
                    search.match_len_limit = state.match_len_limit;
                    search.max_depth = state.max_depth;
                    search.start_length = 0;
                    search.initial_candidate = update.previous_hash4;
                    search.tree_links = state.tree_links;
                    search.tree_link_count = state.tree_link_count;
                    legacy_codec_match_finder_search_binary_tree(search);
                } else if (state.tree_links != nullptr && state.cyclic_pos < state.tree_link_count) {
                    state.tree_links[state.cyclic_pos] = update.previous_hash4;
                }
            }
        }

        LegacyCodecMatchFinderAdvanceState advance{};
        advance.current = state.current;
        advance.position = state.position;
        advance.cyclic_pos = state.cyclic_pos;
        advance.position_limit = state.position_limit;
        advance.refresh = state.refresh;
        advance.refresh_state = state.refresh_state;
        legacy_codec_match_finder_advance_position(advance);
        state.current = advance.current;
        state.position = advance.position;
        state.cyclic_pos = advance.cyclic_pos;
        --count;
    }
}

void legacy_codec_match_finder_binary_tree3_skip(
        LegacyCodecMatchFinderBinaryTree3SkipState &state, std::uint32_t count) {
    while (count != 0) {
        if (state.match_len_limit >= 3U) {
            const LegacyCodecMatchFinderHash3Masked hashes =
                    legacy_codec_match_finder_hash3_masked(
                            state.current, state.crc_table, state.crc_count,
                            state.hash_mask);
            const LegacyCodecMatchFinderHash3Update update =
                    legacy_codec_match_finder_update_hash3_buckets(
                            state.hash_table, state.hash_count, hashes, state.position);
            if (update.updated) {
                if (state.max_depth != 0) {
                    LegacyCodecMatchFinderTreeSearch search{};
                    search.current = state.current;
                    search.position = state.position;
                    search.cyclic_pos = state.cyclic_pos;
                    search.cyclic_size = state.cyclic_size;
                    search.match_len_limit = state.match_len_limit;
                    search.max_depth = state.max_depth;
                    search.start_length = 0;
                    search.initial_candidate = update.previous_hash3;
                    search.tree_links = state.tree_links;
                    search.tree_link_count = state.tree_link_count;
                    legacy_codec_match_finder_search_binary_tree(search);
                } else if (state.tree_links != nullptr && state.cyclic_pos < state.tree_link_count) {
                    state.tree_links[state.cyclic_pos] = update.previous_hash3;
                }
            }
        }

        LegacyCodecMatchFinderAdvanceState advance{};
        advance.current = state.current;
        advance.position = state.position;
        advance.cyclic_pos = state.cyclic_pos;
        advance.position_limit = state.position_limit;
        advance.refresh = state.refresh;
        advance.refresh_state = state.refresh_state;
        legacy_codec_match_finder_advance_position(advance);
        state.current = advance.current;
        state.position = advance.position;
        state.cyclic_pos = advance.cyclic_pos;
        --count;
    }
}

void legacy_codec_match_finder_binary_tree2_skip(
        LegacyCodecMatchFinderBinaryTree2SkipState &state, std::uint32_t count) {
    while (count != 0) {
        if (state.match_len_limit >= 2U) {
            const std::uint32_t hash2 = legacy_codec_match_finder_hash2_direct(state.current);
            const LegacyCodecMatchFinderHash2Update update =
                    legacy_codec_match_finder_update_hash2_bucket(
                            state.hash_table, state.hash_count, hash2, state.position);
            if (update.updated) {
                if (state.max_depth != 0) {
                    LegacyCodecMatchFinderTreeSearch search{};
                    search.current = state.current;
                    search.position = state.position;
                    search.cyclic_pos = state.cyclic_pos;
                    search.cyclic_size = state.cyclic_size;
                    search.match_len_limit = state.match_len_limit;
                    search.max_depth = state.max_depth;
                    search.start_length = 0;
                    search.initial_candidate = update.previous_hash2;
                    search.tree_links = state.tree_links;
                    search.tree_link_count = state.tree_link_count;
                    legacy_codec_match_finder_search_binary_tree(search);
                } else if (state.tree_links != nullptr && state.cyclic_pos < state.tree_link_count) {
                    state.tree_links[state.cyclic_pos] = update.previous_hash2;
                }
            }
        }

        LegacyCodecMatchFinderAdvanceState advance{};
        advance.current = state.current;
        advance.position = state.position;
        advance.cyclic_pos = state.cyclic_pos;
        advance.position_limit = state.position_limit;
        advance.refresh = state.refresh;
        advance.refresh_state = state.refresh_state;
        legacy_codec_match_finder_advance_position(advance);
        state.current = advance.current;
        state.position = advance.position;
        state.cyclic_pos = advance.cyclic_pos;
        --count;
    }
}

LegacyCodecMatchFinderBinaryTreeFindResult legacy_codec_match_finder_binary_tree2_find(
        LegacyCodecMatchFinderBinaryTree2FindState &state) {
    LegacyCodecMatchFinderBinaryTreeFindResult result{};
    std::uint32_t candidate_position = 0;
    bool have_candidate = false;
    if (state.match_len_limit >= 2U) {
        const std::uint32_t hash2 = legacy_codec_match_finder_hash2_direct(state.current);
        const LegacyCodecMatchFinderHash2Update update =
                legacy_codec_match_finder_update_hash2_bucket(
                        state.hash_table, state.hash_count, hash2, state.position);
        candidate_position = update.previous_hash2;
        have_candidate = update.updated;
    }

    if (have_candidate) {
        LegacyCodecMatchFinderTreeSearch search{};
        search.current = state.current;
        search.position = state.position;
        search.cyclic_pos = state.cyclic_pos;
        search.cyclic_size = state.cyclic_size;
        search.match_len_limit = state.match_len_limit;
        search.max_depth = state.max_depth;
        search.start_length = 1U;
        search.initial_candidate = candidate_position;
        search.tree_links = state.tree_links;
        search.tree_link_count = state.tree_link_count;
        search.output = state.output;
        const LegacyCodecMatchFinderTreeSearchResult search_result =
                legacy_codec_match_finder_search_binary_tree(search);
        result.records_added = search_result.records_added;
        result.best_length = search_result.best_length;
        result.terminal_match = search_result.terminal_match;
    }

    LegacyCodecMatchFinderAdvanceState advance{};
    advance.current = state.current;
    advance.position = state.position;
    advance.cyclic_pos = state.cyclic_pos;
    advance.position_limit = state.position_limit;
    advance.refresh = state.refresh;
    advance.refresh_state = state.refresh_state;
    legacy_codec_match_finder_advance_position(advance);
    state.current = advance.current;
    state.position = advance.position;
    state.cyclic_pos = advance.cyclic_pos;
    return result;
}

LegacyCodecMatchFinderBinaryTreeFindResult legacy_codec_match_finder_binary_tree3_find(
        LegacyCodecMatchFinderBinaryTree3FindState &state) {
    LegacyCodecMatchFinderBinaryTreeFindResult result{};
    const std::uint32_t records_before =
            legacy_codec_match_finder_record_count_before(state.output);
    LegacyCodecMatchFinderHash3Update update{};
    if (state.match_len_limit >= 3U) {
        const LegacyCodecMatchFinderHash3Masked hashes =
                legacy_codec_match_finder_hash3_masked(
                        state.current, state.crc_table, state.crc_count,
                        state.hash_mask);
        update = legacy_codec_match_finder_update_hash3_buckets(
                state.hash_table, state.hash_count, hashes, state.position);
    }

    if (update.updated) {
        LegacyCodecMatchFinderShortMatch short_match{};
        short_match.current = state.current;
        short_match.distance = state.position - update.previous_hash2;
        short_match.history_size = state.cyclic_size;
        short_match.start_length = 2U;
        short_match.max_length = state.match_len_limit;
        short_match.output = state.output;
        const LegacyCodecMatchFinderShortMatchResult short_result =
                legacy_codec_match_finder_try_short_match(short_match);
        if (short_result.matched) {
            result.best_length = short_result.matched_length;
        }

        LegacyCodecMatchFinderTreeSearch search{};
        search.current = state.current;
        search.position = state.position;
        search.cyclic_pos = state.cyclic_pos;
        search.cyclic_size = state.cyclic_size;
        search.match_len_limit = state.match_len_limit;
        search.max_depth = state.max_depth;
        search.start_length = legacy_codec_match_finder_start_length_at_least(
                result.best_length, 2U);
        search.initial_candidate = update.previous_hash3;
        search.tree_links = state.tree_links;
        search.tree_link_count = state.tree_link_count;
        search.output = state.output;
        const LegacyCodecMatchFinderTreeSearchResult search_result =
                legacy_codec_match_finder_search_binary_tree(search);
        result.best_length = std::max(result.best_length, search_result.best_length);
        result.terminal_match = search_result.terminal_match;
    }

    LegacyCodecMatchFinderAdvanceState advance{};
    advance.current = state.current;
    advance.position = state.position;
    advance.cyclic_pos = state.cyclic_pos;
    advance.position_limit = state.position_limit;
    advance.refresh = state.refresh;
    advance.refresh_state = state.refresh_state;
    legacy_codec_match_finder_advance_position(advance);
    state.current = advance.current;
    state.position = advance.position;
    state.cyclic_pos = advance.cyclic_pos;

    result.records_added = legacy_codec_match_finder_records_added(
            state.output, records_before);
    return result;
}

LegacyCodecMatchFinderBinaryTreeFindResult legacy_codec_match_finder_binary_tree4_find(
        LegacyCodecMatchFinderBinaryTree4FindState &state) {
    LegacyCodecMatchFinderBinaryTreeFindResult result{};
    const std::uint32_t records_before =
            legacy_codec_match_finder_record_count_before(state.output);
    LegacyCodecMatchFinderHash4Update update{};
    if (state.match_len_limit >= 4U) {
        const LegacyCodecMatchFinderHash4 hashes = legacy_codec_match_finder_hash4(
                state.current, state.crc_table, state.crc_count, state.hash_mask);
        update = legacy_codec_match_finder_update_hash4_buckets(
                state.hash_table, state.hash_count, hashes, state.position);
    }

    if (update.updated) {
        LegacyCodecMatchFinderShortMatchPair short_match{};
        short_match.current = state.current;
        short_match.first_distance = state.position - update.previous_hash2;
        short_match.second_distance = state.position - update.previous_hash3;
        short_match.history_size = state.cyclic_size;
        short_match.max_length = state.match_len_limit;
        short_match.output = state.output;
        const LegacyCodecMatchFinderShortMatchPairResult short_result =
                legacy_codec_match_finder_try_short_match_pair(short_match);
        result.best_length = short_result.best_length;

        LegacyCodecMatchFinderTreeSearch search{};
        search.current = state.current;
        search.position = state.position;
        search.cyclic_pos = state.cyclic_pos;
        search.cyclic_size = state.cyclic_size;
        search.match_len_limit = state.match_len_limit;
        search.max_depth = state.max_depth;
        search.start_length = legacy_codec_match_finder_start_length_at_least(
                result.best_length, 3U);
        search.initial_candidate = update.previous_hash4;
        search.tree_links = state.tree_links;
        search.tree_link_count = state.tree_link_count;
        search.output = state.output;
        const LegacyCodecMatchFinderTreeSearchResult search_result =
                legacy_codec_match_finder_search_binary_tree(search);
        result.best_length = std::max(result.best_length, search_result.best_length);
        result.terminal_match = search_result.terminal_match;
    }

    LegacyCodecMatchFinderAdvanceState advance{};
    advance.current = state.current;
    advance.position = state.position;
    advance.cyclic_pos = state.cyclic_pos;
    advance.position_limit = state.position_limit;
    advance.refresh = state.refresh;
    advance.refresh_state = state.refresh_state;
    legacy_codec_match_finder_advance_position(advance);
    state.current = advance.current;
    state.position = advance.position;
    state.cyclic_pos = advance.cyclic_pos;

    result.records_added = legacy_codec_match_finder_records_added(
            state.output, records_before);
    return result;
}

LegacyCodecMatchFinderMemoryPlan legacy_codec_make_match_finder_memory_plan(
        const LegacyCodecMatchFinderMemoryPlanRequest &request) {
    LegacyCodecMatchFinderMemoryPlan plan{};
    if (!legacy_codec_match_finder_dictionary_size_supported(request.dictionary_size)) {
        return plan;
    }

    const std::uint32_t history_size =
            legacy_codec_match_finder_history_size(request.dictionary_size);
    const std::uint32_t dictionary_fraction_shift =
            legacy_codec_match_finder_dictionary_fraction_shift(request.dictionary_size);
    const std::uint32_t keep_size_after =
            legacy_codec_match_finder_keep_size_after(request);
    const std::uint32_t block_size = legacy_codec_match_finder_block_size(
            request, history_size, dictionary_fraction_shift, keep_size_after);

    std::uint32_t hash_mask = legacy_codec_match_finder_default_hash_mask();
    std::uint32_t hash_size = legacy_codec_match_finder_default_hash_size();
    std::uint32_t son_offset = 0;
    if (legacy_codec_match_finder_uses_extended_hash(request.hash_bytes)) {
        hash_mask = legacy_codec_match_finder_hash_mask(
                request.dictionary_size, request.hash_bytes);
        hash_size = legacy_codec_match_finder_hash_size(hash_mask);
        son_offset = legacy_codec_match_finder_son_offset(request.hash_bytes);
    }

    plan.valid = true;
    plan.history_size = history_size;
    plan.keep_size_before = history_size + request.keep_before;
    plan.keep_size_after = keep_size_after;
    plan.block_size = block_size;
    plan.hash_mask = hash_mask;
    plan.hash_size = hash_size;
    plan.son_offset = son_offset;
    plan.match_buffer_size = legacy_codec_match_finder_match_buffer_size(
            history_size, request.binary_tree_mode);
    return plan;
}

bool legacy_codec_match_finder_dictionary_size_supported(std::uint32_t dictionary_size) {
    return dictionary_size <= 0xc0000000U;
}

std::uint32_t legacy_codec_match_finder_history_size(std::uint32_t dictionary_size) {
    return dictionary_size + 1U;
}

std::uint32_t legacy_codec_match_finder_dictionary_fraction_shift(
        std::uint32_t dictionary_size) {
    return dictionary_size > 0x80000000U ? 2U : 1U;
}

std::uint32_t legacy_codec_match_finder_keep_size_after(
        const LegacyCodecMatchFinderMemoryPlanRequest &request) {
    return request.fast_bytes + request.match_max_len;
}

std::uint32_t legacy_codec_match_finder_block_size(
        const LegacyCodecMatchFinderMemoryPlanRequest &request,
        std::uint32_t history_size, std::uint32_t dictionary_fraction_shift,
        std::uint32_t keep_size_after) {
    return history_size + request.keep_before +
            (request.dictionary_size >> dictionary_fraction_shift) + keep_size_after +
            ((keep_size_after + request.keep_before) >> 1U) + 0x80000U;
}

std::uint32_t legacy_codec_match_finder_match_buffer_size(
        std::uint32_t history_size, bool binary_tree_mode) {
    return history_size << (binary_tree_mode ? 1U : 0U);
}

bool legacy_codec_match_finder_uses_extended_hash(std::uint32_t hash_bytes) {
    return hash_bytes != 2U;
}

std::uint32_t legacy_codec_match_finder_hash_mask(
        std::uint32_t dictionary_size, std::uint32_t hash_bytes) {
    std::uint32_t rounded = dictionary_size == 0 ? 0 : dictionary_size - 1U;
    rounded |= rounded >> 1U;
    rounded |= rounded >> 2U;
    rounded |= rounded >> 4U;
    rounded = (rounded | (rounded >> 8U)) >> 1U;
    rounded |= legacy_codec_match_finder_default_hash_mask();
    if (rounded > legacy_codec_match_finder_hash_mask_cap()) {
        return hash_bytes == 3U ? 0xffffffU : rounded >> 1U;
    }
    return rounded;
}

std::uint32_t legacy_codec_match_finder_hash_size(std::uint32_t hash_mask) {
    return hash_mask + 1U;
}

std::uint32_t legacy_codec_match_finder_son_offset(std::uint32_t hash_bytes) {
    if (hash_bytes < 3U) {
        return 0;
    }
    if (hash_bytes == 3U) {
        return 0x400U;
    }
    return hash_bytes < 5U ? 0x10400U : 0x110400U;
}

void legacy_codec_free_match_finder_window(
        const LegacyCodecMatchFinderAllocationRequest &request) {
    if (request.allocation->window_buffer != nullptr && request.free != nullptr) {
        request.free(request.allocator, request.allocation->window_buffer);
    }
    request.allocation->window_buffer = nullptr;
    request.allocation->window_size = 0;
}

void legacy_codec_free_match_finder_tables(
        const LegacyCodecMatchFinderAllocationRequest &request) {
    if (request.allocation->hash_table != nullptr && request.free != nullptr) {
        request.free(request.allocator, request.allocation->hash_table);
    }
    request.allocation->hash_table = nullptr;
    request.allocation->hash_table_words = 0;
    request.allocation->son_table = nullptr;
    request.allocation->son_table_words = 0;
}

bool legacy_codec_allocate_match_finder_memory(
        const LegacyCodecMatchFinderAllocationRequest &request) {
    if (request.plan == nullptr || request.allocation == nullptr || request.alloc == nullptr ||
            !request.plan->valid) {
        return false;
    }

    LegacyCodecMatchFinderAllocation &allocation = *request.allocation;
    const LegacyCodecMatchFinderMemoryPlan &plan = *request.plan;

    if (request.direct_input) {
        allocation.window_size = plan.block_size;
    } else if (allocation.window_buffer == nullptr || allocation.window_size != plan.block_size) {
        legacy_codec_free_match_finder_window(request);
        void *window = request.alloc(request.allocator, plan.block_size);
        if (window == nullptr) {
            legacy_codec_free_match_finder_tables(request);
            return false;
        }
        allocation.window_buffer = static_cast<std::uint8_t *>(window);
        allocation.window_size = plan.block_size;
    }

    const std::uint32_t hash_words = legacy_codec_match_finder_hash_table_words(plan);
    const std::uint32_t son_words = legacy_codec_match_finder_son_table_words(plan);
    const std::uint32_t table_words =
            legacy_codec_match_finder_total_table_words(hash_words, son_words);
    if (legacy_codec_match_finder_reuse_tables(allocation, hash_words, son_words)) {
        return true;
    }

    legacy_codec_free_match_finder_tables(request);
    void *tables = request.alloc(request.allocator,
            static_cast<std::size_t>(table_words) * sizeof(std::uint32_t));
    if (tables == nullptr) {
        if (!request.direct_input) {
            legacy_codec_free_match_finder_window(request);
        }
        return false;
    }

    allocation.hash_table = static_cast<std::uint32_t *>(tables);
    allocation.hash_table_words = hash_words;
    allocation.son_table = legacy_codec_match_finder_son_table_base(
            allocation.hash_table, hash_words);
    allocation.son_table_words = son_words;
    return true;
}

std::uint32_t legacy_codec_match_finder_hash_table_words(
        const LegacyCodecMatchFinderMemoryPlan &plan) {
    return plan.hash_size + plan.son_offset;
}

std::uint32_t legacy_codec_match_finder_son_table_words(
        const LegacyCodecMatchFinderMemoryPlan &plan) {
    return plan.match_buffer_size;
}

std::uint32_t legacy_codec_match_finder_total_table_words(
        std::uint32_t hash_words, std::uint32_t son_words) {
    return hash_words + son_words;
}

std::uint32_t *legacy_codec_match_finder_son_table_base(
        std::uint32_t *hash_table, std::uint32_t hash_words) {
    return hash_table == nullptr ? nullptr : hash_table + hash_words;
}

bool legacy_codec_match_finder_reuse_tables(
        LegacyCodecMatchFinderAllocation &allocation,
        std::uint32_t hash_words, std::uint32_t son_words) {
    const std::uint32_t table_words =
            legacy_codec_match_finder_total_table_words(hash_words, son_words);
    if (allocation.hash_table == nullptr ||
            allocation.hash_table_words + allocation.son_table_words != table_words) {
        return false;
    }
    allocation.hash_table_words = hash_words;
    allocation.son_table = legacy_codec_match_finder_son_table_base(
            allocation.hash_table, hash_words);
    allocation.son_table_words = son_words;
    return true;
}

}  // namespace kksdk

extern "C" void kksdk_legacy_codec_init_state(void *state) {
    if (state == nullptr) {
        return;
    }
    kksdk::legacy_codec_init_state(*static_cast<kksdk::LegacyCodecState *>(state));
}

extern "C" void *kksdk_legacy_codec_allocate_state(void *allocator,
        void *(*alloc_fn)(void *, unsigned long long)) {
    if (alloc_fn == nullptr) {
        return nullptr;
    }

    void *memory = alloc_fn(allocator, static_cast<unsigned long long>(sizeof(kksdk::LegacyCodecState)));
    if (memory == nullptr) {
        return nullptr;
    }
    auto *state = new (memory) kksdk::LegacyCodecState();
    kksdk::legacy_codec_init_state(*state);
    return state;
}

extern "C" void kksdk_legacy_codec_save_snapshot(const void *view) {
    if (view == nullptr) {
        return;
    }
    kksdk::legacy_codec_save_snapshot(
            *static_cast<const kksdk::LegacyCodecSnapshotView *>(view));
}

extern "C" void kksdk_legacy_codec_restore_snapshot(const void *view) {
    if (view == nullptr) {
        return;
    }
    kksdk::legacy_codec_restore_snapshot(
            *static_cast<const kksdk::LegacyCodecSnapshotView *>(view));
}

extern "C" void kksdk_legacy_codec_reset_probability_state(const void *options,
        const void *view) {
    if (options == nullptr || view == nullptr) {
        return;
    }
    kksdk::legacy_codec_reset_probability_state(
            *static_cast<const kksdk::LegacyCodecResolvedOptions *>(options),
            *static_cast<const kksdk::LegacyCodecProbabilityResetView *>(view));
}

extern "C" int kksdk_legacy_codec_run_bounded_encode(const void *request, void *result) {
    if (request == nullptr || result == nullptr) {
        return 2;
    }

    const kksdk::LegacyCodecBoundedEncodeResult run_result =
            kksdk::legacy_codec_run_bounded_encode(
                    *static_cast<const kksdk::LegacyCodecBoundedEncodeRequest *>(request));
    *static_cast<kksdk::LegacyCodecBoundedEncodeResult *>(result) = run_result;
    return run_result.status;
}

extern "C" int kksdk_legacy_codec_bounded_encode_has_required_callbacks(
        const void *request) {
    if (request == nullptr) {
        return 0;
    }
    return kksdk::legacy_codec_bounded_encode_has_required_callbacks(
            *static_cast<const kksdk::LegacyCodecBoundedEncodeRequest *>(request)) ? 1 : 0;
}

extern "C" void kksdk_legacy_codec_bounded_encode_reset_if_requested(
        const void *request) {
    if (request != nullptr &&
            kksdk::legacy_codec_bounded_encode_has_required_callbacks(
                    *static_cast<const kksdk::LegacyCodecBoundedEncodeRequest *>(request))) {
        kksdk::legacy_codec_bounded_encode_reset_if_requested(
                *static_cast<const kksdk::LegacyCodecBoundedEncodeRequest *>(request));
    }
}

extern "C" unsigned long long kksdk_legacy_codec_bounded_encode_output_position(
        const void *state, unsigned long long fallback_position) {
    if (state == nullptr) {
        return fallback_position;
    }
    return kksdk::legacy_codec_bounded_encode_output_position(
            *static_cast<const kksdk::LegacyCodecState *>(state), fallback_position);
}

extern "C" unsigned int kksdk_legacy_codec_bounded_encode_processed_position(
        const void *state, unsigned int fallback_position) {
    if (state == nullptr) {
        return fallback_position;
    }
    return kksdk::legacy_codec_bounded_encode_processed_position(
            *static_cast<const kksdk::LegacyCodecState *>(state), fallback_position);
}

extern "C" unsigned long long kksdk_legacy_codec_bounded_encode_output_delta(
        unsigned long long before, unsigned long long after) {
    return kksdk::legacy_codec_bounded_encode_output_delta(before, after);
}

extern "C" unsigned int kksdk_legacy_codec_bounded_encode_processed_delta(
        unsigned int before, unsigned int after) {
    return kksdk::legacy_codec_bounded_encode_processed_delta(before, after);
}

extern "C" int kksdk_legacy_codec_prepare_output_stream(const void *request) {
    if (request == nullptr) {
        return 2;
    }
    return kksdk::legacy_codec_prepare_output_stream(
            *static_cast<const kksdk::LegacyCodecPrepareRequest *>(request));
}

extern "C" int kksdk_legacy_codec_prepare_input_progress_stream(const void *request) {
    if (request == nullptr) {
        return 2;
    }
    return kksdk::legacy_codec_prepare_input_progress_stream(
            *static_cast<const kksdk::LegacyCodecPrepareRequest *>(request));
}

extern "C" int kksdk_legacy_codec_prepare_stream_common(const void *request) {
    if (request == nullptr) {
        return kksdk::legacy_codec_status_value(kksdk::LegacyCodecStatus::memory_error);
    }
    return kksdk::legacy_codec_prepare_stream_common(
            *static_cast<const kksdk::LegacyCodecPrepareRequest *>(request));
}

extern "C" int kksdk_legacy_codec_prepare_stream_has_state(const void *request) {
    if (request == nullptr) {
        return 0;
    }
    return kksdk::legacy_codec_prepare_stream_has_state(
            *static_cast<const kksdk::LegacyCodecPrepareRequest *>(request)) ? 1 : 0;
}

extern "C" void kksdk_legacy_codec_prepare_stream_bind_contexts(const void *request) {
    if (request != nullptr &&
            kksdk::legacy_codec_prepare_stream_has_state(
                    *static_cast<const kksdk::LegacyCodecPrepareRequest *>(request))) {
        kksdk::legacy_codec_prepare_stream_bind_contexts(
                *static_cast<const kksdk::LegacyCodecPrepareRequest *>(request));
    }
}

extern "C" void kksdk_legacy_codec_noop_callback() {
    kksdk::legacy_codec_noop_callback();
}

extern "C" int kksdk_legacy_codec_run_encode_loop(const void *request) {
    if (request == nullptr) {
        return 2;
    }
    return kksdk::legacy_codec_run_encode_loop(
            *static_cast<const kksdk::LegacyCodecEncodeLoopRequest *>(request));
}

extern "C" int kksdk_legacy_codec_encode_loop_has_required_callbacks(
        const void *request) {
    if (request == nullptr) {
        return 0;
    }
    return kksdk::legacy_codec_encode_loop_has_required_callbacks(
            *static_cast<const kksdk::LegacyCodecEncodeLoopRequest *>(request)) ? 1 : 0;
}

extern "C" int kksdk_legacy_codec_encode_loop_should_continue(const bool *finished) {
    return kksdk::legacy_codec_encode_loop_should_continue(finished) ? 1 : 0;
}

extern "C" unsigned long long kksdk_legacy_codec_encode_loop_progress_position(
        const void *state) {
    if (state == nullptr) {
        return 0;
    }
    return kksdk::legacy_codec_encode_loop_progress_position(
            *static_cast<const kksdk::LegacyCodecState *>(state));
}

extern "C" int kksdk_legacy_codec_run_owned_encode(const void *request) {
    if (request == nullptr) {
        return 2;
    }
    return kksdk::legacy_codec_run_owned_encode(
            *static_cast<const kksdk::LegacyCodecOwnedEncodeRequest *>(request));
}

extern "C" int kksdk_legacy_codec_owned_encode_has_header(const void *request) {
    if (request == nullptr) {
        return 0;
    }
    return kksdk::legacy_codec_owned_encode_has_header(
            *static_cast<const kksdk::LegacyCodecOwnedEncodeRequest *>(request)) ? 1 : 0;
}

extern "C" int kksdk_legacy_codec_owned_encode_apply_options(
        void *state, const void *options) {
    if (state == nullptr) {
        return kksdk::legacy_codec_status_value(kksdk::LegacyCodecStatus::memory_error);
    }
    return kksdk::legacy_codec_owned_encode_apply_options(
            *static_cast<kksdk::LegacyCodecState *>(state),
            static_cast<const kksdk::LegacyCodecOptions *>(options));
}

extern "C" int kksdk_legacy_codec_owned_encode_apply_header(
        void *state, const void *request) {
    if (state == nullptr || request == nullptr) {
        return kksdk::legacy_codec_status_value(kksdk::LegacyCodecStatus::memory_error);
    }
    return kksdk::legacy_codec_owned_encode_apply_header(
            *static_cast<kksdk::LegacyCodecState *>(state),
            *static_cast<const kksdk::LegacyCodecOwnedEncodeRequest *>(request));
}

extern "C" void kksdk_legacy_codec_refresh_price_caches(const void *request) {
    if (request != nullptr) {
        kksdk::legacy_codec_refresh_price_caches(
                *static_cast<const kksdk::LegacyCodecPriceCacheRefreshRequest *>(request));
    }
}

extern "C" void kksdk_legacy_codec_price_cache_refresh_distance_tables(
        const void *request) {
    if (request != nullptr) {
        kksdk::legacy_codec_price_cache_refresh_distance_tables(
                *static_cast<const kksdk::LegacyCodecPriceCacheRefreshRequest *>(request));
    }
}

extern "C" void kksdk_legacy_codec_price_cache_refresh_optional(
        void (*refresh)(void *), void *context) {
    kksdk::legacy_codec_price_cache_refresh_optional(refresh, context);
}

extern "C" unsigned int kksdk_legacy_codec_price_cache_max_len_price(
        unsigned int fast_bytes) {
    return kksdk::legacy_codec_price_cache_max_len_price(fast_bytes);
}

extern "C" unsigned int kksdk_legacy_codec_price_cache_pos_state_count(
        unsigned int pos_state_bits) {
    return kksdk::legacy_codec_price_cache_pos_state_count(pos_state_bits);
}

extern "C" void kksdk_legacy_codec_price_cache_store_limit(
        unsigned int *limit_slot, unsigned int limit) {
    kksdk::legacy_codec_price_cache_store_limit(limit_slot, limit);
}

extern "C" int kksdk_legacy_codec_run_header_decode(const void *request) {
    if (request == nullptr) {
        return 2;
    }
    return kksdk::legacy_codec_run_header_decode(
            *static_cast<const kksdk::LegacyCodecHeaderDecodeRequest *>(request));
}

extern "C" int kksdk_legacy_codec_header_decode_result_from_status(
        int core_result, int decode_status) {
    return kksdk::legacy_codec_header_decode_result_from_status(
            core_result, decode_status);
}

extern "C" int kksdk_legacy_codec_header_decode_has_minimum_input(
        const unsigned long long *input_size) {
    return kksdk::legacy_codec_header_decode_has_minimum_input(input_size) ? 1 : 0;
}

extern "C" void kksdk_legacy_codec_header_decode_reset_input_size(
        unsigned long long *input_size) {
    kksdk::legacy_codec_header_decode_reset_input_size(input_size);
}

extern "C" void *kksdk_legacy_codec_header_decode_initial_output(
        void **output_position) {
    return kksdk::legacy_codec_header_decode_initial_output(output_position);
}

extern "C" void kksdk_legacy_codec_header_decode_store_output(
        void **output_position, void *decoded_output_position) {
    kksdk::legacy_codec_header_decode_store_output(
            output_position, decoded_output_position);
}

extern "C" int *kksdk_legacy_codec_header_decode_status_slot(
        int *request_decode_status, int *local_decode_status) {
    return kksdk::legacy_codec_header_decode_status_slot(
            request_decode_status, local_decode_status);
}

extern "C" int kksdk_legacy_codec_header_decode_final_status(
        const int *request_decode_status, int local_decode_status) {
    return kksdk::legacy_codec_header_decode_final_status(
            request_decode_status, local_decode_status);
}

extern "C" int kksdk_legacy_codec_status_value(int status) {
    return kksdk::legacy_codec_status_value(static_cast<kksdk::LegacyCodecStatus>(status));
}

extern "C" int kksdk_legacy_codec_decode_status_value(int status) {
    return kksdk::legacy_codec_decode_status_value(
            static_cast<kksdk::LegacyCodecDecodeStatus>(status));
}

extern "C" int kksdk_legacy_codec_run_streaming_decode(const void *request) {
    if (request == nullptr) {
        return 2;
    }
    return kksdk::legacy_codec_run_streaming_decode(
            *static_cast<const kksdk::LegacyCodecStreamingDecodeRequest *>(request));
}

extern "C" unsigned long long kksdk_legacy_codec_streaming_output_limit(
        unsigned long long read_position, unsigned long long write_position,
        unsigned long long output_remaining) {
    return kksdk::legacy_codec_streaming_output_limit(
            read_position, write_position, output_remaining);
}

extern "C" unsigned long long kksdk_legacy_codec_streaming_produced_size(
        unsigned long long read_before, unsigned long long read_after) {
    return kksdk::legacy_codec_streaming_produced_size(read_before, read_after);
}

extern "C" int kksdk_legacy_codec_streaming_finish_mode(
        unsigned long long output_limit, unsigned long long read_position,
        unsigned long long output_remaining, int requested_finish_mode) {
    return kksdk::legacy_codec_streaming_finish_mode(
            output_limit, read_position, output_remaining, requested_finish_mode);
}

extern "C" int kksdk_legacy_codec_streaming_decode_finished(
        int status, unsigned long long produced_size) {
    return kksdk::legacy_codec_streaming_decode_finished(status, produced_size) ? 1 : 0;
}

extern "C" unsigned long long kksdk_legacy_codec_streaming_remaining_after(
        unsigned long long remaining, unsigned long long consumed) {
    return kksdk::legacy_codec_streaming_remaining_after(remaining, consumed);
}

extern "C" unsigned long long kksdk_legacy_codec_range_decoder_initial_limit() {
    return kksdk::legacy_codec_range_decoder_initial_limit();
}

extern "C" void kksdk_legacy_codec_prepare_decoder_range(
        void *state, int reset_buffered_input, int finish_input) {
    if (state != nullptr) {
        kksdk::legacy_codec_prepare_decoder_range(
                *static_cast<kksdk::LegacyCodecDecoderRangeState *>(state),
                reset_buffered_input != 0, finish_input != 0);
    }
}

extern "C" void kksdk_legacy_codec_reset_decoder_range(void *state) {
    if (state != nullptr) {
        kksdk::legacy_codec_reset_decoder_range(
                *static_cast<kksdk::LegacyCodecDecoderRangeState *>(state));
    }
}

extern "C" int kksdk_legacy_codec_range_decoder_normalize(void *cursor) {
    if (cursor == nullptr) {
        return 0;
    }
    return kksdk::legacy_codec_range_decoder_normalize(
            *static_cast<kksdk::LegacyCodecRangeDecoderCursor *>(cursor)) ? 1 : 0;
}

extern "C" int kksdk_legacy_codec_range_decode_bit(void *cursor, void *probability,
        void *out_result) {
    if (cursor == nullptr || probability == nullptr || out_result == nullptr) {
        return 0;
    }
    auto &typed_probability = *static_cast<std::uint16_t *>(probability);
    const kksdk::LegacyCodecRangeBitResult result =
            kksdk::legacy_codec_range_decode_bit(
                    *static_cast<kksdk::LegacyCodecRangeDecoderCursor *>(cursor),
                    typed_probability);
    *static_cast<kksdk::LegacyCodecRangeBitResult *>(out_result) = result;
    return result.ok ? 1 : 0;
}

extern "C" int kksdk_legacy_codec_range_decode_bit_tree(void *cursor,
        void *probabilities, unsigned int symbol_limit, void *out_result) {
    if (cursor == nullptr || probabilities == nullptr || out_result == nullptr) {
        return 0;
    }
    const kksdk::LegacyCodecRangeBitTreeResult result =
            kksdk::legacy_codec_range_decode_bit_tree(
                    *static_cast<kksdk::LegacyCodecRangeDecoderCursor *>(cursor),
                    static_cast<std::uint16_t *>(probabilities),
                    static_cast<std::uint32_t>(symbol_limit));
    *static_cast<kksdk::LegacyCodecRangeBitTreeResult *>(out_result) = result;
    return result.ok ? 1 : 0;
}

extern "C" int kksdk_legacy_codec_range_decode_reverse_bit_tree(void *cursor,
        void *probabilities, unsigned int bit_count, void *out_result) {
    if (cursor == nullptr || probabilities == nullptr || out_result == nullptr) {
        return 0;
    }
    const kksdk::LegacyCodecRangeBitTreeResult result =
            kksdk::legacy_codec_range_decode_reverse_bit_tree(
                    *static_cast<kksdk::LegacyCodecRangeDecoderCursor *>(cursor),
                    static_cast<std::uint16_t *>(probabilities),
                    static_cast<std::uint32_t>(bit_count));
    *static_cast<kksdk::LegacyCodecRangeBitTreeResult *>(out_result) = result;
    return result.ok ? 1 : 0;
}

extern "C" int kksdk_legacy_codec_range_decode_direct_bits(void *cursor,
        unsigned int bit_count, void *out_result) {
    if (cursor == nullptr || out_result == nullptr) {
        return 0;
    }
    const kksdk::LegacyCodecRangeBitTreeResult result =
            kksdk::legacy_codec_range_decode_direct_bits(
                    *static_cast<kksdk::LegacyCodecRangeDecoderCursor *>(cursor),
                    static_cast<std::uint32_t>(bit_count));
    *static_cast<kksdk::LegacyCodecRangeBitTreeResult *>(out_result) = result;
    return result.ok ? 1 : 0;
}

extern "C" int kksdk_legacy_codec_range_decode_length(void *cursor,
        void *probabilities, unsigned int pos_state, void *out_result) {
    if (cursor == nullptr || out_result == nullptr) {
        return 0;
    }
    const kksdk::LegacyCodecRangeLengthResult result =
            kksdk::legacy_codec_range_decode_length(
                    *static_cast<kksdk::LegacyCodecRangeDecoderCursor *>(cursor),
                    static_cast<std::uint16_t *>(probabilities),
                    static_cast<std::uint32_t>(pos_state));
    *static_cast<kksdk::LegacyCodecRangeLengthResult *>(out_result) = result;
    return result.ok ? 1 : 0;
}

extern "C" int kksdk_legacy_codec_range_decode_pos_slot(void *cursor,
        void *probabilities, unsigned int length_symbol, void *out_result) {
    if (cursor == nullptr || out_result == nullptr) {
        return 0;
    }
    const kksdk::LegacyCodecRangeBitTreeResult result =
            kksdk::legacy_codec_range_decode_pos_slot(
                    *static_cast<kksdk::LegacyCodecRangeDecoderCursor *>(cursor),
                    static_cast<std::uint16_t *>(probabilities),
                    static_cast<std::uint32_t>(length_symbol));
    *static_cast<kksdk::LegacyCodecRangeBitTreeResult *>(out_result) = result;
    return result.ok ? 1 : 0;
}

extern "C" int kksdk_legacy_codec_range_decode_reverse_distance(void *cursor,
        void *probabilities, unsigned int pos_slot, void *out_result) {
    if (cursor == nullptr || out_result == nullptr) {
        return 0;
    }
    const kksdk::LegacyCodecRangeDistanceResult result =
            kksdk::legacy_codec_range_decode_reverse_distance(
                    *static_cast<kksdk::LegacyCodecRangeDecoderCursor *>(cursor),
                    static_cast<std::uint16_t *>(probabilities),
                    static_cast<std::uint32_t>(pos_slot));
    *static_cast<kksdk::LegacyCodecRangeDistanceResult *>(out_result) = result;
    return result.ok ? 1 : 0;
}

extern "C" int kksdk_legacy_codec_range_decode_direct_distance(void *cursor,
        void *align_probabilities, unsigned int pos_slot, void *out_result) {
    if (cursor == nullptr || out_result == nullptr) {
        return 0;
    }
    const kksdk::LegacyCodecRangeDistanceResult result =
            kksdk::legacy_codec_range_decode_direct_distance(
                    *static_cast<kksdk::LegacyCodecRangeDecoderCursor *>(cursor),
                    static_cast<std::uint16_t *>(align_probabilities),
                    static_cast<std::uint32_t>(pos_slot));
    *static_cast<kksdk::LegacyCodecRangeDistanceResult *>(out_result) = result;
    return result.ok ? 1 : 0;
}

extern "C" int kksdk_legacy_codec_range_decode_literal(void *cursor,
        const void *request, void *out_result) {
    if (cursor == nullptr || request == nullptr || out_result == nullptr) {
        return 0;
    }
    const kksdk::LegacyCodecRangeLiteralResult result =
            kksdk::legacy_codec_range_decode_literal(
                    *static_cast<kksdk::LegacyCodecRangeDecoderCursor *>(cursor),
                    *static_cast<const kksdk::LegacyCodecRangeLiteralRequest *>(request));
    *static_cast<kksdk::LegacyCodecRangeLiteralResult *>(out_result) = result;
    return result.ok ? 1 : 0;
}

extern "C" int kksdk_legacy_codec_copy_match_to_output_window(
        const void *request, void *out_result) {
    if (request == nullptr || out_result == nullptr) {
        return 0;
    }
    const kksdk::LegacyCodecOutputWindowCopyResult result =
            kksdk::legacy_codec_copy_match_to_output_window(
                    *static_cast<const kksdk::LegacyCodecOutputWindowCopyRequest *>(request));
    *static_cast<kksdk::LegacyCodecOutputWindowCopyResult *>(out_result) = result;
    return result.copied ? 1 : 0;
}

extern "C" int kksdk_legacy_codec_copy_pending_match_to_output_window(
        const void *request, void *out_result) {
    if (request == nullptr || out_result == nullptr) {
        return 0;
    }
    const kksdk::LegacyCodecOutputWindowCopyResult result =
            kksdk::legacy_codec_copy_pending_match_to_output_window(
                    *static_cast<const kksdk::LegacyCodecPendingMatchCopyRequest *>(request));
    *static_cast<kksdk::LegacyCodecOutputWindowCopyResult *>(out_result) = result;
    return result.copied ? 1 : 0;
}

extern "C" unsigned int kksdk_legacy_codec_output_window_copy_length(
        unsigned long long position, unsigned long long output_limit,
        unsigned int requested_length) {
    return kksdk::legacy_codec_output_window_copy_length(
            static_cast<std::uint64_t>(position),
            static_cast<std::uint64_t>(output_limit),
            static_cast<std::uint32_t>(requested_length));
}

extern "C" unsigned long long kksdk_legacy_codec_output_window_source_position(
        unsigned long long position, unsigned int cyclic_size, unsigned int distance) {
    return kksdk::legacy_codec_output_window_source_position(
            static_cast<std::uint64_t>(position),
            static_cast<std::uint32_t>(cyclic_size),
            static_cast<std::uint32_t>(distance));
}

extern "C" int kksdk_legacy_codec_output_window_byte_at_distance(
        const void *request, void *out_result) {
    if (request == nullptr || out_result == nullptr) {
        return 0;
    }
    const kksdk::LegacyCodecOutputWindowByteResult result =
            kksdk::legacy_codec_output_window_byte_at_distance(
                    *static_cast<const kksdk::LegacyCodecOutputWindowByteRequest *>(request));
    *static_cast<kksdk::LegacyCodecOutputWindowByteResult *>(out_result) = result;
    return result.ok ? 1 : 0;
}

extern "C" int kksdk_legacy_codec_output_window_previous_byte(
        const unsigned char *buffer, unsigned long long position,
        unsigned int cyclic_size, void *out_result) {
    if (out_result == nullptr) {
        return 0;
    }
    const kksdk::LegacyCodecOutputWindowByteResult result =
            kksdk::legacy_codec_output_window_previous_byte(
                    buffer,
                    static_cast<std::uint64_t>(position),
                    static_cast<std::uint32_t>(cyclic_size));
    *static_cast<kksdk::LegacyCodecOutputWindowByteResult *>(out_result) = result;
    return result.ok ? 1 : 0;
}

extern "C" int kksdk_legacy_codec_write_byte_to_output_window(
        const void *request, void *out_result) {
    if (request == nullptr || out_result == nullptr) {
        return 0;
    }
    const kksdk::LegacyCodecOutputWindowWriteByteResult result =
            kksdk::legacy_codec_write_byte_to_output_window(
                    *static_cast<const kksdk::LegacyCodecOutputWindowWriteByteRequest *>(request));
    *static_cast<kksdk::LegacyCodecOutputWindowWriteByteResult *>(out_result) = result;
    return result.written ? 1 : 0;
}

extern "C" int kksdk_legacy_codec_distance_slot_info(
        unsigned int pos_slot, void *out_result) {
    if (out_result == nullptr) {
        return 0;
    }
    const kksdk::LegacyCodecDistanceSlotInfo result =
            kksdk::legacy_codec_distance_slot_info(static_cast<std::uint32_t>(pos_slot));
    *static_cast<kksdk::LegacyCodecDistanceSlotInfo *>(out_result) = result;
    return result.valid ? 1 : 0;
}

extern "C" unsigned int kksdk_legacy_codec_decoder_position_state(
        unsigned int processed_size, unsigned int position_bits) {
    return kksdk::legacy_codec_decoder_position_state(
            static_cast<std::uint32_t>(processed_size),
            static_cast<std::uint32_t>(position_bits));
}

extern "C" unsigned long long kksdk_legacy_codec_decoder_iteration_output_limit(
        const void *request) {
    if (request == nullptr) {
        return 0;
    }
    return kksdk::legacy_codec_decoder_iteration_output_limit(
            *static_cast<const kksdk::LegacyCodecDecoderOutputLimitRequest *>(request));
}

extern "C" int kksdk_legacy_codec_decoder_should_save_iteration(
        const unsigned char *input, const unsigned char *input_end,
        unsigned long long position, unsigned long long output_limit) {
    return kksdk::legacy_codec_decoder_should_save_iteration(
            input, input_end, static_cast<std::uint64_t>(position),
            static_cast<std::uint64_t>(output_limit)) ? 1 : 0;
}

extern "C" int kksdk_legacy_codec_decoder_has_history(
        unsigned int processed_size, unsigned int pending_limit) {
    return kksdk::legacy_codec_decoder_has_history(
            static_cast<std::uint32_t>(processed_size),
            static_cast<std::uint32_t>(pending_limit)) ? 1 : 0;
}

extern "C" int kksdk_legacy_codec_decoder_distance_available(
        unsigned int distance_minus_one, unsigned int processed_size,
        unsigned int pending_limit) {
    return kksdk::legacy_codec_decoder_distance_available(
            static_cast<std::uint32_t>(distance_minus_one),
            static_cast<std::uint32_t>(processed_size),
            static_cast<std::uint32_t>(pending_limit)) ? 1 : 0;
}

extern "C" unsigned int kksdk_legacy_codec_decoder_match_length_from_symbol(
        unsigned int length_symbol) {
    return kksdk::legacy_codec_decoder_match_length_from_symbol(
            static_cast<std::uint32_t>(length_symbol));
}

extern "C" int kksdk_legacy_codec_decoder_is_end_marker_distance(
        unsigned int distance_minus_one) {
    return kksdk::legacy_codec_decoder_is_end_marker_distance(
            static_cast<std::uint32_t>(distance_minus_one)) ? 1 : 0;
}

extern "C" unsigned int kksdk_legacy_codec_clamp_pending_match_length(
        unsigned int length) {
    return kksdk::legacy_codec_clamp_pending_match_length(
            static_cast<std::uint32_t>(length));
}

extern "C" int kksdk_legacy_codec_decoder_has_pending_match(unsigned int length) {
    return kksdk::legacy_codec_decoder_has_pending_match(
            static_cast<std::uint32_t>(length)) ? 1 : 0;
}

extern "C" void kksdk_legacy_codec_reps_after_new_match(
        void *reps, unsigned int distance) {
    if (reps != nullptr) {
        kksdk::legacy_codec_reps_after_new_match(
                *static_cast<kksdk::LegacyCodecRepDistances *>(reps),
                static_cast<std::uint32_t>(distance));
    }
}

extern "C" unsigned int kksdk_legacy_codec_reps_after_repeated_match(
        void *reps, unsigned int rep_index) {
    if (reps == nullptr) {
        return 0;
    }
    return kksdk::legacy_codec_reps_after_repeated_match(
            *static_cast<kksdk::LegacyCodecRepDistances *>(reps),
            static_cast<std::uint32_t>(rep_index));
}

extern "C" unsigned int kksdk_legacy_codec_state_after_literal(unsigned int state) {
    return kksdk::legacy_codec_state_after_literal(static_cast<std::uint32_t>(state));
}

extern "C" unsigned int kksdk_legacy_codec_state_after_match(unsigned int state) {
    return kksdk::legacy_codec_state_after_match(static_cast<std::uint32_t>(state));
}

extern "C" unsigned int kksdk_legacy_codec_state_after_rep(unsigned int state) {
    return kksdk::legacy_codec_state_after_rep(static_cast<std::uint32_t>(state));
}

extern "C" unsigned int kksdk_legacy_codec_state_after_short_rep(unsigned int state) {
    return kksdk::legacy_codec_state_after_short_rep(static_cast<std::uint32_t>(state));
}

extern "C" unsigned int kksdk_legacy_codec_literal_context_index(
        unsigned long long processed_position, unsigned char previous_byte,
        unsigned int lc, unsigned int lp) {
    return kksdk::legacy_codec_literal_context_index(
            static_cast<std::uint64_t>(processed_position),
            static_cast<std::uint8_t>(previous_byte),
            static_cast<std::uint32_t>(lc),
            static_cast<std::uint32_t>(lp));
}

extern "C" void *kksdk_legacy_codec_literal_probabilities_for_context(
        void *literal_probabilities, unsigned int context_index) {
    return kksdk::legacy_codec_literal_probabilities_for_context(
            static_cast<std::uint16_t *>(literal_probabilities),
            static_cast<std::uint32_t>(context_index));
}

extern "C" void *kksdk_legacy_codec_match_probability_for_state(
        void *probabilities, unsigned int state, unsigned int pos_state) {
    return kksdk::legacy_codec_match_probability_for_state(
            static_cast<std::uint16_t *>(probabilities),
            static_cast<std::uint32_t>(state),
            static_cast<std::uint32_t>(pos_state));
}

extern "C" void *kksdk_legacy_codec_state_probability_at(
        void *probabilities, unsigned int base_offset, unsigned int state) {
    return kksdk::legacy_codec_state_probability_at(
            static_cast<std::uint16_t *>(probabilities),
            static_cast<std::uint32_t>(base_offset),
            static_cast<std::uint32_t>(state));
}

extern "C" void *kksdk_legacy_codec_state_pos_probability_at(
        void *probabilities, unsigned int base_offset,
        unsigned int state, unsigned int pos_state) {
    return kksdk::legacy_codec_state_pos_probability_at(
            static_cast<std::uint16_t *>(probabilities),
            static_cast<std::uint32_t>(base_offset),
            static_cast<std::uint32_t>(state),
            static_cast<std::uint32_t>(pos_state));
}

extern "C" unsigned int kksdk_legacy_codec_probability_bank_offset(unsigned int bank) {
    return kksdk::legacy_codec_probability_bank_offset(
            static_cast<kksdk::LegacyCodecProbabilityBank>(bank));
}

extern "C" void *kksdk_legacy_codec_probability_at_bank(
        void *probabilities, unsigned int bank, unsigned int state) {
    return kksdk::legacy_codec_probability_at_bank(
            static_cast<std::uint16_t *>(probabilities),
            static_cast<kksdk::LegacyCodecProbabilityBank>(bank),
            static_cast<std::uint32_t>(state));
}

extern "C" void *kksdk_legacy_codec_pos_probability_at_bank(
        void *probabilities, unsigned int bank, unsigned int state, unsigned int pos_state) {
    return kksdk::legacy_codec_pos_probability_at_bank(
            static_cast<std::uint16_t *>(probabilities),
            static_cast<kksdk::LegacyCodecProbabilityBank>(bank),
            static_cast<std::uint32_t>(state),
            static_cast<std::uint32_t>(pos_state));
}

extern "C" void *kksdk_legacy_codec_is_rep_probability(
        void *probabilities, unsigned int state) {
    return kksdk::legacy_codec_is_rep_probability(
            static_cast<std::uint16_t *>(probabilities), static_cast<std::uint32_t>(state));
}

extern "C" void *kksdk_legacy_codec_is_rep_g0_probability(
        void *probabilities, unsigned int state) {
    return kksdk::legacy_codec_is_rep_g0_probability(
            static_cast<std::uint16_t *>(probabilities), static_cast<std::uint32_t>(state));
}

extern "C" void *kksdk_legacy_codec_is_rep_g1_probability(
        void *probabilities, unsigned int state) {
    return kksdk::legacy_codec_is_rep_g1_probability(
            static_cast<std::uint16_t *>(probabilities), static_cast<std::uint32_t>(state));
}

extern "C" void *kksdk_legacy_codec_is_rep_g2_probability(
        void *probabilities, unsigned int state) {
    return kksdk::legacy_codec_is_rep_g2_probability(
            static_cast<std::uint16_t *>(probabilities), static_cast<std::uint32_t>(state));
}

extern "C" void *kksdk_legacy_codec_is_rep0_long_probability(
        void *probabilities, unsigned int state, unsigned int pos_state) {
    return kksdk::legacy_codec_is_rep0_long_probability(
            static_cast<std::uint16_t *>(probabilities),
            static_cast<std::uint32_t>(state),
            static_cast<std::uint32_t>(pos_state));
}

extern "C" void *kksdk_legacy_codec_pos_slot_probabilities(void *probabilities) {
    return kksdk::legacy_codec_pos_slot_probabilities(
            static_cast<std::uint16_t *>(probabilities));
}

extern "C" void *kksdk_legacy_codec_align_probabilities(void *probabilities) {
    return kksdk::legacy_codec_align_probabilities(
            static_cast<std::uint16_t *>(probabilities));
}

extern "C" void *kksdk_legacy_codec_len_probabilities(void *probabilities) {
    return kksdk::legacy_codec_len_probabilities(
            static_cast<std::uint16_t *>(probabilities));
}

extern "C" void *kksdk_legacy_codec_rep_len_probabilities(void *probabilities) {
    return kksdk::legacy_codec_rep_len_probabilities(
            static_cast<std::uint16_t *>(probabilities));
}

extern "C" void *kksdk_legacy_codec_literal_probabilities(void *probabilities) {
    return kksdk::legacy_codec_literal_probabilities(
            static_cast<std::uint16_t *>(probabilities));
}

extern "C" void kksdk_legacy_codec_select_match_finder_callbacks(
        const void *selection, void *out_callbacks) {
    if (selection == nullptr || out_callbacks == nullptr) {
        return;
    }
    *static_cast<kksdk::LegacyCodecMatchFinderCallbacks *>(out_callbacks) =
            kksdk::legacy_codec_select_match_finder_callbacks(
                    *static_cast<const kksdk::LegacyCodecMatchFinderSelection *>(selection));
}

extern "C" unsigned char kksdk_legacy_codec_match_finder_get_byte(
        const void *view, int offset) {
    if (view == nullptr) {
        return 0;
    }
    return kksdk::legacy_codec_match_finder_get_byte(
            *static_cast<const kksdk::LegacyCodecMatchFinderView *>(view), offset);
}

extern "C" unsigned int kksdk_legacy_codec_match_finder_available_bytes(const void *view) {
    if (view == nullptr) {
        return 0;
    }
    return kksdk::legacy_codec_match_finder_available_bytes(
            *static_cast<const kksdk::LegacyCodecMatchFinderView *>(view));
}

extern "C" const unsigned char *kksdk_legacy_codec_match_finder_current_pointer(
        const void *view) {
    if (view == nullptr) {
        return nullptr;
    }
    return kksdk::legacy_codec_match_finder_current_pointer(
            *static_cast<const kksdk::LegacyCodecMatchFinderView *>(view));
}

extern "C" void kksdk_legacy_codec_match_finder_rewind_cursor(void *cursor,
        unsigned int amount) {
    if (cursor != nullptr) {
        kksdk::legacy_codec_match_finder_rewind_cursor(
                *static_cast<kksdk::LegacyCodecMatchFinderCursor *>(cursor), amount);
    }
}

extern "C" void kksdk_legacy_codec_match_finder_move_window(void *window) {
    if (window != nullptr) {
        kksdk::legacy_codec_match_finder_move_window(
                *static_cast<kksdk::LegacyCodecMatchFinderWindow *>(window));
    }
}

extern "C" int kksdk_legacy_codec_match_finder_should_move_window(const void *window) {
    if (window == nullptr) {
        return 0;
    }
    return kksdk::legacy_codec_match_finder_should_move_window(
            *static_cast<const kksdk::LegacyCodecMatchFinderWindow *>(window)) ? 1 : 0;
}

extern "C" int kksdk_legacy_codec_match_finder_should_fill_input(const void *state) {
    if (state == nullptr) {
        return 0;
    }
    return kksdk::legacy_codec_match_finder_should_fill_input(
            *static_cast<const kksdk::LegacyCodecMatchFinderFillState *>(state)) ? 1 : 0;
}

extern "C" unsigned int kksdk_legacy_codec_match_finder_position_cap(
        const void *state) {
    if (state == nullptr) {
        return 0;
    }
    return kksdk::legacy_codec_match_finder_position_cap(
            *static_cast<const kksdk::LegacyCodecMatchFinderLimitState *>(state));
}

extern "C" unsigned int kksdk_legacy_codec_match_finder_limit_available(
        const void *state) {
    if (state == nullptr) {
        return 0;
    }
    return kksdk::legacy_codec_match_finder_limit_available(
            *static_cast<const kksdk::LegacyCodecMatchFinderLimitState *>(state));
}

extern "C" unsigned int kksdk_legacy_codec_match_finder_position_delta(
        unsigned int available, unsigned int keep_size_after, unsigned int position_cap) {
    return kksdk::legacy_codec_match_finder_position_delta(
            available, keep_size_after, position_cap);
}

extern "C" unsigned int kksdk_legacy_codec_match_finder_match_len_limit(
        unsigned int available, unsigned int match_max_len) {
    return kksdk::legacy_codec_match_finder_match_len_limit(available, match_max_len);
}

extern "C" void kksdk_legacy_codec_match_finder_fill_input(void *state) {
    if (state != nullptr) {
        kksdk::legacy_codec_match_finder_fill_input(
                *static_cast<kksdk::LegacyCodecMatchFinderInputBuffer *>(state));
    }
}

extern "C" unsigned int kksdk_legacy_codec_match_finder_direct_input_writable(
        const void *state) {
    if (state == nullptr) {
        return 0;
    }
    return kksdk::legacy_codec_match_finder_direct_input_writable(
            *static_cast<const kksdk::LegacyCodecMatchFinderInputBuffer *>(state));
}

extern "C" unsigned int kksdk_legacy_codec_match_finder_direct_input_consumed(
        const void *state) {
    if (state == nullptr) {
        return 0;
    }
    return kksdk::legacy_codec_match_finder_direct_input_consumed(
            *static_cast<const kksdk::LegacyCodecMatchFinderInputBuffer *>(state));
}

extern "C" unsigned int kksdk_legacy_codec_match_finder_input_buffered_size(
        const void *state) {
    if (state == nullptr) {
        return 0;
    }
    return kksdk::legacy_codec_match_finder_input_buffered_size(
            *static_cast<const kksdk::LegacyCodecMatchFinderInputBuffer *>(state));
}

extern "C" unsigned long long kksdk_legacy_codec_match_finder_input_target_offset(
        const void *state) {
    if (state == nullptr) {
        return 0;
    }
    return kksdk::legacy_codec_match_finder_input_target_offset(
            *static_cast<const kksdk::LegacyCodecMatchFinderInputBuffer *>(state));
}

extern "C" unsigned long long kksdk_legacy_codec_match_finder_input_read_size(
        unsigned int buffered, unsigned long long end_offset) {
    return kksdk::legacy_codec_match_finder_input_read_size(buffered, end_offset);
}

extern "C" void kksdk_legacy_codec_match_finder_refresh_limits(void *state) {
    if (state != nullptr) {
        kksdk::legacy_codec_match_finder_refresh_limits(
                *static_cast<kksdk::LegacyCodecMatchFinderLimitState *>(state));
    }
}

extern "C" unsigned int kksdk_legacy_codec_match_finder_hash3(
        const unsigned char *current, const unsigned int *crc_table, unsigned int crc_count) {
    return kksdk::legacy_codec_match_finder_hash3(
            current, crc_table, static_cast<std::uint32_t>(crc_count));
}

extern "C" int kksdk_legacy_codec_match_finder_hash4(
        const unsigned char *current, const unsigned int *crc_table, unsigned int crc_count,
        unsigned int hash_mask, void *out_hashes) {
    if (out_hashes == nullptr) {
        return 0;
    }
    *static_cast<kksdk::LegacyCodecMatchFinderHash4 *>(out_hashes) =
            kksdk::legacy_codec_match_finder_hash4(current, crc_table,
                    static_cast<std::uint32_t>(crc_count),
                    static_cast<std::uint32_t>(hash_mask));
    return 1;
}

extern "C" int kksdk_legacy_codec_match_finder_update_hash4_buckets(
        void *hash_table, unsigned int hash_count, const void *hashes,
        unsigned int position, void *out_update) {
    if (hashes == nullptr || out_update == nullptr) {
        return 0;
    }
    const kksdk::LegacyCodecMatchFinderHash4Update update =
            kksdk::legacy_codec_match_finder_update_hash4_buckets(
                    static_cast<std::uint32_t *>(hash_table),
                    static_cast<std::uint32_t>(hash_count),
                    *static_cast<const kksdk::LegacyCodecMatchFinderHash4 *>(hashes),
                    static_cast<std::uint32_t>(position));
    *static_cast<kksdk::LegacyCodecMatchFinderHash4Update *>(out_update) = update;
    return update.updated ? 1 : 0;
}

extern "C" int kksdk_legacy_codec_match_finder_hash3_masked(
        const unsigned char *current, const unsigned int *crc_table, unsigned int crc_count,
        unsigned int hash_mask, void *out_hashes) {
    if (out_hashes == nullptr) {
        return 0;
    }
    *static_cast<kksdk::LegacyCodecMatchFinderHash3Masked *>(out_hashes) =
            kksdk::legacy_codec_match_finder_hash3_masked(current, crc_table,
                    static_cast<std::uint32_t>(crc_count),
                    static_cast<std::uint32_t>(hash_mask));
    return 1;
}

extern "C" int kksdk_legacy_codec_match_finder_update_hash3_buckets(
        void *hash_table, unsigned int hash_count, const void *hashes,
        unsigned int position, void *out_update) {
    if (hashes == nullptr || out_update == nullptr) {
        return 0;
    }
    const kksdk::LegacyCodecMatchFinderHash3Update update =
            kksdk::legacy_codec_match_finder_update_hash3_buckets(
                    static_cast<std::uint32_t *>(hash_table),
                    static_cast<std::uint32_t>(hash_count),
                    *static_cast<const kksdk::LegacyCodecMatchFinderHash3Masked *>(hashes),
                    static_cast<std::uint32_t>(position));
    *static_cast<kksdk::LegacyCodecMatchFinderHash3Update *>(out_update) = update;
    return update.updated ? 1 : 0;
}

extern "C" unsigned int kksdk_legacy_codec_match_finder_hash2_direct(
        const unsigned char *current) {
    return kksdk::legacy_codec_match_finder_hash2_direct(current);
}

extern "C" int kksdk_legacy_codec_match_finder_update_hash2_bucket(
        void *hash_table, unsigned int hash_count, unsigned int hash2,
        unsigned int position, void *out_update) {
    if (out_update == nullptr) {
        return 0;
    }
    const kksdk::LegacyCodecMatchFinderHash2Update update =
            kksdk::legacy_codec_match_finder_update_hash2_bucket(
                    static_cast<std::uint32_t *>(hash_table),
                    static_cast<std::uint32_t>(hash_count),
                    static_cast<std::uint32_t>(hash2),
                    static_cast<std::uint32_t>(position));
    *static_cast<kksdk::LegacyCodecMatchFinderHash2Update *>(out_update) = update;
    return update.updated ? 1 : 0;
}

extern "C" unsigned int kksdk_legacy_codec_match_finder_count_match(
        const unsigned char *current, unsigned int distance, unsigned int start_length,
        unsigned int max_length) {
    return kksdk::legacy_codec_match_finder_count_match(current,
            static_cast<std::uint32_t>(distance),
            static_cast<std::uint32_t>(start_length),
            static_cast<std::uint32_t>(max_length));
}

extern "C" int kksdk_legacy_codec_match_finder_compare_at(
        const unsigned char *current, unsigned int distance, unsigned int length) {
    return kksdk::legacy_codec_match_finder_compare_at(current,
            static_cast<std::uint32_t>(distance),
            static_cast<std::uint32_t>(length));
}

extern "C" void kksdk_legacy_codec_match_finder_update_tree_branch(
        void *branch, int comparison, unsigned int matched_length) {
    if (branch != nullptr) {
        kksdk::legacy_codec_match_finder_update_tree_branch(
                *static_cast<kksdk::LegacyCodecMatchFinderTreeBranch *>(branch),
                comparison, static_cast<std::uint32_t>(matched_length));
    }
}

extern "C" void kksdk_legacy_codec_match_finder_splice_tree_children(
        void *lower_slot, void *upper_slot, const void *candidate_links) {
    kksdk::legacy_codec_match_finder_splice_tree_children(
            static_cast<std::uint32_t *>(lower_slot),
            static_cast<std::uint32_t *>(upper_slot),
            static_cast<const std::uint32_t *>(candidate_links));
}

extern "C" void *kksdk_legacy_codec_match_finder_tree_links_for_distance(
        void *tree_links, unsigned int link_count, unsigned int cyclic_pos,
        unsigned int cyclic_size, unsigned int distance) {
    return kksdk::legacy_codec_match_finder_tree_links_for_distance(
            static_cast<std::uint32_t *>(tree_links),
            static_cast<std::uint32_t>(link_count),
            static_cast<std::uint32_t>(cyclic_pos),
            static_cast<std::uint32_t>(cyclic_size),
            static_cast<std::uint32_t>(distance));
}

extern "C" void *kksdk_legacy_codec_match_finder_chain_link_for_distance(
        void *chain_links, unsigned int link_count, unsigned int cyclic_pos,
        unsigned int cyclic_size, unsigned int distance) {
    return kksdk::legacy_codec_match_finder_chain_link_for_distance(
            static_cast<std::uint32_t *>(chain_links),
            static_cast<std::uint32_t>(link_count),
            static_cast<std::uint32_t>(cyclic_pos),
            static_cast<std::uint32_t>(cyclic_size),
            static_cast<std::uint32_t>(distance));
}

extern "C" int kksdk_legacy_codec_match_finder_distance_in_history(
        unsigned int distance, unsigned int history_size) {
    return kksdk::legacy_codec_match_finder_distance_in_history(
            static_cast<std::uint32_t>(distance),
            static_cast<std::uint32_t>(history_size)) ? 1 : 0;
}

extern "C" unsigned int kksdk_legacy_codec_match_finder_wrapped_position(
        unsigned int cyclic_pos, unsigned int cyclic_size, unsigned int distance) {
    return kksdk::legacy_codec_match_finder_wrapped_position(
            static_cast<std::uint32_t>(cyclic_pos),
            static_cast<std::uint32_t>(cyclic_size),
            static_cast<std::uint32_t>(distance));
}

extern "C" unsigned int kksdk_legacy_codec_match_finder_tree_link_index(
        unsigned int wrapped_position) {
    return kksdk::legacy_codec_match_finder_tree_link_index(
            static_cast<std::uint32_t>(wrapped_position));
}

extern "C" unsigned int kksdk_legacy_codec_match_finder_record_count_before(
        const void *output) {
    return kksdk::legacy_codec_match_finder_record_count_before(
            static_cast<const kksdk::LegacyCodecMatchFinderMatchOutput *>(output));
}

extern "C" unsigned int kksdk_legacy_codec_match_finder_records_added(
        const void *output, unsigned int records_before) {
    return kksdk::legacy_codec_match_finder_records_added(
            static_cast<const kksdk::LegacyCodecMatchFinderMatchOutput *>(output),
            static_cast<std::uint32_t>(records_before));
}

extern "C" unsigned int kksdk_legacy_codec_match_finder_next_tree_candidate(
        int comparison, const void *candidate_links) {
    return kksdk::legacy_codec_match_finder_next_tree_candidate(
            comparison, static_cast<const std::uint32_t *>(candidate_links));
}

extern "C" unsigned int kksdk_legacy_codec_match_finder_start_length_at_least(
        unsigned int length, unsigned int minimum) {
    return kksdk::legacy_codec_match_finder_start_length_at_least(
            static_cast<std::uint32_t>(length), static_cast<std::uint32_t>(minimum));
}

extern "C" long long kksdk_legacy_codec_match_finder_candidate_start_offset(
        unsigned int start_length, unsigned int distance) {
    return static_cast<long long>(kksdk::legacy_codec_match_finder_candidate_start_offset(
            static_cast<std::uint32_t>(start_length), static_cast<std::uint32_t>(distance)));
}

extern "C" int kksdk_legacy_codec_match_finder_candidate_start_matches(
        const unsigned char *current, unsigned int distance, unsigned int start_length) {
    return kksdk::legacy_codec_match_finder_candidate_start_matches(
            current, static_cast<std::uint32_t>(distance),
            static_cast<std::uint32_t>(start_length)) ? 1 : 0;
}

extern "C" unsigned int kksdk_legacy_codec_match_finder_best_length_after_match(
        unsigned int best_length, unsigned int matched_length) {
    return kksdk::legacy_codec_match_finder_best_length_after_match(
            static_cast<std::uint32_t>(best_length),
            static_cast<std::uint32_t>(matched_length));
}

extern "C" int kksdk_legacy_codec_match_finder_probe_tree(
        void *probe, void *out_result) {
    if (probe == nullptr || out_result == nullptr) {
        return 0;
    }
    const kksdk::LegacyCodecMatchFinderTreeProbeResult result =
            kksdk::legacy_codec_match_finder_probe_tree(
                    *static_cast<kksdk::LegacyCodecMatchFinderTreeProbe *>(probe));
    *static_cast<kksdk::LegacyCodecMatchFinderTreeProbeResult *>(out_result) = result;
    return result.valid ? 1 : 0;
}

extern "C" int kksdk_legacy_codec_match_finder_search_binary_tree(
        void *search, void *out_result) {
    if (search == nullptr || out_result == nullptr) {
        return 0;
    }
    const kksdk::LegacyCodecMatchFinderTreeSearchResult result =
            kksdk::legacy_codec_match_finder_search_binary_tree(
                    *static_cast<kksdk::LegacyCodecMatchFinderTreeSearch *>(search));
    *static_cast<kksdk::LegacyCodecMatchFinderTreeSearchResult *>(out_result) = result;
    return result.records_added != 0 || result.terminal_match ? 1 : 0;
}

extern "C" int kksdk_legacy_codec_match_finder_try_short_match(
        void *match, void *out_result) {
    if (match == nullptr || out_result == nullptr) {
        return 0;
    }
    const kksdk::LegacyCodecMatchFinderShortMatchResult result =
            kksdk::legacy_codec_match_finder_try_short_match(
                    *static_cast<kksdk::LegacyCodecMatchFinderShortMatch *>(match));
    *static_cast<kksdk::LegacyCodecMatchFinderShortMatchResult *>(out_result) = result;
    return result.matched ? 1 : 0;
}

extern "C" int kksdk_legacy_codec_match_finder_try_short_match_pair(
        void *match, void *out_result) {
    if (match == nullptr || out_result == nullptr) {
        return 0;
    }
    const kksdk::LegacyCodecMatchFinderShortMatchPairResult result =
            kksdk::legacy_codec_match_finder_try_short_match_pair(
                    *static_cast<kksdk::LegacyCodecMatchFinderShortMatchPair *>(match));
    *static_cast<kksdk::LegacyCodecMatchFinderShortMatchPairResult *>(out_result) = result;
    return (result.first_matched || result.second_matched) ? 1 : 0;
}

extern "C" int kksdk_legacy_codec_match_finder_record_if_better(
        void *output, unsigned int length, unsigned int distance) {
    if (output == nullptr) {
        return 0;
    }
    return kksdk::legacy_codec_match_finder_record_if_better(
            *static_cast<kksdk::LegacyCodecMatchFinderMatchOutput *>(output),
            static_cast<std::uint32_t>(length),
            static_cast<std::uint32_t>(distance)) ? 1 : 0;
}

extern "C" void kksdk_legacy_codec_match_finder_advance_position(void *state) {
    if (state != nullptr) {
        kksdk::legacy_codec_match_finder_advance_position(
                *static_cast<kksdk::LegacyCodecMatchFinderAdvanceState *>(state));
    }
}

extern "C" void kksdk_legacy_codec_match_finder_hash_chain_skip(
        void *state, unsigned int count) {
    if (state != nullptr) {
        kksdk::legacy_codec_match_finder_hash_chain_skip(
                *static_cast<kksdk::LegacyCodecMatchFinderHashChainSkipState *>(state),
                static_cast<std::uint32_t>(count));
    }
}

extern "C" int kksdk_legacy_codec_match_finder_hash_chain_find(
        void *state, void *out_result) {
    if (state == nullptr || out_result == nullptr) {
        return 0;
    }
    const kksdk::LegacyCodecMatchFinderHashChainFindResult result =
            kksdk::legacy_codec_match_finder_hash_chain_find(
                    *static_cast<kksdk::LegacyCodecMatchFinderHashChainFindState *>(state));
    *static_cast<kksdk::LegacyCodecMatchFinderHashChainFindResult *>(out_result) = result;
    return result.records_added != 0 ? 1 : 0;
}

extern "C" void kksdk_legacy_codec_match_finder_binary_tree4_skip(
        void *state, unsigned int count) {
    if (state != nullptr) {
        kksdk::legacy_codec_match_finder_binary_tree4_skip(
                *static_cast<kksdk::LegacyCodecMatchFinderBinaryTree4SkipState *>(state),
                static_cast<std::uint32_t>(count));
    }
}

extern "C" void kksdk_legacy_codec_match_finder_binary_tree3_skip(
        void *state, unsigned int count) {
    if (state != nullptr) {
        kksdk::legacy_codec_match_finder_binary_tree3_skip(
                *static_cast<kksdk::LegacyCodecMatchFinderBinaryTree3SkipState *>(state),
                static_cast<std::uint32_t>(count));
    }
}

extern "C" void kksdk_legacy_codec_match_finder_binary_tree2_skip(
        void *state, unsigned int count) {
    if (state != nullptr) {
        kksdk::legacy_codec_match_finder_binary_tree2_skip(
                *static_cast<kksdk::LegacyCodecMatchFinderBinaryTree2SkipState *>(state),
                static_cast<std::uint32_t>(count));
    }
}

extern "C" int kksdk_legacy_codec_match_finder_binary_tree2_find(
        void *state, void *out_result) {
    if (state == nullptr || out_result == nullptr) {
        return 0;
    }
    const kksdk::LegacyCodecMatchFinderBinaryTreeFindResult result =
            kksdk::legacy_codec_match_finder_binary_tree2_find(
                    *static_cast<kksdk::LegacyCodecMatchFinderBinaryTree2FindState *>(state));
    *static_cast<kksdk::LegacyCodecMatchFinderBinaryTreeFindResult *>(out_result) = result;
    return result.records_added != 0 || result.terminal_match ? 1 : 0;
}

extern "C" int kksdk_legacy_codec_match_finder_binary_tree3_find(
        void *state, void *out_result) {
    if (state == nullptr || out_result == nullptr) {
        return 0;
    }
    const kksdk::LegacyCodecMatchFinderBinaryTreeFindResult result =
            kksdk::legacy_codec_match_finder_binary_tree3_find(
                    *static_cast<kksdk::LegacyCodecMatchFinderBinaryTree3FindState *>(state));
    *static_cast<kksdk::LegacyCodecMatchFinderBinaryTreeFindResult *>(out_result) = result;
    return result.records_added != 0 || result.terminal_match ? 1 : 0;
}

extern "C" int kksdk_legacy_codec_match_finder_binary_tree4_find(
        void *state, void *out_result) {
    if (state == nullptr || out_result == nullptr) {
        return 0;
    }
    const kksdk::LegacyCodecMatchFinderBinaryTreeFindResult result =
            kksdk::legacy_codec_match_finder_binary_tree4_find(
                    *static_cast<kksdk::LegacyCodecMatchFinderBinaryTree4FindState *>(state));
    *static_cast<kksdk::LegacyCodecMatchFinderBinaryTreeFindResult *>(out_result) = result;
    return result.records_added != 0 || result.terminal_match ? 1 : 0;
}

extern "C" void kksdk_legacy_codec_match_finder_init(void *state) {
    if (state != nullptr) {
        kksdk::legacy_codec_match_finder_init(
                *static_cast<kksdk::LegacyCodecMatchFinderInitState *>(state));
    }
}

extern "C" void kksdk_legacy_codec_match_finder_normalize_offsets(
        unsigned int base, void *values, unsigned int count) {
    kksdk::legacy_codec_match_finder_normalize_offsets(
            base, static_cast<std::uint32_t *>(values), count);
}

extern "C" unsigned int kksdk_legacy_codec_match_finder_normalized_offset(
        unsigned int value, unsigned int base) {
    return kksdk::legacy_codec_match_finder_normalized_offset(value, base);
}

extern "C" int kksdk_legacy_codec_make_match_finder_memory_plan(
        const void *request, void *out_plan) {
    if (request == nullptr || out_plan == nullptr) {
        return 0;
    }
    const kksdk::LegacyCodecMatchFinderMemoryPlan plan =
            kksdk::legacy_codec_make_match_finder_memory_plan(
                    *static_cast<const kksdk::LegacyCodecMatchFinderMemoryPlanRequest *>(request));
    *static_cast<kksdk::LegacyCodecMatchFinderMemoryPlan *>(out_plan) = plan;
    return plan.valid ? 1 : 0;
}

extern "C" int kksdk_legacy_codec_match_finder_dictionary_size_supported(
        unsigned int dictionary_size) {
    return kksdk::legacy_codec_match_finder_dictionary_size_supported(dictionary_size) ? 1 : 0;
}

extern "C" unsigned int kksdk_legacy_codec_match_finder_history_size(
        unsigned int dictionary_size) {
    return kksdk::legacy_codec_match_finder_history_size(dictionary_size);
}

extern "C" unsigned int kksdk_legacy_codec_match_finder_dictionary_fraction_shift(
        unsigned int dictionary_size) {
    return kksdk::legacy_codec_match_finder_dictionary_fraction_shift(dictionary_size);
}

extern "C" unsigned int kksdk_legacy_codec_match_finder_keep_size_after(
        const void *request) {
    if (request == nullptr) {
        return 0;
    }
    return kksdk::legacy_codec_match_finder_keep_size_after(
            *static_cast<const kksdk::LegacyCodecMatchFinderMemoryPlanRequest *>(request));
}

extern "C" unsigned int kksdk_legacy_codec_match_finder_block_size(
        const void *request, unsigned int history_size,
        unsigned int dictionary_fraction_shift, unsigned int keep_size_after) {
    if (request == nullptr) {
        return 0;
    }
    return kksdk::legacy_codec_match_finder_block_size(
            *static_cast<const kksdk::LegacyCodecMatchFinderMemoryPlanRequest *>(request),
            history_size, dictionary_fraction_shift, keep_size_after);
}

extern "C" unsigned int kksdk_legacy_codec_match_finder_match_buffer_size(
        unsigned int history_size, int binary_tree_mode) {
    return kksdk::legacy_codec_match_finder_match_buffer_size(
            history_size, binary_tree_mode != 0);
}

extern "C" int kksdk_legacy_codec_match_finder_uses_extended_hash(
        unsigned int hash_bytes) {
    return kksdk::legacy_codec_match_finder_uses_extended_hash(hash_bytes) ? 1 : 0;
}

extern "C" unsigned int kksdk_legacy_codec_match_finder_hash_mask(
        unsigned int dictionary_size, unsigned int hash_bytes) {
    return kksdk::legacy_codec_match_finder_hash_mask(dictionary_size, hash_bytes);
}

extern "C" unsigned int kksdk_legacy_codec_match_finder_hash_size(
        unsigned int hash_mask) {
    return kksdk::legacy_codec_match_finder_hash_size(hash_mask);
}

extern "C" unsigned int kksdk_legacy_codec_match_finder_son_offset(
        unsigned int hash_bytes) {
    return kksdk::legacy_codec_match_finder_son_offset(hash_bytes);
}

extern "C" int kksdk_legacy_codec_allocate_match_finder_memory(const void *request) {
    if (request == nullptr) {
        return 0;
    }
    return kksdk::legacy_codec_allocate_match_finder_memory(
            *static_cast<const kksdk::LegacyCodecMatchFinderAllocationRequest *>(request)) ? 1 : 0;
}

extern "C" void kksdk_legacy_codec_free_match_finder_window(const void *request) {
    if (request != nullptr) {
        kksdk::legacy_codec_free_match_finder_window(
                *static_cast<const kksdk::LegacyCodecMatchFinderAllocationRequest *>(request));
    }
}

extern "C" void kksdk_legacy_codec_free_match_finder_tables(const void *request) {
    if (request != nullptr) {
        kksdk::legacy_codec_free_match_finder_tables(
                *static_cast<const kksdk::LegacyCodecMatchFinderAllocationRequest *>(request));
    }
}

extern "C" unsigned int kksdk_legacy_codec_match_finder_hash_table_words(
        const void *plan) {
    if (plan == nullptr) {
        return 0;
    }
    return kksdk::legacy_codec_match_finder_hash_table_words(
            *static_cast<const kksdk::LegacyCodecMatchFinderMemoryPlan *>(plan));
}

extern "C" unsigned int kksdk_legacy_codec_match_finder_son_table_words(
        const void *plan) {
    if (plan == nullptr) {
        return 0;
    }
    return kksdk::legacy_codec_match_finder_son_table_words(
            *static_cast<const kksdk::LegacyCodecMatchFinderMemoryPlan *>(plan));
}

extern "C" unsigned int kksdk_legacy_codec_match_finder_total_table_words(
        unsigned int hash_words, unsigned int son_words) {
    return kksdk::legacy_codec_match_finder_total_table_words(hash_words, son_words);
}

extern "C" void *kksdk_legacy_codec_match_finder_son_table_base(
        void *hash_table, unsigned int hash_words) {
    return kksdk::legacy_codec_match_finder_son_table_base(
            static_cast<std::uint32_t *>(hash_table), hash_words);
}

extern "C" int kksdk_legacy_codec_match_finder_reuse_tables(
        void *allocation, unsigned int hash_words, unsigned int son_words) {
    if (allocation == nullptr) {
        return 0;
    }
    return kksdk::legacy_codec_match_finder_reuse_tables(
            *static_cast<kksdk::LegacyCodecMatchFinderAllocation *>(allocation),
            hash_words, son_words) ? 1 : 0;
}
