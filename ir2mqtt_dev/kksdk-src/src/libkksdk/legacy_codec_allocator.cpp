#include "legacy_codec_allocator.hpp"

namespace kksdk {

static void release_pointer(void *&ptr, const LegacyCodecAllocator &allocator) {
    if (ptr != nullptr && allocator.free != nullptr) {
        allocator.free(allocator.self, ptr);
    }
    ptr = nullptr;
}

void legacy_codec_release_primary(LegacyCodecBufferPair &buffers,
        const LegacyCodecAllocator &allocator) {
    release_pointer(buffers.primary, allocator);
}

void legacy_codec_release_pair(LegacyCodecBufferPair &buffers,
        const LegacyCodecAllocator &allocator) {
    release_pointer(buffers.primary, allocator);
    release_pointer(buffers.secondary, allocator);
}

void legacy_codec_release_match_finder_buffers(LegacyCodecMatchFinderBuffers &buffers,
        const LegacyCodecAllocator &allocator) {
    release_pointer(buffers.primary, allocator);
    if (!buffers.keep_secondary) {
        release_pointer(buffers.secondary, allocator);
    }
}

void legacy_codec_release_probability_buffers(LegacyCodecWorkspaceBuffers &buffers,
        const LegacyCodecAllocator &allocator) {
    release_pointer(buffers.probability_models, allocator);
    release_pointer(buffers.probability_models_mirror, allocator);
}

void legacy_codec_release_workspace_buffers(LegacyCodecWorkspaceBuffers &buffers,
        const LegacyCodecAllocator &allocator, LegacyCodecResetFn reset, void *reset_user) {
    if (reset != nullptr) {
        reset(buffers.range_state, reset_user);
    }
    legacy_codec_release_probability_buffers(buffers, allocator);
    release_pointer(buffers.output_buffer, allocator);
}

void legacy_codec_release_workspace_and_self(LegacyCodecWorkspaceBuffers &buffers,
        const LegacyCodecAllocator &allocator, LegacyCodecResetFn reset, void *reset_user,
        void *self) {
    legacy_codec_release_workspace_buffers(buffers, allocator, reset, reset_user);
    if (self != nullptr && allocator.free != nullptr) {
        allocator.free(allocator.self, self);
    }
}

}  // namespace kksdk

extern "C" void kksdk_legacy_codec_release_primary(void *buffers, void *allocator,
        void (*free_fn)(void *, void *)) {
    if (buffers == nullptr) {
        return;
    }
    kksdk::LegacyCodecAllocator typed_allocator{allocator, free_fn};
    kksdk::legacy_codec_release_primary(
            *static_cast<kksdk::LegacyCodecBufferPair *>(buffers), typed_allocator);
}

extern "C" void kksdk_legacy_codec_release_pair(void *buffers, void *allocator,
        void (*free_fn)(void *, void *)) {
    if (buffers == nullptr) {
        return;
    }
    kksdk::LegacyCodecAllocator typed_allocator{allocator, free_fn};
    kksdk::legacy_codec_release_pair(
            *static_cast<kksdk::LegacyCodecBufferPair *>(buffers), typed_allocator);
}

extern "C" void kksdk_legacy_codec_release_match_finder_buffers(void *buffers,
        void *allocator, void (*free_fn)(void *, void *)) {
    if (buffers == nullptr) {
        return;
    }
    kksdk::LegacyCodecAllocator typed_allocator{allocator, free_fn};
    kksdk::legacy_codec_release_match_finder_buffers(
            *static_cast<kksdk::LegacyCodecMatchFinderBuffers *>(buffers), typed_allocator);
}

extern "C" void kksdk_legacy_codec_release_probability_buffers(void *buffers,
        void *allocator, void (*free_fn)(void *, void *)) {
    if (buffers == nullptr) {
        return;
    }
    kksdk::LegacyCodecAllocator typed_allocator{allocator, free_fn};
    kksdk::legacy_codec_release_probability_buffers(
            *static_cast<kksdk::LegacyCodecWorkspaceBuffers *>(buffers), typed_allocator);
}

extern "C" void kksdk_legacy_codec_release_workspace_buffers(void *buffers,
        void *allocator, void (*free_fn)(void *, void *),
        void (*reset_fn)(void *, void *), void *reset_user) {
    if (buffers == nullptr) {
        return;
    }
    kksdk::LegacyCodecAllocator typed_allocator{allocator, free_fn};
    kksdk::legacy_codec_release_workspace_buffers(
            *static_cast<kksdk::LegacyCodecWorkspaceBuffers *>(buffers), typed_allocator,
            reset_fn, reset_user);
}

extern "C" void kksdk_legacy_codec_release_workspace_and_self(void *buffers,
        void *allocator, void (*free_fn)(void *, void *),
        void (*reset_fn)(void *, void *), void *reset_user, void *self) {
    if (buffers == nullptr) {
        return;
    }
    kksdk::LegacyCodecAllocator typed_allocator{allocator, free_fn};
    kksdk::legacy_codec_release_workspace_and_self(
            *static_cast<kksdk::LegacyCodecWorkspaceBuffers *>(buffers), typed_allocator,
            reset_fn, reset_user, self);
}
