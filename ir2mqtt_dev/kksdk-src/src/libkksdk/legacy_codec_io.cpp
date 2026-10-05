#include "legacy_codec_io.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>

namespace kksdk {

std::size_t legacy_codec_read(LegacyCodecInput &input, void *out, std::size_t requested) {
    if (out == nullptr || requested == 0 || input.data == nullptr || input.offset >= input.size) {
        return 0;
    }

    std::size_t available = input.size - input.offset;
    std::size_t amount = std::min(requested, available);
    std::memcpy(out, input.data + input.offset, amount);
    input.offset += amount;
    return amount;
}

std::size_t legacy_codec_write(LegacyCodecOutput &output, const void *data, std::size_t length) {
    if (data == nullptr || length == 0) {
        return 0;
    }

    const auto *bytes = static_cast<const std::uint8_t *>(data);
    output.bytes.insert(output.bytes.end(), bytes, bytes + length);
    return length;
}

void legacy_codec_flush_stream(const LegacyCodecStreamView &stream) {
    if (stream.flush != nullptr) {
        stream.flush(stream.stream);
    }
}

unsigned long long legacy_codec_adjusted_stream_position(const LegacyCodecStreamView &stream) {
    if (stream.tell == nullptr) {
        return 0;
    }
    const unsigned long long position = stream.tell(stream.stream);
    return position >= stream.lookahead_bytes ? position - stream.lookahead_bytes : 0;
}

std::size_t legacy_codec_bounded_write(LegacyCodecBoundedOutput &output, const void *data,
        std::size_t requested) {
    if (data == nullptr || output.cursor == nullptr || requested == 0) {
        return 0;
    }

    std::size_t amount = requested;
    if (output.remaining < requested) {
        output.overflow = true;
        amount = output.remaining;
    }
    if (amount == 0) {
        return 0;
    }

    std::memcpy(output.cursor, data, amount);
    output.cursor += amount;
    output.remaining -= amount;
    return amount;
}

void legacy_codec_file_reset(LegacyCodecFile &file) {
    file.handle = nullptr;
}

static int open_file(LegacyCodecFile &file, const char *path, const char *mode) {
    file.handle = std::fopen(path, mode);
    return file.handle == nullptr ? errno : 0;
}

int legacy_codec_file_open_read(LegacyCodecFile &file, const char *path) {
    return open_file(file, path, "rb");
}

int legacy_codec_file_open_write(LegacyCodecFile &file, const char *path) {
    return open_file(file, path, "wb+");
}

void legacy_codec_file_close(LegacyCodecFile &file) {
    if (file.handle != nullptr && std::fclose(file.handle) == 0) {
        file.handle = nullptr;
    }
}

int legacy_codec_file_read(LegacyCodecFile &file, void *data, std::size_t &size) {
    if (file.handle == nullptr || data == nullptr) {
        size = 0;
        return EINVAL;
    }
    if (size == 0) {
        return 0;
    }

    const std::size_t requested = size;
    size = std::fread(data, 1, requested, file.handle);
    return size == requested ? 0 : std::ferror(file.handle);
}

int legacy_codec_file_write(LegacyCodecFile &file, const void *data, std::size_t &size) {
    if (file.handle == nullptr || data == nullptr) {
        size = 0;
        return EINVAL;
    }
    if (size == 0) {
        return 0;
    }

    const std::size_t requested = size;
    size = std::fwrite(data, 1, requested, file.handle);
    return size == requested ? 0 : std::ferror(file.handle);
}

int legacy_codec_file_seek(LegacyCodecFile &file, long &position, int origin) {
    if (file.handle == nullptr || origin < SEEK_SET || origin > SEEK_END) {
        return 1;
    }

    const int result = std::fseek(file.handle, position, origin);
    position = std::ftell(file.handle);
    return result;
}

int legacy_codec_file_size(LegacyCodecFile &file, long &size) {
    if (file.handle == nullptr) {
        size = 0;
        return EINVAL;
    }

    const long saved = std::ftell(file.handle);
    const int result = std::fseek(file.handle, 0, SEEK_END);
    size = std::ftell(file.handle);
    std::fseek(file.handle, saved, SEEK_SET);
    return result;
}

int legacy_codec_file_read_view(LegacyCodecFile &file, void *data, std::size_t &size) {
    if (file.handle == nullptr || data == nullptr) {
        size = 0;
        return EINVAL;
    }
    if (size == 0) {
        return 0;
    }

    const std::size_t requested = size;
    size = std::fread(data, 1, requested, file.handle);
    return size != requested && std::ferror(file.handle) != 0 ? 8 : 0;
}

std::size_t legacy_codec_file_write_view(LegacyCodecFile &file, const void *data,
        std::size_t size) {
    if (file.handle == nullptr || data == nullptr || size == 0) {
        return 0;
    }
    return std::fwrite(data, 1, size, file.handle);
}

int legacy_codec_read_exact(const LegacyCodecReadView &view, void *data, std::size_t size,
        int short_read_result) {
    if (view.read == nullptr || (data == nullptr && size != 0)) {
        return EINVAL;
    }

    auto *cursor = static_cast<std::uint8_t *>(data);
    while (size != 0) {
        std::size_t chunk = size;
        const int result = view.read(view.stream, cursor, chunk);
        if (result != 0) {
            return result;
        }
        if (chunk == 0) {
            return short_read_result;
        }
        cursor += chunk;
        size -= chunk;
    }
    return 0;
}

int legacy_codec_read_byte(const LegacyCodecReadView &view, std::uint8_t &value) {
    std::size_t size = 1;
    const int result = view.read == nullptr ? EINVAL : view.read(view.stream, &value, size);
    if (result != 0) {
        return result;
    }
    return size == 1 ? 0 : 6;
}

int legacy_codec_seek_to_position(LegacyCodecReadView &view, std::uint64_t position) {
    if (view.read == nullptr) {
        return EINVAL;
    }
    std::size_t unused = 0;
    return view.read(view.stream, &position, unused);
}

int legacy_codec_borrow_copy(LegacyCodecBorrowView &view, void *data, std::size_t size) {
    if (size == 0) {
        return 0;
    }
    if (view.borrow == nullptr || view.consume == nullptr || data == nullptr) {
        return EINVAL;
    }

    const std::uint8_t *source = nullptr;
    const int result = view.borrow(view.stream, source);
    if (result != 0) {
        return result;
    }
    std::memcpy(data, source, size);
    return view.consume(view.stream, size);
}

int legacy_codec_borrow_copy_nested(LegacyCodecBorrowView &view, void *data,
        std::size_t size) {
    return legacy_codec_borrow_copy(view, data, size);
}

int legacy_codec_buffered_borrow(LegacyCodecBufferedInput &input, const std::uint8_t *&data,
        std::size_t &size, bool full_buffer_read) {
    const std::size_t available = input.limit - input.cursor;
    if (available == 0 && size != 0) {
        input.cursor = 0;
        std::size_t requested = full_buffer_read ? input.buffer.size() :
                std::min<std::size_t>(size, input.buffer.size());
        const int result = input.upstream.read == nullptr ? EINVAL :
                input.upstream.read(input.upstream.stream, input.buffer.data(), requested);
        input.limit = requested;
        if (result != 0) {
            return result;
        }
    }

    const std::size_t refreshed = input.limit - input.cursor;
    size = std::min(size, refreshed);
    data = input.buffer.data() + input.cursor;
    return 0;
}

int legacy_codec_buffered_consume(LegacyCodecBufferedInput &input, std::size_t size) {
    input.cursor += size;
    if (input.cursor > input.limit) {
        input.cursor = input.limit;
    }
    return 0;
}

int legacy_codec_buffered_read(LegacyCodecBufferedInput &input, void *data, std::size_t &size) {
    if (data == nullptr && size != 0) {
        return EINVAL;
    }

    const std::size_t available = input.limit - input.cursor;
    if (available != 0) {
        const std::size_t copied = std::min(size, available);
        std::memcpy(data, input.buffer.data() + input.cursor, copied);
        input.cursor += copied;
        size = copied;
        return 0;
    }

    return input.upstream.read == nullptr ? EINVAL :
            input.upstream.read(input.upstream.stream, data, size);
}

void legacy_codec_buffered_reset(LegacyCodecBufferedInput &input, bool close_upstream) {
    input.cursor = 0;
    input.limit = 0;
    if (close_upstream && input.close != nullptr) {
        input.close(input.upstream.stream);
    }
}

void legacy_codec_close_nested(LegacyCodecCloseView &view) {
    if (view.close != nullptr) {
        view.close(view.stream);
    }
}

}  // namespace kksdk

