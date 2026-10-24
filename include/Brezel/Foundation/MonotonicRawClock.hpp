#pragma once

#include <chrono>
#include <cstdint>

#if defined(__linux__)
#include <time.h>
#endif

namespace Brezel {

/**
 * @brief Nanoseconds of a monotonic clock that no time correction steps or slews.
 *
 * Linux: CLOCK_MONOTONIC_RAW, the hardware counter. CLOCK_MONOTONIC (std::chrono::steady_clock) never jumps but
 * follows the rate corrections of an NTP daemon (systemd-timesyncd, chrony): a loop paced by it speeds up or slows
 * down by up to a few hundred ppm while the daemon corrects, which a loop locked to an external clock (an EtherCAT
 * reference clock) sees as drift. macOS and Windows: std::chrono::steady_clock, already raw there (CLOCK_UPTIME_RAW,
 * the performance counter).
 *
 * The epoch is unspecified: compare values of this clock with each other only, never with steady_clock or a
 * timestamp taken from another clock. Thread-agnostic, wait-free, safe on a real-time thread.
 */
inline int64_t monotonicRawNanoseconds() noexcept {
#if defined(__linux__)
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1'000'000'000LL + ts.tv_nsec;
#else
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
#endif
}

} // namespace Brezel
