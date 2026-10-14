#include "Ngs2Test.hpp"
#include "prx/libc/include/General.hpp"

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

static void PutBe32(std::vector<std::uint8_t>& data, std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) data[offset + i] = static_cast<std::uint8_t>(value >> (24 - 8 * i));
}

static std::vector<std::uint8_t> Wave(std::uint32_t channels, std::uint32_t frames) {
    std::vector<std::uint8_t> data(0x30 + channels * frames * 16);
    std::memcpy(data.data(), "VAGp", 4);
    PutBe32(data, 4, 0x20001);
    PutBe32(data, 0xc, channels * frames * 16);
    PutBe32(data, 0x10, 48000);
    data[0x1e] = static_cast<std::uint8_t>(channels);
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            auto* unit = data.data() + 0x30 + (frame * channels + channel) * 16;
            unit[0] = 12;
            std::memset(unit + 2, channel == 0 ? 0x21 : 0xfe, 14);
        }
    }
    return data;
}

static void Parse() {
    auto data = Wave(2, 2);
    Ngs2WaveformInfo info{};
    Require(sceNgs2ParseWaveformData(data.data(), data.size(), &info) == SCE_NGS2_OK);
    Require(info.format.waveform_type == SCE_NGS2_WAVEFORM_TYPE_VAG && info.format.num_channels == 2 && info.format.sample_rate == 48000);
    Require(info.data_offset == 0x30 && info.data_size == 64 && info.num_samples == 56);
    Require(info.audio_unit_size == 32 && info.num_audio_unit_samples == 28 && info.num_audio_unit_per_frame == 1);
    Require(info.audio_frame_size == 32 && info.num_audio_frame_samples == 28 && info.num_blocks == 1);
    Require(info.block[0].data_offset == 0x30 && info.block[0].data_size == 64 && info.block[0].num_samples == 56);
    for (std::size_t size = 4; size < 0x30; ++size)
        Require(sceNgs2ParseWaveformData(data.data(), size, &info) == SCE_NGS2_ERROR_INVALID_WAVEFORM_DATA);
    Require(sceNgs2ParseWaveformData(data.data(), data.size() - 1, &info) == SCE_NGS2_ERROR_INVALID_WAVEFORM_DATA);
    PutBe32(data, 0xc, 63);
    Require(sceNgs2ParseWaveformData(data.data(), data.size(), &info) == SCE_NGS2_ERROR_INVALID_WAVEFORM_DATA);
    PutBe32(data, 0xc, 64);
    data[0x1e] = 9;
    Require(sceNgs2ParseWaveformData(data.data(), data.size(), &info) == SCE_NGS2_ERROR_INVALID_WAVEFORM_FORMAT);
    data[0x1e] = 2;
    PutBe32(data, 4, 0x30000);
    Require(sceNgs2ParseWaveformData(data.data(), data.size(), &info) == SCE_NGS2_OK);
    PutBe32(data, 4, 0x20);
    Require(sceNgs2ParseWaveformData(data.data(), data.size(), &info) == SCE_NGS2_ERROR_UNKNOWN_WAVEFORM_FORMAT);

    data = Wave(1, 2);
    data[0x1e] = 0;
    Require(sceNgs2ParseWaveformData(data.data(), data.size(), &info) == SCE_NGS2_OK && info.format.num_channels == 1);
    const char* guestPath = "/aps5_ngs2_hevag.vag";
    const auto path = ResolvePath_nid_no_patch(guestPath);
    {
        std::ofstream out(path, std::ios::binary);
        out.write("pad", 3);
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    Require(sceNgs2ParseWaveformFile(guestPath, 3, &info) == SCE_NGS2_OK);
    Require(info.data_offset == 0x33 && info.block[0].data_offset == 0x33 && info.num_samples == 56);
    std::filesystem::remove(path);
}

static void Playback() {
    auto data = Wave(2, 2);
    data[0x51] = 7;
    data[0x61] = 7;
    Ngs2WaveformInfo parsed{};
    Require(sceNgs2ParseWaveformData(data.data(), data.size(), &parsed) == SCE_NGS2_OK);
    const auto system = CreateSystem();
    const auto master = Mastering(system, 2);
    const auto sampler = Voice(CreateRack(system, SCE_NGS2_RACK_ID_SAMPLER));
    Control(sampler, SCE_NGS2_SAMPLER_VOICE_PARAM_SETUP, Ngs2SamplerVoiceSetupParam{{}, parsed.format});
    parsed.block[0].num_skip_samples = 3;
    parsed.block[0].num_samples = 53;
    parsed.block[0].num_repeats = 1;
    Control(sampler, SCE_NGS2_SAMPLER_VOICE_PARAM_ADD_WAVEFORM_BLOCKS, Ngs2SamplerVoiceWaveformBlocksParam{{}, data.data(), 0, 1, parsed.block});
    Patch(sampler, master);
    Event(sampler, SCE_NGS2_VOICE_EVENT_PLAY);
    for (unsigned grain = 0; grain < 14; ++grain) {
        std::array<float, Grain * 2> output{};
        const Ngs2RenderBufferInfo render{output.data(), sizeof(output), SCE_NGS2_WAVEFORM_TYPE_PCM_F32L, 2};
        Require(sceNgs2SystemRender(system, &render, 1) == SCE_NGS2_OK);
        for (unsigned i = 0; i < Grain; ++i) {
            const unsigned sample = grain * Grain + i;
            const unsigned position = sample % 53 + 3;
            Require(output[i * 2] == (sample >= 106 || position >= 28 ? 0.0f : (position % 2 == 0 ? 1.0f : 2.0f) / 32768));
            Require(output[i * 2 + 1] == (sample >= 106 || position >= 28 ? 0.0f : (position % 2 == 0 ? -2.0f : -1.0f) / 32768));
        }
    }
    Ngs2SamplerVoiceState state{};
    Require(sceNgs2VoiceGetState(sampler, &state.voice_state, sizeof(state)) == SCE_NGS2_OK);
    Require(state.num_decoded_samples == 106 && state.decoded_data_size == 128);
    Require(state.waveform_data == data.data() + data.size());
    Require(sceNgs2SystemDestroy(system, nullptr) == SCE_NGS2_OK);
}

static void HistoryAndErrors() {
    auto data = Wave(1, 2);
    data[0x30] = 0xdc;
    data[0x31] = 0x10;
    std::memset(data.data() + 0x32, 0x77, 14);
    data[0x40] = 0xdc;
    data[0x41] = 0x10;
    std::memset(data.data() + 0x42, 0x77, 14);
    const auto system = CreateSystem();
    const auto master = Mastering(system, 1);
    const auto sampler = Voice(CreateRack(system, SCE_NGS2_RACK_ID_SAMPLER));
    bool configurationThrew = false;
    try {
        Control(sampler, SCE_NGS2_SAMPLER_VOICE_PARAM_SETUP, Ngs2SamplerVoiceSetupParam{{}, {SCE_NGS2_WAVEFORM_TYPE_VAG, 1, 48000, 1, 0, 0}});
    } catch (const std::exception&) { configurationThrew = true; }
    Require(configurationThrew);
    Control(sampler, SCE_NGS2_SAMPLER_VOICE_PARAM_SETUP, Ngs2SamplerVoiceSetupParam{{}, {SCE_NGS2_WAVEFORM_TYPE_VAG, 1, 48000, 0, 0, 0}});
    const Ngs2WaveformBlock block{0x30, 32, 0, 0, 56, 0, 0};
    for (const std::uint8_t flags : {std::uint8_t{0x80}, std::uint8_t{0x16}}) {
        data[0x31] = flags;
        bool threw = false;
        try {
            Control(sampler, SCE_NGS2_SAMPLER_VOICE_PARAM_ADD_WAVEFORM_BLOCKS, Ngs2SamplerVoiceWaveformBlocksParam{{}, data.data(), SCE_NGS2_WAVEFORM_BLOCKS_FLAG_RESET, 1, &block});
        } catch (const std::exception&) { threw = true; }
        Require(threw);
    }
    data[0x31] = 0x10;
    Control(sampler, SCE_NGS2_SAMPLER_VOICE_PARAM_ADD_WAVEFORM_BLOCKS, Ngs2SamplerVoiceWaveformBlocksParam{{}, data.data(), SCE_NGS2_WAVEFORM_BLOCKS_FLAG_RESET, 1, &block});
    Patch(sampler, master);
    Event(sampler, SCE_NGS2_VOICE_EVENT_PLAY);
    constexpr std::array<int, 8> expected{7, 11, 7, 3, 3, 5, 6, 6};
    std::array<float, Grain> output{};
    const Ngs2RenderBufferInfo render{output.data(), sizeof(output), SCE_NGS2_WAVEFORM_TYPE_PCM_F32L, 1};
    Require(sceNgs2SystemRender(system, &render, 1) == SCE_NGS2_OK);
    for (std::size_t i = 0; i < expected.size(); ++i) Require(output[i] == expected[i] / 32768.0f);
    for (unsigned grain = 1; grain < 7; ++grain) {
        Require(sceNgs2SystemRender(system, &render, 1) == SCE_NGS2_OK);
        if (grain >= 2) {
            for (float sample : output) Require(sample == 5.0f / 32768);
        }
    }
    Require(sceNgs2SystemDestroy(system, nullptr) == SCE_NGS2_OK);
}

int main() {
    Parse();
    Playback();
    HistoryAndErrors();
}
