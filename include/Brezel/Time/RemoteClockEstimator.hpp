#pragma once

#include <cstdint>

namespace Brezel {

/**
 * @brief Estimates the offset of a remote monotonic clock from the local one, from one-way timestamped samples.
 *
 * Each sample pairs a remote timestamp (the sender's clock at sending) with the local time it arrived. The offset is
 * the smallest `localNs - remoteNs` over a sliding window of a few seconds: the fastest arrival carries the least
 * transit delay. Fixed-size storage, no allocation. Thread-agnostic: one instance belongs to one thread (no internal
 * synchronisation).
 *
 * Interface only: the bodies are stubs that hold no estimate (hasEstimate() false, offset 0); the implementation
 * follows with its tests (Stacato run 2026-10-27, lane N, T3.16).
 */
class RemoteClockEstimator {
public:
    /// Thread-agnostic. Forget every sample (a new session, a remote restart)
    void reset() noexcept {}

    /// Thread-agnostic. One sample: the remote clock at sending and the local clock at arrival, both in ns
    void addSample(uint64_t remoteNs, uint64_t localNs) noexcept {
        (void)remoteNs;
        (void)localNs;
    }

    /// Thread-agnostic. True once the window holds enough samples for an offset
    [[nodiscard]] bool hasEstimate() const noexcept { return false; }

    /// Thread-agnostic. local minus remote (ns); 0 without an estimate
    [[nodiscard]] int64_t offsetNs() const noexcept { return 0; }

    /// Thread-agnostic. A remote timestamp in the local clock (remoteNs + offsetNs())
    [[nodiscard]] uint64_t toLocalNs(uint64_t remoteNs) const noexcept {
        return remoteNs + static_cast<uint64_t>(offsetNs());
    }
};

} // namespace Brezel
