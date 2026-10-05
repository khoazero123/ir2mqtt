#pragma once

#include "legacy_codec_allocator.hpp"
#include "legacy_codec_options.hpp"
#include "legacy_codec_tables.hpp"

#include <cstddef>
#include <cstdint>

namespace kksdk {

using LegacyCodecAllocFn = void *(*)(void *allocator, std::size_t size);

struct LegacyCodecState {
    LegacyCodecOptions options;
    LegacyCodecResolvedOptions resolved_options;
    LegacyCodecLenSlotTable len_slot_table{};
    LegacyCodecPriceTable price_table{};
    LegacyCodecWorkspaceBuffers workspace;
    void *stream_context = nullptr;
    void *progress_context = nullptr;
};

enum class LegacyCodecStatus : int {
    ok = 0,
    memory_error = 2,
    invalid_properties = 4,
    option_error = 5,
    input_error = 6,
};

enum class LegacyCodecDecodeStatus : int {
    finished_with_marker = 3,
};

struct LegacyCodecSnapshotRegion {
    void *active = nullptr;
    void *snapshot = nullptr;
    std::size_t size = 0;
};

struct LegacyCodecSnapshotView {
    LegacyCodecSnapshotRegion literal_low;
    LegacyCodecSnapshotRegion literal_high;
    LegacyCodecSnapshotRegion probability_models;
    std::uint32_t *active_processed_flag = nullptr;
    std::uint32_t *snapshot_processed_flag = nullptr;
};

struct LegacyCodecProbabilityResetView {
    std::uint16_t *primary_probabilities = nullptr;
    std::size_t primary_probability_count = 0;
    std::uint16_t *secondary_probabilities = nullptr;
    std::size_t secondary_probability_count = 0;
    std::uint64_t *processed_position = nullptr;
    std::uint64_t *range_limit = nullptr;
    std::uint64_t range_start = 1;
    std::uint32_t *lc_mask = nullptr;
    std::uint32_t *pb_mask = nullptr;
};

using LegacyCodecCoreEncodeFn = int (*)(void *state, void *user, unsigned int finish_mode,
        unsigned int input_limit);
using LegacyCodecPrepareEncodeFn = int (*)(LegacyCodecState *state, void *input, void *output);
using LegacyCodecProgressFn = int (*)(void *progress, void *stream_context,
        unsigned long long adjusted_position);
using LegacyCodecFreeStateFn = void (*)(void *allocator, void *ptr);
using LegacyCodecRefreshFn = void (*)(void *context);
using LegacyCodecRefreshLengthPricesFn = void (*)(void *context, std::uint32_t pos_state,
        const LegacyCodecPriceTable *prices);

struct LegacyCodecOneShotDecodeState {
    LegacyCodecOptions options;
    void *probability_models = nullptr;
    void *output_context = nullptr;
    void *output_position = nullptr;
    std::uint32_t probability_model_size = 0;
    bool initialized = true;
};

struct LegacyCodecDecoderRangeState {
    std::uint64_t read_pos = 0;
    std::uint64_t buffered_input = 0;
    std::uint64_t range_limit = 0x100000000ULL;
    std::uint32_t status = 0;
    bool input_finished = true;
};

struct LegacyCodecRangeDecoderCursor {
    std::uint32_t range = 0xffffffffU;
    std::uint32_t code = 0;
    const std::uint8_t *input = nullptr;
    const std::uint8_t *input_end = nullptr;
    std::uint64_t consumed = 0;
};

struct LegacyCodecRangeBitResult {
    bool ok = false;
    std::uint32_t bit = 0;
};

struct LegacyCodecRangeBitTreeResult {
    bool ok = false;
    std::uint32_t symbol = 0;
};

struct LegacyCodecRangeLengthResult {
    bool ok = false;
    std::uint32_t symbol = 0;
};

struct LegacyCodecRangeLiteralResult {
    bool ok = false;
    std::uint8_t value = 0;
};

struct LegacyCodecRangeLiteralRequest {
    std::uint16_t *probabilities = nullptr;
    bool use_match_byte = false;
    std::uint8_t match_byte = 0;
};

struct LegacyCodecOutputWindowCopyRequest {
    std::uint8_t *buffer = nullptr;
    std::uint64_t position = 0;
    std::uint64_t output_limit = 0;
    std::uint32_t cyclic_size = 0;
    std::uint32_t distance = 0;
    std::uint32_t length = 0;
};

struct LegacyCodecOutputWindowCopyResult {
    bool copied = false;
    std::uint32_t bytes_copied = 0;
    std::uint32_t remaining_length = 0;
    std::uint64_t new_position = 0;
};

struct LegacyCodecPendingMatchCopyRequest {
    std::uint8_t *buffer = nullptr;
    std::uint64_t position = 0;
    std::uint64_t output_limit = 0;
    std::uint32_t cyclic_size = 0;
    std::uint32_t distance = 0;
    std::uint32_t remaining_length = 0;
};

struct LegacyCodecOutputWindowByteRequest {
    const std::uint8_t *buffer = nullptr;
    std::uint64_t position = 0;
    std::uint32_t cyclic_size = 0;
    std::uint32_t distance = 0;
};

struct LegacyCodecOutputWindowByteResult {
    bool ok = false;
    std::uint8_t value = 0;
};

struct LegacyCodecOutputWindowWriteByteRequest {
    std::uint8_t *buffer = nullptr;
    std::uint64_t position = 0;
    std::uint64_t output_limit = 0;
    std::uint32_t cyclic_size = 0;
    std::uint8_t value = 0;
};

struct LegacyCodecOutputWindowWriteByteResult {
    bool written = false;
    std::uint64_t new_position = 0;
};

struct LegacyCodecDistanceSlotInfo {
    bool valid = false;
    std::uint32_t distance_minus_one_base = 0;
    std::uint32_t footer_bit_count = 0;
    bool direct_footer = false;
    std::uint32_t align_bit_count = 0;
};

struct LegacyCodecRangeDistanceResult {
    bool ok = false;
    std::uint32_t distance = 0;
    std::uint32_t distance_minus_one = 0;
};

struct LegacyCodecDecoderOutputLimitRequest {
    std::uint64_t position = 0;
    std::uint64_t requested_limit = 0;
    std::uint32_t available_size = 0;
    std::uint32_t processed_size = 0;
    std::uint32_t pending_length = 0;
};

struct LegacyCodecRepDistances {
    std::uint32_t rep0 = 0;
    std::uint32_t rep1 = 0;
    std::uint32_t rep2 = 0;
    std::uint32_t rep3 = 0;
};

enum class LegacyCodecProbabilityBank : std::uint32_t {
    is_match = 0,
    is_rep = 0xc0,
    is_rep_g0 = 0xcc,
    is_rep_g1 = 0xd8,
    is_rep_g2 = 0xe4,
    is_rep0_long = 0xf0,
    pos_slot = 0x1b0,
    align = 0x322,
    len_choice = 0x332,
    rep_len_choice = 0x534,
    literal = 0x736,
};

using LegacyCodecCoreDecodeFn = int (*)(LegacyCodecOneShotDecodeState *state,
        unsigned long long output_limit, const void *input, unsigned long long *input_size,
        int finish_mode, int *status);
using LegacyCodecStreamingCoreDecodeFn = int (*)(void *decoder,
        unsigned long long output_limit, const void *input, unsigned long long *input_size,
        int finish_mode, void *status);
using LegacyCodecMatchFinderCallback = void (*)();
using LegacyCodecMatchFinderReadFn = int (*)(void *stream, std::uint8_t *destination,
        std::uint64_t *size);
using LegacyCodecMatchFinderRefreshFn = void (*)(void *state);

struct LegacyCodecMatchFinderMatchOutput;

struct LegacyCodecMatchFinderCallbacks {
    LegacyCodecMatchFinderCallback init = nullptr;
    LegacyCodecMatchFinderCallback get_byte = nullptr;
    LegacyCodecMatchFinderCallback available_bytes = nullptr;
    LegacyCodecMatchFinderCallback current_pointer = nullptr;
    LegacyCodecMatchFinderCallback find_matches = nullptr;
    LegacyCodecMatchFinderCallback skip = nullptr;
};

struct LegacyCodecMatchFinderCallbackCatalog {
    LegacyCodecMatchFinderCallback init = nullptr;
    LegacyCodecMatchFinderCallback get_byte = nullptr;
    LegacyCodecMatchFinderCallback available_bytes = nullptr;
    LegacyCodecMatchFinderCallback current_pointer = nullptr;
    LegacyCodecMatchFinderCallback hash_chain_find = nullptr;
    LegacyCodecMatchFinderCallback hash_chain_skip = nullptr;
    LegacyCodecMatchFinderCallback binary_tree_2_find = nullptr;
    LegacyCodecMatchFinderCallback binary_tree_2_skip = nullptr;
    LegacyCodecMatchFinderCallback binary_tree_3_find = nullptr;
    LegacyCodecMatchFinderCallback binary_tree_3_skip = nullptr;
    LegacyCodecMatchFinderCallback binary_tree_4_find = nullptr;
    LegacyCodecMatchFinderCallback binary_tree_4_skip = nullptr;
};

struct LegacyCodecMatchFinderSelection {
    bool binary_tree_mode = false;
    std::uint32_t hash_bytes = 0;
    const LegacyCodecMatchFinderCallbackCatalog *catalog = nullptr;
};

struct LegacyCodecMatchFinderView {
    const std::uint8_t *current = nullptr;
    std::uint32_t position = 0;
    std::uint32_t limit = 0;
};

struct LegacyCodecMatchFinderCursor {
    std::uint32_t cyclic_pos = 0;
    std::uint32_t buffer_pos = 0;
    std::uint32_t available_bytes = 0;
};

struct LegacyCodecMatchFinderWindow {
    std::uint8_t *buffer = nullptr;
    std::uint8_t *current = nullptr;
    std::uint32_t read_pos = 0;
    std::uint32_t write_pos = 0;
    std::uint32_t keep_size_before = 0;
    std::uint32_t block_size = 0;
    std::uint32_t keep_size_after = 0;
    bool direct_input = false;
};

struct LegacyCodecMatchFinderFillState {
    bool stream_error = false;
    std::uint32_t read_pos = 0;
    std::uint32_t write_pos = 0;
    std::uint32_t keep_size_after = 0;
};

struct LegacyCodecMatchFinderInputBuffer {
    std::uint8_t *buffer = nullptr;
    std::uint32_t read_pos = 0;
    std::uint32_t write_pos = 0;
    std::uint32_t block_size = 0;
    std::uint32_t keep_size_after = 0;
    bool stream_end = false;
    int stream_error = 0;
    bool direct_input = false;
    std::uint64_t direct_input_remaining = 0;
    void *stream = nullptr;
    LegacyCodecMatchFinderReadFn read = nullptr;
};

struct LegacyCodecMatchFinderLimitState {
    std::uint32_t cyclic_pos = 0;
    std::uint32_t cyclic_size = 0;
    std::uint32_t read_pos = 0;
    std::uint32_t write_pos = 0;
    std::uint32_t keep_size_after = 0;
    std::uint32_t match_max_len = 0;
    std::uint32_t match_len_limit = 0;
    std::uint32_t position_limit = 0;
};

struct LegacyCodecMatchFinderHashChainSkipState {
    const std::uint8_t *current = nullptr;
    std::uint32_t position = 0;
    std::uint32_t cyclic_pos = 0;
    std::uint32_t position_limit = 0;
    std::uint32_t match_len_limit = 0;
    std::uint32_t *hash_table = nullptr;
    std::uint32_t hash_count = 0;
    std::uint32_t *chain_table = nullptr;
    std::uint32_t chain_count = 0;
    const std::uint32_t *crc_table = nullptr;
    std::uint32_t crc_count = 0;
    LegacyCodecMatchFinderRefreshFn refresh = nullptr;
    void *refresh_state = nullptr;
};

struct LegacyCodecMatchFinderHashChainFindState {
    const std::uint8_t *current = nullptr;
    std::uint32_t position = 0;
    std::uint32_t cyclic_pos = 0;
    std::uint32_t position_limit = 0;
    std::uint32_t match_len_limit = 0;
    std::uint32_t max_depth = 0;
    std::uint32_t *hash_table = nullptr;
    std::uint32_t hash_count = 0;
    std::uint32_t *chain_table = nullptr;
    std::uint32_t chain_count = 0;
    const std::uint32_t *crc_table = nullptr;
    std::uint32_t crc_count = 0;
    LegacyCodecMatchFinderMatchOutput *output = nullptr;
    LegacyCodecMatchFinderRefreshFn refresh = nullptr;
    void *refresh_state = nullptr;
};

struct LegacyCodecMatchFinderHashChainFindResult {
    std::uint32_t records_added = 0;
    std::uint32_t best_length = 0;
};

struct LegacyCodecMatchFinderBinaryTree4SkipState {
    const std::uint8_t *current = nullptr;
    std::uint32_t position = 0;
    std::uint32_t cyclic_pos = 0;
    std::uint32_t cyclic_size = 0;
    std::uint32_t position_limit = 0;
    std::uint32_t match_len_limit = 0;
    std::uint32_t max_depth = 0;
    std::uint32_t hash_mask = 0;
    std::uint32_t *hash_table = nullptr;
    std::uint32_t hash_count = 0;
    std::uint32_t *tree_links = nullptr;
    std::uint32_t tree_link_count = 0;
    const std::uint32_t *crc_table = nullptr;
    std::uint32_t crc_count = 0;
    LegacyCodecMatchFinderRefreshFn refresh = nullptr;
    void *refresh_state = nullptr;
};

struct LegacyCodecMatchFinderBinaryTree3SkipState {
    const std::uint8_t *current = nullptr;
    std::uint32_t position = 0;
    std::uint32_t cyclic_pos = 0;
    std::uint32_t cyclic_size = 0;
    std::uint32_t position_limit = 0;
    std::uint32_t match_len_limit = 0;
    std::uint32_t max_depth = 0;
    std::uint32_t hash_mask = 0;
    std::uint32_t *hash_table = nullptr;
    std::uint32_t hash_count = 0;
    std::uint32_t *tree_links = nullptr;
    std::uint32_t tree_link_count = 0;
    const std::uint32_t *crc_table = nullptr;
    std::uint32_t crc_count = 0;
    LegacyCodecMatchFinderRefreshFn refresh = nullptr;
    void *refresh_state = nullptr;
};

struct LegacyCodecMatchFinderBinaryTree2SkipState {
    const std::uint8_t *current = nullptr;
    std::uint32_t position = 0;
    std::uint32_t cyclic_pos = 0;
    std::uint32_t cyclic_size = 0;
    std::uint32_t position_limit = 0;
    std::uint32_t match_len_limit = 0;
    std::uint32_t max_depth = 0;
    std::uint32_t *hash_table = nullptr;
    std::uint32_t hash_count = 0;
    std::uint32_t *tree_links = nullptr;
    std::uint32_t tree_link_count = 0;
    LegacyCodecMatchFinderRefreshFn refresh = nullptr;
    void *refresh_state = nullptr;
};

struct LegacyCodecMatchFinderBinaryTree2FindState {
    const std::uint8_t *current = nullptr;
    std::uint32_t position = 0;
    std::uint32_t cyclic_pos = 0;
    std::uint32_t cyclic_size = 0;
    std::uint32_t position_limit = 0;
    std::uint32_t match_len_limit = 0;
    std::uint32_t max_depth = 0;
    std::uint32_t *hash_table = nullptr;
    std::uint32_t hash_count = 0;
    std::uint32_t *tree_links = nullptr;
    std::uint32_t tree_link_count = 0;
    LegacyCodecMatchFinderMatchOutput *output = nullptr;
    LegacyCodecMatchFinderRefreshFn refresh = nullptr;
    void *refresh_state = nullptr;
};

struct LegacyCodecMatchFinderBinaryTree3FindState {
    const std::uint8_t *current = nullptr;
    std::uint32_t position = 0;
    std::uint32_t cyclic_pos = 0;
    std::uint32_t cyclic_size = 0;
    std::uint32_t position_limit = 0;
    std::uint32_t match_len_limit = 0;
    std::uint32_t max_depth = 0;
    std::uint32_t hash_mask = 0;
    std::uint32_t *hash_table = nullptr;
    std::uint32_t hash_count = 0;
    std::uint32_t *tree_links = nullptr;
    std::uint32_t tree_link_count = 0;
    const std::uint32_t *crc_table = nullptr;
    std::uint32_t crc_count = 0;
    LegacyCodecMatchFinderMatchOutput *output = nullptr;
    LegacyCodecMatchFinderRefreshFn refresh = nullptr;
    void *refresh_state = nullptr;
};

struct LegacyCodecMatchFinderBinaryTree4FindState {
    const std::uint8_t *current = nullptr;
    std::uint32_t position = 0;
    std::uint32_t cyclic_pos = 0;
    std::uint32_t cyclic_size = 0;
    std::uint32_t position_limit = 0;
    std::uint32_t match_len_limit = 0;
    std::uint32_t max_depth = 0;
    std::uint32_t hash_mask = 0;
    std::uint32_t *hash_table = nullptr;
    std::uint32_t hash_count = 0;
    std::uint32_t *tree_links = nullptr;
    std::uint32_t tree_link_count = 0;
    const std::uint32_t *crc_table = nullptr;
    std::uint32_t crc_count = 0;
    LegacyCodecMatchFinderMatchOutput *output = nullptr;
    LegacyCodecMatchFinderRefreshFn refresh = nullptr;
    void *refresh_state = nullptr;
};

struct LegacyCodecMatchFinderBinaryTreeFindResult {
    std::uint32_t records_added = 0;
    std::uint32_t best_length = 0;
    bool terminal_match = false;
};

struct LegacyCodecMatchFinderAdvanceState {
    const std::uint8_t *current = nullptr;
    std::uint32_t position = 0;
    std::uint32_t cyclic_pos = 0;
    std::uint32_t position_limit = 0;
    LegacyCodecMatchFinderRefreshFn refresh = nullptr;
    void *refresh_state = nullptr;
};

struct LegacyCodecMatchFinderHash4 {
    std::uint32_t hash2 = 0;
    std::uint32_t hash3 = 0;
    std::uint32_t hash4 = 0;
};

struct LegacyCodecMatchFinderHash4Update {
    bool updated = false;
    std::uint32_t previous_hash2 = 0;
    std::uint32_t previous_hash3 = 0;
    std::uint32_t previous_hash4 = 0;
};

struct LegacyCodecMatchFinderHash3Masked {
    std::uint32_t hash2 = 0;
    std::uint32_t hash3 = 0;
};

struct LegacyCodecMatchFinderHash3Update {
    bool updated = false;
    std::uint32_t previous_hash2 = 0;
    std::uint32_t previous_hash3 = 0;
};

struct LegacyCodecMatchFinderHash2Update {
    bool updated = false;
    std::uint32_t previous_hash2 = 0;
};

struct LegacyCodecMatchFinderMatchRecord {
    std::uint32_t length = 0;
    std::uint32_t distance = 0;
};

struct LegacyCodecMatchFinderMatchOutput {
    LegacyCodecMatchFinderMatchRecord *records = nullptr;
    std::uint32_t capacity = 0;
    std::uint32_t count = 0;
    std::uint32_t best_length = 0;
};

struct LegacyCodecMatchFinderTreeBranch {
    std::uint32_t *lower_slot = nullptr;
    std::uint32_t *upper_slot = nullptr;
    std::uint32_t *candidate_links = nullptr;
    std::uint32_t candidate_position = 0;
    std::uint32_t lower_match_length = 0;
    std::uint32_t upper_match_length = 0;
};

struct LegacyCodecMatchFinderTreeProbe {
    const std::uint8_t *current = nullptr;
    std::uint32_t distance = 0;
    std::uint32_t start_length = 0;
    std::uint32_t max_length = 0;
    LegacyCodecMatchFinderTreeBranch *branch = nullptr;
    LegacyCodecMatchFinderMatchOutput *output = nullptr;
};

struct LegacyCodecMatchFinderTreeProbeResult {
    bool valid = false;
    bool terminal_match = false;
    std::uint32_t matched_length = 0;
    int comparison = 0;
};

struct LegacyCodecMatchFinderTreeSearch {
    const std::uint8_t *current = nullptr;
    std::uint32_t position = 0;
    std::uint32_t cyclic_pos = 0;
    std::uint32_t cyclic_size = 0;
    std::uint32_t match_len_limit = 0;
    std::uint32_t max_depth = 0;
    std::uint32_t start_length = 0;
    std::uint32_t initial_candidate = 0;
    std::uint32_t *tree_links = nullptr;
    std::uint32_t tree_link_count = 0;
    LegacyCodecMatchFinderMatchOutput *output = nullptr;
};

struct LegacyCodecMatchFinderTreeSearchResult {
    std::uint32_t records_added = 0;
    std::uint32_t best_length = 0;
    bool terminal_match = false;
};

struct LegacyCodecMatchFinderShortMatch {
    const std::uint8_t *current = nullptr;
    std::uint32_t distance = 0;
    std::uint32_t history_size = 0;
    std::uint32_t start_length = 0;
    std::uint32_t max_length = 0;
    LegacyCodecMatchFinderMatchOutput *output = nullptr;
};

struct LegacyCodecMatchFinderShortMatchResult {
    bool matched = false;
    std::uint32_t matched_length = 0;
};

struct LegacyCodecMatchFinderShortMatchPair {
    const std::uint8_t *current = nullptr;
    std::uint32_t first_distance = 0;
    std::uint32_t second_distance = 0;
    std::uint32_t history_size = 0;
    std::uint32_t max_length = 0;
    LegacyCodecMatchFinderMatchOutput *output = nullptr;
};

struct LegacyCodecMatchFinderShortMatchPairResult {
    bool first_matched = false;
    bool second_matched = false;
    std::uint32_t best_length = 0;
    std::uint32_t best_distance = 0;
};

struct LegacyCodecMatchFinderInitState {
    std::uint32_t *hash_table = nullptr;
    std::uint32_t hash_count = 0;
    std::uint8_t *buffer = nullptr;
    std::uint32_t base_position = 0;
    std::uint32_t current_position = 0;
    std::uint32_t match_max_len = 0;
    LegacyCodecMatchFinderInputBuffer input;
    std::uint32_t match_len_limit = 0;
    std::uint32_t position_limit = 0;
};

struct LegacyCodecMatchFinderMemoryPlanRequest {
    std::uint32_t dictionary_size = 0;
    std::uint32_t keep_before = 0;
    std::uint32_t fast_bytes = 0;
    std::uint32_t match_max_len = 0;
    std::uint32_t hash_bytes = 4;
    bool binary_tree_mode = false;
};

struct LegacyCodecMatchFinderMemoryPlan {
    bool valid = false;
    std::uint32_t history_size = 0;
    std::uint32_t keep_size_before = 0;
    std::uint32_t keep_size_after = 0;
    std::uint32_t block_size = 0;
    std::uint32_t hash_mask = 0;
    std::uint32_t hash_size = 0;
    std::uint32_t son_offset = 0;
    std::uint32_t match_buffer_size = 0;
};

struct LegacyCodecMatchFinderAllocation {
    std::uint8_t *window_buffer = nullptr;
    std::uint32_t window_size = 0;
    std::uint32_t *hash_table = nullptr;
    std::uint32_t hash_table_words = 0;
    std::uint32_t *son_table = nullptr;
    std::uint32_t son_table_words = 0;
};

struct LegacyCodecMatchFinderAllocationRequest {
    const LegacyCodecMatchFinderMemoryPlan *plan = nullptr;
    LegacyCodecMatchFinderAllocation *allocation = nullptr;
    void *allocator = nullptr;
    LegacyCodecAllocFn alloc = nullptr;
    LegacyCodecFreeFn free = nullptr;
    bool direct_input = false;
};

struct LegacyCodecBoundedEncodeRequest {
    LegacyCodecState *state = nullptr;
    void *core_user = nullptr;
    LegacyCodecCoreEncodeFn core_encode = nullptr;
    LegacyCodecProbabilityResetView reset_view;
    bool reset_before_encode = false;
    unsigned int finish_mode = 0;
    unsigned int input_limit = 0;
    unsigned long long output_before = 0;
    unsigned int processed_before = 0;
};

struct LegacyCodecBoundedEncodeResult {
    int status = 0;
    unsigned long long output_delta = 0;
    unsigned int processed_delta = 0;
};

struct LegacyCodecPrepareRequest {
    LegacyCodecState *state = nullptr;
    void *stream_context = nullptr;
    void *progress_context = nullptr;
    bool has_progress_context = false;
    unsigned int input_limit = 0;
};

struct LegacyCodecEncodeLoopRequest {
    LegacyCodecState *state = nullptr;
    LegacyCodecPrepareEncodeFn prepare = nullptr;
    LegacyCodecCoreEncodeFn encode_step = nullptr;
    void *prepare_input = nullptr;
    void *prepare_output = nullptr;
    void *core_user = nullptr;
    LegacyCodecProgressFn progress = nullptr;
    void *progress_user = nullptr;
    bool *finished = nullptr;
    unsigned int finish_mode = 0;
    unsigned int input_limit = 0;
    int progress_error_status = 10;
};

struct LegacyCodecOwnedEncodeRequest {
    void *allocator = nullptr;
    LegacyCodecAllocFn alloc = nullptr;
    LegacyCodecFreeStateFn free = nullptr;
    LegacyCodecEncodeLoopRequest loop;
    const LegacyCodecOptions *options = nullptr;
    const std::uint8_t *header = nullptr;
    std::uint32_t header_size = 0;
};

struct LegacyCodecPriceCacheRefreshRequest {
    bool distance_prices_dirty = false;
    std::uint32_t fast_bytes = 0;
    std::uint32_t pos_state_bits = 0;
    std::uint32_t *len_price_limit = nullptr;
    std::uint32_t *rep_len_price_limit = nullptr;
    const LegacyCodecPriceTable *prices = nullptr;
    LegacyCodecRefreshFn refresh_distance_prices = nullptr;
    void *distance_context = nullptr;
    LegacyCodecRefreshFn refresh_align_prices = nullptr;
    void *align_context = nullptr;
    LegacyCodecRefreshLengthPricesFn refresh_len_prices = nullptr;
    void *len_context = nullptr;
    LegacyCodecRefreshLengthPricesFn refresh_rep_len_prices = nullptr;
    void *rep_len_context = nullptr;
};

struct LegacyCodecHeaderDecodeRequest {
    void *allocator = nullptr;
    LegacyCodecAllocFn alloc = nullptr;
    LegacyCodecFreeStateFn free = nullptr;
    LegacyCodecCoreDecodeFn core_decode = nullptr;
    void *output_context = nullptr;
    void **output_position = nullptr;
    const void *input = nullptr;
    unsigned long long *input_size = nullptr;
    const std::uint8_t *header = nullptr;
    std::uint32_t header_size = 0;
    unsigned long long output_limit = 0;
    int finish_mode = 0;
    int *decode_status = nullptr;
};

struct LegacyCodecStreamingDecodeState {
    void *decoder = nullptr;
    const std::uint8_t *output_buffer = nullptr;
    unsigned long long read_pos = 0;
    unsigned long long write_pos = 0;
    LegacyCodecStreamingCoreDecodeFn core_decode = nullptr;
};

struct LegacyCodecStreamingDecodeRequest {
    LegacyCodecStreamingDecodeState *state = nullptr;
    std::uint8_t *output = nullptr;
    unsigned long long *output_size = nullptr;
    const void *input = nullptr;
    unsigned long long *input_size = nullptr;
    int finish_mode = 0;
    void *decode_status = nullptr;
};

void legacy_codec_init_state(LegacyCodecState &state);
LegacyCodecState *legacy_codec_allocate_state(void *allocator, LegacyCodecAllocFn alloc_fn);
void legacy_codec_save_snapshot(const LegacyCodecSnapshotView &view);
void legacy_codec_restore_snapshot(const LegacyCodecSnapshotView &view);
void legacy_codec_reset_probability_state(const LegacyCodecResolvedOptions &options,
        const LegacyCodecProbabilityResetView &view);
LegacyCodecBoundedEncodeResult legacy_codec_run_bounded_encode(
        const LegacyCodecBoundedEncodeRequest &request);
bool legacy_codec_bounded_encode_has_required_callbacks(
        const LegacyCodecBoundedEncodeRequest &request);
void legacy_codec_bounded_encode_reset_if_requested(
        const LegacyCodecBoundedEncodeRequest &request);
unsigned long long legacy_codec_bounded_encode_output_position(
        const LegacyCodecState &state, unsigned long long fallback_position);
unsigned int legacy_codec_bounded_encode_processed_position(
        const LegacyCodecState &state, unsigned int fallback_position);
unsigned long long legacy_codec_bounded_encode_output_delta(
        unsigned long long before, unsigned long long after);
unsigned int legacy_codec_bounded_encode_processed_delta(
        unsigned int before, unsigned int after);
int legacy_codec_prepare_output_stream(const LegacyCodecPrepareRequest &request);
int legacy_codec_prepare_input_progress_stream(const LegacyCodecPrepareRequest &request);
int legacy_codec_prepare_stream_common(const LegacyCodecPrepareRequest &request);
bool legacy_codec_prepare_stream_has_state(const LegacyCodecPrepareRequest &request);
void legacy_codec_prepare_stream_bind_contexts(const LegacyCodecPrepareRequest &request);
void legacy_codec_noop_callback();
int legacy_codec_run_encode_loop(const LegacyCodecEncodeLoopRequest &request);
bool legacy_codec_encode_loop_has_required_callbacks(
        const LegacyCodecEncodeLoopRequest &request);
bool legacy_codec_encode_loop_should_continue(const bool *finished);
unsigned long long legacy_codec_encode_loop_progress_position(
        const LegacyCodecState &state);
int legacy_codec_run_owned_encode(const LegacyCodecOwnedEncodeRequest &request);
bool legacy_codec_owned_encode_has_header(const LegacyCodecOwnedEncodeRequest &request);
int legacy_codec_owned_encode_apply_options(
        LegacyCodecState &state, const LegacyCodecOptions *options);
int legacy_codec_owned_encode_apply_header(
        LegacyCodecState &state, const LegacyCodecOwnedEncodeRequest &request);
void legacy_codec_refresh_price_caches(const LegacyCodecPriceCacheRefreshRequest &request);
void legacy_codec_price_cache_refresh_distance_tables(
        const LegacyCodecPriceCacheRefreshRequest &request);
void legacy_codec_price_cache_refresh_optional(
        LegacyCodecRefreshFn refresh, void *context);
std::uint32_t legacy_codec_price_cache_max_len_price(std::uint32_t fast_bytes);
std::uint32_t legacy_codec_price_cache_pos_state_count(std::uint32_t pos_state_bits);
void legacy_codec_price_cache_store_limit(
        std::uint32_t *limit_slot, std::uint32_t limit);
int legacy_codec_run_header_decode(const LegacyCodecHeaderDecodeRequest &request);
int legacy_codec_header_decode_result_from_status(int core_result, int decode_status);
bool legacy_codec_header_decode_has_minimum_input(const unsigned long long *input_size);
void legacy_codec_header_decode_reset_input_size(unsigned long long *input_size);
void *legacy_codec_header_decode_initial_output(void **output_position);
void legacy_codec_header_decode_store_output(
        void **output_position, void *decoded_output_position);
int *legacy_codec_header_decode_status_slot(
        int *request_decode_status, int *local_decode_status);
int legacy_codec_header_decode_final_status(
        const int *request_decode_status, int local_decode_status);
int legacy_codec_status_value(LegacyCodecStatus status);
int legacy_codec_decode_status_value(LegacyCodecDecodeStatus status);
int legacy_codec_run_streaming_decode(const LegacyCodecStreamingDecodeRequest &request);
unsigned long long legacy_codec_streaming_output_limit(
        unsigned long long read_position, unsigned long long write_position,
        unsigned long long output_remaining);
unsigned long long legacy_codec_streaming_produced_size(
        unsigned long long read_before, unsigned long long read_after);
int legacy_codec_streaming_finish_mode(
        unsigned long long output_limit, unsigned long long read_position,
        unsigned long long output_remaining, int requested_finish_mode);
bool legacy_codec_streaming_decode_finished(
        int status, unsigned long long produced_size);
unsigned long long legacy_codec_streaming_remaining_after(
        unsigned long long remaining, unsigned long long consumed);
unsigned long long legacy_codec_range_decoder_initial_limit();
void legacy_codec_prepare_decoder_range(LegacyCodecDecoderRangeState &state,
        bool reset_buffered_input, bool finish_input);
void legacy_codec_reset_decoder_range(LegacyCodecDecoderRangeState &state);
bool legacy_codec_range_decoder_normalize(LegacyCodecRangeDecoderCursor &cursor);
LegacyCodecRangeBitResult legacy_codec_range_decode_bit(
        LegacyCodecRangeDecoderCursor &cursor, std::uint16_t &probability);
LegacyCodecRangeBitTreeResult legacy_codec_range_decode_bit_tree(
        LegacyCodecRangeDecoderCursor &cursor, std::uint16_t *probabilities,
        std::uint32_t symbol_limit);
LegacyCodecRangeBitTreeResult legacy_codec_range_decode_reverse_bit_tree(
        LegacyCodecRangeDecoderCursor &cursor, std::uint16_t *probabilities,
        std::uint32_t bit_count);
LegacyCodecRangeBitTreeResult legacy_codec_range_decode_direct_bits(
        LegacyCodecRangeDecoderCursor &cursor, std::uint32_t bit_count);
LegacyCodecRangeLengthResult legacy_codec_range_decode_length(
        LegacyCodecRangeDecoderCursor &cursor, std::uint16_t *probabilities,
        std::uint32_t pos_state);
LegacyCodecRangeBitTreeResult legacy_codec_range_decode_pos_slot(
        LegacyCodecRangeDecoderCursor &cursor, std::uint16_t *probabilities,
        std::uint32_t length_symbol);
LegacyCodecRangeDistanceResult legacy_codec_range_decode_reverse_distance(
        LegacyCodecRangeDecoderCursor &cursor, std::uint16_t *probabilities,
        std::uint32_t pos_slot);
LegacyCodecRangeDistanceResult legacy_codec_range_decode_direct_distance(
        LegacyCodecRangeDecoderCursor &cursor, std::uint16_t *align_probabilities,
        std::uint32_t pos_slot);
LegacyCodecRangeLiteralResult legacy_codec_range_decode_literal(
        LegacyCodecRangeDecoderCursor &cursor, const LegacyCodecRangeLiteralRequest &request);
LegacyCodecOutputWindowCopyResult legacy_codec_copy_match_to_output_window(
        const LegacyCodecOutputWindowCopyRequest &request);
LegacyCodecOutputWindowCopyResult legacy_codec_copy_pending_match_to_output_window(
        const LegacyCodecPendingMatchCopyRequest &request);
std::uint32_t legacy_codec_output_window_copy_length(
        std::uint64_t position, std::uint64_t output_limit, std::uint32_t requested_length);
std::uint64_t legacy_codec_output_window_source_position(
        std::uint64_t position, std::uint32_t cyclic_size, std::uint32_t distance);
LegacyCodecOutputWindowByteResult legacy_codec_output_window_byte_at_distance(
        const LegacyCodecOutputWindowByteRequest &request);
LegacyCodecOutputWindowByteResult legacy_codec_output_window_previous_byte(
        const std::uint8_t *buffer, std::uint64_t position, std::uint32_t cyclic_size);
LegacyCodecOutputWindowWriteByteResult legacy_codec_write_byte_to_output_window(
        const LegacyCodecOutputWindowWriteByteRequest &request);
LegacyCodecDistanceSlotInfo legacy_codec_distance_slot_info(std::uint32_t pos_slot);
std::uint32_t legacy_codec_decoder_position_state(
        std::uint32_t processed_size, std::uint32_t position_bits);
std::uint64_t legacy_codec_decoder_iteration_output_limit(
        const LegacyCodecDecoderOutputLimitRequest &request);
bool legacy_codec_decoder_should_save_iteration(
        const std::uint8_t *input, const std::uint8_t *input_end,
        std::uint64_t position, std::uint64_t output_limit);
bool legacy_codec_decoder_has_history(
        std::uint32_t processed_size, std::uint32_t pending_limit);
bool legacy_codec_decoder_distance_available(
        std::uint32_t distance_minus_one, std::uint32_t processed_size,
        std::uint32_t pending_limit);
std::uint32_t legacy_codec_decoder_match_length_from_symbol(std::uint32_t length_symbol);
bool legacy_codec_decoder_is_end_marker_distance(std::uint32_t distance_minus_one);
std::uint32_t legacy_codec_clamp_pending_match_length(std::uint32_t length);
bool legacy_codec_decoder_has_pending_match(std::uint32_t length);
void legacy_codec_reps_after_new_match(
        LegacyCodecRepDistances &reps, std::uint32_t distance);
std::uint32_t legacy_codec_reps_after_repeated_match(
        LegacyCodecRepDistances &reps, std::uint32_t rep_index);
std::uint32_t legacy_codec_state_after_literal(std::uint32_t state);
std::uint32_t legacy_codec_state_after_match(std::uint32_t state);
std::uint32_t legacy_codec_state_after_rep(std::uint32_t state);
std::uint32_t legacy_codec_state_after_short_rep(std::uint32_t state);
std::uint32_t legacy_codec_literal_context_index(std::uint64_t processed_position,
        std::uint8_t previous_byte, std::uint32_t lc, std::uint32_t lp);
std::uint16_t *legacy_codec_literal_probabilities_for_context(
        std::uint16_t *literal_probabilities, std::uint32_t context_index);
std::uint16_t *legacy_codec_match_probability_for_state(
        std::uint16_t *probabilities, std::uint32_t state, std::uint32_t pos_state);
std::uint16_t *legacy_codec_state_probability_at(
        std::uint16_t *probabilities, std::uint32_t base_offset, std::uint32_t state);
std::uint16_t *legacy_codec_state_pos_probability_at(
        std::uint16_t *probabilities, std::uint32_t base_offset,
        std::uint32_t state, std::uint32_t pos_state);
std::uint32_t legacy_codec_probability_bank_offset(LegacyCodecProbabilityBank bank);
std::uint16_t *legacy_codec_probability_at_bank(
        std::uint16_t *probabilities, LegacyCodecProbabilityBank bank, std::uint32_t state);
std::uint16_t *legacy_codec_pos_probability_at_bank(
        std::uint16_t *probabilities, LegacyCodecProbabilityBank bank,
        std::uint32_t state, std::uint32_t pos_state);
std::uint16_t *legacy_codec_is_rep_probability(
        std::uint16_t *probabilities, std::uint32_t state);
std::uint16_t *legacy_codec_is_rep_g0_probability(
        std::uint16_t *probabilities, std::uint32_t state);
std::uint16_t *legacy_codec_is_rep_g1_probability(
        std::uint16_t *probabilities, std::uint32_t state);
std::uint16_t *legacy_codec_is_rep_g2_probability(
        std::uint16_t *probabilities, std::uint32_t state);
std::uint16_t *legacy_codec_is_rep0_long_probability(
        std::uint16_t *probabilities, std::uint32_t state, std::uint32_t pos_state);
std::uint16_t *legacy_codec_pos_slot_probabilities(
        std::uint16_t *probabilities);
std::uint16_t *legacy_codec_align_probabilities(
        std::uint16_t *probabilities);
std::uint16_t *legacy_codec_len_probabilities(
        std::uint16_t *probabilities);
std::uint16_t *legacy_codec_rep_len_probabilities(
        std::uint16_t *probabilities);
std::uint16_t *legacy_codec_literal_probabilities(
        std::uint16_t *probabilities);
LegacyCodecMatchFinderCallbacks legacy_codec_select_match_finder_callbacks(
        const LegacyCodecMatchFinderSelection &selection);
std::uint8_t legacy_codec_match_finder_get_byte(const LegacyCodecMatchFinderView &view,
        int offset);
std::uint32_t legacy_codec_match_finder_available_bytes(
        const LegacyCodecMatchFinderView &view);
const std::uint8_t *legacy_codec_match_finder_current_pointer(
        const LegacyCodecMatchFinderView &view);
void legacy_codec_match_finder_rewind_cursor(LegacyCodecMatchFinderCursor &cursor,
        std::uint32_t amount);
void legacy_codec_match_finder_move_window(LegacyCodecMatchFinderWindow &window);
bool legacy_codec_match_finder_should_move_window(
        const LegacyCodecMatchFinderWindow &window);
bool legacy_codec_match_finder_should_fill_input(
        const LegacyCodecMatchFinderFillState &state);
void legacy_codec_match_finder_fill_input(LegacyCodecMatchFinderInputBuffer &state);
std::uint32_t legacy_codec_match_finder_direct_input_writable(
        const LegacyCodecMatchFinderInputBuffer &state);
std::uint32_t legacy_codec_match_finder_direct_input_consumed(
        const LegacyCodecMatchFinderInputBuffer &state);
std::uint32_t legacy_codec_match_finder_input_buffered_size(
        const LegacyCodecMatchFinderInputBuffer &state);
std::uint64_t legacy_codec_match_finder_input_target_offset(
        const LegacyCodecMatchFinderInputBuffer &state);
std::uint64_t legacy_codec_match_finder_input_read_size(
        std::uint32_t buffered, std::uint64_t end_offset);
void legacy_codec_match_finder_refresh_limits(LegacyCodecMatchFinderLimitState &state);
std::uint32_t legacy_codec_match_finder_position_cap(
        const LegacyCodecMatchFinderLimitState &state);
std::uint32_t legacy_codec_match_finder_limit_available(
        const LegacyCodecMatchFinderLimitState &state);
std::uint32_t legacy_codec_match_finder_position_delta(
        std::uint32_t available, std::uint32_t keep_size_after,
        std::uint32_t position_cap);
std::uint32_t legacy_codec_match_finder_match_len_limit(
        std::uint32_t available, std::uint32_t match_max_len);
std::uint32_t legacy_codec_match_finder_hash3(const std::uint8_t *current,
        const std::uint32_t *crc_table, std::uint32_t crc_count);
LegacyCodecMatchFinderHash4 legacy_codec_match_finder_hash4(
        const std::uint8_t *current, const std::uint32_t *crc_table,
        std::uint32_t crc_count, std::uint32_t hash_mask);
LegacyCodecMatchFinderHash4Update legacy_codec_match_finder_update_hash4_buckets(
        std::uint32_t *hash_table, std::uint32_t hash_count,
        const LegacyCodecMatchFinderHash4 &hashes, std::uint32_t position);
LegacyCodecMatchFinderHash3Masked legacy_codec_match_finder_hash3_masked(
        const std::uint8_t *current, const std::uint32_t *crc_table,
        std::uint32_t crc_count, std::uint32_t hash_mask);
LegacyCodecMatchFinderHash3Update legacy_codec_match_finder_update_hash3_buckets(
        std::uint32_t *hash_table, std::uint32_t hash_count,
        const LegacyCodecMatchFinderHash3Masked &hashes, std::uint32_t position);
std::uint32_t legacy_codec_match_finder_hash2_direct(const std::uint8_t *current);
LegacyCodecMatchFinderHash2Update legacy_codec_match_finder_update_hash2_bucket(
        std::uint32_t *hash_table, std::uint32_t hash_count,
        std::uint32_t hash2, std::uint32_t position);
std::uint32_t legacy_codec_match_finder_count_match(const std::uint8_t *current,
        std::uint32_t distance, std::uint32_t start_length, std::uint32_t max_length);
int legacy_codec_match_finder_compare_at(const std::uint8_t *current,
        std::uint32_t distance, std::uint32_t length);
void legacy_codec_match_finder_update_tree_branch(
        LegacyCodecMatchFinderTreeBranch &branch, int comparison,
        std::uint32_t matched_length);
void legacy_codec_match_finder_splice_tree_children(
        std::uint32_t *lower_slot, std::uint32_t *upper_slot,
        const std::uint32_t *candidate_links);
std::uint32_t *legacy_codec_match_finder_tree_links_for_distance(
        std::uint32_t *tree_links, std::uint32_t link_count,
        std::uint32_t cyclic_pos, std::uint32_t cyclic_size, std::uint32_t distance);
std::uint32_t *legacy_codec_match_finder_chain_link_for_distance(
        std::uint32_t *chain_links, std::uint32_t link_count,
        std::uint32_t cyclic_pos, std::uint32_t cyclic_size, std::uint32_t distance);
bool legacy_codec_match_finder_distance_in_history(
        std::uint32_t distance, std::uint32_t history_size);
std::uint32_t legacy_codec_match_finder_wrapped_position(
        std::uint32_t cyclic_pos, std::uint32_t cyclic_size, std::uint32_t distance);
std::uint32_t legacy_codec_match_finder_tree_link_index(std::uint32_t wrapped_position);
std::uint32_t legacy_codec_match_finder_record_count_before(
        const LegacyCodecMatchFinderMatchOutput *output);
std::uint32_t legacy_codec_match_finder_records_added(
        const LegacyCodecMatchFinderMatchOutput *output, std::uint32_t records_before);
std::uint32_t legacy_codec_match_finder_next_tree_candidate(
        int comparison, const std::uint32_t *candidate_links);
std::uint32_t legacy_codec_match_finder_start_length_at_least(
        std::uint32_t length, std::uint32_t minimum);
std::ptrdiff_t legacy_codec_match_finder_candidate_start_offset(
        std::uint32_t start_length, std::uint32_t distance);
bool legacy_codec_match_finder_candidate_start_matches(
        const std::uint8_t *current, std::uint32_t distance,
        std::uint32_t start_length);
std::uint32_t legacy_codec_match_finder_best_length_after_match(
        std::uint32_t best_length, std::uint32_t matched_length);
LegacyCodecMatchFinderTreeProbeResult legacy_codec_match_finder_probe_tree(
        LegacyCodecMatchFinderTreeProbe &probe);
LegacyCodecMatchFinderTreeSearchResult legacy_codec_match_finder_search_binary_tree(
        LegacyCodecMatchFinderTreeSearch &search);
LegacyCodecMatchFinderShortMatchResult legacy_codec_match_finder_try_short_match(
        LegacyCodecMatchFinderShortMatch &match);
LegacyCodecMatchFinderShortMatchPairResult
legacy_codec_match_finder_try_short_match_pair(
        LegacyCodecMatchFinderShortMatchPair &match);
bool legacy_codec_match_finder_record_if_better(
        LegacyCodecMatchFinderMatchOutput &output, std::uint32_t length,
        std::uint32_t distance);
void legacy_codec_match_finder_advance_position(
        LegacyCodecMatchFinderAdvanceState &state);
void legacy_codec_match_finder_hash_chain_skip(
        LegacyCodecMatchFinderHashChainSkipState &state, std::uint32_t count);
LegacyCodecMatchFinderHashChainFindResult legacy_codec_match_finder_hash_chain_find(
        LegacyCodecMatchFinderHashChainFindState &state);
void legacy_codec_match_finder_binary_tree4_skip(
        LegacyCodecMatchFinderBinaryTree4SkipState &state, std::uint32_t count);
void legacy_codec_match_finder_binary_tree3_skip(
        LegacyCodecMatchFinderBinaryTree3SkipState &state, std::uint32_t count);
void legacy_codec_match_finder_binary_tree2_skip(
        LegacyCodecMatchFinderBinaryTree2SkipState &state, std::uint32_t count);
LegacyCodecMatchFinderBinaryTreeFindResult legacy_codec_match_finder_binary_tree2_find(
        LegacyCodecMatchFinderBinaryTree2FindState &state);
LegacyCodecMatchFinderBinaryTreeFindResult legacy_codec_match_finder_binary_tree3_find(
        LegacyCodecMatchFinderBinaryTree3FindState &state);
LegacyCodecMatchFinderBinaryTreeFindResult legacy_codec_match_finder_binary_tree4_find(
        LegacyCodecMatchFinderBinaryTree4FindState &state);
void legacy_codec_match_finder_init(LegacyCodecMatchFinderInitState &state);
void legacy_codec_match_finder_normalize_offsets(std::uint32_t base,
        std::uint32_t *values, std::uint32_t count);
std::uint32_t legacy_codec_match_finder_normalized_offset(
        std::uint32_t value, std::uint32_t base);
LegacyCodecMatchFinderMemoryPlan legacy_codec_make_match_finder_memory_plan(
        const LegacyCodecMatchFinderMemoryPlanRequest &request);
bool legacy_codec_match_finder_dictionary_size_supported(std::uint32_t dictionary_size);
std::uint32_t legacy_codec_match_finder_history_size(std::uint32_t dictionary_size);
std::uint32_t legacy_codec_match_finder_dictionary_fraction_shift(
        std::uint32_t dictionary_size);
std::uint32_t legacy_codec_match_finder_keep_size_after(
        const LegacyCodecMatchFinderMemoryPlanRequest &request);
std::uint32_t legacy_codec_match_finder_block_size(
        const LegacyCodecMatchFinderMemoryPlanRequest &request,
        std::uint32_t history_size, std::uint32_t dictionary_fraction_shift,
        std::uint32_t keep_size_after);
std::uint32_t legacy_codec_match_finder_match_buffer_size(
        std::uint32_t history_size, bool binary_tree_mode);
bool legacy_codec_match_finder_uses_extended_hash(std::uint32_t hash_bytes);
std::uint32_t legacy_codec_match_finder_hash_mask(
        std::uint32_t dictionary_size, std::uint32_t hash_bytes);
std::uint32_t legacy_codec_match_finder_hash_size(std::uint32_t hash_mask);
std::uint32_t legacy_codec_match_finder_son_offset(std::uint32_t hash_bytes);
void legacy_codec_free_match_finder_window(
        const LegacyCodecMatchFinderAllocationRequest &request);
void legacy_codec_free_match_finder_tables(
        const LegacyCodecMatchFinderAllocationRequest &request);
std::uint32_t legacy_codec_match_finder_hash_table_words(
        const LegacyCodecMatchFinderMemoryPlan &plan);
std::uint32_t legacy_codec_match_finder_son_table_words(
        const LegacyCodecMatchFinderMemoryPlan &plan);
std::uint32_t legacy_codec_match_finder_total_table_words(
        std::uint32_t hash_words, std::uint32_t son_words);
std::uint32_t *legacy_codec_match_finder_son_table_base(
        std::uint32_t *hash_table, std::uint32_t hash_words);
bool legacy_codec_match_finder_reuse_tables(
        LegacyCodecMatchFinderAllocation &allocation,
        std::uint32_t hash_words, std::uint32_t son_words);
bool legacy_codec_allocate_match_finder_memory(
        const LegacyCodecMatchFinderAllocationRequest &request);

}  // namespace kksdk