extern "C" unsigned long long kksdk_legacy_codec_read(void *input, void *out,
        unsigned long long requested) {
    if (input == nullptr) {
        return 0;
    }
    return static_cast<unsigned long long>(kksdk::legacy_codec_read(
            *static_cast<kksdk::LegacyCodecInput *>(input), out,
            static_cast<std::size_t>(requested)));
}

extern "C" unsigned long long kksdk_legacy_codec_write(void *output, const void *data,
        unsigned long long length) {
    if (output == nullptr) {
        return 0;
    }
    return static_cast<unsigned long long>(kksdk::legacy_codec_write(
            *static_cast<kksdk::LegacyCodecOutput *>(output), data,
            static_cast<std::size_t>(length)));
}

extern "C" void kksdk_legacy_codec_flush_stream(const void *stream) {
    if (stream == nullptr) {
        return;
    }
    kksdk::legacy_codec_flush_stream(*static_cast<const kksdk::LegacyCodecStreamView *>(stream));
}

extern "C" unsigned long long kksdk_legacy_codec_adjusted_stream_position(const void *stream) {
    if (stream == nullptr) {
        return 0;
    }
    return kksdk::legacy_codec_adjusted_stream_position(
            *static_cast<const kksdk::LegacyCodecStreamView *>(stream));
}

