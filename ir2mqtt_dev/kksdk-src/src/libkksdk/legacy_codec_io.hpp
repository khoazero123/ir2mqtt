#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <array>
#include <vector>

namespace kksdk {

struct LegacyCodecInput {
    const std::uint8_t *data = nullptr;
    std::size_t size = 0;
    std::size_t offset = 0;
};

struct LegacyCodecOutput {
    std::vector<std::uint8_t> bytes;
};

using LegacyCodecFlushFn = void (*)(void *stream);
using LegacyCodecTellFn = unsigned long long (*)(void *stream);
using LegacyCodecReadFn = int (*)(void *stream, void *data, std::size_t &size);
using LegacyCodecWriteFn = std::size_t (*)(void *stream, const void *data, std::size_t size);
using LegacyCodecBorrowFn = int (*)(void *stream, const std::uint8_t *&data);
using LegacyCodecConsumeFn = int (*)(void *stream, std::size_t size);
using LegacyCodecCloseFn = void (*)(void *stream);

struct LegacyCodecStreamView {
    void *stream = nullptr;
    LegacyCodecFlushFn flush = nullptr;
    LegacyCodecTellFn tell = nullptr;
    std::uint32_t lookahead_bytes = 0;
};

struct LegacyCodecBoundedOutput {
    std::uint8_t *cursor = nullptr;
    std::size_t remaining = 0;
    bool overflow = false;
};

struct LegacyCodecFile {
    std::FILE *handle = nullptr;
};

struct LegacyCodecReadView {
    void *stream = nullptr;
    LegacyCodecReadFn read = nullptr;
};

struct LegacyCodecWriteView {
    void *stream = nullptr;
    LegacyCodecWriteFn write = nullptr;
};

struct LegacyCodecBorrowView {
    void *stream = nullptr;
    LegacyCodecBorrowFn borrow = nullptr;
    LegacyCodecConsumeFn consume = nullptr;
};

struct LegacyCodecCloseView {
    void *stream = nullptr;
    LegacyCodecCloseFn close = nullptr;
};

struct LegacyCodecBufferedInput {
    LegacyCodecReadView upstream;
    LegacyCodecCloseFn close = nullptr;
    std::size_t cursor = 0;
    std::size_t limit = 0;
    std::array<std::uint8_t, 0x4000> buffer{};
};

std::size_t legacy_codec_read(LegacyCodecInput &input, void *out, std::size_t requested);
std::size_t legacy_codec_write(LegacyCodecOutput &output, const void *data, std::size_t length);
void legacy_codec_flush_stream(const LegacyCodecStreamView &stream);
unsigned long long legacy_codec_adjusted_stream_position(const LegacyCodecStreamView &stream);
std::size_t legacy_codec_bounded_write(LegacyCodecBoundedOutput &output, const void *data,
        std::size_t requested);
void legacy_codec_file_reset(LegacyCodecFile &file);
int legacy_codec_file_open_read(LegacyCodecFile &file, const char *path);
int legacy_codec_file_open_write(LegacyCodecFile &file, const char *path);
void legacy_codec_file_close(LegacyCodecFile &file);
int legacy_codec_file_read(LegacyCodecFile &file, void *data, std::size_t &size);
int legacy_codec_file_write(LegacyCodecFile &file, const void *data, std::size_t &size);
int legacy_codec_file_seek(LegacyCodecFile &file, long &position, int origin);
int legacy_codec_file_size(LegacyCodecFile &file, long &size);
int legacy_codec_file_read_view(LegacyCodecFile &file, void *data, std::size_t &size);
std::size_t legacy_codec_file_write_view(LegacyCodecFile &file, const void *data,
        std::size_t size);
int legacy_codec_read_exact(const LegacyCodecReadView &view, void *data, std::size_t size,
        int short_read_result);
int legacy_codec_read_byte(const LegacyCodecReadView &view, std::uint8_t &value);
int legacy_codec_seek_to_position(LegacyCodecReadView &view, std::uint64_t position);
int legacy_codec_borrow_copy(LegacyCodecBorrowView &view, void *data, std::size_t size);
int legacy_codec_borrow_copy_nested(LegacyCodecBorrowView &view, void *data, std::size_t size);
int legacy_codec_buffered_borrow(LegacyCodecBufferedInput &input, const std::uint8_t *&data,
        std::size_t &size, bool full_buffer_read);
int legacy_codec_buffered_consume(LegacyCodecBufferedInput &input, std::size_t size);
int legacy_codec_buffered_read(LegacyCodecBufferedInput &input, void *data, std::size_t &size);
void legacy_codec_buffered_reset(LegacyCodecBufferedInput &input, bool close_upstream);
void legacy_codec_close_nested(LegacyCodecCloseView &view);

}  // namespace kksdk

extern "C" unsigned long long kksdk_legacy_codec_read(void *input, void *out,
        unsigned long long requested);
extern "C" unsigned long long kksdk_legacy_codec_write(void *output, const void *data,
        unsigned long long length);
extern "C" void kksdk_legacy_codec_flush_stream(const void *stream);
extern "C" unsigned long long kksdk_legacy_codec_adjusted_stream_position(const void *stream);
extern "C" unsigned long long kksdk_legacy_codec_bounded_write(void *output,
        const void *data, unsigned long long requested);
extern "C" void kksdk_legacy_codec_file_reset(void *file);
extern "C" int kksdk_legacy_codec_file_open_read(void *file, const char *path);
extern "C" int kksdk_legacy_codec_file_open_write(void *file, const char *path);
extern "C" void kksdk_legacy_codec_file_close(void *file);
extern "C" int kksdk_legacy_codec_file_read(void *file, void *data,
        unsigned long long *size);
extern "C" int kksdk_legacy_codec_file_write(void *file, const void *data,
        unsigned long long *size);
extern "C" int kksdk_legacy_codec_file_seek(void *file, long long *position, unsigned int origin);
extern "C" int kksdk_legacy_codec_file_size(void *file, long long *size);
extern "C" int kksdk_legacy_codec_file_read_view(void *file, void *data,
        unsigned long long *size);
extern "C" unsigned long long kksdk_legacy_codec_file_write_view(void *file,
        const void *data, unsigned long long size);
extern "C" int kksdk_legacy_codec_read_exact(void *view, void *data,
        unsigned long long size, int short_read_result);
extern "C" int kksdk_legacy_codec_read_byte(void *view, unsigned char *value);
extern "C" int kksdk_legacy_codec_seek_to_position(void *view, unsigned long long position);
extern "C" int kksdk_legacy_codec_borrow_copy(void *view, void *data,
        unsigned long long size);
extern "C" int kksdk_legacy_codec_borrow_copy_nested(void *view, void *data,
        unsigned long long size);
extern "C" int kksdk_legacy_codec_buffered_borrow(void *input, const unsigned char **data,
        unsigned long long *size, int full_buffer_read);
extern "C" int kksdk_legacy_codec_buffered_consume(void *input, unsigned long long size);
extern "C" int kksdk_legacy_codec_buffered_read(void *input, void *data,
        unsigned long long *size);
extern "C" void kksdk_legacy_codec_buffered_reset(void *input, int close_upstream);
extern "C" void kksdk_legacy_codec_close_nested(void *view);
