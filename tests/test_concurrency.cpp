// SPDX-License-Identifier: MIT
// Brezel tests. Copyright (c) 2026 Leo Becker. See LICENSE.md.
//
// Concurrency: TripleBuffer, RingBuffer, TriggeredCapture, PeriodicLoop, StallWatchdog and ThreadUtils. Each
// primitive is tested on one thread for its rules, then with one writer thread and one reader thread; every thread runs
// under a BrezelTest::Watchdog and is joined on every path (ScopedThread).
// Headers: Concurrency/TripleBuffer.hpp, Concurrency/RingBuffer.hpp, Concurrency/TriggeredCapture.hpp,
// Concurrency/PeriodicLoop.hpp, Concurrency/StallWatchdog.hpp, Concurrency/ThreadUtils.hpp.

#include "BrezelTest.hpp"

#include "Brezel/Concurrency/PeriodicLoop.hpp"
#include "Brezel/Concurrency/RingBuffer.hpp"
#include "Brezel/Concurrency/StallWatchdog.hpp"
#include "Brezel/Concurrency/ThreadUtils.hpp"
#include "Brezel/Concurrency/TriggeredCapture.hpp"
#include "Brezel/Concurrency/TripleBuffer.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(__APPLE__) || defined(__linux__)
#include <pthread.h>
#endif

using namespace Brezel;
using BrezelTest::ScopedThread;
using BrezelTest::Watchdog;
using namespace std::chrono_literals;

