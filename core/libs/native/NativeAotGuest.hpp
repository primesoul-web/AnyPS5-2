#ifndef NATIVE_AOT_GUEST_HPP
#define NATIVE_AOT_GUEST_HPP

#include <cstddef>
#include <cstdint>

extern "C" {

struct alignas(16) NativeAotGuestState {
    std::uint64_t Gpr[16];
    std::uint8_t Xmm[16][16];
    std::uint64_t FsBase;
    std::uint64_t Mxcsr;
    std::uint64_t X87[8][2];
    std::uint64_t X87Top;
    std::uint64_t X87Control;
};

using NativeAotGuestFunction = std::uint64_t (*)(NativeAotGuestState*, std::uint64_t);

struct NativeAotGuestSegment {
    std::uint64_t Address;
    std::uint64_t FileSize;
    std::uint64_t MemorySize;
    std::uint64_t FileOffset;
    std::uint64_t Flags;
};

struct NativeAotGuestRelocation {
    std::uint64_t Address;
    std::uint64_t Kind;
    std::uint64_t Symbol;
    std::int64_t Addend;
};

struct NativeAotGuestEntry {
    std::uint64_t Address;
    NativeAotGuestFunction Function;
};

struct NativeAotGuestImage {
    std::uint64_t Version;
    const std::uint8_t* Bytes;
    std::uint64_t ByteCount;
    const NativeAotGuestSegment* Segments;
    std::uint64_t SegmentCount;
    const NativeAotGuestRelocation* Relocations;
    std::uint64_t RelocationCount;
    const char* const* Symbols;
    const std::uint64_t* SymbolFlags;
    std::uint64_t SymbolCount;
    const NativeAotGuestEntry* Entries;
    std::uint64_t EntryCount;
    std::uint64_t EntryPoint;
    std::uint64_t Span;
    std::uint64_t RelroAddress;
    std::uint64_t RelroSize;
    std::uint64_t* Base;
    std::uint64_t TlsAddress;
    std::uint64_t TlsFileSize;
    std::uint64_t TlsMemorySize;
    std::uint64_t TlsAlignment;
};

[[noreturn]] void NativeAotGuestTrap(std::uint64_t kind, std::uint64_t address, std::uint64_t detail);
NativeAotGuestFunction NativeAotGuestLookup(std::uint64_t address);
void NativeAotGuestCpuid(NativeAotGuestState* state);
std::uint64_t NativeAotGuestReadTsc();
}

namespace NativeAotGuest {

enum Trap : std::uint64_t {
    UnknownTarget = 1,
    UnexpectedReturn = 2,
    DivideError = 3,
    UnknownEntry = 4,
    FallThrough = 5,
    InvalidOpcode = 6,
    Halt = 7,
    MissingImport = 8,
    UnsupportedRelocation = 9,
    UnsupportedOperation = 10,
};

enum SymbolFlag : std::uint64_t {
    Weak = 1,
};

using Provider = void (*)(NativeAotGuestState&);

struct ProviderBinding {
    const char* Name;
    Provider Function;
    void* Data;
};

void RegisterProviders(const ProviderBinding* bindings, std::size_t count);
std::uint64_t Load(const NativeAotGuestImage& image);
std::uint64_t ImageBase();
NativeAotGuestState& CurrentState();
std::uint64_t ReturnSentinel();
std::uint64_t Invoke(NativeAotGuestState& state, std::uint64_t target);
std::uint64_t Argument(const NativeAotGuestState& state, std::size_t index);
double DoubleArgument(const NativeAotGuestState& state, std::size_t index);
void Return(NativeAotGuestState& state, std::uint64_t value);
void ReturnDouble(NativeAotGuestState& state, double value);
std::uint64_t Push(NativeAotGuestState& state, std::uint64_t value);
std::uint64_t ProviderAddress(const char* name);
std::uint64_t AllocateStack(std::size_t bytes);
[[noreturn]] void Start(const NativeAotGuestImage& image, int argc, char** argv, char** envp);

}

#endif
