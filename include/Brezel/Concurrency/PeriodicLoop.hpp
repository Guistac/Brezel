#pragma once

#include <chrono>
#include <functional>
#include <thread>
#include <atomic>
#include <string>
#include <string_view>

namespace Brezel {

/**
 * @brief High-precision periodic execution loop running on a dedicated background thread.
 * 
 * Implements drift correction by computing absolute wakeup times rather than using fixed sleep intervals,
 * ensuring strict long-term frequency stability.
 */
class PeriodicLoop {
public:
    /**
     * @brief Construct a new Periodic Loop
     * @param threadName Name attached to the spawned background thread (useful for debugging/profiling).
     */
    explicit PeriodicLoop(std::string_view threadName = "PeriodicLoop")
        : m_threadName(threadName), m_running(false) 
    {
    }

    ~PeriodicLoop() {
        stop();
    }

    PeriodicLoop(const PeriodicLoop&) = delete;
    PeriodicLoop& operator=(const PeriodicLoop&) = delete;

    /**
     * @brief Start the periodic loop in a background thread.
     * 
     * @param targetFrequencyHz Target frequency in Hertz.
     * @param callback Function to execute every period. Passed the actual delta time (dt) in seconds.
     */
    void start(double targetFrequencyHz, std::function<void(double)> callback) {
        if (m_running.exchange(true)) return; // Already running

        m_thread = std::thread([this, targetFrequencyHz, callback]() {
            // macOS/Linux thread naming could be added here if OS-specific headers were included
            
            using namespace std::chrono;
            
            const double targetPeriodSec = 1.0 / targetFrequencyHz;
            const auto targetPeriod = duration<double>(targetPeriodSec);
            
            auto nextWakeupTime = steady_clock::now() + targetPeriod;
            auto lastTickTime = steady_clock::now();

            while (m_running.load(std::memory_order_relaxed)) {
                auto now = steady_clock::now();
                double dt = duration<double>(now - lastTickTime).count();
                lastTickTime = now;

                if (callback) {
                    callback(dt);
                }

                now = steady_clock::now();
                if (nextWakeupTime > now) {
                    std::this_thread::sleep_until(nextWakeupTime);
                }

                nextWakeupTime += targetPeriod;

                // Anti-windup: if we fell heavily behind (e.g., breakpoint or heavy load),
                // snap the wakeup time back to the present to prevent a rapid burst of execution.
                if (steady_clock::now() > nextWakeupTime + targetPeriod) {
                    nextWakeupTime = steady_clock::now() + targetPeriod;
                }
            }
        });
    }

    /**
     * @brief Stop the periodic loop and wait for the thread to exit.
     */
    void stop() {
        if (m_running.exchange(false)) {
            if (m_thread.joinable()) {
                m_thread.join();
            }
        }
    }

    /**
     * @brief Check if the loop is currently running.
     */
    bool isRunning() const {
        return m_running.load();
    }

private:
    std::string m_threadName;
    std::atomic<bool> m_running;
    std::thread m_thread;
};

} // namespace Brezel
