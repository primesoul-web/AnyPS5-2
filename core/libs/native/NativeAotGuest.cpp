#include "native/NativeAotGuest.hpp"
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mach/mach_time.h>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

namespace {

constexpr std::uint64_t PageSize = 16384;
constexpr std::uint64_t RelocationRelative = 8;
constexpr std::uint64_t RelocationGlobalData = 6;
constexpr std::uint64_t RelocationJumpSlot = 7;
constexpr std::uint64_t RelocationAbsolute = 1;
constexpr std::uint64_t RelocationNone = 0;
constexpr std::size_t Rsp = 4;
constexpr std::size_t ArgumentRegisters[6] = {7, 6, 2, 1, 8, 9};

const NativeAotGuestImage* loaded = nullptr;
std::uint64_t imageBase = 0;
std::uint64_t importRegion = 0;
std::uint64_t importRegionSize = 0;
std::vector<NativeAotGuest::Provider> importProviders;
std::vector<NativeAotGuest::ProviderBinding> registry;
thread_local NativeAotGuestState* current = nullptr;

const char* TrapName(std::uint64_t kind) {
    switch (kind) {
        case NativeAotGuest::UnknownTarget: return "control transfer to an address without converted code";
        case NativeAotGuest::UnexpectedReturn: return "guest return address does not match any converted caller";
        case NativeAotGuest::DivideError: return "divide error";
        case NativeAotGuest::UnknownEntry: return "converted unit entered at an unknown address";
        case NativeAotGuest::FallThrough: return "execution continued past the end of converted code";
        case NativeAotGuest::InvalidOpcode: return "invalid opcode";
        case NativeAotGuest::Halt: return "privileged halt instruction";
        case NativeAotGuest::MissingImport: return "missing import provider";
        case NativeAotGuest::UnsupportedRelocation: return "unsupported relocation";
        case NativeAotGuest::MisalignedVector: return "misaligned aligned vector access";
        case NativeAotGuest::SplitLock: return "locked access crosses its natural alignment";
        default: return "unsupported guest operation";
    }
}

int TrapSignal(std::uint64_t kind) {
    switch (kind) {
        case NativeAotGuest::DivideError: return SIGFPE;
        case NativeAotGuest::InvalidOpcode: return SIGILL;
        default: return SIGSEGV;
    }
}

[[noreturn]] void Fail(const std::string& message) {
    std::fprintf(stderr, "native AOT guest: %s\n", message.c_str());
    std::fflush(stderr);
    std::_Exit(127);
}

const NativeAotGuest::ProviderBinding* FindProvider(const char* name) {
    for (const auto& binding : registry)
        if (std::strcmp(binding.Name, name) == 0) return &binding;
    return nullptr;
}

std::uint64_t ImportThunk(NativeAotGuestState* state, std::uint64_t address) {
    const auto index = (address - importRegion) / 16 - 1;
    if (index >= importProviders.size() || importProviders[index] == nullptr)
        NativeAotGuestTrap(NativeAotGuest::MissingImport, address, index);
    auto* previous = current;
    current = state;
    importProviders[index](*state);
    current = previous;
    const auto rsp = state->Gpr[Rsp];
    std::uint64_t target;
    std::memcpy(&target, reinterpret_cast<const void*>(rsp), sizeof(target));
    state->Gpr[Rsp] = rsp + 8;
    return target;
}

void ProtectImage(const NativeAotGuestImage& image, std::uint64_t span) {
    std::vector<int> pages(span / PageSize, PROT_NONE);
    std::vector<bool> relro(span / PageSize, false);
    for (std::uint64_t index = 0; index < image.SegmentCount; ++index) {
        const auto& segment = image.Segments[index];
        if (segment.MemorySize == 0) continue;
        const auto protection = (segment.Flags & 2) ? PROT_READ | PROT_WRITE : PROT_READ;
        for (auto page = segment.Address / PageSize; page <= (segment.Address + segment.MemorySize - 1) / PageSize; ++page)
            pages[page] |= protection;
    }
    if (image.RelroSize != 0) {
        const auto first = (image.RelroAddress + PageSize - 1) / PageSize;
        const auto last = (image.RelroAddress + image.RelroSize) / PageSize;
        for (auto page = first; page < last; ++page) relro[page] = true;
    }
    for (std::uint64_t page = 0; page < pages.size(); ++page) {
        const auto protection = relro[page] ? (pages[page] & ~PROT_WRITE) : pages[page];
        if (mprotect(reinterpret_cast<void*>(imageBase + page * PageSize), PageSize, protection) != 0)
            Fail("cannot protect guest image memory");
    }
}

}

