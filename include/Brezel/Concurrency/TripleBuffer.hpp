#pragma once

#include <atomic>
#include <cstdint>

namespace Brezel {

/**
 * @brief Lock-free, wait-free Simpson 4-Slot Buffer for Single-Producer Single-Consumer (SPSC) data exchange.
 * 
 * Proven mathematically (H.R. Simpson, 1990) to guarantee 100% wait-free, non-blocking reads and writes
 * without data tearing, retries, or priority inversion, even when writer and reader run at different frequencies.
 * The proof assumes sequential consistency on the handshake: the reader's store of readingPair_ and the writer's
 * load of it form a store-buffering pattern, so write() and read() each carry one seq_cst fence (BUG-22).
 * Exactly one thread may call write() (or several serialized by one mutex) and exactly one thread read();
 * readLatest() is an unsynchronized peek that may tear against a concurrent write.
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
        // Store-buffering fence (BUG-22): orders this write's load of readingPair_ after the previous write's stores
        // and against the reader's store of readingPair_; without it two back-to-back writes can land in the slot
        // the reader is copying (torn reads measured on arm64)
        std::atomic_thread_fence(std::memory_order_seq_cst);
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
        // Cleared before the pair is chosen: a write that lands during the copy keeps its flag (BUG-22)
        hasNew_.exchange(false, std::memory_order_acq_rel);
        bool pair = latestPair_.load(std::memory_order_acquire);
        readingPair_.store(pair, std::memory_order_release);
        // Store-buffering fence (BUG-22): the writer must see readingPair_ before this load of slotInPair_
        std::atomic_thread_fence(std::memory_order_seq_cst);
        bool slot = slotInPair_[pair ? 1 : 0].load(std::memory_order_acquire);

        outVal = data_[pair ? 1 : 0][slot ? 1 : 0];
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