extern "C" void kksdk_legacy_codec_init_state(void *state);
extern "C" void *kksdk_legacy_codec_allocate_state(void *allocator,
        void *(*alloc_fn)(void *, unsigned long long));
extern "C" void kksdk_legacy_codec_save_snapshot(const void *view);
extern "C" void kksdk_legacy_codec_restore_snapshot(const void *view);
extern "C" void kksdk_legacy_codec_reset_probability_state(const void *options,
        const void *view);
extern "C" int kksdk_legacy_codec_run_bounded_encode(const void *request, void *result);
extern "C" int kksdk_legacy_codec_bounded_encode_has_required_callbacks(
        const void *request);
extern "C" void kksdk_legacy_codec_bounded_encode_reset_if_requested(
        const void *request);
extern "C" unsigned long long kksdk_legacy_codec_bounded_encode_output_position(
        const void *state, unsigned long long fallback_position);
extern "C" unsigned int kksdk_legacy_codec_bounded_encode_processed_position(
        const void *state, unsigned int fallback_position);
extern "C" unsigned long long kksdk_legacy_codec_bounded_encode_output_delta(
        unsigned long long before, unsigned long long after);
extern "C" unsigned int kksdk_legacy_codec_bounded_encode_processed_delta(
        unsigned int before, unsigned int after);
