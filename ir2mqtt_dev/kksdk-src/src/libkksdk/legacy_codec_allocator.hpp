#pragma once

#include <cstddef>

namespace kksdk {

using LegacyCodecFreeFn = void (*)(void *allocator, void *ptr);
using LegacyCodecResetFn = void (*)(void *range_state, void *user);

struct LegacyCodecAllocator {
    void *self = nullptr;
    LegacyCodecFreeFn free = nullptr;
};

struct LegacyCodecBufferPair {
    void *primary = nullptr;
    void *secondary = nullptr;
};

struct LegacyCodecWorkspaceBuffers {
    void *probability_models = nullptr;
    void *probability_models_mirror = nullptr;
    void *output_buffer = nullptr;
    void *range_state = nullptr;
};

struct LegacyCodecMatchFinderBuffers {
    void *primary = nullptr;
    void *secondary = nullptr;
    bool keep_secondary = false;
};

void legacy_codec_release_primary(LegacyCodecBufferPair &buffers,
        const LegacyCodecAllocator &allocator);
void legacy_codec_release_pair(LegacyCodecBufferPair &buffers,
        const LegacyCodecAllocator &allocator);
void legacy_codec_release_match_finder_buffers(LegacyCodecMatchFinderBuffers &buffers,
        const LegacyCodecAllocator &allocator);
void legacy_codec_release_probability_buffers(LegacyCodecWorkspaceBuffers &buffers,
        const LegacyCodecAllocator &allocator);
void legacy_codec_release_workspace_buffers(LegacyCodecWorkspaceBuffers &buffers,
        const LegacyCodecAllocator &allocator, LegacyCodecResetFn reset, void *reset_user);
void legacy_codec_release_workspace_and_self(LegacyCodecWorkspaceBuffers &buffers,
        const LegacyCodecAllocator &allocator, LegacyCodecResetFn reset, void *reset_user,
        void *self);

}  // namespace kksdk

extern "C" void kksdk_legacy_codec_release_primary(void *buffers, void *allocator,
        void (*free_fn)(void *, void *));
extern "C" void kksdk_legacy_codec_release_pair(void *buffers, void *allocator,
        void (*free_fn)(void *, void *));
extern "C" void kksdk_legacy_codec_release_match_finder_buffers(void *buffers,
        void *allocator, void (*free_fn)(void *, void *));
extern "C" void kksdk_legacy_codec_release_probability_buffers(void *buffers,
        void *allocator, void (*free_fn)(void *, void *));
extern "C" void kksdk_legacy_codec_release_workspace_buffers(void *buffers,
        void *allocator, void (*free_fn)(void *, void *),
        void (*reset_fn)(void *, void *), void *reset_user);
extern "C" void kksdk_legacy_codec_release_workspace_and_self(void *buffers,
        void *allocator, void (*free_fn)(void *, void *),
        void (*reset_fn)(void *, void *), void *reset_user, void *self);
