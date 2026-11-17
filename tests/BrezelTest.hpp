// SPDX-License-Identifier: MIT
// Brezel tests. Copyright (c) 2026 Leo Becker. See LICENSE.md.
//
// The test framework of Brezel's tests: no dependency, one header. A test is a function registered with BREZEL_TEST,
// which names the header it pins down and the behaviour, in plain words. main() runs every registered test (or those
// whose name contains the first argument), prints one line per test and returns non-zero when any check failed.
//
//   BREZEL_TEST(project_clear, "Core/Project.hpp", "clear() empties the registry and the UUID index") {
//       CHECK(...);            // records a failure and goes on
//       REQUIRE(...);          // records a failure and ends this test
//   }
//
// Threads: every thread a test starts runs under a Watchdog (a deadline for the whole test; on expiry the process
// prints the test and exits with code 124, so a hang is a failure, never a stuck ctest) and is joined on every path by
// a ScopedThread. CHECK may be called from any thread.
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace BrezelTest {

struct Case {
    const char* name;
    const char* header;
    const char* behaviour;
    void (*fn)();
};

inline std::vector<Case>& cases() {
    static std::vector<Case> all;
    return all;
}

struct Registrar {
    Registrar(const char* name, const char* header, const char* behaviour, void (*fn)()) {
        cases().push_back(Case{name, header, behaviour, fn});
    }
};

/// Failures of the running test (any thread)
inline std::atomic<int> g_failures{0};
inline std::atomic<const char*> g_currentTest{""};

/// Thrown by REQUIRE to end the running test
struct Abort {};

inline void fail(const char* file, int line, const std::string& what) {
    g_failures.fetch_add(1, std::memory_order_relaxed);
    std::fprintf(stderr, "    [FAILED CHECK] %s:%d: %s\n", file, line, what.c_str());
    std::fflush(stderr);
}

template <typename A, typename B>
std::string describe(const char* expr, const A& a, const B& b) {
    std::ostringstream out;
    out << expr << "  (left: ";
    if constexpr (requires(std::ostream& o, const A& v) { o << v; }) out << a; else out << "?";
    out << ", right: ";
    if constexpr (requires(std::ostream& o, const B& v) { o << v; }) out << b; else out << "?";
    out << ")";
    return out.str();
}

/**
 * @brief A deadline for one test. Started on construction; on expiry it prints the test's name and the watchdog's
 * label and ends the process with code 124 (std::_Exit: no destructor runs, so a deadlocked thread cannot hang the
 * exit). Destroyed in time, it stops and joins its own thread. Thread: the test's thread.
 */
class Watchdog {
public:
    Watchdog(const char* label, std::chrono::milliseconds deadline) : m_label(label) {
        m_thread = std::thread([this, deadline] {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (!m_cv.wait_for(lock, deadline, [this] { return m_done; })) {
                std::fprintf(stderr, "[WATCHDOG] test '%s' (%s) did not finish within %lld ms: exiting\n",
                             g_currentTest.load(), m_label, static_cast<long long>(deadline.count()));
                std::fflush(stderr);
                std::_Exit(124);
            }
        });
    }
    ~Watchdog() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_done = true;
        }
        m_cv.notify_all();
        if (m_thread.joinable()) m_thread.join();
    }
    Watchdog(const Watchdog&) = delete;
    Watchdog& operator=(const Watchdog&) = delete;

private:
    const char* m_label;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_done = false;
    std::thread m_thread;
};

/**
 * @brief A thread that is joined on every path: the destructor sets the stop flag (the body polls it) and joins;
 * waitForEnd() joins without setting it.
 * Thread: the test's thread owns it.
 */
class ScopedThread {
public:
    explicit ScopedThread(std::function<void(const std::atomic<bool>& stop)> body) {
        m_thread = std::thread([this, body = std::move(body)] {
            try {
                body(m_stop);
            } catch (const Abort&) {
            } catch (const std::exception& e) {
                fail(__FILE__, __LINE__, std::string("exception in a test thread: ") + e.what());
            }
        });
    }
    ~ScopedThread() { join(); }
    void requestStop() noexcept { m_stop.store(true, std::memory_order_release); }
    /// Asks the body to stop, then joins
    void join() {
        requestStop();
        if (m_thread.joinable()) m_thread.join();
    }
    /// Joins without asking the body to stop: waits for it to end on its own (the Watchdog bounds the wait)
    void waitForEnd() {
        if (m_thread.joinable()) m_thread.join();
    }
    ScopedThread(const ScopedThread&) = delete;
    ScopedThread& operator=(const ScopedThread&) = delete;

private:
    std::atomic<bool> m_stop{false};
    std::thread m_thread;
};

