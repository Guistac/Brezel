#pragma once

#include <vector>
#include <mutex>
#include <utility>

namespace Brezel {

/**
 * @brief Multiple-Producer, Single-Consumer (MPSC) queue for thread boundary crossing.
 * 
 * Uses a highly optimized double-buffering mutex approach. Producers acquire a brief lock to push,
 * and the consumer acquires the lock only to swap the entire vector in O(1) time, ensuring minimal
 * contention. Ideal for routing discrete NRT mutations (e.g., GUI -> Supervisor).
 */
template <typename T>
class MutationQueue {
public:
    MutationQueue() = default;
    ~MutationQueue() = default;

    /**
     * @brief Push a new mutation into the queue (Producer thread).
     */
    void push(const T& mutation) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pushBuffer.push_back(mutation);
    }

    /**
     * @brief Push a new mutation into the queue via move (Producer thread).
     */
    void push(T&& mutation) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pushBuffer.push_back(std::move(mutation));
    }

    /**
     * @brief Drain the queue (Consumer thread). 
     * Instantly swaps the internal buffer with a fresh one and returns all pending mutations.
     */
    std::vector<T> drain() {
        std::vector<T> result;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            std::swap(m_pushBuffer, result);
        }
        return result;
    }

private:
    std::mutex m_mutex;
    std::vector<T> m_pushBuffer;
};

} // namespace Brezel
