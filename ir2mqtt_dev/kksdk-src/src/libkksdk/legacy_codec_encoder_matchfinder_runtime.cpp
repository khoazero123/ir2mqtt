#include "legacy_codec_encoder_matchfinder_runtime.hpp"

#include "legacy_codec_encoder_matchfinder_layout.hpp"
#include "legacy_codec_encoder_state.hpp"
#include "legacy_codec_state.hpp"
#include "legacy_codec_tables.hpp"

#include <cstring>

namespace kksdk {
void legacy_codec_encoder_matchfinder_runtime_normalize(void *matchfinder);

bool g_lift_mf_find_hold_position = false;

void legacy_codec_encoder_set_matchfinder_find_hold_position(bool hold) {
    g_lift_mf_find_hold_position = hold;
}

bool legacy_codec_encoder_matchfinder_find_hold_position() {
    return g_lift_mf_find_hold_position;
}

namespace {

using namespace legacy_oem_matchfinder;
using namespace legacy_oem_encoder;

using OemStreamReadFn = int (*)(void *stream, std::int64_t dest, std::int64_t *size);

constexpr std::uint32_t kEncoderMatchBufferRecordCapacity =
        static_cast<std::uint32_t>((kStateSize - kMatchBuffer) /
                sizeof(LegacyCodecMatchFinderMatchRecord));

std::uint8_t *matchfinder_bytes(void *matchfinder) {
    return static_cast<std::uint8_t *>(matchfinder);
}

std::uint64_t *matchfinder_words(void *matchfinder) {
    return static_cast<std::uint64_t *>(matchfinder);
}

void refresh_matchfinder_limits(void *matchfinder) {
    auto *bytes = matchfinder_bytes(matchfinder);
    auto *words = matchfinder_words(matchfinder);

    LegacyCodecMatchFinderLimitState limits{};
    limits.cyclic_pos = *reinterpret_cast<std::uint32_t *>(bytes + kCyclicPos);
    limits.cyclic_size = *reinterpret_cast<std::uint32_t *>(bytes + kHistorySize);
    limits.read_pos = static_cast<std::uint32_t>(words[1]);
    limits.write_pos = static_cast<std::uint32_t>(words[2]);
    limits.keep_size_after = *reinterpret_cast<std::uint32_t *>(bytes + kKeepAfter);
    limits.match_max_len = static_cast<std::uint32_t>(words[4]);

    legacy_codec_match_finder_refresh_limits(limits);

    *reinterpret_cast<std::uint32_t *>(bytes + kCyclicPos) = limits.cyclic_pos;
    *reinterpret_cast<std::uint32_t *>(bytes + kPositionLimit) = limits.position_limit;
    *reinterpret_cast<std::uint32_t *>(bytes + kMatchLenLimit) = limits.match_len_limit;
}

void normalize_hash_table_offsets(void *matchfinder, std::uint32_t base) {
    auto *bytes = matchfinder_bytes(matchfinder);
    const std::uint32_t hash_words =
            *reinterpret_cast<std::uint32_t *>(bytes + kHashTableWords);
    const std::uint32_t son_words =
            *reinterpret_cast<std::uint32_t *>(bytes + kSonTableWords);
    const std::uint32_t total_words = hash_words + son_words;
    if (total_words == 0U) {
        return;
    }

    auto *hash_table = *reinterpret_cast<std::uint32_t **>(bytes + kHashTables);
    legacy_codec_match_finder_normalize_offsets(base, hash_table, total_words);
}

void load_bt4_common(void *matchfinder, std::uint32_t &position, std::uint32_t &cyclic_pos,
        const std::uint8_t *&current, std::uint32_t &position_limit,
        std::uint32_t &match_len_limit, std::uint32_t &max_depth, std::uint32_t &hash_mask,
        std::uint32_t *&hash_table, std::uint32_t &hash_count, std::uint32_t *&tree_links,
        std::uint32_t &tree_link_count, const std::uint32_t *&crc_table) {
    auto *bytes = matchfinder_bytes(matchfinder);
    auto *words = matchfinder_words(matchfinder);

    current = reinterpret_cast<const std::uint8_t *>(words[0]);
    position = static_cast<std::uint32_t>(words[1]);
    cyclic_pos = *reinterpret_cast<std::uint32_t *>(bytes + kCyclicPos);
    position_limit = *reinterpret_cast<std::uint32_t *>(bytes + kPositionLimit);
    match_len_limit = *reinterpret_cast<std::uint32_t *>(bytes + kMatchLenLimit);
    max_depth = *reinterpret_cast<std::uint32_t *>(bytes + kMaxDepth);
    hash_mask = *reinterpret_cast<std::uint32_t *>(bytes + kHashMask);
    hash_table = *reinterpret_cast<std::uint32_t **>(bytes + kHashTables);
    hash_count = *reinterpret_cast<std::uint32_t *>(bytes + kHashTableWords);
    tree_links = *reinterpret_cast<std::uint32_t **>(bytes + kSonTablePtr);
    tree_link_count = *reinterpret_cast<std::uint32_t *>(bytes + kSonTableWords);
    crc_table = reinterpret_cast<const std::uint32_t *>(bytes + kCrcTable);
}

void load_hc_common(void *matchfinder, std::uint32_t &position, std::uint32_t &cyclic_pos,
        const std::uint8_t *&current, std::uint32_t &position_limit,
        std::uint32_t &match_len_limit, std::uint32_t &max_depth, std::uint32_t *&hash_table,
        std::uint32_t &hash_count, std::uint32_t *&chain_table, std::uint32_t &chain_count,
        const std::uint32_t *&crc_table) {
    auto *bytes = matchfinder_bytes(matchfinder);
    auto *words = matchfinder_words(matchfinder);

    current = reinterpret_cast<const std::uint8_t *>(words[0]);
    position = static_cast<std::uint32_t>(words[1]);
    cyclic_pos = *reinterpret_cast<std::uint32_t *>(bytes + kCyclicPos);
    position_limit = *reinterpret_cast<std::uint32_t *>(bytes + kPositionLimit);
    match_len_limit = *reinterpret_cast<std::uint32_t *>(bytes + kMatchLenLimit);
    max_depth = *reinterpret_cast<std::uint32_t *>(bytes + kMaxDepth);
    hash_table = *reinterpret_cast<std::uint32_t **>(bytes + kHashTables);
    hash_count = *reinterpret_cast<std::uint32_t *>(bytes + kHashTableWords);
    chain_table = *reinterpret_cast<std::uint32_t **>(bytes + kSonTablePtr);
    chain_count = *reinterpret_cast<std::uint32_t *>(bytes + kHistorySize);
    crc_table = reinterpret_cast<const std::uint32_t *>(bytes + kCrcTable);
}

void load_hc_skip_common(void *matchfinder, std::uint32_t &position, std::uint32_t &cyclic_pos,
        const std::uint8_t *&current, std::uint32_t &position_limit,
        std::uint32_t &match_len_limit, std::uint32_t *&hash_table, std::uint32_t &hash_count,
        std::uint32_t *&chain_table, std::uint32_t &chain_count,
        const std::uint32_t *&crc_table) {
    std::uint32_t unused_depth = 0U;
    load_hc_common(matchfinder, position, cyclic_pos, current, position_limit, match_len_limit,
            unused_depth, hash_table, hash_count, chain_table, chain_count, crc_table);
}

void load_bt2_common(void *matchfinder, std::uint32_t &position, std::uint32_t &cyclic_pos,
        const std::uint8_t *&current, std::uint32_t &position_limit,
        std::uint32_t &match_len_limit, std::uint32_t &max_depth, std::uint32_t *&hash_table,
        std::uint32_t &hash_count, std::uint32_t *&tree_links, std::uint32_t &tree_link_count) {
    auto *bytes = matchfinder_bytes(matchfinder);
    auto *words = matchfinder_words(matchfinder);

    current = reinterpret_cast<const std::uint8_t *>(words[0]);
    position = static_cast<std::uint32_t>(words[1]);
    cyclic_pos = *reinterpret_cast<std::uint32_t *>(bytes + kCyclicPos);
    position_limit = *reinterpret_cast<std::uint32_t *>(bytes + kPositionLimit);
    match_len_limit = *reinterpret_cast<std::uint32_t *>(bytes + kMatchLenLimit);
    max_depth = *reinterpret_cast<std::uint32_t *>(bytes + kMaxDepth);
    hash_table = *reinterpret_cast<std::uint32_t **>(bytes + kHashTables);
    hash_count = *reinterpret_cast<std::uint32_t *>(bytes + kHashTableWords);
    tree_links = *reinterpret_cast<std::uint32_t **>(bytes + kSonTablePtr);
    tree_link_count = *reinterpret_cast<std::uint32_t *>(bytes + kSonTableWords);
}

void store_bt4_cursor(void *matchfinder, const std::uint8_t *current, std::uint32_t position,
        std::uint32_t cyclic_pos) {
    auto *bytes = matchfinder_bytes(matchfinder);
    auto *words = matchfinder_words(matchfinder);

    words[0] = reinterpret_cast<std::uint64_t>(current);
    words[1] = position;
    *reinterpret_cast<std::uint32_t *>(bytes + kCyclicPos) = cyclic_pos;
}

LegacyCodecMatchFinderMatchOutput make_match_output(void *match_buffer) {
    LegacyCodecMatchFinderMatchOutput output{};
    output.records = static_cast<LegacyCodecMatchFinderMatchRecord *>(match_buffer);
    output.capacity = kEncoderMatchBufferRecordCapacity;
    output.count = 0U;
    output.best_length = 0U;
    return output;
}

struct MatchFinderLiveState {
    const std::uint8_t **current = nullptr;
    std::uint32_t *position = nullptr;
    std::uint32_t *cyclic_pos = nullptr;
    std::uint32_t *position_limit = nullptr;
    std::uint32_t *match_len_limit = nullptr;
};

thread_local MatchFinderLiveState g_live_matchfinder;

void bind_live_matchfinder(const std::uint8_t **current, std::uint32_t *position,
        std::uint32_t *cyclic_pos, std::uint32_t *position_limit,
        std::uint32_t *match_len_limit) {
    g_live_matchfinder.current = current;
    g_live_matchfinder.position = position;
    g_live_matchfinder.cyclic_pos = cyclic_pos;
    g_live_matchfinder.position_limit = position_limit;
    g_live_matchfinder.match_len_limit = match_len_limit;
}

void clear_live_matchfinder() {
    g_live_matchfinder = {};
}

void reload_live_limits(void *matchfinder) {
    if (g_live_matchfinder.position == nullptr) {
        return;
    }
    auto *bytes = matchfinder_bytes(matchfinder);
    auto *words = matchfinder_words(matchfinder);
    *g_live_matchfinder.current = reinterpret_cast<const std::uint8_t *>(words[0]);
    *g_live_matchfinder.position = static_cast<std::uint32_t>(words[1]);
    *g_live_matchfinder.cyclic_pos = *reinterpret_cast<std::uint32_t *>(bytes + kCyclicPos);
    if (g_live_matchfinder.position_limit != nullptr) {
        *g_live_matchfinder.position_limit =
                *reinterpret_cast<std::uint32_t *>(bytes + kPositionLimit);
    }
    if (g_live_matchfinder.match_len_limit != nullptr) {
        *g_live_matchfinder.match_len_limit =
                *reinterpret_cast<std::uint32_t *>(bytes + kMatchLenLimit);
    }
}

void matchfinder_refresh_callback(void *matchfinder) {
    if (g_live_matchfinder.position != nullptr) {
        store_bt4_cursor(matchfinder, *g_live_matchfinder.current,
                *g_live_matchfinder.position, *g_live_matchfinder.cyclic_pos);
    }
    legacy_codec_encoder_matchfinder_runtime_normalize(matchfinder);
    reload_live_limits(matchfinder);
}

}  // namespace

void legacy_codec_encoder_matchfinder_runtime_normalize(void *matchfinder) {
    if (matchfinder == nullptr) {
        return;
    }

    auto *bytes = matchfinder_bytes(matchfinder);
    auto *words = matchfinder_words(matchfinder);

    std::int32_t read_pos = static_cast<std::int32_t>(words[1]);
    if (read_pos == -1) {
        const std::int32_t dictionary_size =
                static_cast<std::int32_t>(
                        *reinterpret_cast<std::uint32_t *>(bytes + kMfDictionarySize));
        const std::uint32_t normalize_base =
                static_cast<std::uint32_t>(-dictionary_size - 2) & 0xfffffc00U;
        normalize_hash_table_offsets(matchfinder, normalize_base);

        const std::uint32_t hash_words =
                *reinterpret_cast<std::uint32_t *>(bytes + kHashTableWords);
        const std::uint32_t son_words =
                *reinterpret_cast<std::uint32_t *>(bytes + kSonTableWords);
        if (hash_words + son_words == 0U) {
            read_pos = -1;
        } else {
            read_pos -= static_cast<std::int32_t>(normalize_base);
        }
        words[1] = static_cast<std::uint64_t>(static_cast<std::uint32_t>(read_pos));

        std::uint32_t position_limit =
                *reinterpret_cast<std::uint32_t *>(bytes + kPositionLimit);
        position_limit -= normalize_base;
        *reinterpret_cast<std::uint32_t *>(bytes + kPositionLimit) = position_limit;
    }

    if (*reinterpret_cast<std::int32_t *>(bytes + kStreamEnd) == 0) {
        const std::uint32_t keep_after =
                *reinterpret_cast<std::uint32_t *>(bytes + kKeepAfter);
        const std::uint32_t write_pos = static_cast<std::uint32_t>(words[2]);
        const std::uint32_t buffered = write_pos - static_cast<std::uint32_t>(words[1]);
        if (keep_after == buffered) {
            if (*reinterpret_cast<std::int32_t *>(bytes + kDirectInput) == 0) {
                auto *buffer = reinterpret_cast<std::uint8_t *>(words[8]);
                auto *current = reinterpret_cast<std::uint8_t *>(words[0]);
                const std::uint32_t block_size =
                        *reinterpret_cast<std::uint32_t *>(bytes + kWindowBlockSize);
                const std::uint32_t keep_before =
                        *reinterpret_cast<std::uint32_t *>(bytes + kKeepBefore);
                if (static_cast<std::uint64_t>(buffer + block_size - current) <= keep_after) {
                    const std::uint32_t move_bytes = keep_after + keep_before;
                    std::memmove(buffer, current - keep_before, move_bytes);
                    words[0] = reinterpret_cast<std::uint64_t>(buffer + keep_before);
                }
            }
            legacy_codec_encoder_matchfinder_runtime_fill(matchfinder);
        }
    }

    const std::uint32_t history_size =
            *reinterpret_cast<std::uint32_t *>(bytes + kHistorySize);
    if (*reinterpret_cast<std::uint32_t *>(bytes + kCyclicPos) == history_size) {
        *reinterpret_cast<std::uint32_t *>(bytes + kCyclicPos) = 0U;
    }

    refresh_matchfinder_limits(matchfinder);
}

void legacy_codec_encoder_matchfinder_runtime_fill(void *matchfinder) {
    if (matchfinder == nullptr) {
        return;
    }

    auto *bytes = matchfinder_bytes(matchfinder);
    auto *words = matchfinder_words(matchfinder);

    if (*reinterpret_cast<std::int32_t *>(bytes + kStreamEnd) != 0 ||
            *reinterpret_cast<std::int32_t *>(bytes + kStreamError) != 0) {
        return;
    }

    if (*reinterpret_cast<std::int32_t *>(bytes + kDirectInput) != 0) {
        const std::uint32_t write_pos = static_cast<std::uint32_t>(words[2]);
        std::uint64_t remaining = words[13];
        std::uint32_t consumed = static_cast<std::uint32_t>(remaining);
        if (~write_pos <= consumed) {
            consumed = ~write_pos;
        }
        words[13] = remaining - consumed;
        words[2] = static_cast<std::uint64_t>(write_pos + consumed);
        if (remaining - consumed == 0) {
            *reinterpret_cast<std::int32_t *>(bytes + kStreamEnd) = 1;
        }
        return;
    }

    void **stream = reinterpret_cast<void **>(words[9]);
    if (stream == nullptr || *stream == nullptr) {
        return;
    }

    std::int32_t write_pos = static_cast<std::int32_t>(words[2]);
    std::int32_t read_pos = static_cast<std::int32_t>(words[1]);

    do {
        const std::uint8_t *current = reinterpret_cast<const std::uint8_t *>(words[0]);
        const std::int64_t dest =
                reinterpret_cast<std::int64_t>(current + (write_pos - read_pos));
        const std::uint8_t *window_end =
                reinterpret_cast<const std::uint8_t *>(words[8]) +
                *reinterpret_cast<const std::uint32_t *>(bytes + kWindowBlockSize);
        std::int64_t read_size = window_end - reinterpret_cast<const std::uint8_t *>(dest);
        if (read_size == 0) {
            break;
        }

        const auto read_fn = reinterpret_cast<OemStreamReadFn>(*stream);
        const int stream_status = read_fn(stream, dest, &read_size);
        *reinterpret_cast<std::int32_t *>(bytes + kStreamError) = stream_status;
        if (stream_status != 0) {
            break;
        }
        if (read_size == 0) {
            *reinterpret_cast<std::int32_t *>(bytes + kStreamEnd) = 1;
            break;
        }

        read_pos = static_cast<std::int32_t>(words[1]);
        write_pos = static_cast<std::int32_t>(words[2]) + static_cast<std::int32_t>(read_size);
        words[2] = static_cast<std::uint64_t>(static_cast<std::uint32_t>(write_pos));
    } while (static_cast<std::uint32_t>(write_pos - read_pos) <=
            *reinterpret_cast<std::uint32_t *>(bytes + kKeepAfter));
}

void legacy_codec_encoder_matchfinder_runtime_init(void *matchfinder) {
    if (matchfinder == nullptr) {
        return;
    }

    auto *bytes = matchfinder_bytes(matchfinder);
    auto *words = matchfinder_words(matchfinder);

    legacy_codec_fill_crc_table(reinterpret_cast<std::uint32_t *>(bytes + kCrcTable),
            kCrcTableEntries);

    const std::uint32_t hash_words =
            *reinterpret_cast<std::uint32_t *>(bytes + kHashTableWords);
    if (hash_words != 0U) {
        auto *hash_table = reinterpret_cast<std::uint32_t *>(words[5]);
        for (std::uint32_t index = 0; index < hash_words; ++index) {
            hash_table[index] = 0U;
        }
    }

    *reinterpret_cast<std::uint32_t *>(bytes + kCyclicPos) = 0U;
    *reinterpret_cast<std::int32_t *>(bytes + kStreamError) = 0;

    words[0] = words[8];
    const std::uint32_t history_size = *reinterpret_cast<std::uint32_t *>(bytes + kHistorySize);
    words[1] = history_size;
    words[2] = history_size;
    *reinterpret_cast<std::int32_t *>(bytes + kStreamEnd) = 0;

    legacy_codec_encoder_matchfinder_runtime_fill(matchfinder);
    refresh_matchfinder_limits(matchfinder);
}

extern "C" void legacy_codec_encoder_lifted_mf_init(void *matchfinder) {
    legacy_codec_encoder_matchfinder_runtime_init(matchfinder);
}

extern "C" std::uint8_t legacy_codec_encoder_lifted_mf_get_byte(void *matchfinder, int offset) {
    if (matchfinder == nullptr) {
        return 0U;
    }
    const auto *words = matchfinder_words(matchfinder);
    const auto *current = reinterpret_cast<const std::uint8_t *>(words[0]);
    if (current == nullptr) {
        return 0U;
    }
    const auto *buffer = reinterpret_cast<const std::uint8_t *>(words[8]);
    const auto *target = current + offset;
    if (buffer != nullptr && target < buffer) {
        return 0U;
    }
    return *target;
}

extern "C" int legacy_codec_encoder_lifted_mf_avail(void *matchfinder) {
    if (matchfinder == nullptr) {
        return 0;
    }
    const auto *words = matchfinder_words(matchfinder);
    return static_cast<int>(words[2]) - static_cast<int>(words[1]);
}

extern "C" void *legacy_codec_encoder_lifted_mf_get_ptr(void *matchfinder) {
    if (matchfinder == nullptr) {
        return nullptr;
    }
    return *reinterpret_cast<void **>(matchfinder);
}

extern "C" int legacy_codec_encoder_lifted_mf_hc_find(void *matchfinder, void *match_buffer) {
    if (matchfinder == nullptr || match_buffer == nullptr) {
        return 0;
    }

    LegacyCodecMatchFinderHashChainFindState state{};
    load_hc_common(matchfinder, state.position, state.cyclic_pos, state.current,
            state.position_limit, state.match_len_limit, state.max_depth, state.hash_table,
            state.hash_count, state.chain_table, state.chain_count, state.crc_table);
    state.crc_count = kCrcTableEntries;
    state.refresh = &matchfinder_refresh_callback;
    state.refresh_state = matchfinder;

    LegacyCodecMatchFinderMatchOutput output = make_match_output(match_buffer);
    state.output = &output;

    bind_live_matchfinder(&state.current, &state.position, &state.cyclic_pos,
            &state.position_limit, &state.match_len_limit);

    const std::uint32_t position_before = state.position;
    const std::uint8_t *current_before = state.current;
    const std::uint32_t cyclic_before = state.cyclic_pos;

    const std::uint32_t records_before = output.count;
    const LegacyCodecMatchFinderHashChainFindResult result =
            legacy_codec_match_finder_hash_chain_find(state);

    if (g_lift_mf_find_hold_position) {
        state.position = position_before;
        state.cyclic_pos = cyclic_before;
        state.current = current_before;
    }

    clear_live_matchfinder();
    store_bt4_cursor(matchfinder, state.current, state.position, state.cyclic_pos);
    *reinterpret_cast<std::uint32_t *>(matchfinder_bytes(matchfinder) + kPositionLimit) =
            state.position_limit;
    *reinterpret_cast<std::uint32_t *>(matchfinder_bytes(matchfinder) + kMatchLenLimit) =
            state.match_len_limit;

    const std::uint32_t records_added = legacy_codec_match_finder_records_added(
            &output, records_before);
    if (records_added == 0U && result.best_length == 0U) {
        return 0;
    }
    return static_cast<int>(output.count * 2U);
}

extern "C" void legacy_codec_encoder_lifted_mf_hc_skip(void *matchfinder, int count) {
    if (matchfinder == nullptr || count <= 0) {
        return;
    }

    LegacyCodecMatchFinderHashChainSkipState state{};
    load_hc_skip_common(matchfinder, state.position, state.cyclic_pos, state.current,
            state.position_limit, state.match_len_limit, state.hash_table, state.hash_count,
            state.chain_table, state.chain_count, state.crc_table);
    state.crc_count = kCrcTableEntries;
    state.refresh = &matchfinder_refresh_callback;
    state.refresh_state = matchfinder;

    bind_live_matchfinder(&state.current, &state.position, &state.cyclic_pos,
            &state.position_limit, &state.match_len_limit);

    legacy_codec_match_finder_hash_chain_skip(state, static_cast<std::uint32_t>(count));

    clear_live_matchfinder();
    store_bt4_cursor(matchfinder, state.current, state.position, state.cyclic_pos);
    *reinterpret_cast<std::uint32_t *>(matchfinder_bytes(matchfinder) + kPositionLimit) =
            state.position_limit;
    *reinterpret_cast<std::uint32_t *>(matchfinder_bytes(matchfinder) + kMatchLenLimit) =
            state.match_len_limit;
}

extern "C" int legacy_codec_encoder_lifted_mf_bt2_find(void *matchfinder, void *match_buffer) {
    if (matchfinder == nullptr || match_buffer == nullptr) {
        return 0;
    }

    LegacyCodecMatchFinderBinaryTree2FindState state{};
    load_bt2_common(matchfinder, state.position, state.cyclic_pos, state.current,
            state.position_limit, state.match_len_limit, state.max_depth, state.hash_table,
            state.hash_count, state.tree_links, state.tree_link_count);
    state.cyclic_size = *reinterpret_cast<std::uint32_t *>(matchfinder_bytes(matchfinder) +
            kHistorySize);
    state.refresh = &matchfinder_refresh_callback;
    state.refresh_state = matchfinder;

    LegacyCodecMatchFinderMatchOutput output = make_match_output(match_buffer);
    state.output = &output;

    bind_live_matchfinder(&state.current, &state.position, &state.cyclic_pos,
            &state.position_limit, &state.match_len_limit);

    const std::uint32_t records_before = output.count;
    const LegacyCodecMatchFinderBinaryTreeFindResult result =
            legacy_codec_match_finder_binary_tree2_find(state);

    clear_live_matchfinder();
    store_bt4_cursor(matchfinder, state.current, state.position, state.cyclic_pos);
    *reinterpret_cast<std::uint32_t *>(matchfinder_bytes(matchfinder) + kPositionLimit) =
            state.position_limit;
    *reinterpret_cast<std::uint32_t *>(matchfinder_bytes(matchfinder) + kMatchLenLimit) =
            state.match_len_limit;

    const std::uint32_t records_added = legacy_codec_match_finder_records_added(
            &output, records_before);
    if (records_added == 0U && !result.terminal_match) {
        return 0;
    }
    return static_cast<int>(output.count * 2U);
}

extern "C" void legacy_codec_encoder_lifted_mf_bt2_skip(void *matchfinder, int count) {
    if (matchfinder == nullptr || count <= 0) {
        return;
    }

    LegacyCodecMatchFinderBinaryTree2SkipState state{};
    load_bt2_common(matchfinder, state.position, state.cyclic_pos, state.current,
            state.position_limit, state.match_len_limit, state.max_depth, state.hash_table,
            state.hash_count, state.tree_links, state.tree_link_count);
    state.cyclic_size = *reinterpret_cast<std::uint32_t *>(matchfinder_bytes(matchfinder) +
            kHistorySize);
    state.refresh = &matchfinder_refresh_callback;
    state.refresh_state = matchfinder;

    bind_live_matchfinder(&state.current, &state.position, &state.cyclic_pos,
            &state.position_limit, &state.match_len_limit);

    legacy_codec_match_finder_binary_tree2_skip(state, static_cast<std::uint32_t>(count));

    clear_live_matchfinder();
    store_bt4_cursor(matchfinder, state.current, state.position, state.cyclic_pos);
    *reinterpret_cast<std::uint32_t *>(matchfinder_bytes(matchfinder) + kPositionLimit) =
            state.position_limit;
    *reinterpret_cast<std::uint32_t *>(matchfinder_bytes(matchfinder) + kMatchLenLimit) =
            state.match_len_limit;
}

extern "C" int legacy_codec_encoder_lifted_mf_bt3_find(void *matchfinder, void *match_buffer) {
    if (matchfinder == nullptr || match_buffer == nullptr) {
        return 0;
    }

    LegacyCodecMatchFinderBinaryTree3FindState state{};
    load_bt4_common(matchfinder, state.position, state.cyclic_pos, state.current,
            state.position_limit, state.match_len_limit, state.max_depth, state.hash_mask,
            state.hash_table, state.hash_count, state.tree_links, state.tree_link_count,
            state.crc_table);
    state.cyclic_size = *reinterpret_cast<std::uint32_t *>(matchfinder_bytes(matchfinder) +
            kHistorySize);
    state.crc_count = kCrcTableEntries;
    state.refresh = &matchfinder_refresh_callback;
    state.refresh_state = matchfinder;

    LegacyCodecMatchFinderMatchOutput output = make_match_output(match_buffer);
    state.output = &output;

    bind_live_matchfinder(&state.current, &state.position, &state.cyclic_pos,
            &state.position_limit, &state.match_len_limit);

    const std::uint32_t records_before = output.count;
    const LegacyCodecMatchFinderBinaryTreeFindResult result =
            legacy_codec_match_finder_binary_tree3_find(state);

    clear_live_matchfinder();
    store_bt4_cursor(matchfinder, state.current, state.position, state.cyclic_pos);
    *reinterpret_cast<std::uint32_t *>(matchfinder_bytes(matchfinder) + kPositionLimit) =
            state.position_limit;
    *reinterpret_cast<std::uint32_t *>(matchfinder_bytes(matchfinder) + kMatchLenLimit) =
            state.match_len_limit;

    const std::uint32_t records_added = legacy_codec_match_finder_records_added(
            &output, records_before);
    if (records_added == 0U && !result.terminal_match) {
        return 0;
    }
    return static_cast<int>(output.count * 2U);
}

extern "C" void legacy_codec_encoder_lifted_mf_bt3_skip(void *matchfinder, int count) {
    if (matchfinder == nullptr || count <= 0) {
        return;
    }

    LegacyCodecMatchFinderBinaryTree3SkipState state{};
    load_bt4_common(matchfinder, state.position, state.cyclic_pos, state.current,
            state.position_limit, state.match_len_limit, state.max_depth, state.hash_mask,
            state.hash_table, state.hash_count, state.tree_links, state.tree_link_count,
            state.crc_table);
    state.cyclic_size = *reinterpret_cast<std::uint32_t *>(matchfinder_bytes(matchfinder) +
            kHistorySize);
    state.crc_count = kCrcTableEntries;
    state.refresh = &matchfinder_refresh_callback;
    state.refresh_state = matchfinder;

    bind_live_matchfinder(&state.current, &state.position, &state.cyclic_pos,
            &state.position_limit, &state.match_len_limit);

    legacy_codec_match_finder_binary_tree3_skip(state, static_cast<std::uint32_t>(count));

    clear_live_matchfinder();
    store_bt4_cursor(matchfinder, state.current, state.position, state.cyclic_pos);
    *reinterpret_cast<std::uint32_t *>(matchfinder_bytes(matchfinder) + kPositionLimit) =
            state.position_limit;
    *reinterpret_cast<std::uint32_t *>(matchfinder_bytes(matchfinder) + kMatchLenLimit) =
            state.match_len_limit;
}

extern "C" int legacy_codec_encoder_lifted_mf_bt4_find(void *matchfinder, void *match_buffer) {
    if (matchfinder == nullptr || match_buffer == nullptr) {
        return 0;
    }

    LegacyCodecMatchFinderBinaryTree4FindState state{};
    load_bt4_common(matchfinder, state.position, state.cyclic_pos, state.current,
            state.position_limit, state.match_len_limit, state.max_depth, state.hash_mask,
            state.hash_table, state.hash_count, state.tree_links, state.tree_link_count,
            state.crc_table);
    state.cyclic_size = *reinterpret_cast<std::uint32_t *>(matchfinder_bytes(matchfinder) +
            kHistorySize);
    state.crc_count = kCrcTableEntries;
    state.refresh = &matchfinder_refresh_callback;
    state.refresh_state = matchfinder;

    LegacyCodecMatchFinderMatchOutput output = make_match_output(match_buffer);
    state.output = &output;

    bind_live_matchfinder(&state.current, &state.position, &state.cyclic_pos,
            &state.position_limit, &state.match_len_limit);

    const std::uint32_t records_before = output.count;
    const LegacyCodecMatchFinderBinaryTreeFindResult result =
            legacy_codec_match_finder_binary_tree4_find(state);

    clear_live_matchfinder();
    store_bt4_cursor(matchfinder, state.current, state.position, state.cyclic_pos);
    *reinterpret_cast<std::uint32_t *>(matchfinder_bytes(matchfinder) + kPositionLimit) =
            state.position_limit;
    *reinterpret_cast<std::uint32_t *>(matchfinder_bytes(matchfinder) + kMatchLenLimit) =
            state.match_len_limit;

    const std::uint32_t records_added = legacy_codec_match_finder_records_added(
            &output, records_before);
    if (records_added == 0U && !result.terminal_match) {
        return 0;
    }
    return static_cast<int>(output.count * 2U);
}

extern "C" void legacy_codec_encoder_lifted_mf_bt4_skip(void *matchfinder, int count) {
    if (matchfinder == nullptr || count <= 0) {
        return;
    }

    LegacyCodecMatchFinderBinaryTree4SkipState state{};
    load_bt4_common(matchfinder, state.position, state.cyclic_pos, state.current,
            state.position_limit, state.match_len_limit, state.max_depth, state.hash_mask,
            state.hash_table, state.hash_count, state.tree_links, state.tree_link_count,
            state.crc_table);
    state.cyclic_size = *reinterpret_cast<std::uint32_t *>(matchfinder_bytes(matchfinder) +
            kHistorySize);
    state.crc_count = kCrcTableEntries;
    state.refresh = &matchfinder_refresh_callback;
    state.refresh_state = matchfinder;

    bind_live_matchfinder(&state.current, &state.position, &state.cyclic_pos,
            &state.position_limit, &state.match_len_limit);

    legacy_codec_match_finder_binary_tree4_skip(state, static_cast<std::uint32_t>(count));

    clear_live_matchfinder();
    store_bt4_cursor(matchfinder, state.current, state.position, state.cyclic_pos);
    *reinterpret_cast<std::uint32_t *>(matchfinder_bytes(matchfinder) + kPositionLimit) =
            state.position_limit;
    *reinterpret_cast<std::uint32_t *>(matchfinder_bytes(matchfinder) + kMatchLenLimit) =
            state.match_len_limit;
}

}  // namespace kksdk