extern "C" int kksdk_legacy_codec_prepare_output_stream(const void *request);
extern "C" int kksdk_legacy_codec_prepare_input_progress_stream(const void *request);
extern "C" int kksdk_legacy_codec_prepare_stream_common(const void *request);
extern "C" int kksdk_legacy_codec_prepare_stream_has_state(const void *request);
extern "C" void kksdk_legacy_codec_prepare_stream_bind_contexts(const void *request);
extern "C" void kksdk_legacy_codec_noop_callback();
extern "C" int kksdk_legacy_codec_run_encode_loop(const void *request);
extern "C" int kksdk_legacy_codec_encode_loop_has_required_callbacks(
        const void *request);
extern "C" int kksdk_legacy_codec_encode_loop_should_continue(const bool *finished);
extern "C" unsigned long long kksdk_legacy_codec_encode_loop_progress_position(
        const void *state);
extern "C" int kksdk_legacy_codec_run_owned_encode(const void *request);
extern "C" int kksdk_legacy_codec_owned_encode_has_header(const void *request);
extern "C" int kksdk_legacy_codec_owned_encode_apply_options(
        void *state, const void *options);
extern "C" int kksdk_legacy_codec_owned_encode_apply_header(
        void *state, const void *request);
extern "C" void kksdk_legacy_codec_refresh_price_caches(const void *request);
extern "C" void kksdk_legacy_codec_price_cache_refresh_distance_tables(
        const void *request);