namespace {

/// A sample whose words all follow from seq, so a torn copy is visible
struct Sample {
    uint64_t seq{0};
    uint64_t words[7]{};
    static Sample make(uint64_t seq) {
        Sample s;
        s.seq = seq;
        for (int i = 0; i < 7; ++i) s.words[i] = seq * 0x9E3779B97F4A7C15ULL + static_cast<uint64_t>(i);
        return s;
    }
    bool consistent() const {
        for (int i = 0; i < 7; ++i) {
            if (words[i] != seq * 0x9E3779B97F4A7C15ULL + static_cast<uint64_t>(i)) return false;
        }
        return true;
    }
};

template <typename Pred>
bool waitUntil(Pred pred, std::chrono::milliseconds limit) {
    const auto end = std::chrono::steady_clock::now() + limit;
    while (!pred()) {
        if (std::chrono::steady_clock::now() > end) return false;
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

std::string currentThreadName() {
    char name[64] = {};
#if defined(__APPLE__) || defined(__linux__)
    pthread_getname_np(pthread_self(), name, sizeof(name));
#endif
    return name;
}

template <typename T>
constexpr bool hasOverrunCount = requires(T& t) { t.overrunCount(); };

} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// TripleBuffer
// ---------------------------------------------------------------------------------------------------------------------

BREZEL_TEST(known_defect_BUG55_triple_buffer_read_before_write, "Concurrency/TripleBuffer.hpp",
            "read() returns true also before the first write, with a value-initialized value; hasNew() is the "
            "freshness test (BUG-55)") {
    TripleBuffer<Sample> tb;
    CHECK(!tb.hasNew());
    Sample out = Sample::make(99);
    CHECK(tb.read(out));
    CHECK_EQ(out.seq, uint64_t(0));
    CHECK_EQ(out.words[3], uint64_t(0));
}

BREZEL_TEST(triple_buffer_latest_value, "Concurrency/TripleBuffer.hpp",
            "write() makes hasNew() true and read() clears it; the latest of several writes wins; reading again "
            "returns the same value; readLatest() peeks without clearing the flag") {
    TripleBuffer<Sample> tb;
    tb.write(Sample::make(1));
    CHECK(tb.hasNew());
    Sample out;
    CHECK(tb.read(out));
    CHECK_EQ(out.seq, uint64_t(1));
    CHECK(!tb.hasNew());
    CHECK(tb.read(out));
    CHECK_EQ(out.seq, uint64_t(1));
    for (uint64_t i = 2; i <= 10; ++i) tb.write(Sample::make(i));
    Sample peek;
    CHECK(tb.readLatest(peek));
    CHECK_EQ(peek.seq, uint64_t(10));
    CHECK(tb.hasNew());
    CHECK(tb.read(out));
    CHECK_EQ(out.seq, uint64_t(10));
    CHECK(out.consistent());
}

BREZEL_TEST(triple_buffer_two_threads_no_tear, "Concurrency/TripleBuffer.hpp",
            "one writer thread writing back to back and one reader thread reading in a loop: every value read is "
            "whole (never mixed from two writes) and the sequence read never goes back") {
    TripleBuffer<Sample> tb;
    constexpr uint64_t kWrites = 400000;
    std::atomic<bool> writerDone{false};
    uint64_t reads = 0;
    uint64_t torn = 0;
    uint64_t backwards = 0;
    uint64_t lastSeq = 0;
    Watchdog watchdog("triple buffer threads", 60s);
    tb.write(Sample::make(0));  // a whole sample before the reader starts: before any write read() copies an all-zero
                                // sample (BUG-55), which consistent() would count as torn (review R-A F-1)
    {
        ScopedThread writer([&](const std::atomic<bool>& stop) {
            for (uint64_t i = 1; i <= kWrites && !stop.load(std::memory_order_relaxed); ++i) tb.write(Sample::make(i));
            writerDone.store(true, std::memory_order_release);
        });
        ScopedThread reader([&](const std::atomic<bool>& stop) {
            Sample s;
            while (!stop.load(std::memory_order_relaxed)) {
                const bool last = writerDone.load(std::memory_order_acquire);
                tb.read(s);
                ++reads;
                if (!s.consistent()) ++torn;
                if (s.seq < lastSeq) ++backwards;
                lastSeq = s.seq;
                if (last) break;
            }
        });
        reader.waitForEnd();
        writer.waitForEnd();
    }
    CHECK_EQ(torn, uint64_t(0));
    CHECK_EQ(backwards, uint64_t(0));
    CHECK_EQ(lastSeq, kWrites);
    CHECK(reads > 0);
    std::printf("    %llu reads, %llu torn\n", static_cast<unsigned long long>(reads),
                static_cast<unsigned long long>(torn));
}

// ---------------------------------------------------------------------------------------------------------------------
// RingBuffer
// ---------------------------------------------------------------------------------------------------------------------

BREZEL_TEST(ring_buffer_fifo_and_capacity, "Concurrency/RingBuffer.hpp",
            "a RingBuffer of capacity N holds N-1 items; push fails when full and pop when empty; items come out in "
            "push order across many wraps; size() and empty() follow; clear() empties it") {
    RingBuffer<int, 4> ring;
    CHECK(ring.empty());
    CHECK_EQ(ring.size(), size_t(0));
    CHECK(ring.push(1));
    CHECK(ring.push(2));
    CHECK(ring.push(3));
    CHECK(!ring.push(4));  // capacity 4 holds 3
    CHECK_EQ(ring.size(), size_t(3));
    int out = 0;
    CHECK(ring.pop(out));
    CHECK_EQ(out, 1);
    int next = 4;
    int expected = 2;
    for (int round = 0; round < 50; ++round) {
        CHECK(ring.push(next++));
        CHECK(ring.pop(out));
        CHECK_EQ(out, expected++);
    }
    CHECK_EQ(ring.size(), size_t(2));
    ring.clear();
    CHECK(ring.empty());
    CHECK(!ring.pop(out));
}

BREZEL_TEST(ring_buffer_two_threads_fifo, "Concurrency/RingBuffer.hpp",
            "one producer thread and one consumer thread: 200000 items arrive complete and in order through a ring "
            "of 64") {
    RingBuffer<uint64_t, 64> ring;
    constexpr uint64_t kItems = 200000;
    uint64_t received = 0;
    bool ordered = true;
    Watchdog watchdog("ring buffer threads", 60s);
    {
        ScopedThread producer([&](const std::atomic<bool>& stop) {
            for (uint64_t i = 0; i < kItems && !stop.load(std::memory_order_relaxed);) {
                if (ring.push(i)) ++i;
                else std::this_thread::yield();
            }
        });
        ScopedThread consumer([&](const std::atomic<bool>& stop) {
            uint64_t v = 0;
            while (received < kItems && !stop.load(std::memory_order_relaxed)) {
                if (ring.pop(v)) {
                    if (v != received) ordered = false;
                    ++received;
                } else {
                    std::this_thread::yield();
                }
            }
        });
        consumer.waitForEnd();
        producer.waitForEnd();
    }
    CHECK(ordered);
    CHECK_EQ(received, kItems);
}

// ---------------------------------------------------------------------------------------------------------------------
// TriggeredCapture
// ---------------------------------------------------------------------------------------------------------------------

BREZEL_TEST(triggered_capture_states, "Concurrency/TriggeredCapture.hpp",
            "IDLE records nothing; arm() takes effect at the next record(); a sample's trigger flag or a request "
            "starts the post-trigger count; after it the capture is FROZEN with the samples before, the trigger "
            "sample and the samples after, oldest first; FROZEN records nothing; a request made before arm() does not "
            "fire it; the post-trigger count is clamped to Capacity - 1; disarm() discards") {
    using Capture = TriggeredCapture<int, 8>;
    Capture c;
    CHECK(c.state() == Capture::State::IDLE);
    CHECK(c.record(1, true) == Capture::TriggerResult::NONE);
    CHECK(!c.forEachFrozen([](int, size_t) {}));

    c.arm(2);
    CHECK(c.state() == Capture::State::IDLE);  // a request until the writer's next record
    for (int i = 1; i <= 20; ++i) CHECK(c.record(i, false) == Capture::TriggerResult::NONE);
    CHECK(c.state() == Capture::State::ARMED);
    CHECK(c.record(21, true) == Capture::TriggerResult::BY_SAMPLE);
    CHECK(c.state() == Capture::State::TRIGGERED);
    CHECK(c.record(22, true) == Capture::TriggerResult::NONE);  // a second trigger is ignored
    CHECK(c.record(23, false) == Capture::TriggerResult::NONE);
    CHECK(c.state() == Capture::State::FROZEN);
    CHECK_EQ(c.freezeCount(), uint32_t(1));
    CHECK(c.record(24, true) == Capture::TriggerResult::NONE);
    std::vector<int> frozen;
    size_t triggerIndex = 99;
    CHECK(c.forEachFrozen([&](int v, size_t i) {
        CHECK_EQ(i, frozen.size());
        frozen.push_back(v);
    }, &triggerIndex));
    CHECK((frozen == std::vector<int>{16, 17, 18, 19, 20, 21, 22, 23}));
    CHECK_EQ(triggerIndex, size_t(5));

    c.requestTrigger();  // before arm: must not fire the next capture
    c.arm(0);
    CHECK(c.record(1, false) == Capture::TriggerResult::NONE);
    CHECK(c.state() == Capture::State::ARMED);
    c.requestTrigger();
    CHECK(c.record(2, false) == Capture::TriggerResult::BY_REQUEST);
    CHECK(c.state() == Capture::State::FROZEN);  // post-trigger 0: frozen at the trigger sample
    frozen.clear();
    CHECK(c.forEachFrozen([&](int v, size_t) { frozen.push_back(v); }, &triggerIndex));
    CHECK((frozen == std::vector<int>{1, 2}));
    CHECK_EQ(triggerIndex, size_t(1));
    CHECK_EQ(c.freezeCount(), uint32_t(2));

    c.arm(100);  // clamped to 7
    c.record(0, true);
    int after = 0;
    while (c.state() != Capture::State::FROZEN && after < 50) c.record(++after, false);
    CHECK_EQ(after, 7);

    c.arm(3);
    c.record(1, false);
    c.disarm();
    c.record(2, false);
    CHECK(c.state() == Capture::State::IDLE);
    CHECK(!c.forEachFrozen([](int, size_t) {}));
    static_assert(Capture::capacity() == 8);
}

BREZEL_TEST(triggered_capture_two_threads, "Concurrency/TriggeredCapture.hpp",
            "a writer thread records a counter at full speed while a controller thread arms, requests a trigger and "
            "reads each frozen capture: 50 captures, each of consecutive samples (16 once the ring is full) with the "
            "trigger sample followed by the 4 post-trigger samples") {
    using Capture = TriggeredCapture<uint64_t, 16>;
    Capture c;
    Watchdog watchdog("capture threads", 60s);
    int good = 0;
    {
        ScopedThread writer([&](const std::atomic<bool>& stop) {
            uint64_t counter = 0;
            while (!stop.load(std::memory_order_relaxed)) {
                c.record(++counter, false);
                if ((counter & 0xFF) == 0) std::this_thread::yield();
            }
        });
        for (int capture = 0; capture < 50; ++capture) {
            const uint32_t before = c.freezeCount();
            c.arm(4);
            REQUIRE(waitUntil([&] { return c.state() == Capture::State::ARMED; }, 5000ms));
            std::this_thread::sleep_for(2ms);  // usually fills the ring; a shorter capture is checked the same way
            c.requestTrigger();
            REQUIRE(waitUntil([&] { return c.freezeCount() != before; }, 5000ms));
            std::vector<uint64_t> samples;
            size_t triggerIndex = 0;
            CHECK(c.forEachFrozen([&](uint64_t v, size_t) { samples.push_back(v); }, &triggerIndex));
            bool consecutive = samples.size() >= 5 && samples.size() <= 16;
            for (size_t i = 1; i < samples.size(); ++i) consecutive = consecutive && samples[i] == samples[i - 1] + 1;
            if (consecutive && triggerIndex == samples.size() - 5) ++good;
        }
    }
    CHECK_EQ(good, 50);
}

// ---------------------------------------------------------------------------------------------------------------------
// PeriodicLoop
// ---------------------------------------------------------------------------------------------------------------------

BREZEL_TEST(periodic_loop_rate_and_stop, "Concurrency/PeriodicLoop.hpp",
            "start() runs the callback on a named thread at the rate asked, passing the measured dt: with a callback "
            "that takes 3 ms of a 5 ms period the mean dt stays within 20 percent of 5 ms (absolute wake-ups, not a "
            "sleep after the callback); a second start() is ignored; stop() joins and can be called twice") {
    Watchdog watchdog("periodic loop", 20s);
    std::atomic<int> ticks{0};
    std::atomic<int> otherTicks{0};
    std::atomic<bool> dtPositive{true};
    double dtSum = 0.0;  // the loop's thread only; read after stop() has joined it
    int dtCount = 0;
    std::string threadName;
    std::mutex nameMutex;
    PeriodicLoop loop("BrezelLoopTest");
    CHECK(!loop.isRunning());
    loop.start(200.0, [&](double dt) {
        if (ticks.fetch_add(1) == 0) {
            std::lock_guard<std::mutex> lock(nameMutex);
            threadName = currentThreadName();
        } else {
            if (!(dt > 0.0)) dtPositive = false;
            dtSum += dt;
            ++dtCount;
        }
        std::this_thread::sleep_for(3ms);  // 60 percent of the period: a relative sleep would make the period 8 ms
    });
    loop.start(1000.0, [&](double) { otherTicks.fetch_add(1); });
    CHECK(loop.isRunning());
    std::this_thread::sleep_for(300ms);
    loop.stop();
    CHECK(!loop.isRunning());
    const int afterStop = ticks.load();
    std::this_thread::sleep_for(30ms);
    loop.stop();
    CHECK_EQ(ticks.load(), afterStop);
    CHECK(afterStop >= 20);  // 60 expected; wide margins for a loaded machine
    CHECK(afterStop <= 90);
    CHECK_EQ(otherTicks.load(), 0);
    CHECK(dtPositive.load());
    REQUIRE(dtCount > 0);
    const double meanDt = dtSum / dtCount;
    std::printf("    mean dt %.3f ms over %d ticks\n", meanDt * 1e3, dtCount);
    CHECK(meanDt > 0.004);
    CHECK(meanDt < 0.006);
#if defined(__APPLE__) || defined(__linux__)
    std::lock_guard<std::mutex> lock(nameMutex);
    CHECK_EQ(threadName, std::string("BrezelLoopTest"));  // 14 characters: within Linux's 15
#endif
}

BREZEL_TEST(known_defect_B09_periodic_loop_drops_periods, "Concurrency/PeriodicLoop.hpp",
            "after a callback that overruns by many periods the loop snaps its next wake-up to now: the missed "
            "periods are dropped, not caught up in a burst, and no count of them exists (09_BREZEL.md B-09)") {
    static_assert(!hasOverrunCount<PeriodicLoop>);
    Watchdog watchdog("periodic loop overrun", 20s);
    std::atomic<int> ticks{0};
    PeriodicLoop loop("BrezelOverrun");
    const auto start = std::chrono::steady_clock::now();
    loop.start(100.0, [&](double) {
        if (ticks.fetch_add(1) == 0) std::this_thread::sleep_for(150ms);  // 15 periods late
    });
    std::this_thread::sleep_until(start + 250ms);
    const int ticksIn250ms = ticks.load();
    loop.stop();
    // 25 periods elapsed; with a catch-up about 25 ticks, with the snap about 1 + 10
    CHECK(ticksIn250ms <= 18);
    CHECK(ticksIn250ms >= 2);
    std::printf("    %d ticks in 250 ms after a 150 ms overrun at 100 Hz\n", ticksIn250ms);
}

// ---------------------------------------------------------------------------------------------------------------------
// StallWatchdog
// ---------------------------------------------------------------------------------------------------------------------

BREZEL_TEST(stall_watchdog_by_hand, "Concurrency/StallWatchdog.hpp",
            "check(now) reports an entry whose last beat is older than its timeout as stalled once, and once as "
            "recovered after the next beat; watch() counts as a beat; unwatch() stops the checks; 16 entries at most; "
            "ids out of range are ignored") {
    struct Event {
        std::string name;
        bool stalled;
    };
    std::vector<Event> events;
    StallWatchdog wd(10ms);
    wd.setListener([&](std::string_view name, uint64_t, bool stalled) { events.push_back({std::string(name), stalled}); });
    const int id = wd.watch("worker", 50ms);
    REQUIRE(id >= 0);
    wd.check(StallWatchdog::nowNs());
    CHECK(events.empty());
    CHECK(!wd.isStalled(id));
    const uint64_t late = StallWatchdog::nowNs() + 100'000'000ULL;
    wd.check(late);
    wd.check(late + 1);
    REQUIRE(events.size() == 1);
    CHECK_EQ(events[0].name, std::string("worker"));
    CHECK(events[0].stalled);
    CHECK(wd.isStalled(id));
    CHECK(wd.anyStalled());
    StallWatchdog::Status s = wd.status(id);
    CHECK(s.watched);
    CHECK(s.stalled);
    CHECK_EQ(s.stallCount, uint32_t(1));
    CHECK_EQ(s.timeoutNs, uint64_t(50'000'000ULL));
    CHECK_EQ(std::string(s.name), std::string("worker"));
    wd.beat(id);
    wd.check(StallWatchdog::nowNs());
    REQUIRE(events.size() == 2);
    CHECK(!events[1].stalled);
    CHECK(!wd.anyStalled());
    wd.unwatch(id);
    wd.check(StallWatchdog::nowNs() + 1'000'000'000ULL);
    CHECK_EQ(events.size(), size_t(2));
    CHECK(!wd.status(id).watched);

    std::vector<int> ids;
    for (int i = 0; i < StallWatchdog::MAX_WATCHED; ++i) ids.push_back(wd.watch("w", 1s));
    CHECK(ids.back() >= 0);
    CHECK_EQ(wd.watch("one too many", 1s), -1);
    wd.beat(-1);
    wd.beat(StallWatchdog::MAX_WATCHED);
    wd.unwatch(-1);
    CHECK(!wd.isStalled(99));
    CHECK(!wd.status(99).watched);
}

BREZEL_TEST(stall_watchdog_thread, "Concurrency/StallWatchdog.hpp",
            "with its own thread started, the watchdog reports a worker thread that stops beating as stalled within "
            "its timeout plus an interval, then as recovered when it beats again; stop() joins") {
    Watchdog watchdog("stall watchdog threads", 20s);
    std::atomic<int> stalls{0};
    std::atomic<int> recoveries{0};
    std::atomic<bool> pause{false};
    StallWatchdog wd(5ms);
    wd.setListener([&](std::string_view, uint64_t, bool stalled) { (stalled ? stalls : recoveries).fetch_add(1); });
    const int id = wd.watch("worker", 30ms);
    wd.start("BrezelStallTest");
    {
        ScopedThread worker([&](const std::atomic<bool>& stop) {
            while (!stop.load(std::memory_order_relaxed)) {
                if (!pause.load(std::memory_order_relaxed)) wd.beat(id);
                std::this_thread::sleep_for(1ms);
            }
        });
        std::this_thread::sleep_for(100ms);
        CHECK_EQ(stalls.load(), 0);
        pause = true;
        CHECK(waitUntil([&] { return stalls.load() == 1; }, 2000ms));
        pause = false;
        CHECK(waitUntil([&] { return recoveries.load() == 1; }, 2000ms));
    }
    wd.stop();
    CHECK_EQ(stalls.load(), 1);
    wd.stop();  // twice is safe
}

// ---------------------------------------------------------------------------------------------------------------------
// ThreadUtils
// ---------------------------------------------------------------------------------------------------------------------

BREZEL_TEST(thread_utils, "Concurrency/ThreadUtils.hpp",
            "setThreadName names the calling thread (cut to 15 characters on Linux, 63 on macOS); "
            "makeNonRealtimeThread returns true (a no-op on macOS)") {
    Watchdog watchdog("thread utils", 10s);
    std::string shortName;
    std::string longName;
    bool nonRealtime = false;
    {
        ScopedThread t([&](const std::atomic<bool>&) {
            setThreadName("brezel-test");
            shortName = currentThreadName();
            setThreadName("brezel-a-thread-name-longer-than-fifteen-characters");
            longName = currentThreadName();
            nonRealtime = makeNonRealtimeThread(-1);
        });
    }
    CHECK_EQ(shortName, std::string("brezel-test"));
#if defined(__linux__)
    CHECK_EQ(longName, std::string("brezel-a-thread"));
#elif defined(__APPLE__)
    CHECK_EQ(longName, std::string("brezel-a-thread-name-longer-than-fifteen-characters"));
#endif
    CHECK(nonRealtime);
}

BREZEL_TEST_MAIN("concurrency")
