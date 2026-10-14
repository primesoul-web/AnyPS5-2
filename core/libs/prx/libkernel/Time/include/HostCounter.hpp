#ifndef CORE_LIBS_PRX_LIBKERNEL_TIME_HOSTCOUNTER_HPP
#define CORE_LIBS_PRX_LIBKERNEL_TIME_HOSTCOUNTER_HPP

#include <chrono>
#include <cstdint>
#include <thread>
#if defined(__x86_64__)
#include <x86intrin.h>
#elif defined(__APPLE__) && defined(__aarch64__)
#include <time.h>
#else
#error Unsupported host counter architecture
#endif

namespace HostCounter {

inline std::uint64_t Read() {
#if defined(__x86_64__)
    return __rdtsc();
#else
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
#endif
}

inline std::uint64_t Frequency() {
#if defined(__x86_64__)
    static const std::uint64_t frequency = [] {
        const auto start = std::chrono::steady_clock::now();
        const auto ticks = Read();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const auto endTicks = Read();
        const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start).count();
        return static_cast<std::uint64_t>(static_cast<long double>(endTicks - ticks) *
            1000000000.0L / static_cast<long double>(nanos));
    }();
    return frequency;
#else
    return 1000000000ULL;
#endif
}

}

#endif