extern "C" void kksdk_legacy_codec_price_cache_refresh_optional(
        void (*refresh)(void *), void *context);
extern "C" unsigned int kksdk_legacy_codec_price_cache_max_len_price(
        unsigned int fast_bytes);
extern "C" unsigned int kksdk_legacy_codec_price_cache_pos_state_count(
        unsigned int pos_state_bits);
extern "C" void kksdk_legacy_codec_price_cache_store_limit(
        unsigned int *limit_slot, unsigned int limit);
extern "C" int kksdk_legacy_codec_run_header_decode(const void *request);
extern "C" int kksdk_legacy_codec_header_decode_result_from_status(
        int core_result, int decode_status);
extern "C" int kksdk_legacy_codec_header_decode_has_minimum_input(
        const unsigned long long *input_size);
extern "C" void kksdk_legacy_codec_header_decode_reset_input_size(
        unsigned long long *input_size);
extern "C" void *kksdk_legacy_codec_header_decode_initial_output(
        void **output_position);
extern "C" void kksdk_legacy_codec_header_decode_store_output(
        void **output_position, void *decoded_output_position);
extern "C" int *kksdk_legacy_codec_header_decode_status_slot(
        int *request_decode_status, int *local_decode_status);
extern "C" int kksdk_legacy_codec_header_decode_final_status(
        const int *request_decode_status, int local_decode_status);
