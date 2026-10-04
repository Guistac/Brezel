#pragma once

#include <atomic>
#include <cstddef>
#include <array>

namespace Brezel {

/**
 * @brief Lock-free, wait-free Single-Producer Single-Consumer (SPSC) Ring Buffer.
 * 
 * Perfect for transferring discrete events (e.g. Commands, E-Stops) across the RT/NRT boundary.
 * 
 * @tparam T The payload type (must be trivially copyable).
 * @tparam Capacity The fixed size of the buffer (must be a power of 2 for optimal performance, though any size works with modulo).
 */
template <typename T, std::size_t Capacity>
class RingBuffer {
public:
    static_assert(Capacity > 0, "Capacity must be greater than 0");

    RingBuffer() : head_(0), tail_(0) {}

    /**
     * @brief Push an element into the queue (Wait-free for Producer).
     * @return true if successful, false if the queue is full.
     */
    bool push(const T& value) {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t current_head = head_.load(std::memory_order_relaxed);
        
        // Next head index
        size_t next_head = (current_head + 1) % Capacity;

        if (next_head == current_tail) {
            // Buffer is full
            return false;
        }

        buffer_[current_head] = value;
        head_.store(next_head, std::memory_order_release);
        return true;
    }

    /**
     * @brief Pop an element from the queue (Wait-free for Consumer).
     * @return true if successful, false if the queue is empty.
     */
    bool pop(T& out_value) {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t current_head = head_.load(std::memory_order_acquire);

        if (current_tail == current_head) {
            // Buffer is empty
            return false;
        }

        out_value = buffer_[current_tail];
        tail_.store((current_tail + 1) % Capacity, std::memory_order_release);
        return true;
    }

    /**
     * @brief Clear the queue (Must only be called from Consumer or Producer exclusively).
     */
    void clear() {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }
    
    /**
     * @brief Get an estimate of the number of items in the queue.
     */
    size_t size() const {
        const size_t current_tail = tail_.load(std::memory_order_acquire);
        const size_t current_head = head_.load(std::memory_order_acquire);
        if (current_head >= current_tail) {
            return current_head - current_tail;
        }
        return Capacity - current_tail + current_head;
    }

    bool empty() const {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

private:
    std::array<T, Capacity> buffer_{};
    
    // alignas(64) prevents "false sharing" cache line invalidation between cores
    alignas(64) std::atomic<size_t> head_{0}; // Written by Producer, read by Consumer
    alignas(64) std::atomic<size_t> tail_{0}; // Written by Consumer, read by Producer
};

} // namespace Brezel
