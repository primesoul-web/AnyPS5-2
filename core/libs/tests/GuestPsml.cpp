#include "prx/libc/include/general/VabiMacros.hpp"
#include <array>
#include <cstdint>
#include <cstdlib>

extern "C" {
std::int32_t APS5_VABI scePsmlMfsrGetContextBufferRequirement1100(void* requirement, const void* param);
std::int32_t APS5_VABI scePsmlMfsrCreateContext1100(void** context, const void* param);
std::int32_t APS5_VABI scePsmlMfsrGetDispatchMfsrPacket1100(void* context, void* commandBuffer, const void* param);
std::int32_t APS5_VABI scePsmlMfsrGetContextBufferRequirement800M3_2(void* requirement, const void* param);
std::int32_t APS5_VABI scePsmlMfsrCreateContext800M3_2(void** context, const void* param);
std::int32_t APS5_VABI scePsmlMfsrCreateContext1300(void** context, const void* param);
std::int32_t APS5_VABI scePsmlMfsrGetDispatchMfsrPacket900(void* context, void* commandBuffer, const void* param);
std::int32_t APS5_VABI scePsmlMfsrGetDispatchMfsrPacket1000(void* context, void* commandBuffer, const void* param);
std::int32_t APS5_VABI scePsmlMfsrGetDispatchMfsrPacket1300(void* context, void* commandBuffer, const void* param);
}

namespace {

void Require(bool value) { if (!value) std::abort(); }

constexpr std::int32_t kErrNotInitialized = static_cast<std::int32_t>(0x8A810001);

struct CommandBuffer {
    std::uint32_t* cursor;
    std::uint32_t sizeInDwords;
};

using CreateContext = std::int32_t (APS5_VABI*)(void**, const void*);
using GetDispatchPacket = std::int32_t (APS5_VABI*)(void*, void*, const void*);

void CreateContextUntouched(CreateContext create) {
    std::array<std::uint8_t, 0x158> param{};
    std::uint64_t contextStorage = 0;
    void* const sentinel = &contextStorage;
    void* context = sentinel;
    Require(create(&context, param.data()) == kErrNotInitialized);
    Require(context == sentinel);
    Require(create(nullptr, nullptr) == kErrNotInitialized);
}

void DispatchPacketUntouched(GetDispatchPacket dispatch) {
    std::array<std::uint8_t, 0x158> param{};
    std::uint64_t contextStorage = 0;
    std::array<std::uint32_t, 64> dwords;
    dwords.fill(0xa5a5a5a5u);
    const auto untouchedDwords = dwords;
    CommandBuffer commandBuffer{dwords.data(), static_cast<std::uint32_t>(dwords.size())};
    Require(dispatch(&contextStorage, &commandBuffer, param.data()) == kErrNotInitialized);
    Require(dispatch(nullptr, &commandBuffer, nullptr) == kErrNotInitialized);
    Require(commandBuffer.cursor == dwords.data());
    Require(commandBuffer.sizeInDwords == dwords.size());
    Require(dwords == untouchedDwords);
}

}

int main() {
    std::array<std::uint8_t, 0x158> param{};

    std::array<std::uint8_t, 0x18> requirement;
    requirement.fill(0xa5);
    const auto untouchedRequirement = requirement;
    Require(scePsmlMfsrGetContextBufferRequirement1100(requirement.data(), param.data()) == kErrNotInitialized);
    Require(requirement == untouchedRequirement);
    Require(scePsmlMfsrGetContextBufferRequirement1100(nullptr, nullptr) == kErrNotInitialized);

    std::uint64_t contextStorage = 0;
    void* const sentinel = &contextStorage;
    void* context = sentinel;
    Require(scePsmlMfsrCreateContext1100(&context, param.data()) == kErrNotInitialized);
    Require(context == sentinel);
    Require(scePsmlMfsrCreateContext1100(nullptr, nullptr) == kErrNotInitialized);

    std::array<std::uint32_t, 64> dwords;
    dwords.fill(0xa5a5a5a5u);
    const auto untouchedDwords = dwords;
    CommandBuffer commandBuffer{dwords.data(), static_cast<std::uint32_t>(dwords.size())};
    Require(scePsmlMfsrGetDispatchMfsrPacket1100(&contextStorage, &commandBuffer, param.data()) == kErrNotInitialized);
    Require(commandBuffer.cursor == dwords.data());
    Require(commandBuffer.sizeInDwords == dwords.size());
    Require(dwords == untouchedDwords);
    Require(scePsmlMfsrGetDispatchMfsrPacket1100(nullptr, &commandBuffer, param.data()) == kErrNotInitialized);
    Require(commandBuffer.cursor == dwords.data());
    Require(dwords == untouchedDwords);

    std::array<std::uint8_t, 0x18> olderRequirement;
    olderRequirement.fill(0xa5);
    const auto untouchedOlderRequirement = olderRequirement;
    Require(scePsmlMfsrGetContextBufferRequirement800M3_2(olderRequirement.data(), param.data()) == kErrNotInitialized);
    Require(olderRequirement == untouchedOlderRequirement);
    Require(scePsmlMfsrGetContextBufferRequirement800M3_2(nullptr, nullptr) == kErrNotInitialized);

    CreateContextUntouched(scePsmlMfsrCreateContext800M3_2);
    CreateContextUntouched(scePsmlMfsrCreateContext1300);
    DispatchPacketUntouched(scePsmlMfsrGetDispatchMfsrPacket900);
    DispatchPacketUntouched(scePsmlMfsrGetDispatchMfsrPacket1000);
    DispatchPacketUntouched(scePsmlMfsrGetDispatchMfsrPacket1300);
}