extern "C" int kksdk_legacy_codec_status_value(int status);
extern "C" int kksdk_legacy_codec_decode_status_value(int status);
extern "C" int kksdk_legacy_codec_run_streaming_decode(const void *request);
extern "C" unsigned long long kksdk_legacy_codec_streaming_output_limit(
        unsigned long long read_position, unsigned long long write_position,
        unsigned long long output_remaining);
extern "C" unsigned long long kksdk_legacy_codec_streaming_produced_size(
        unsigned long long read_before, unsigned long long read_after);
extern "C" int kksdk_legacy_codec_streaming_finish_mode(
        unsigned long long output_limit, unsigned long long read_position,
        unsigned long long output_remaining, int requested_finish_mode);
extern "C" int kksdk_legacy_codec_streaming_decode_finished(
        int status, unsigned long long produced_size);
extern "C" unsigned long long kksdk_legacy_codec_streaming_remaining_after(
        unsigned long long remaining, unsigned long long consumed);
extern "C" unsigned long long kksdk_legacy_codec_range_decoder_initial_limit();
extern "C" void kksdk_legacy_codec_prepare_decoder_range(
        void *state, int reset_buffered_input, int finish_input);
extern "C" void kksdk_legacy_codec_reset_decoder_range(void *state);
extern "C" int kksdk_legacy_codec_range_decoder_normalize(void *cursor);
extern "C" int kksdk_legacy_codec_range_decode_bit(void *cursor, void *probability,
        void *out_result);