inline int runAll(int argc, char** argv, const char* suite) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int failedTests = 0;
    int ran = 0;
    std::printf("== Brezel tests: %s (C++ %ld) ==\n", suite, static_cast<long>(__cplusplus));
    for (const Case& c : cases()) {
        if (filter && !std::strstr(c.name, filter)) continue;
        ++ran;
        g_currentTest.store(c.name);
        g_failures.store(0);
        std::printf("[RUN] %s  --  %s: %s\n", c.name, c.header, c.behaviour);
        std::fflush(stdout);
        try {
            c.fn();
        } catch (const Abort&) {
        } catch (const std::exception& e) {
            fail(__FILE__, __LINE__, std::string("unexpected exception: ") + e.what());
        }
        if (g_failures.load() != 0) {
            ++failedTests;
            std::printf("[FAILED] %s (%d failed checks)\n", c.name, g_failures.load());
        } else {
            std::printf("[PASSED] %s\n", c.name);
        }
        std::fflush(stdout);
    }
    std::printf("== %s: %d tests, %d failed ==\n", suite, ran, failedTests);
    if (ran == 0) {
        std::printf("no test matched the filter\n");
        return 2;
    }
    return failedTests == 0 ? 0 : 1;
}

} // namespace BrezelTest

#define BREZEL_TEST_CONCAT_(a, b) a##b
#define BREZEL_TEST_CONCAT(a, b) BREZEL_TEST_CONCAT_(a, b)

/// Declares and registers a test: BREZEL_TEST(name, "Dir/Header.hpp", "the behaviour pinned down") { body }
#define BREZEL_TEST(name, header, behaviour)                                                                     \
    static void name();                                                                                         \
    static const BrezelTest::Registrar BREZEL_TEST_CONCAT(name, _registrar)(#name, header, behaviour, &name);   \
    static void name()

#define CHECK(cond)                                                                                              \
    do {                                                                                                         \
        if (!(cond)) BrezelTest::fail(__FILE__, __LINE__, #cond);                                                \
    } while (0)

#define CHECK_EQ(a, b)                                                                                           \
    do {                                                                                                         \
        const auto& brezel_test_a_ = (a);                                                                        \
        const auto& brezel_test_b_ = (b);                                                                        \
        if (!(brezel_test_a_ == brezel_test_b_))                                                                 \
            BrezelTest::fail(__FILE__, __LINE__, BrezelTest::describe(#a " == " #b, brezel_test_a_, brezel_test_b_)); \
    } while (0)

#define CHECK_NEAR(a, b, tolerance)                                                                              \
    do {                                                                                                         \
        const double brezel_test_a_ = static_cast<double>(a);                                                    \
        const double brezel_test_b_ = static_cast<double>(b);                                                    \
        const double brezel_test_d_ = brezel_test_a_ > brezel_test_b_ ? brezel_test_a_ - brezel_test_b_          \
                                                                      : brezel_test_b_ - brezel_test_a_;         \
        if (!(brezel_test_d_ <= (tolerance)))                                                                    \
            BrezelTest::fail(__FILE__, __LINE__, BrezelTest::describe(#a " ~= " #b, brezel_test_a_, brezel_test_b_)); \
    } while (0)

#define REQUIRE(cond)                                                                                            \
    do {                                                                                                         \
        if (!(cond)) {                                                                                           \
            BrezelTest::fail(__FILE__, __LINE__, std::string("required: ") + #cond);                             \
            throw BrezelTest::Abort{};                                                                           \
        }                                                                                                        \
    } while (0)

/// The main() of a test binary
#define BREZEL_TEST_MAIN(suite)                                                                                  \
    int main(int argc, char** argv) { return BrezelTest::runAll(argc, argv, suite); }
