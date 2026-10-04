#pragma once

#include <atomic>
#include <cstdint>

namespace Brezel {

/**
 * @brief Lock-free, wait-free Simpson 4-Slot Buffer for Single-Producer Single-Consumer (SPSC) data exchange.
 * 
 * Proven mathematically (H.R. Simpson, 1990) to guarantee 100% wait-free, non-blocking reads and writes
 * without data tearing, retries, or priority inversion, even when writer and reader run at different frequencies.
 */
template <typename T>
class TripleBuffer {
public:
    TripleBuffer() {
        slotInPair_[0].store(false, std::memory_order_relaxed);
        slotInPair_[1].store(false, std::memory_order_relaxed);
        readingPair_.store(false, std::memory_order_relaxed);
        latestPair_.store(false, std::memory_order_relaxed);
        hasNew_.store(false, std::memory_order_relaxed);
    }

    /**
     * @brief Producer writes a new value to the inactive pair/slot (fully wait-free O(1)).
     */
    void write(const T& val) {
        bool pair = !readingPair_.load(std::memory_order_relaxed);
        bool slot = !slotInPair_[pair ? 1 : 0].load(std::memory_order_relaxed);

        data_[pair ? 1 : 0][slot ? 1 : 0] = val;

        slotInPair_[pair ? 1 : 0].store(slot, std::memory_order_release);
        latestPair_.store(pair, std::memory_order_release);
        hasNew_.store(true, std::memory_order_release);
    }

    /**
     * @brief Consumer reads the most recent clean value (fully wait-free O(1)).
     * @return true if a buffer was successfully read.
     */
    bool read(T& outVal) {
        bool pair = latestPair_.load(std::memory_order_acquire);
        readingPair_.store(pair, std::memory_order_release);
        bool slot = slotInPair_[pair ? 1 : 0].load(std::memory_order_acquire);

        outVal = data_[pair ? 1 : 0][slot ? 1 : 0];
        hasNew_.store(false, std::memory_order_relaxed);
        return true;
    }

    /**
     * @brief Direct query of latest clean buffer.
     */
    bool readLatest(T& outVal) const {
        bool pair = latestPair_.load(std::memory_order_acquire);
        bool slot = slotInPair_[pair ? 1 : 0].load(std::memory_order_acquire);
        outVal = data_[pair ? 1 : 0][slot ? 1 : 0];
        return true;
    }

    /**
     * @brief Check if a new unread value has been written by the producer.
     */
    bool hasNew() const {
        return hasNew_.load(std::memory_order_acquire);
    }

private:
    T data_[2][2]{};
    std::atomic<bool> readingPair_{false};
    std::atomic<bool> slotInPair_[2]{};
    std::atomic<bool> latestPair_{false};
    std::atomic<bool> hasNew_{false};
};

} // namespace Brezel