extern "C" int kksdk_legacy_codec_range_decode_bit_tree(void *cursor,
        void *probabilities, unsigned int symbol_limit, void *out_result);
extern "C" int kksdk_legacy_codec_range_decode_reverse_bit_tree(void *cursor,
        void *probabilities, unsigned int bit_count, void *out_result);
extern "C" int kksdk_legacy_codec_range_decode_direct_bits(void *cursor,
        unsigned int bit_count, void *out_result);
extern "C" int kksdk_legacy_codec_range_decode_length(void *cursor,
        void *probabilities, unsigned int pos_state, void *out_result);
extern "C" int kksdk_legacy_codec_range_decode_pos_slot(void *cursor,
        void *probabilities, unsigned int length_symbol, void *out_result);
extern "C" int kksdk_legacy_codec_range_decode_reverse_distance(void *cursor,
        void *probabilities, unsigned int pos_slot, void *out_result);
extern "C" int kksdk_legacy_codec_range_decode_direct_distance(void *cursor,
        void *align_probabilities, unsigned int pos_slot, void *out_result);
extern "C" int kksdk_legacy_codec_range_decode_literal(void *cursor,
        const void *request, void *out_result);
extern "C" int kksdk_legacy_codec_copy_match_to_output_window(
        const void *request, void *out_result);