extern "C" [[noreturn]] void NativeAotGuestTrap(std::uint64_t kind, std::uint64_t address, std::uint64_t detail) {
    std::fprintf(stderr, "native AOT guest trap: %s at 0x%llx (image offset 0x%llx, detail 0x%llx)\n", TrapName(kind),
                 static_cast<unsigned long long>(address), static_cast<unsigned long long>(address - imageBase),
                 static_cast<unsigned long long>(detail));
    std::fflush(stderr);
    std::signal(TrapSignal(kind), SIG_DFL);
    std::raise(TrapSignal(kind));
    std::_Exit(128 + TrapSignal(kind));
}

extern "C" NativeAotGuestFunction NativeAotGuestFind(std::uint64_t address) {
    if (loaded != nullptr && address >= imageBase && address - imageBase < loaded->Span) {
        const auto offset = address - imageBase;
        const auto* begin = loaded->Entries;
        const auto* end = begin + loaded->EntryCount;
        const auto* found = std::lower_bound(begin, end, offset, [](const NativeAotGuestEntry& entry, std::uint64_t value) {
            return entry.Address < value;
        });
        if (found != end && found->Address == offset) return found->Function;
    }
    if (address > importRegion && address - importRegion < importRegionSize && (address - importRegion) % 16 == 0)
        return ImportThunk;
    return nullptr;
}

extern "C" NativeAotGuestFunction NativeAotGuestLookup(std::uint64_t address) {
    if (const auto function = NativeAotGuestFind(address)) return function;
    NativeAotGuestTrap(NativeAotGuest::UnknownTarget, address, 0);
}

extern "C" void NativeAotGuestCpuid(NativeAotGuestState* state) {
    const auto leaf = static_cast<std::uint32_t>(state->Gpr[0]);
    const auto subleaf = static_cast<std::uint32_t>(state->Gpr[1]);
    std::uint32_t a = 0, b = 0, c = 0, d = 0;
    if (leaf == 0) {
        a = 7;
        std::memcpy(&b, "AnyP", 4);
        std::memcpy(&d, "S5Gu", 4);
        std::memcpy(&c, "estC", 4);
    } else if (leaf == 1) {
        a = 0x000906a0;
        c = (1u << 23);
        d = (1u << 0) | (1u << 4) | (1u << 8) | (1u << 15) | (1u << 25) | (1u << 26);
    } else if (leaf == 7 && subleaf == 0) {
        b = (1u << 3) | (1u << 8);
    }
    state->Gpr[0] = a;
    state->Gpr[3] = b;
    state->Gpr[1] = c;
    state->Gpr[2] = d;
}

extern "C" std::uint64_t NativeAotGuestReadTsc() {
    return mach_absolute_time();
}

