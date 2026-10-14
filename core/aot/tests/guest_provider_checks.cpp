#include "native/NativeAotGlibc.hpp"
#include "native/NativeAotGuest.hpp"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

NativeAotGuestState state;
std::uint64_t stack[4096];
int failures = 0;

void Check(bool condition, const char* label) {
    if (!condition) {
        std::printf("FAIL %s\n", label);
        ++failures;
    }
}

std::uint64_t Call(const char* name, std::initializer_list<std::uint64_t> arguments, std::initializer_list<std::uint64_t> extra = {}) {
    const auto* binding = NativeAotGuest::FindProviderBinding(name);
    if (binding == nullptr || binding->Function == nullptr) {
        std::printf("FAIL missing %s\n", name);
        std::exit(1);
    }
    const int order[6] = {7, 6, 2, 1, 8, 9};
    int index = 0;
    for (auto value : arguments) {
        if (index == 6) {
            std::printf("FAIL too many register arguments for %s\n", name);
            std::exit(1);
        }
        state.Gpr[order[index++]] = value;
    }
    auto* top = stack + 2048;
    std::uint64_t position = 1;
    for (auto value : extra) top[position++] = value;
    state.Gpr[4] = reinterpret_cast<std::uint64_t>(top);
    binding->Function(state);
    return state.Gpr[0];
}

template<class Value>
std::uint64_t Raw(Value value) {
    return static_cast<std::uint64_t>(value);
}

std::uint64_t Address(const void* pointer) {
    return reinterpret_cast<std::uint64_t>(pointer);
}

void Formatting() {
    char buffer[256];
    const double values[2] = {2.5, -0.125};
    std::memcpy(state.Xmm[0], &values[0], 8);
    std::memcpy(state.Xmm[1], &values[1], 8);
    const auto written = Call("snprintf", {Address(buffer), sizeof(buffer), Address("%d|%u|%x|%5ld|%-3s|%c|%p|%.2f|%e|%%|%hhd|%*d"),
                                           Raw(-7), Raw(4294967295u), Raw(255)},
                              {Raw(42), Address("ab"), Raw('Z'), 0, Raw(300), Raw(4), Raw(9)});
    Check(std::string(buffer) == "-7|4294967295|ff|   42|ab |Z|(nil)|2.50|-1.250000e-01|%|44|   9", "snprintf conversions");
    Check(written == std::strlen(buffer), "snprintf length");
    Call("snprintf", {Address(buffer), 4, Address("%s"), Address("truncated")});
    Check(std::string(buffer) == "tru", "snprintf truncation");
}

void Scanning() {
    int number = 0, hex = 0, consumed = 0;
    double real = 0;
    char word[16] = {}, set[16] = {};
    const auto assigned = Call("__isoc99_sscanf", {Address("  12 abc 3.5 0x1f xyz!"), Address("%d %s %lf %x %[a-z]%n"),
                                                   Address(&number), Address(word), Address(&real), Address(&hex)},
                               {Address(set), Address(&consumed)});
    Check(consumed == 21, "sscanf %n count");
    Check(assigned == 5 && number == 12 && std::string(word) == "abc" && real == 3.5 && hex == 31 && std::string(set) == "xyz" && consumed == 21,
          "sscanf conversions");
    Check(static_cast<int>(Call("__isoc99_sscanf", {Address(""), Address("%d"), Address(&number)})) == -1, "sscanf end of input");
    Check(Call("__isoc99_sscanf", {Address("7 8"), Address("%*d %d"), Address(&number)}) == 1 && number == 8, "sscanf suppression");
    Check(Call("__isoc99_sscanf", {Address("x"), Address("%d"), Address(&number)}) == 0, "sscanf mismatch");
}

void Errors() {
    auto* location = reinterpret_cast<int*>(Call("__errno_location", {}));
    Call("close", {Raw(-1)});
    Check(*location == 9, "close sets EBADF");
    Call("open", {Address("/definitely/missing/path"), 0, 0});
    Check(*location == 2, "open sets ENOENT");
    const auto* message = reinterpret_cast<const char*>(Call("strerror", {Raw(11)}));
    Check(std::string(message) == std::strerror(EAGAIN), "strerror uses glibc numbering");
}