extern "C" int kksdk_legacy_codec_copy_pending_match_to_output_window(
        const void *request, void *out_result);
extern "C" unsigned int kksdk_legacy_codec_output_window_copy_length(
        unsigned long long position, unsigned long long output_limit,
        unsigned int requested_length);
extern "C" unsigned long long kksdk_legacy_codec_output_window_source_position(
        unsigned long long position, unsigned int cyclic_size, unsigned int distance);
extern "C" int kksdk_legacy_codec_output_window_byte_at_distance(
        const void *request, void *out_result);
extern "C" int kksdk_legacy_codec_output_window_previous_byte(
        const unsigned char *buffer, unsigned long long position,
        unsigned int cyclic_size, void *out_result);
extern "C" int kksdk_legacy_codec_write_byte_to_output_window(
        const void *request, void *out_result);
extern "C" int kksdk_legacy_codec_distance_slot_info(
        unsigned int pos_slot, void *out_result);
extern "C" unsigned int kksdk_legacy_codec_decoder_position_state(
        unsigned int processed_size, unsigned int position_bits);
extern "C" unsigned long long kksdk_legacy_codec_decoder_iteration_output_limit(
        const void *request);
extern "C" int kksdk_legacy_codec_decoder_should_save_iteration(
        const unsigned char *input, const unsigned char *input_end,
        unsigned long long position, unsigned long long output_limit);
extern "C" int kksdk_legacy_codec_decoder_has_history(
        unsigned int processed_size, unsigned int pending_limit);
extern "C" int kksdk_legacy_codec_decoder_distance_available(
        unsigned int distance_minus_one, unsigned int processed_size,
        unsigned int pending_limit);
extern "C" unsigned int kksdk_legacy_codec_decoder_match_length_from_symbol(
        unsigned int length_symbol);
extern "C" int kksdk_legacy_codec_decoder_is_end_marker_distance(
        unsigned int distance_minus_one);
extern "C" unsigned int kksdk_legacy_codec_clamp_pending_match_length(
        unsigned int length);
extern "C" int kksdk_legacy_codec_decoder_has_pending_match(unsigned int length);
extern "C" void kksdk_legacy_codec_reps_after_new_match(
        void *reps, unsigned int distance);
extern "C" unsigned int kksdk_legacy_codec_reps_after_repeated_match(
        void *reps, unsigned int rep_index);
extern "C" unsigned int kksdk_legacy_codec_state_after_literal(unsigned int state);
extern "C" unsigned int kksdk_legacy_codec_state_after_match(unsigned int state);
extern "C" unsigned int kksdk_legacy_codec_state_after_rep(unsigned int state);
extern "C" unsigned int kksdk_legacy_codec_state_after_short_rep(unsigned int state);
extern "C" unsigned int kksdk_legacy_codec_literal_context_index(
        unsigned long long processed_position, unsigned char previous_byte,
        unsigned int lc, unsigned int lp);
extern "C" void *kksdk_legacy_codec_literal_probabilities_for_context(
        void *literal_probabilities, unsigned int context_index);
extern "C" void *kksdk_legacy_codec_match_probability_for_state(
        void *probabilities, unsigned int state, unsigned int pos_state);
extern "C" void *kksdk_legacy_codec_state_probability_at(
        void *probabilities, unsigned int base_offset, unsigned int state);
extern "C" void *kksdk_legacy_codec_state_pos_probability_at(
        void *probabilities, unsigned int base_offset,
        unsigned int state, unsigned int pos_state);
extern "C" unsigned int kksdk_legacy_codec_probability_bank_offset(unsigned int bank);
extern "C" void *kksdk_legacy_codec_probability_at_bank(
        void *probabilities, unsigned int bank, unsigned int state);
extern "C" void *kksdk_legacy_codec_pos_probability_at_bank(
        void *probabilities, unsigned int bank, unsigned int state, unsigned int pos_state);
extern "C" void *kksdk_legacy_codec_is_rep_probability(
        void *probabilities, unsigned int state);
extern "C" void *kksdk_legacy_codec_is_rep_g0_probability(
        void *probabilities, unsigned int state);
extern "C" void *kksdk_legacy_codec_is_rep_g1_probability(
        void *probabilities, unsigned int state);
extern "C" void *kksdk_legacy_codec_is_rep_g2_probability(
        void *probabilities, unsigned int state);
extern "C" void *kksdk_legacy_codec_is_rep0_long_probability(
        void *probabilities, unsigned int state, unsigned int pos_state);
extern "C" void *kksdk_legacy_codec_pos_slot_probabilities(void *probabilities);
extern "C" void *kksdk_legacy_codec_align_probabilities(void *probabilities);
extern "C" void *kksdk_legacy_codec_len_probabilities(void *probabilities);
extern "C" void *kksdk_legacy_codec_rep_len_probabilities(void *probabilities);
extern "C" void *kksdk_legacy_codec_literal_probabilities(void *probabilities);
extern "C" void kksdk_legacy_codec_select_match_finder_callbacks(
        const void *selection, void *out_callbacks);
extern "C" unsigned char kksdk_legacy_codec_match_finder_get_byte(
        const void *view, int offset);
extern "C" unsigned int kksdk_legacy_codec_match_finder_available_bytes(const void *view);
extern "C" const unsigned char *kksdk_legacy_codec_match_finder_current_pointer(
        const void *view);
extern "C" void kksdk_legacy_codec_match_finder_rewind_cursor(void *cursor,
        unsigned int amount);
