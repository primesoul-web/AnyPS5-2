#include "prx/libc/include/NativeAotBundle.hpp"
#include <array>
#include <stdexcept>

namespace {

unsigned Step(unsigned value) { return value + 1; }
void Check(bool valid) { if (!valid) throw std::runtime_error("native bundle regression"); }

template<class Function>
void Reject(Function function) {
    try { function(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("invalid native bundle accepted");
}

}

int main() {
    unsigned shared = 42;
    const NativeAotDataRange data[]{ {&shared, sizeof(shared)} };
    const NativeAotModuleExport symbols[]{ {"step", reinterpret_cast<const void*>(&Step), 0, 1}, {"shared", &shared, sizeof(shared), 2} };
    const void* functions[]{reinterpret_cast<const void*>(&Step)};
    const NativeAotModuleDependency dependency[]{ {0, 3} };
    std::array<NativeAotModule, 2> modules{{ {1, "counter", 3, data, 1, symbols, 2, nullptr, 0, functions, 1},
        {1, "app", 7, nullptr, 0, nullptr, 0, dependency, 1, nullptr, 0} }};
    NativeAotBundleRegistry registry(modules, 1);
    Check(registry.MainIndex() == 1 && registry.InitializationOrder().size() == 2 && registry.InitializationOrder()[0] == 0);
    Check(registry.Find("counter", 3, "shared", 2) == &shared);
    const auto step = reinterpret_cast<unsigned (*)(unsigned)>(const_cast<void*>(registry.Find("counter", 3, "step", 1)));
    Check(step(41) == 42);
    Reject([&] { registry.Find("counter", 4, "shared", 2); });
    Reject([&] { registry.Find("counter", 3, "missing", 2); });
    Reject([&] { registry.Find("counter", 3, "shared", 1); });
    auto invalid = modules;
    invalid[0].moduleVersion = 4;
    Reject([&] { NativeAotBundleRegistry check(invalid, 1); });
    invalid = modules;
    invalid[1].id = "counter";
    Reject([&] { NativeAotBundleRegistry check(invalid, 1); });
    invalid = modules;
    invalid[1].ranges = data;
    invalid[1].rangeCount = 1;
    Reject([&] { NativeAotBundleRegistry check(invalid, 1); });
    const NativeAotModuleDependency cycle[]{ {1, 7} };
    invalid = modules;
    invalid[0].dependencies = cycle;
    invalid[0].dependencyCount = 1;
    Reject([&] { NativeAotBundleRegistry check(invalid, 1); });
    Reject([&] { NativeAotBundleRegistry check(modules, 0); });
    const NativeAotModuleExport escaped[]{ {"outside", reinterpret_cast<const char*>(&shared) + sizeof(shared), 1, 2} };
    invalid = modules;
    invalid[0].exports = escaped;
    invalid[0].exportCount = 1;
    Reject([&] { NativeAotBundleRegistry check(invalid, 1); });
    return 0;
}
