#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

#include "Ngs2Internal.hpp"

extern "C" {
#include "coding/coding.h"
}

namespace {

struct FrameStream {
    STREAMFILE stream{};
    const std::uint8_t* frame;
};

std::size_t ReadFrame(STREAMFILE* stream, std::uint8_t* destination, offv_t offset, std::size_t size) {
    if (offset < 0 || offset >= 16) return 0;
    size = std::min(size, 16 - static_cast<std::size_t>(offset));
    std::memcpy(destination, reinterpret_cast<FrameStream*>(stream)->frame + offset, size);
    return size;
}

std::uint32_t ReadBe32(const std::uint8_t* data) {
    return (static_cast<std::uint32_t>(data[0]) << 24) | (static_cast<std::uint32_t>(data[1]) << 16)
        | (static_cast<std::uint32_t>(data[2]) << 8) | data[3];
}

}

int Ngs2ParseHevag(const std::uint8_t* header, std::size_t size, Ngs2WaveformInfo& info) {
    if (size < 0x30) return SCE_NGS2_ERROR_INVALID_WAVEFORM_DATA;
    const auto version = ReadBe32(header + 4);
    if (version != 0x20001 && version != 0x30000) return SCE_NGS2_ERROR_UNKNOWN_WAVEFORM_FORMAT;
    if (ReadBe32(header + 0x18) != 0 || (ReadBe32(header + 0x1c) & 0xffff00ffu) != 0)
        throw std::runtime_error("NGS2: alternate VAG header layout is not implemented");
    const std::uint32_t channels = header[0x1e] == 0 ? 1 : header[0x1e];
    const auto rate = ReadBe32(header + 0x10);
    if (channels > NGS2_MAX_CHANNELS || rate == 0 || rate > 192000) return SCE_NGS2_ERROR_INVALID_WAVEFORM_FORMAT;
    const auto bytes = ReadBe32(header + 0xc);
    const auto frameBytes = 16 * channels;
    if (bytes > size - 0x30 || bytes % frameBytes != 0) return SCE_NGS2_ERROR_INVALID_WAVEFORM_DATA;
    const std::uint64_t samples = static_cast<std::uint64_t>(bytes / frameBytes) * 28;
    if (samples > std::numeric_limits<std::uint32_t>::max()) return SCE_NGS2_ERROR_INVALID_WAVEFORM_DATA;
    info.format = {SCE_NGS2_WAVEFORM_TYPE_VAG, channels, rate, 0, 0, 0};
    info.data_offset = 0x30;
    info.data_size = bytes;
    info.num_samples = static_cast<std::uint32_t>(samples);
    info.audio_unit_size = frameBytes;
    info.num_audio_unit_samples = 28;
    info.num_audio_unit_per_frame = 1;
    info.audio_frame_size = frameBytes;
    info.num_audio_frame_samples = 28;
    info.num_blocks = 1;
    info.block[0] = {0x30, bytes, 0, 0, info.num_samples, 0, 0};
    return SCE_NGS2_OK;
}

void Ngs2DecodeHevag(Ngs2Block& block, std::uint32_t channels) {
    const std::uint64_t samples = static_cast<std::uint64_t>(block.info.num_skip_samples) + block.info.num_samples;
    const auto frames = (samples + 27) / 28;
    if (frames > block.info.data_size / (16 * channels)) throw std::invalid_argument("NGS2: truncated HE-VAG block");
    block.decoded.resize(static_cast<std::size_t>(frames * 28 * channels));
    std::array<VGMSTREAMCHANNEL, NGS2_MAX_CHANNELS> histories{};
    FrameStream input{};
    input.stream.read = ReadFrame;
    for (std::uint64_t frame = 0; frame < frames; ++frame) {
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            input.frame = block.data + (frame * channels + channel) * 16;
            const auto predictor = (input.frame[1] & 0xf0) | (input.frame[0] >> 4);
            const auto flags = input.frame[1] & 0xf;
            if (predictor >= 128 || (input.frame[0] & 0xf) > 12 || flags > 7)
                throw std::invalid_argument("NGS2: invalid HE-VAG frame");
            if (flags != 0 && flags != 1 && flags != 7)
                throw std::runtime_error("NGS2: HE-VAG embedded loops are not implemented");
            auto& history = histories[channel];
            history.streamfile = &input.stream;
            decode_hevag(&history, block.decoded.data() + frame * 28 * channels + channel, static_cast<int>(channels), 0, 28);
        }
    }
}
