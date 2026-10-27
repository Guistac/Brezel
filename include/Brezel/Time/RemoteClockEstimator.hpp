#pragma once

#include <array>
#include <cstddef>
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
 * The window is a ring of BUCKETS buckets of WINDOW_NS / BUCKETS each, keyed by the local arrival time; each holds the
 * smallest difference of its samples. The window ends at the newest sample's arrival (not at "now"): with no new
 * samples the last estimate stays. A sample older than the window is ignored. The estimate exists once the window
 * holds at least MIN_SAMPLES samples spread over at least MIN_SPAN_NS of arrival time, so one burst of queued samples
 * after a stall cannot make it alone.
 *
 * What it does not do: no rate (drift) estimate (crystal drift is about 50 ppm, a quarter of a millisecond over the
 * window), no two-way exchange (the result is the offset plus the fastest one-way transit).
 */
class RemoteClockEstimator {
public:
    static constexpr uint64_t WINDOW_NS = 5'000'000'000ULL;   ///< Sliding window of the minimum
    static constexpr size_t BUCKETS = 20;                     ///< 250 ms each
    static constexpr uint64_t BUCKET_NS = WINDOW_NS / BUCKETS;
    static constexpr uint32_t MIN_SAMPLES = 5;
    static constexpr uint64_t MIN_SPAN_NS = 1'000'000'000ULL; ///< Arrival time the samples must cover

    /// Thread-agnostic. Forget every sample (a new session, a remote restart)
    void reset() noexcept {
        buckets_ = {};
        newestBucket_ = 0;
        hasSamples_ = false;
        valid_ = false;
        offsetNs_ = 0;
    }

    /// Thread-agnostic. One sample: the remote clock at sending and the local clock at arrival, both in ns
    void addSample(uint64_t remoteNs, uint64_t localNs) noexcept {
        const uint64_t index = localNs / BUCKET_NS;
        if (hasSamples_ && index + BUCKETS <= newestBucket_) return;  // Older than the window
        if (!hasSamples_ || index > newestBucket_) newestBucket_ = index;
        hasSamples_ = true;

        // Modular difference: exact for any two clocks less than 292 years apart
        const int64_t diff = static_cast<int64_t>(localNs - remoteNs);
        Bucket& bucket = buckets_[index % BUCKETS];
        if (bucket.count == 0 || bucket.index != index) {
            bucket = Bucket{index, diff, localNs, localNs, 1};
        } else {
            if (diff < bucket.minDiffNs) bucket.minDiffNs = diff;
            if (localNs < bucket.firstLocalNs) bucket.firstLocalNs = localNs;
            if (localNs > bucket.lastLocalNs) bucket.lastLocalNs = localNs;
            if (bucket.count < UINT32_MAX) ++bucket.count;
        }
        update();
    }

    /// Thread-agnostic. True once the window holds enough samples for an offset
    [[nodiscard]] bool hasEstimate() const noexcept { return valid_; }

    /// Thread-agnostic. local minus remote (ns); 0 without an estimate
    [[nodiscard]] int64_t offsetNs() const noexcept { return valid_ ? offsetNs_ : 0; }

    /// Thread-agnostic. A remote timestamp in the local clock (remoteNs + offsetNs())
    [[nodiscard]] uint64_t toLocalNs(uint64_t remoteNs) const noexcept {
        return remoteNs + static_cast<uint64_t>(offsetNs());
    }

    /**
     * @brief Thread-agnostic. A remote stamp sent as a wrapping 32-bit microsecond count (it wraps every 71.6 minutes),
     * in full remote ns: the value nearest the remote time that corresponds to localNs (localNs - offsetNs()). Valid
     * for a stamp within about 35 minutes of that time; meaningful only with an estimate (without one the reference
     * is localNs itself).
     */
    [[nodiscard]] uint64_t remoteNsFromWrappedUs(uint32_t remoteUs, uint64_t localNs) const noexcept {
        const uint64_t referenceUs = (localNs - static_cast<uint64_t>(offsetNs())) / 1000ULL;
        // How far the stamp lies before the reference, in the wrapping count (negative: after it)
        const auto behindUs = static_cast<int32_t>(static_cast<uint32_t>(referenceUs) - remoteUs);
        const int64_t fullUs = static_cast<int64_t>(referenceUs) - behindUs;
        return fullUs > 0 ? static_cast<uint64_t>(fullUs) * 1000ULL : 0;
    }

private:
    struct Bucket {
        uint64_t index{0};         ///< localNs / BUCKET_NS of its samples
        int64_t minDiffNs{0};
        uint64_t firstLocalNs{0};
        uint64_t lastLocalNs{0};
        uint32_t count{0};         ///< 0: empty
    };

    void update() noexcept {
        bool any = false;
        int64_t minDiff = 0;
        uint64_t first = 0;
        uint64_t last = 0;
        uint64_t samples = 0;
        for (const Bucket& bucket : buckets_) {
            if (bucket.count == 0 || bucket.index + BUCKETS <= newestBucket_ || bucket.index > newestBucket_) continue;
            if (!any || bucket.minDiffNs < minDiff) minDiff = bucket.minDiffNs;
            if (!any || bucket.firstLocalNs < first) first = bucket.firstLocalNs;
            if (!any || bucket.lastLocalNs > last) last = bucket.lastLocalNs;
            samples += bucket.count;
            any = true;
        }
        valid_ = any && samples >= MIN_SAMPLES && last - first >= MIN_SPAN_NS;
        offsetNs_ = any ? minDiff : 0;
    }

    std::array<Bucket, BUCKETS> buckets_{};
    uint64_t newestBucket_{0};
    bool hasSamples_{false};
    bool valid_{false};
    int64_t offsetNs_{0};
};

} // namespace Brezel