extern "C" unsigned long long kksdk_legacy_codec_bounded_write(void *output,
        const void *data, unsigned long long requested) {
    if (output == nullptr) {
        return 0;
    }
    return static_cast<unsigned long long>(kksdk::legacy_codec_bounded_write(
            *static_cast<kksdk::LegacyCodecBoundedOutput *>(output), data,
            static_cast<std::size_t>(requested)));
}

extern "C" void kksdk_legacy_codec_file_reset(void *file) {
    if (file != nullptr) {
        kksdk::legacy_codec_file_reset(*static_cast<kksdk::LegacyCodecFile *>(file));
    }
}

extern "C" int kksdk_legacy_codec_file_open_read(void *file, const char *path) {
    if (file == nullptr || path == nullptr) {
        return EINVAL;
    }
    return kksdk::legacy_codec_file_open_read(*static_cast<kksdk::LegacyCodecFile *>(file), path);
}

extern "C" int kksdk_legacy_codec_file_open_write(void *file, const char *path) {
    if (file == nullptr || path == nullptr) {
        return EINVAL;
    }
    return kksdk::legacy_codec_file_open_write(*static_cast<kksdk::LegacyCodecFile *>(file), path);
}

extern "C" void kksdk_legacy_codec_file_close(void *file) {
    if (file != nullptr) {
        kksdk::legacy_codec_file_close(*static_cast<kksdk::LegacyCodecFile *>(file));
    }
}

extern "C" int kksdk_legacy_codec_file_read(void *file, void *data,
        unsigned long long *size) {
    if (file == nullptr || size == nullptr) {
        return EINVAL;
    }

    std::size_t amount = static_cast<std::size_t>(*size);
    const int result =
            kksdk::legacy_codec_file_read(*static_cast<kksdk::LegacyCodecFile *>(file), data,
                    amount);
    *size = static_cast<unsigned long long>(amount);
    return result;
}

extern "C" int kksdk_legacy_codec_file_write(void *file, const void *data,
        unsigned long long *size) {
    if (file == nullptr || size == nullptr) {
        return EINVAL;
    }

    std::size_t amount = static_cast<std::size_t>(*size);
    const int result =
            kksdk::legacy_codec_file_write(*static_cast<kksdk::LegacyCodecFile *>(file), data,
                    amount);
    *size = static_cast<unsigned long long>(amount);
    return result;
}

extern "C" int kksdk_legacy_codec_file_seek(void *file, long long *position,
        unsigned int origin) {
    if (file == nullptr || position == nullptr) {
        return EINVAL;
    }

    long file_position = static_cast<long>(*position);
    const int result = kksdk::legacy_codec_file_seek(
            *static_cast<kksdk::LegacyCodecFile *>(file), file_position,
            static_cast<int>(origin));
    *position = static_cast<long long>(file_position);
    return result;
}

extern "C" int kksdk_legacy_codec_file_size(void *file, long long *size) {
    if (file == nullptr || size == nullptr) {
        return EINVAL;
    }

    long file_size = 0;
    const int result =
            kksdk::legacy_codec_file_size(*static_cast<kksdk::LegacyCodecFile *>(file),
                    file_size);
    *size = static_cast<long long>(file_size);
    return result;
}