namespace NativeAotGuest {

void RegisterProviders(const ProviderBinding* bindings, std::size_t count) {
    registry.insert(registry.end(), bindings, bindings + count);
}

const ProviderBinding* FindProviderBinding(const char* name) {
    return FindProvider(name);
}

std::uint64_t ProviderAddress(const char* name) {
    if (loaded == nullptr) return 0;
    for (std::uint64_t index = 0; index < loaded->SymbolCount; ++index)
        if (std::strcmp(loaded->Symbols[index], name) == 0 && importProviders[index] != nullptr)
            return importRegion + 16 * (index + 1);
    const auto* binding = FindProvider(name);
    if (binding == nullptr || binding->Function == nullptr) return 0;
    Fail(std::string("provider is not imported by the guest image: ") + name);
}

std::uint64_t Load(const NativeAotGuestImage& image) {
    if (image.Version != 2) Fail("unsupported guest image descriptor");
    if (loaded != nullptr) Fail("only one guest image is supported per process");
    const auto span = (image.Span + PageSize - 1) & ~(PageSize - 1);
    auto* memory = mmap(nullptr, span, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (memory == MAP_FAILED) Fail("cannot reserve guest image memory");
    imageBase = reinterpret_cast<std::uint64_t>(memory);
    for (std::uint64_t index = 0; index < image.SegmentCount; ++index) {
        const auto& segment = image.Segments[index];
        if (segment.Address + segment.MemorySize > image.Span || segment.FileOffset + segment.FileSize > image.ByteCount)
            Fail("guest segment exceeds the image");
        std::memcpy(reinterpret_cast<void*>(imageBase + segment.Address), image.Bytes + segment.FileOffset, segment.FileSize);
    }
    importRegionSize = 16 * (image.SymbolCount + 1);
    const auto regionBytes = (importRegionSize + PageSize - 1) & ~(PageSize - 1);
    auto* region = mmap(nullptr, regionBytes, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (region == MAP_FAILED) Fail("cannot reserve guest import addresses");
    importRegion = reinterpret_cast<std::uint64_t>(region);
    importProviders.assign(image.SymbolCount, nullptr);
    std::vector<std::uint64_t> values(image.SymbolCount, 0);
    std::string missing;
    for (std::uint64_t index = 0; index < image.SymbolCount; ++index) {
        const auto* binding = FindProvider(image.Symbols[index]);
        if (binding == nullptr) {
            if ((image.SymbolFlags[index] & Weak) == 0) missing += std::string(missing.empty() ? "" : ", ") + image.Symbols[index];
            continue;
        }
        if (binding->Function != nullptr) {
            importProviders[index] = binding->Function;
            values[index] = importRegion + 16 * (index + 1);
        } else {
            values[index] = reinterpret_cast<std::uint64_t>(binding->Data);
        }
    }
    if (!missing.empty()) Fail("missing import providers: " + missing);
    for (std::uint64_t index = 0; index < image.RelocationCount; ++index) {
        const auto& relocation = image.Relocations[index];
        if (relocation.Address + 8 > image.Span) Fail("guest relocation outside the image");
        std::uint64_t value;
        switch (relocation.Kind) {
            case RelocationNone: continue;
            case RelocationRelative: value = imageBase + static_cast<std::uint64_t>(relocation.Addend); break;
            case RelocationGlobalData:
            case RelocationJumpSlot:
            case RelocationAbsolute:
                if (relocation.Symbol >= image.SymbolCount) Fail("guest relocation symbol out of range");
                value = values[relocation.Symbol] == 0 ? 0 : values[relocation.Symbol] + static_cast<std::uint64_t>(relocation.Addend);
                break;
            default: Fail("unsupported guest relocation kind " + std::to_string(relocation.Kind));
        }
        std::memcpy(reinterpret_cast<void*>(imageBase + relocation.Address), &value, sizeof(value));
    }
    ProtectImage(image, span);
    *image.Base = imageBase;
    loaded = &image;
    return imageBase;
}

std::uint64_t ImageBase() {
    return imageBase;
}

std::uint64_t ImageSpan() {
    return loaded == nullptr ? 0 : loaded->Span;
}

std::uint64_t ExportAddress(const char* name) {
    if (loaded == nullptr || name == nullptr) return 0;
    for (std::uint64_t index = 0; index < loaded->ExportCount; ++index)
        if (std::strcmp(loaded->Exports[index].Name, name) == 0) return imageBase + loaded->Exports[index].Address;
    for (std::uint64_t index = 0; index < loaded->SymbolCount; ++index)
        if (std::strcmp(loaded->Symbols[index], name) == 0 && importProviders[index] != nullptr)
            return importRegion + 16 * (index + 1);
    return 0;
}

NativeAotGuestState& CurrentState() {
    if (current == nullptr) Fail("no active guest state on this thread");
    return *current;
}

std::uint64_t ReturnSentinel() {
    return importRegion;
}

std::uint64_t Push(NativeAotGuestState& state, std::uint64_t value) {
    state.Gpr[Rsp] -= 8;
    std::memcpy(reinterpret_cast<void*>(state.Gpr[Rsp]), &value, sizeof(value));
    return state.Gpr[Rsp];
}

std::uint64_t Invoke(NativeAotGuestState& state, std::uint64_t target) {
    const auto saved = state.Gpr[Rsp];
    state.Gpr[Rsp] &= ~std::uint64_t{15};
    Push(state, ReturnSentinel());
    auto* previous = current;
    current = &state;
    const auto result = NativeAotGuestLookup(target)(&state, target);
    current = previous;
    if (result != ReturnSentinel()) NativeAotGuestTrap(UnexpectedReturn, result, target);
    state.Gpr[Rsp] = saved;
    return state.Gpr[0];
}

std::uint64_t Argument(const NativeAotGuestState& state, std::size_t index) {
    if (index < 6) return state.Gpr[ArgumentRegisters[index]];
    std::uint64_t value;
    std::memcpy(&value, reinterpret_cast<const void*>(state.Gpr[Rsp] + 8 + 8 * (index - 6)), sizeof(value));
    return value;
}

double DoubleArgument(const NativeAotGuestState& state, std::size_t index) {
    if (index >= 8) Fail("stack floating-point arguments require a varargs or signature reader");
    double value;
    std::memcpy(&value, state.Xmm[index], sizeof(value));
    return value;
}

void Return(NativeAotGuestState& state, std::uint64_t value) {
    state.Gpr[0] = value;
}

void ReturnDouble(NativeAotGuestState& state, double value) {
    std::memset(state.Xmm[0], 0, sizeof(state.Xmm[0]));
    std::memcpy(state.Xmm[0], &value, sizeof(value));
}

std::uint64_t CreateThreadArea() {
    const auto segmentAlignment = std::max<std::uint64_t>(loaded == nullptr ? 1 : loaded->TlsAlignment, 1);
    const auto alignment = std::max<std::uint64_t>(segmentAlignment, 64);
    const auto block = loaded == nullptr ? 0 : (loaded->TlsMemorySize + segmentAlignment - 1) / segmentAlignment * segmentAlignment;
    const auto bytes = block + 2 * alignment + 0x100;
    auto* memory = static_cast<std::uint8_t*>(std::calloc(1, bytes));
    if (memory == nullptr) Fail("cannot allocate guest thread area");
    const auto start = reinterpret_cast<std::uint64_t>(memory);
    const auto pointer = (start + block + alignment - 1) / alignment * alignment;
    if (loaded != nullptr && loaded->TlsFileSize != 0)
        std::memcpy(reinterpret_cast<void*>(pointer - block), reinterpret_cast<const void*>(imageBase + loaded->TlsAddress), loaded->TlsFileSize);
    std::uint64_t guard[2];
    arc4random_buf(guard, sizeof(guard));
    guard[0] &= ~std::uint64_t{0xff};
    std::memcpy(reinterpret_cast<void*>(pointer), &pointer, 8);
    std::memcpy(reinterpret_cast<void*>(pointer + 0x10), &pointer, 8);
    std::memcpy(reinterpret_cast<void*>(pointer + 0x28), &guard[0], 8);
    std::memcpy(reinterpret_cast<void*>(pointer + 0x30), &guard[1], 8);
    std::memcpy(reinterpret_cast<void*>(pointer + 0xf0), &memory, sizeof(memory));
    return pointer;
}

void FreeThreadArea(std::uint64_t pointer) {
    void* memory;
    std::memcpy(&memory, reinterpret_cast<const void*>(pointer + 0xf0), sizeof(memory));
    std::free(memory);
}

bool ConcurrentMemory() {
    return loaded != nullptr && loaded->Concurrent == 1;
}

void FreeStack(std::uint64_t top, std::size_t bytes) {
    const auto size = (bytes + PageSize - 1) & ~(PageSize - 1);
    if (munmap(reinterpret_cast<void*>(top - size - PageSize), size + PageSize) != 0)
        Fail("cannot release guest stack");
}

std::uint64_t AllocateStack(std::size_t bytes) {
    const auto size = (bytes + PageSize - 1) & ~(PageSize - 1);
    auto* memory = mmap(nullptr, size + PageSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (memory == MAP_FAILED) Fail("cannot allocate guest stack");
    if (mprotect(memory, PageSize, PROT_NONE) != 0) Fail("cannot protect guest stack guard");
    return reinterpret_cast<std::uint64_t>(memory) + size + PageSize;
}

void Start(const NativeAotGuestImage& image, int argc, char** argv, char** envp) {
    Load(image);
    static NativeAotGuestState state;
    std::memset(&state, 0, sizeof(state));
    state.Mxcsr = 0x1f80;
    state.X87Control = 0x37f;
    auto top = AllocateStack(8 * 1024 * 1024);
    static std::uint8_t random[16];
    arc4random_buf(random, sizeof(random));
    std::size_t environment = 0;
    while (envp != nullptr && envp[environment] != nullptr) ++environment;
    const std::uint64_t auxiliary[] = {6, PageSize, 25, reinterpret_cast<std::uint64_t>(random), 9, imageBase + image.EntryPoint, 0, 0};
    const auto words = 1 + argc + 1 + environment + 1 + sizeof(auxiliary) / 8;
    top = (top - words * 8) & ~std::uint64_t{15};
    auto* cursor = reinterpret_cast<std::uint64_t*>(top);
    *cursor++ = static_cast<std::uint64_t>(argc);
    for (int index = 0; index < argc; ++index) *cursor++ = reinterpret_cast<std::uint64_t>(argv[index]);
    *cursor++ = 0;
    for (std::size_t index = 0; index < environment; ++index) *cursor++ = reinterpret_cast<std::uint64_t>(envp[index]);
    *cursor++ = 0;
    std::memcpy(cursor, auxiliary, sizeof(auxiliary));
    state.Gpr[Rsp] = top;
    state.Gpr[2] = 0;
    state.FsBase = CreateThreadArea();
    current = &state;
    const auto entry = imageBase + image.EntryPoint;
    const auto result = NativeAotGuestLookup(entry)(&state, entry);
    NativeAotGuestTrap(UnexpectedReturn, result, entry);
}

}