void Files() {
    char path[] = "/tmp/anyps5-provider-XXXXXX";
    const int descriptor = mkstemp(path);
    write(descriptor, "hello", 5);
    close(descriptor);
    unlink(path);
    const auto created = static_cast<int>(Call("open", {Address(path), Raw(0100 | 0200 | 1), Raw(0600)}));
    Check(created >= 0, "open translates O_CREAT|O_EXCL|O_WRONLY");
    Check(static_cast<int>(Call("open", {Address(path), Raw(0100 | 0200 | 1), Raw(0600)})) == -1, "O_EXCL rejects existing file");
    write(created, "hello world", 11);
    close(created);
    std::uint8_t stat[144] = {};
    Check(Call("__xstat", {1, Address(path), Address(stat)}) == 0, "stat succeeds");
    std::int64_t size;
    std::uint32_t mode;
    std::memcpy(&size, stat + 48, 8);
    std::memcpy(&mode, stat + 24, 4);
    Check(size == 11 && (mode & 0170000) == 0100000 && (mode & 0777) == 0600, "glibc stat layout");
    std::int64_t times[4] = {1000, 0, 0, (1 << 30) - 2};
    Check(Call("utimensat", {Raw(-100), Address(path), Address(times), 0}) == 0, "utimensat translates AT_FDCWD and UTIME_OMIT");
    struct stat host {};
    ::stat(path, &host);
    Check(host.st_atimespec.tv_sec == 1000, "utimensat sets access time");
    unlink(path);
}

void Ctype() {
    auto** classes = reinterpret_cast<const unsigned short**>(Call("__ctype_b_loc", {}));
    auto** lower = reinterpret_cast<const std::int32_t**>(Call("__ctype_tolower_loc", {}));
    Check(((*classes)['a'] & 0x400) && ((*classes)['a'] & 0x200) && !((*classes)['A'] & 0x200), "ctype alpha/lower bits");
    Check(((*classes)['7'] & 0x800) && ((*classes)[' '] & 0x2000) && ((*classes)['\t'] & 0x1) && ((*classes)['!'] & 0x4), "ctype digit/space/blank/punct bits");
    Check((*classes)[-1] == 0 && (*classes)[200] == 0, "ctype EOF and high bytes");
    Check((*lower)['Q'] == 'q' && (*lower)['q'] == 'q' && (*lower)[-1] == -1, "tolower table");
    const auto locale = Call("newlocale", {Raw(0x1fbf), Address("C"), 0});
    std::uint64_t ctypeField;
    std::memcpy(&ctypeField, reinterpret_cast<const void*>(locale + 104), 8);
    Check(locale != 0 && ctypeField == Address(*classes), "glibc locale_t ctype field");
    Check(Call("newlocale", {Raw(0x1fbf), Address("xx_YY.UTF-8"), 0}) == 0, "missing locale returns null");
    std::int32_t wide = 0;
    Check(Call("mbrtowc", {Address(&wide), Address("A"), 1, 0}) == 1 && wide == 'A', "mbrtowc ASCII");
    Check(static_cast<std::int64_t>(Call("mbrtowc", {Address(&wide), Address("\xc3\xa9"), 2, 0})) == -1, "mbrtowc rejects non-ASCII in C locale");
}

void Numbers() {
    char* end = nullptr;
    Check(Call("strtol", {Address("-123x"), Address(&end), 10}) == Raw(-123) && *end == 'x', "strtol");
    Call("strtod", {Address("2.75"), 0});
    double value;
    std::memcpy(&value, state.Xmm[0], 8);
    Check(value == 2.75, "strtod returns xmm0");
    wchar_t buffer[32];
    Check(Call("swprintf", {Address(buffer), 32, Address(L"%d-%s"), Raw(5), Address("ok")}) == 4 && std::wcscmp(buffer, L"5-ok") == 0, "swprintf");
    void* aligned = reinterpret_cast<void*>(Call("aligned_alloc", {64, 100}));
    Check(aligned != nullptr && Address(aligned) % 64 == 0, "aligned_alloc alignment");
    std::free(aligned);
    Check(Call("syscall", {186}) == Raw(getpid()), "gettid of the main thread");
    std::int64_t time[2] = {};
    Check(Call("clock_gettime", {1, Address(time)}) == 0 && (time[0] > 0 || time[1] > 0), "CLOCK_MONOTONIC translation");
    Check(Call("sysconf", {84}) == Raw(sysconf(_SC_NPROCESSORS_ONLN)), "sysconf processor count");
}

}

int main() {
    NativeAotGlibc::Register();
    Formatting();
    Scanning();
    Errors();
    Files();
    Ctype();
    Numbers();
    std::printf(failures == 0 ? "ok\n" : "failed\n");
    return failures == 0 ? 0 : 1;
}