extern "C" void kksdk_legacy_codec_match_finder_move_window(void *window);
extern "C" int kksdk_legacy_codec_match_finder_should_move_window(const void *window);
extern "C" int kksdk_legacy_codec_match_finder_should_fill_input(const void *state);
extern "C" unsigned int kksdk_legacy_codec_match_finder_position_cap(
        const void *state);
extern "C" unsigned int kksdk_legacy_codec_match_finder_limit_available(
        const void *state);
extern "C" unsigned int kksdk_legacy_codec_match_finder_position_delta(
        unsigned int available, unsigned int keep_size_after, unsigned int position_cap);
extern "C" unsigned int kksdk_legacy_codec_match_finder_match_len_limit(
        unsigned int available, unsigned int match_max_len);
extern "C" void kksdk_legacy_codec_match_finder_fill_input(void *state);
extern "C" unsigned int kksdk_legacy_codec_match_finder_direct_input_writable(
        const void *state);
extern "C" unsigned int kksdk_legacy_codec_match_finder_direct_input_consumed(
        const void *state);
extern "C" unsigned int kksdk_legacy_codec_match_finder_input_buffered_size(
        const void *state);
extern "C" unsigned long long kksdk_legacy_codec_match_finder_input_target_offset(
        const void *state);
extern "C" unsigned long long kksdk_legacy_codec_match_finder_input_read_size(
        unsigned int buffered, unsigned long long end_offset);
extern "C" void kksdk_legacy_codec_match_finder_refresh_limits(void *state);
extern "C" unsigned int kksdk_legacy_codec_match_finder_hash3(
        const unsigned char *current, const unsigned int *crc_table, unsigned int crc_count);
extern "C" int kksdk_legacy_codec_match_finder_hash4(
        const unsigned char *current, const unsigned int *crc_table, unsigned int crc_count,
        unsigned int hash_mask, void *out_hashes);
extern "C" int kksdk_legacy_codec_match_finder_update_hash4_buckets(
        void *hash_table, unsigned int hash_count, const void *hashes,
        unsigned int position, void *out_update);
extern "C" int kksdk_legacy_codec_match_finder_hash3_masked(
        const unsigned char *current, const unsigned int *crc_table, unsigned int crc_count,
        unsigned int hash_mask, void *out_hashes);
extern "C" int kksdk_legacy_codec_match_finder_update_hash3_buckets(
        void *hash_table, unsigned int hash_count, const void *hashes,
        unsigned int position, void *out_update);
extern "C" unsigned int kksdk_legacy_codec_match_finder_hash2_direct(
        const unsigned char *current);
extern "C" int kksdk_legacy_codec_match_finder_update_hash2_bucket(
        void *hash_table, unsigned int hash_count, unsigned int hash2,
        unsigned int position, void *out_update);
extern "C" unsigned int kksdk_legacy_codec_match_finder_count_match(
        const unsigned char *current, unsigned int distance, unsigned int start_length,
        unsigned int max_length);
extern "C" int kksdk_legacy_codec_match_finder_compare_at(
        const unsigned char *current, unsigned int distance, unsigned int length);
extern "C" void kksdk_legacy_codec_match_finder_update_tree_branch(
        void *branch, int comparison, unsigned int matched_length);
extern "C" void kksdk_legacy_codec_match_finder_splice_tree_children(
        void *lower_slot, void *upper_slot, const void *candidate_links);
extern "C" void *kksdk_legacy_codec_match_finder_tree_links_for_distance(
        void *tree_links, unsigned int link_count, unsigned int cyclic_pos,
        unsigned int cyclic_size, unsigned int distance);
extern "C" void *kksdk_legacy_codec_match_finder_chain_link_for_distance(
        void *chain_links, unsigned int link_count, unsigned int cyclic_pos,
        unsigned int cyclic_size, unsigned int distance);
extern "C" int kksdk_legacy_codec_match_finder_distance_in_history(
        unsigned int distance, unsigned int history_size);
extern "C" unsigned int kksdk_legacy_codec_match_finder_wrapped_position(
        unsigned int cyclic_pos, unsigned int cyclic_size, unsigned int distance);
extern "C" unsigned int kksdk_legacy_codec_match_finder_tree_link_index(
        unsigned int wrapped_position);
extern "C" unsigned int kksdk_legacy_codec_match_finder_record_count_before(
        const void *output);
extern "C" unsigned int kksdk_legacy_codec_match_finder_records_added(
        const void *output, unsigned int records_before);
extern "C" unsigned int kksdk_legacy_codec_match_finder_next_tree_candidate(
        int comparison, const void *candidate_links);
extern "C" unsigned int kksdk_legacy_codec_match_finder_start_length_at_least(
        unsigned int length, unsigned int minimum);
extern "C" long long kksdk_legacy_codec_match_finder_candidate_start_offset(
        unsigned int start_length, unsigned int distance);
extern "C" int kksdk_legacy_codec_match_finder_candidate_start_matches(
        const unsigned char *current, unsigned int distance, unsigned int start_length);
extern "C" unsigned int kksdk_legacy_codec_match_finder_best_length_after_match(
        unsigned int best_length, unsigned int matched_length);
extern "C" int kksdk_legacy_codec_match_finder_probe_tree(
        void *probe, void *out_result);
extern "C" int kksdk_legacy_codec_match_finder_search_binary_tree(
        void *search, void *out_result);
extern "C" int kksdk_legacy_codec_match_finder_try_short_match(
        void *match, void *out_result);
extern "C" int kksdk_legacy_codec_match_finder_try_short_match_pair(
        void *match, void *out_result);
extern "C" int kksdk_legacy_codec_match_finder_record_if_better(
        void *output, unsigned int length, unsigned int distance);
extern "C" void kksdk_legacy_codec_match_finder_advance_position(void *state);
extern "C" void kksdk_legacy_codec_match_finder_hash_chain_skip(
        void *state, unsigned int count);
extern "C" int kksdk_legacy_codec_match_finder_hash_chain_find(
        void *state, void *out_result);
extern "C" void kksdk_legacy_codec_match_finder_binary_tree4_skip(
        void *state, unsigned int count);
extern "C" void kksdk_legacy_codec_match_finder_binary_tree3_skip(
        void *state, unsigned int count);
extern "C" void kksdk_legacy_codec_match_finder_binary_tree2_skip(
        void *state, unsigned int count);
extern "C" int kksdk_legacy_codec_match_finder_binary_tree2_find(
        void *state, void *out_result);
extern "C" int kksdk_legacy_codec_match_finder_binary_tree3_find(
        void *state, void *out_result);
extern "C" int kksdk_legacy_codec_match_finder_binary_tree4_find(
        void *state, void *out_result);
extern "C" void kksdk_legacy_codec_match_finder_init(void *state);
extern "C" void kksdk_legacy_codec_match_finder_normalize_offsets(
        unsigned int base, void *values, unsigned int count);
extern "C" unsigned int kksdk_legacy_codec_match_finder_normalized_offset(
        unsigned int value, unsigned int base);
extern "C" int kksdk_legacy_codec_make_match_finder_memory_plan(
        const void *request, void *out_plan);
extern "C" int kksdk_legacy_codec_match_finder_dictionary_size_supported(
        unsigned int dictionary_size);
extern "C" unsigned int kksdk_legacy_codec_match_finder_history_size(
        unsigned int dictionary_size);
extern "C" unsigned int kksdk_legacy_codec_match_finder_dictionary_fraction_shift(
        unsigned int dictionary_size);
extern "C" unsigned int kksdk_legacy_codec_match_finder_keep_size_after(
        const void *request);
extern "C" unsigned int kksdk_legacy_codec_match_finder_block_size(
        const void *request, unsigned int history_size,
        unsigned int dictionary_fraction_shift, unsigned int keep_size_after);
extern "C" unsigned int kksdk_legacy_codec_match_finder_match_buffer_size(
        unsigned int history_size, int binary_tree_mode);
extern "C" int kksdk_legacy_codec_match_finder_uses_extended_hash(
        unsigned int hash_bytes);
extern "C" unsigned int kksdk_legacy_codec_match_finder_hash_mask(
        unsigned int dictionary_size, unsigned int hash_bytes);
extern "C" unsigned int kksdk_legacy_codec_match_finder_hash_size(
        unsigned int hash_mask);
extern "C" unsigned int kksdk_legacy_codec_match_finder_son_offset(
        unsigned int hash_bytes);
extern "C" int kksdk_legacy_codec_allocate_match_finder_memory(const void *request);
extern "C" void kksdk_legacy_codec_free_match_finder_window(const void *request);
extern "C" void kksdk_legacy_codec_free_match_finder_tables(const void *request);
extern "C" unsigned int kksdk_legacy_codec_match_finder_hash_table_words(
        const void *plan);
extern "C" unsigned int kksdk_legacy_codec_match_finder_son_table_words(
        const void *plan);
extern "C" unsigned int kksdk_legacy_codec_match_finder_total_table_words(
        unsigned int hash_words, unsigned int son_words);
extern "C" void *kksdk_legacy_codec_match_finder_son_table_base(
        void *hash_table, unsigned int hash_words);
extern "C" int kksdk_legacy_codec_match_finder_reuse_tables(
        void *allocation, unsigned int hash_words, unsigned int son_words);
