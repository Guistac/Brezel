#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string_view>
#include <thread>

#include "ThreadUtils.hpp"

namespace Brezel {

/**
 * @brief Detects a stalled thread from outside it.
 *
 * A watched thread calls beat() at least once per timeout (wait-free: one atomic store). The watchdog's own thread
 * checks every interval; a watched entry whose last beat is older than its timeout is stalled, and the listener hears
 * the transition once (stalled, then recovered on the next beat). A thread that never beats after watch() is detected
 * too: watch() counts as the first beat. A thread that stops on purpose calls unwatch().
 *
 * The listener runs on the watchdog thread, without any watchdog lock held, and must not block (log, set a flag).
 * watch() and unwatch() take a mutex and may be called from any thread at any time; beat() never locks.
 */
class StallWatchdog {
public:
    static constexpr int MAX_WATCHED = 16;
    static constexpr int NAME_SIZE = 32;

    /// Watchdog thread: `stalled` true when an entry goes stalled, false when it beats again
    using Listener = std::function<void(std::string_view name, uint64_t ageNs, bool stalled)>;

    struct Status {
        bool watched = false;
        bool stalled = false;
        uint64_t ageNs = 0;      ///< Time since the last beat
        uint64_t timeoutNs = 0;
        uint32_t stallCount = 0; ///< Stalls since watch()
        char name[NAME_SIZE]{};
    };

    explicit StallWatchdog(std::chrono::nanoseconds checkInterval = std::chrono::milliseconds(100))
        : checkIntervalNs_(static_cast<uint64_t>(checkInterval.count())) {}

    ~StallWatchdog() { stop(); }

    StallWatchdog(const StallWatchdog&) = delete;
    StallWatchdog& operator=(const StallWatchdog&) = delete;

    static uint64_t nowNs() noexcept {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    /// Any thread: the first free entry, or -1 when all MAX_WATCHED are taken
    int watch(std::string_view name, std::chrono::nanoseconds timeout) {
        std::lock_guard<std::mutex> lock(registryMutex_);
        for (int id = 0; id < MAX_WATCHED; ++id) {
            Entry& e = entries_[id];
            if (e.watched.load(std::memory_order_relaxed)) continue;
            const size_t n = std::min(name.size(), static_cast<size_t>(NAME_SIZE - 1));
            std::copy_n(name.data(), n, e.name);
            e.name[n] = '\0';
            e.timeoutNs.store(static_cast<uint64_t>(timeout.count()), std::memory_order_relaxed);
            e.stallCount.store(0, std::memory_order_relaxed);
            e.stalled.store(false, std::memory_order_relaxed);
            e.lastBeatNs.store(nowNs(), std::memory_order_relaxed);
            e.watched.store(true, std::memory_order_release);
            return id;
        }
        return -1;
    }

    /// Any thread: the entry is no longer checked (its thread stopped on purpose)
    void unwatch(int id) {
        if (id < 0 || id >= MAX_WATCHED) return;
        std::lock_guard<std::mutex> lock(registryMutex_);
        entries_[id].watched.store(false, std::memory_order_release);
    }

    /// The watched thread: wait-free
    void beat(int id) noexcept {
        if (id < 0 || id >= MAX_WATCHED) return;
        entries_[id].lastBeatNs.store(nowNs(), std::memory_order_release);
    }

    /// Before start()
    void setListener(Listener listener) { listener_ = std::move(listener); }

    void start(std::string_view threadName = "Stall Watchdog") {
        std::lock_guard<std::mutex> lock(runMutex_);
        if (thread_.joinable()) return;
        stopRequested_ = false;
        thread_ = std::thread([this, name = std::string(threadName)] {
            setThreadName(name);
            std::unique_lock<std::mutex> runLock(runMutex_);
            while (!stopRequested_) {
                runLock.unlock();
                check(nowNs());
                runLock.lock();
                runCv_.wait_for(runLock, std::chrono::nanoseconds(checkIntervalNs_), [this] { return stopRequested_; });
            }
        });
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(runMutex_);
            stopRequested_ = true;
        }
        runCv_.notify_all();
        if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id()) thread_.join();
    }

    /// The watchdog thread (or a test driving it by hand): checks every entry once at `now`
    void check(uint64_t now) {
        for (int id = 0; id < MAX_WATCHED; ++id) {
            Entry& e = entries_[id];
            if (!e.watched.load(std::memory_order_acquire)) continue;
            const uint64_t last = e.lastBeatNs.load(std::memory_order_acquire);
            const uint64_t age = now > last ? now - last : 0;
            const bool late = age > e.timeoutNs.load(std::memory_order_relaxed);
            if (late == e.stalled.load(std::memory_order_relaxed)) continue;
            e.stalled.store(late, std::memory_order_release);
            if (late) e.stallCount.fetch_add(1, std::memory_order_relaxed);
            if (listener_) {
                char name[NAME_SIZE];
                {
                    std::lock_guard<std::mutex> lock(registryMutex_);  // watch() may be reusing the entry
                    std::copy_n(e.name, NAME_SIZE, name);
                }
                listener_(std::string_view(name), age, late);
            }
        }
    }

    /// Any thread
    bool isStalled(int id) const noexcept {
        if (id < 0 || id >= MAX_WATCHED) return false;
        return entries_[id].watched.load(std::memory_order_acquire) && entries_[id].stalled.load(std::memory_order_acquire);
    }

    /// Any thread: true when a watched entry is stalled
    bool anyStalled() const noexcept {
        for (int id = 0; id < MAX_WATCHED; ++id) {
            if (isStalled(id)) return true;
        }
        return false;
    }

    /// Any thread: a copy for display; the name may be stale while watch() reuses the entry
    Status status(int id) const {
        Status s{};
        if (id < 0 || id >= MAX_WATCHED) return s;
        std::lock_guard<std::mutex> lock(registryMutex_);
        const Entry& e = entries_[id];
        s.watched = e.watched.load(std::memory_order_acquire);
        s.stalled = e.stalled.load(std::memory_order_acquire);
        const uint64_t now = nowNs();
        const uint64_t last = e.lastBeatNs.load(std::memory_order_acquire);
        s.ageNs = now > last ? now - last : 0;
        s.timeoutNs = e.timeoutNs.load(std::memory_order_relaxed);
        s.stallCount = e.stallCount.load(std::memory_order_relaxed);
        std::copy_n(e.name, NAME_SIZE, s.name);
        return s;
    }

private:
    struct Entry {
        std::atomic<bool> watched{false};
        std::atomic<bool> stalled{false};
        std::atomic<uint64_t> lastBeatNs{0};
        std::atomic<uint64_t> timeoutNs{0};
        std::atomic<uint32_t> stallCount{0};
        char name[NAME_SIZE]{};  // Written by watch() under registryMutex_ before `watched` is released
    };

    std::array<Entry, MAX_WATCHED> entries_{};
    mutable std::mutex registryMutex_;
    Listener listener_;
    const uint64_t checkIntervalNs_;

    std::mutex runMutex_;
    std::condition_variable runCv_;
    bool stopRequested_ = false;
    std::thread thread_;
};

} // namespace Brezel
