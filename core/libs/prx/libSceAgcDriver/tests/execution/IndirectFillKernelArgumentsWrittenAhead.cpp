#include "VulkanTestDevice.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver.hpp"
#include "prx/libSceAgcDriver/Submit/include/Acb.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

alignas(256) std::array<std::uint32_t, 9> WriterCode{0xf4080200u, 0xfa000000u, 0xbf8cc07fu, 0x7e000202u, 0x7e020203u, 0x7e040204u, 0xe07c0000u, 0x80020000u, 0xbf810000u};
alignas(256) std::array<std::uint32_t, 9> FillKernel{0xd7460004u, 0x04010c08u, 0x7e000204u, 0x7e020205u, 0x7e040206u, 0x7e060207u, 0xe01c2000u, 0x80000004u, 0xbf810000u};

constexpr std::uint32_t Records = 256;
constexpr std::uint32_t GroupThreads = 64;
constexpr std::uint32_t Untouched = 0xcafebabeu;
constexpr std::array<std::uint32_t, 4> Pattern{0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u};
alignas(256) std::array<std::uint32_t, 4> Arguments{1, 1, 1, 0};
alignas(256) std::array<std::uint32_t, 4> ArgumentsDescriptor{};
alignas(256) std::array<std::uint32_t, Records * 4> Output{};
alignas(256) volatile std::uint32_t Gate = 1;
alignas(256) volatile std::uint32_t Done = 0;

std::uint32_t Low(const volatile void* pointer) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pointer));
}

std::uint32_t High(const volatile void* pointer) {
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(pointer)) >> 32u);
}

std::array<std::uint32_t, 4> Descriptor(const void* pointer, std::uint32_t bytes) {
    return {Low(pointer), High(pointer) & 0xffffu, bytes, 0x31016facu};
}

void SetShaderRegister(std::vector<std::uint32_t>& words, std::uint32_t offset, std::uint32_t value) {
    words.insert(words.end(), {0xc0017600u, offset, value});
}

void Program(std::vector<std::uint32_t>& words, const void* code, std::initializer_list<std::uint32_t> userData, std::uint32_t resource, std::initializer_list<std::uint32_t> threads) {
    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(code));
    SetShaderRegister(words, 0x20c, static_cast<std::uint32_t>(address >> 8u));
    SetShaderRegister(words, 0x20d, static_cast<std::uint32_t>(address >> 40u) & 0xffu);
    std::uint32_t offset = 0x207;
    for (const auto value : threads) SetShaderRegister(words, offset++, value);
    SetShaderRegister(words, 0x212, 0);
    SetShaderRegister(words, 0x213, resource);
    offset = 0x240;
    for (const auto value : userData) SetShaderRegister(words, offset++, value);
}

std::uint32_t FilledRecords() {
    std::uint32_t filled = 0;
    for (std::uint32_t record = 0; record < Records; ++record) {
        const auto* bytes = Output.data() + record * 4u;
        if (std::equal(Pattern.begin(), Pattern.end(), bytes)) ++filled;
        else if (!std::all_of(bytes, bytes + 4, [](std::uint32_t word) { return word == Untouched; })) throw std::runtime_error("record " + std::to_string(record) + " holds neither the pattern nor its initial value");
    }
    return filled;
}

}

int main() {
    try {
        auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        device.reset();
        ArgumentsDescriptor = Descriptor(Arguments.data(), sizeof(Arguments));
        Output.fill(Untouched);
        const auto fillKernel = [&](std::vector<std::uint32_t>& words) {
            Program(words, FillKernel.data(), {Low(Output.data()), (High(Output.data()) & 0xffffu) | (16u << 16u), Records, 0x4bfacu, Pattern[0], Pattern[1], Pattern[2], Pattern[3]}, (8u << 1u) | 0x80u, {GroupThreads, 1, 1});
        };
        std::vector<std::uint32_t> warmUp;
        fillKernel(warmUp);
        warmUp.insert(warmUp.end(), {0xc0031500u, 1, 1, 1, 0x8041u});
        warmUp.insert(warmUp.end(), {0xc0033700u, 0x00100200u, Low(&Done), High(&Done), 2u});
        Packet first{warmUp.data(), static_cast<std::uint32_t>(warmUp.size()), 0, {}};
        Require(sceAgcDriverSubmitAcb(0x20, &first) == 0, "warm-up submit failed");
        AgcDriverWaitIdle_nid_postfix();
        Require(Done == 2, "the warm-up submission did not finish");
        Require(FilledRecords() == GroupThreads, "the warm-up fill of one group filled " + std::to_string(FilledRecords()) + " records, expected " + std::to_string(GroupThreads));
        Output.fill(Untouched);
        std::vector<std::uint32_t> words;
        words.insert(words.end(), {0xc0053c00u, 0x13u, Low(&Gate), High(&Gate), 1u, 0xffffffffu, 0x19u});
        Program(words, WriterCode.data(), {Low(ArgumentsDescriptor.data()), High(ArgumentsDescriptor.data()), 2u, 1u, 1u}, 5u << 1u, {1, 1, 1});
        words.insert(words.end(), {0xc0031500u, 1, 1, 1, 0x8041u});
        fillKernel(words);
        words.insert(words.end(), {0xc0021600u, Low(Arguments.data()), High(Arguments.data()), 0x8041u});
        words.insert(words.end(), {0xc0033700u, 0x00100200u, Low(&Done), High(&Done), 1u});
        Packet packet{words.data(), static_cast<std::uint32_t>(words.size()), 0, {}};
        Require(sceAgcDriverSubmitAcb(0x20, &packet) == 0, "compute submit failed");
        AgcDriverWaitIdle_nid_postfix();
        Require(Done == 1, "the submission did not finish");
        Require(Arguments[0] == 2 && Arguments[1] == 1 && Arguments[2] == 1, "the first dispatch did not write the indirect arguments");
        const auto filled = FilledRecords();
        Require(filled == 2 * GroupThreads, "an indirect fill kernel dispatch whose arguments the dispatch before it writes filled " + std::to_string(filled) + " records, expected " + std::to_string(2 * GroupThreads) + ": its group counts must be read after the earlier dispatch ran, not when the gated group was resolved");
        std::cout << "indirect fill kernel arguments written ahead test passed\n";
        AgcDriverShutdown_nid_postfix();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        try {
            AgcDriverShutdown_nid_postfix();
        } catch (const std::exception& shutdownError) {
            std::cerr << shutdownError.what() << '\n';
        }
        return 1;
    }
}