extern "C" int kksdk_legacy_codec_file_read_view(void *file, void *data,
        unsigned long long *size) {
    if (file == nullptr || size == nullptr) {
        return EINVAL;
    }

    std::size_t amount = static_cast<std::size_t>(*size);
    const int result = kksdk::legacy_codec_file_read_view(
            *static_cast<kksdk::LegacyCodecFile *>(file), data, amount);
    *size = static_cast<unsigned long long>(amount);
    return result;
}

extern "C" unsigned long long kksdk_legacy_codec_file_write_view(void *file,
        const void *data, unsigned long long size) {
    if (file == nullptr) {
        return 0;
    }
    return static_cast<unsigned long long>(kksdk::legacy_codec_file_write_view(
            *static_cast<kksdk::LegacyCodecFile *>(file), data, static_cast<std::size_t>(size)));
}

extern "C" int kksdk_legacy_codec_read_exact(void *view, void *data,
        unsigned long long size, int short_read_result) {
    if (view == nullptr) {
        return EINVAL;
    }
    return kksdk::legacy_codec_read_exact(*static_cast<kksdk::LegacyCodecReadView *>(view),
            data, static_cast<std::size_t>(size), short_read_result);
}

extern "C" int kksdk_legacy_codec_read_byte(void *view, unsigned char *value) {
    if (view == nullptr || value == nullptr) {
        return EINVAL;
    }
    std::uint8_t byte = 0;
    const int result =
            kksdk::legacy_codec_read_byte(*static_cast<kksdk::LegacyCodecReadView *>(view),
                    byte);
    *value = byte;
    return result;
}

extern "C" int kksdk_legacy_codec_seek_to_position(void *view, unsigned long long position) {
    if (view == nullptr) {
        return EINVAL;
    }
    return kksdk::legacy_codec_seek_to_position(*static_cast<kksdk::LegacyCodecReadView *>(view),
            position);
}

extern "C" int kksdk_legacy_codec_borrow_copy(void *view, void *data,
        unsigned long long size) {
    if (view == nullptr) {
        return EINVAL;
    }
    return kksdk::legacy_codec_borrow_copy(*static_cast<kksdk::LegacyCodecBorrowView *>(view),
            data, static_cast<std::size_t>(size));
}

extern "C" int kksdk_legacy_codec_borrow_copy_nested(void *view, void *data,
        unsigned long long size) {
    if (view == nullptr) {
        return EINVAL;
    }
    return kksdk::legacy_codec_borrow_copy_nested(
            *static_cast<kksdk::LegacyCodecBorrowView *>(view), data,
            static_cast<std::size_t>(size));
}

extern "C" int kksdk_legacy_codec_buffered_borrow(void *input, const unsigned char **data,
        unsigned long long *size, int full_buffer_read) {
    if (input == nullptr || data == nullptr || size == nullptr) {
        return EINVAL;
    }

    const std::uint8_t *borrowed = nullptr;
    std::size_t amount = static_cast<std::size_t>(*size);
    const int result = kksdk::legacy_codec_buffered_borrow(
            *static_cast<kksdk::LegacyCodecBufferedInput *>(input), borrowed, amount,
            full_buffer_read != 0);
    *data = borrowed;
    *size = static_cast<unsigned long long>(amount);
    return result;
}

extern "C" int kksdk_legacy_codec_buffered_consume(void *input, unsigned long long size) {
    if (input == nullptr) {
        return EINVAL;
    }
    return kksdk::legacy_codec_buffered_consume(
            *static_cast<kksdk::LegacyCodecBufferedInput *>(input),
            static_cast<std::size_t>(size));
}

extern "C" int kksdk_legacy_codec_buffered_read(void *input, void *data,
        unsigned long long *size) {
    if (input == nullptr || size == nullptr) {
        return EINVAL;
    }

    std::size_t amount = static_cast<std::size_t>(*size);
    const int result = kksdk::legacy_codec_buffered_read(
            *static_cast<kksdk::LegacyCodecBufferedInput *>(input), data, amount);
    *size = static_cast<unsigned long long>(amount);
    return result;
}

extern "C" void kksdk_legacy_codec_buffered_reset(void *input, int close_upstream) {
    if (input != nullptr) {
        kksdk::legacy_codec_buffered_reset(*static_cast<kksdk::LegacyCodecBufferedInput *>(input),
                close_upstream != 0);
    }
}

extern "C" void kksdk_legacy_codec_close_nested(void *view) {
    if (view != nullptr) {
        kksdk::legacy_codec_close_nested(*static_cast<kksdk::LegacyCodecCloseView *>(view));
    }
}
