#include "streamhelper_legacy_lzma.hpp"
#include "irdevice_jni.hpp"

#include "kksdk/kksdk_host.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" int kksdk_streamhelper_lzma_encode(const std::uint8_t *input,
        std::size_t input_size, std::uint8_t **out, std::size_t *out_size) {
    if (out == nullptr || out_size == nullptr) {
        return -1;
    }
    *out = nullptr;
    *out_size = 0;
    if (input == nullptr || input_size == 0) {
        return -2;
    }

    std::vector<std::uint8_t> encoded;
    if (!kksdk::streamhelper_lzma_encode_buffer(input, input_size, encoded) ||
            encoded.empty()) {
        return -3;
    }

    auto *buffer = static_cast<std::uint8_t *>(std::malloc(encoded.size()));
    if (buffer == nullptr) {
        return -4;
    }
    std::memcpy(buffer, encoded.data(), encoded.size());
    *out = buffer;
    *out_size = encoded.size();
    return 0;
}

extern "C" void kksdk_streamhelper_lzma_free(void *ptr) {
    std::free(ptr);
}

extern "C" int kksdk_irdevice_encode_pulse(const std::uint8_t *remote_data,
        std::size_t remote_size, const std::uint8_t *command, std::size_t command_size,
        std::uint32_t **out_durations, std::size_t *out_duration_count) {
    if (out_durations == nullptr || out_duration_count == nullptr) {
        return -1;
    }
    *out_durations = nullptr;
    *out_duration_count = 0;
    if (remote_data == nullptr || remote_size == 0 || command == nullptr ||
            command_size == 0) {
        return -2;
    }
    if (remote_size > 0xffffffffULL || command_size > 0xffffffffULL) {
        return -3;
    }

    const unsigned int remote_status = kksdk_irdevice_set_remote_view(
            remote_data, static_cast<unsigned int>(remote_size));
    if (remote_status != 0) {
        kksdk_irdevice_reset_remote_view();
        return -1000 - static_cast<int>(remote_status);
    }

    unsigned short duration_count = 0;
    unsigned char repeat_count = 0;
    std::vector<unsigned short> durations(0x400U);
    const int status = kksdk_irdevice_encode_command_frame(command,
            static_cast<unsigned int>(command_size), durations.data(),
            static_cast<unsigned short>(durations.size()), &duration_count, &repeat_count);
    kksdk_irdevice_reset_remote_view();
    if (status != 0) {
        return status;
    }

    const std::size_t expanded_count = static_cast<std::size_t>(duration_count) *
            static_cast<std::size_t>(repeat_count);
    auto *expanded = static_cast<std::uint32_t *>(
            std::malloc(expanded_count * sizeof(std::uint32_t)));
    if (expanded == nullptr && expanded_count != 0) {
        return -4;
    }
    for (unsigned int repeat = 0; repeat < repeat_count; ++repeat) {
        for (unsigned int index = 0; index < duration_count; ++index) {
            expanded[static_cast<std::size_t>(repeat) * duration_count + index] =
                    durations[index];
        }
    }

    *out_durations = expanded;
    *out_duration_count = expanded_count;
    return 0;
}

extern "C" void kksdk_irdevice_free_pulse(std::uint32_t *durations) {
    std::free(durations);
}

extern "C" const char *kksdk_host_version(void) {
    return "kksdk-host-native-1";
}
