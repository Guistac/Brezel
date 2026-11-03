#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace Brezel {

/**
 * @brief Pre- and post-trigger capture of a periodic signal, written by one real-time thread and read by one
 * controller thread (an oscilloscope's single-shot mode in software).
 *
 * The writer calls record() once per cycle. While armed, every sample goes into a fixed ring of Capacity samples. A
 * trigger (a flag on the sample, or a request from the controller) starts the post-trigger count; after that many more
 * samples the capture freezes: the ring then holds the samples before the trigger, the trigger sample and the samples
 * after it, and the writer stops writing until the controller arms again.
 *
 * Threads:
 * - writer (one thread, typically the RT cycle): record(). Wait-free, no allocation, no lock, no system call. It is the
 *   only thread that touches the ring and the indices.
 * - controller (one thread, typically the GUI or main loop): arm(), disarm(), requestTrigger(), and forEachFrozen().
 *   arm(), disarm() and requestTrigger() are requests: the writer applies them at its next record(). forEachFrozen()
 *   reads the ring only while the state is FROZEN, a state the writer leaves only on a request from the controller, so
 *   the read never races a write.
 * - any thread: state(), freezeCount().
 *
 * T must be trivially copyable. The storage is inline (Capacity samples): allocate the owner once, outside the cycle.
 */
template <typename T, std::size_t Capacity>
class TriggeredCapture {
    static_assert(std::is_trivially_copyable_v<T>, "TriggeredCapture samples must be trivially copyable");
    static_assert(Capacity >= 2, "TriggeredCapture needs room for a trigger sample and one more");

public:
    enum class State : uint8_t {
        IDLE,       ///< not recording (initial, or after disarm())
        ARMED,      ///< recording, waiting for a trigger
        TRIGGERED,  ///< recording the post-trigger samples
        FROZEN      ///< complete; readable with forEachFrozen() until the next arm()
    };

    /// What record() did with this sample's trigger
    enum class TriggerResult : uint8_t {
        NONE,       ///< no trigger accepted in this call
        BY_SAMPLE,  ///< the sample's trigger flag started the post-trigger count
        BY_REQUEST  ///< a requestTrigger() started it
    };

    static constexpr std::size_t capacity() noexcept { return Capacity; }

    // =========================================================================
    // Writer thread
    // =========================================================================

    /**
     * @brief Writer thread, once per cycle. Applies pending requests, then stores the sample if armed or triggered.
     * A trigger is accepted only while ARMED; the sample that carries it is the trigger sample.
     */
    TriggerResult record(const T& sample, bool trigger) noexcept {
        applyRequests();
        const State s = state_.load(std::memory_order_relaxed);  // the writer is the only thread that stores it
        if (s != State::ARMED && s != State::TRIGGERED) return TriggerResult::NONE;

        ring_[head_] = sample;
        head_ = (head_ + 1) % Capacity;
        if (count_ < Capacity) ++count_;

        TriggerResult result = TriggerResult::NONE;
        if (s == State::ARMED) {
            const uint32_t requested = triggerRequest_.load(std::memory_order_acquire);
            if (trigger || requested != seenTriggerRequest_) {
                result = trigger ? TriggerResult::BY_SAMPLE : TriggerResult::BY_REQUEST;
                seenTriggerRequest_ = requested;
                afterTrigger_ = 0;
                if (postTrigger_ == 0) {
                    freeze();
                } else {
                    state_.store(State::TRIGGERED, std::memory_order_release);
                }
            }
        } else if (++afterTrigger_ >= postTrigger_) {
            freeze();
        }
        return result;
    }

    // =========================================================================
    // Controller thread
    // =========================================================================

    /**
     * @brief Controller thread. Clears the capture and arms it; postTriggerSamples (clamped to Capacity - 1) samples
     * follow the trigger sample. Applied by the writer at its next record(). Do not call while forEachFrozen() runs.
     */
    void arm(std::size_t postTriggerSamples) noexcept {
        requestedPostTrigger_.store(static_cast<uint32_t>(postTriggerSamples < Capacity ? postTriggerSamples : Capacity - 1),
                                    std::memory_order_relaxed);
        armRequest_.fetch_add(1, std::memory_order_release);
    }

    /// Controller thread. Stops recording; the content is discarded. Applied by the writer at its next record().
    void disarm() noexcept { disarmRequest_.fetch_add(1, std::memory_order_release); }

    /// Controller thread (any thread is safe). Triggers on the next recorded sample if armed; ignored otherwise.
    void requestTrigger() noexcept { triggerRequest_.fetch_add(1, std::memory_order_release); }

    /**
     * @brief Controller thread. While FROZEN, calls f(sample, index) for every captured sample from the oldest to the
     * newest and returns true; triggerIndex (if not null) receives the index of the trigger sample. Returns false and
     * calls nothing when the capture is not frozen.
     */
    template <typename F>
    bool forEachFrozen(F&& f, std::size_t* triggerIndex = nullptr) const {
        if (state_.load(std::memory_order_acquire) != State::FROZEN) return false;
        const std::size_t start = (head_ + Capacity - count_) % Capacity;
        for (std::size_t i = 0; i < count_; ++i) f(ring_[(start + i) % Capacity], i);
        if (triggerIndex) *triggerIndex = count_ - 1 - afterTrigger_;
        return true;
    }

    // =========================================================================
    // Any thread
    // =========================================================================

    State state() const noexcept { return state_.load(std::memory_order_acquire); }

    /// Number of captures frozen since construction; a new value means a new capture to read
    uint32_t freezeCount() const noexcept { return freezeCount_.load(std::memory_order_acquire); }

private:
    // Writer thread
    void applyRequests() noexcept {
        const uint32_t disarm = disarmRequest_.load(std::memory_order_acquire);
        if (disarm != seenDisarmRequest_) {
            seenDisarmRequest_ = disarm;
            state_.store(State::IDLE, std::memory_order_release);
        }
        const uint32_t arm = armRequest_.load(std::memory_order_acquire);
        if (arm != seenArmRequest_) {
            seenArmRequest_ = arm;
            head_ = 0;
            count_ = 0;
            afterTrigger_ = 0;
            postTrigger_ = requestedPostTrigger_.load(std::memory_order_relaxed);
            // A trigger requested before this arm does not fire it
            seenTriggerRequest_ = triggerRequest_.load(std::memory_order_acquire);
            state_.store(State::ARMED, std::memory_order_release);
        }
    }

    // Writer thread: the ring and indices are complete before the release store publishes FROZEN
    void freeze() noexcept {
        state_.store(State::FROZEN, std::memory_order_release);
        freezeCount_.fetch_add(1, std::memory_order_release);
    }

    std::array<T, Capacity> ring_{};
    // Writer-owned (read by the controller only while FROZEN)
    std::size_t head_{0};
    std::size_t count_{0};
    std::size_t afterTrigger_{0};
    std::size_t postTrigger_{0};
    uint32_t seenArmRequest_{0};
    uint32_t seenDisarmRequest_{0};
    uint32_t seenTriggerRequest_{0};

    std::atomic<State> state_{State::IDLE};
    std::atomic<uint32_t> freezeCount_{0};
    std::atomic<uint32_t> armRequest_{0};
    std::atomic<uint32_t> disarmRequest_{0};
    std::atomic<uint32_t> triggerRequest_{0};
    std::atomic<uint32_t> requestedPostTrigger_{0};
};

} // namespace Brezel
