#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <source_location>
#include <thread>

#if defined(__APPLE__) || defined(__linux__)
#include <pthread.h>
#endif
#if defined(__linux__)
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace Brezel {

class EcsWall;

namespace detail {
/// Per-thread record of the projects whose read()/write() scope this thread holds (ECS wall bookkeeping).
struct EcsWallHeldScope {
    const EcsWall* wall = nullptr;
    std::uint32_t depth = 0;
};
struct EcsWallThreadScopes {
    static constexpr std::uint32_t kMaxHeldProjects = 8;
    EcsWallHeldScope held[kMaxHeldProjects]{};
    std::uint32_t count = 0;
    std::uint32_t overflow = 0; // scopes beyond kMaxHeldProjects projects: treated as "holds a scope"
};
inline thread_local EcsWallThreadScopes t_ecsWallScopes{};
} // namespace detail

/// @brief The runtime ECS wall: counts and locates entity accesses made without a lock.
/// @details One wall per `Brezel::Project`. The rule it checks is "this thread holds a `read()` or `write()` scope of
/// the project, or it is the project's writer thread". `Entity` accessors call `check()` with the caller's
/// `std::source_location`. A violation increments an atomic total and is recorded in a fixed table of
/// `kMaxSites` sites (file, line, function, thread name, count, first time). Recording is lock-free and allocates
/// nothing: the violating thread only records. Reporting the sites (logging, telemetry, GUI) is left to the owner of
/// the writer thread, which polls `site()`.
///
/// Mode: in a build without `NDEBUG` a violation prints the site to stderr and aborts, unless the environment
/// variable `BREZEL_ECS_WALL=count` is set when the project is constructed; with `NDEBUG` it only counts.
/// Thread-agnostic: every member is safe from any thread.
class EcsWall {
public:
    static constexpr std::size_t kMaxSites = 32;
    static constexpr std::size_t kThreadNameChars = 32;

    enum class Mode : std::uint8_t { Assert, Count };

    /// @brief A copy of one recorded site.
    struct SiteInfo {
        const char* file = "";          ///< `std::source_location::file_name()` of the access (static storage)
        const char* function = "";      ///< `std::source_location::function_name()` of the access (static storage)
        std::uint32_t line = 0;
        char thread[kThreadNameChars]{}; ///< Name of the violating thread, or its id in hex if it has no name
        std::uint64_t firstTimeNs = 0;  ///< steady_clock time of the first violation at this site
        std::uint64_t count = 0;        ///< Violations at this site so far
    };

    EcsWall() noexcept : m_mode(modeFromEnvironment()) {}
    EcsWall(const EcsWall&) = delete;
    EcsWall& operator=(const EcsWall&) = delete;

    /// @brief Prints the recorded sites to stderr when the wall saw violations (teardown diagnostics).
    ~EcsWall() { printSummary(stderr); }

    /// @brief Sets the thread allowed to access entities without a scope (the single writer). An empty id clears it.
    void setWriterThread(std::thread::id id) noexcept { m_writer.store(id, std::memory_order_release); }
    std::thread::id writerThread() const noexcept { return m_writer.load(std::memory_order_acquire); }

    Mode mode() const noexcept { return m_mode; }

    /// @brief True if the calling thread is the writer thread or holds a read or write scope of this wall's project.
    bool callerMayAccess() const noexcept {
        if (m_writer.load(std::memory_order_relaxed) == std::this_thread::get_id()) return true;
        return heldScope(this) != nullptr || detail::t_ecsWallScopes.overflow > 0;
    }

    /// @brief Checks one entity access; records (and in Assert mode aborts on) a violation.
    void check(const std::source_location& location) noexcept {
        if (callerMayAccess()) return;
        recordViolation(location.file_name(), location.line(), location.function_name());
#ifndef NDEBUG
        if (m_mode == Mode::Assert) {
            char thread[kThreadNameChars];
            currentThreadName(thread, sizeof(thread));
            std::fprintf(stderr,
                         "Brezel ECS wall: entity access without a read()/write() scope off the writer thread at "
                         "%s:%u (%s), thread '%s'. Set BREZEL_ECS_WALL=count to count instead of aborting.\n",
                         location.file_name(), static_cast<unsigned>(location.line()), location.function_name(),
                         thread);
            std::fflush(stderr);
            std::abort();
        }
#endif
    }

    /// @brief Records one violation at an explicit site (used by `check()`; public for tests and adapters).
    void recordViolation(const char* file, std::uint32_t line, const char* function = "") noexcept {
        m_total.fetch_add(1, std::memory_order_relaxed);
        char thread[kThreadNameChars];
        currentThreadName(thread, sizeof(thread));
        for (auto& slot : m_sites) {
            std::uint32_t state = slot.state.load(std::memory_order_acquire);
            if (state == kFree) {
                std::uint32_t expected = kFree;
                if (slot.state.compare_exchange_strong(expected, kClaimed, std::memory_order_acq_rel)) {
                    slot.file = file ? file : "";
                    slot.function = function ? function : "";
                    slot.line = line;
                    std::memcpy(slot.thread, thread, sizeof(slot.thread));
                    slot.firstTimeNs = nowNs();
                    slot.count.store(1, std::memory_order_relaxed);
                    slot.state.store(kPublished, std::memory_order_release);
                    return;
                }
                state = expected;
            }
            // Another thread is filling this slot: wait a bounded moment for it, then compare.
            for (int spin = 0; state == kClaimed && spin < 1024; ++spin) {
                state = slot.state.load(std::memory_order_acquire);
            }
            if (state == kPublished && slot.line == line && std::strcmp(slot.file, file ? file : "") == 0 &&
                std::strcmp(slot.thread, thread) == 0) {
                slot.count.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
        m_unrecorded.fetch_add(1, std::memory_order_relaxed);
    }

    /// @brief Violations since construction, at every site, recorded or not.
    std::uint64_t totalViolations() const noexcept { return m_total.load(std::memory_order_relaxed); }

    /// @brief Violations whose site found no free slot (the table holds the first `kMaxSites` distinct sites).
    std::uint64_t unrecordedViolations() const noexcept { return m_unrecorded.load(std::memory_order_relaxed); }

    /// @brief Copies slot `index` into `out`; false while that slot is empty or being filled.
    bool site(std::size_t index, SiteInfo& out) const noexcept {
        if (index >= kMaxSites) return false;
        const auto& slot = m_sites[index];
        if (slot.state.load(std::memory_order_acquire) != kPublished) return false;
        out.file = slot.file;
        out.function = slot.function;
        out.line = slot.line;
        std::memcpy(out.thread, slot.thread, sizeof(out.thread));
        out.firstTimeNs = slot.firstTimeNs;
        out.count = slot.count.load(std::memory_order_relaxed);
        return true;
    }

    /// @brief Number of recorded sites.
    std::size_t siteCount() const noexcept {
        std::size_t n = 0;
        for (const auto& slot : m_sites) n += slot.state.load(std::memory_order_acquire) == kPublished ? 1 : 0;
        return n;
    }

    /// @brief Writes the total and every recorded site to `stream`; nothing when there was no violation.
    void printSummary(std::FILE* stream) const noexcept {
        const std::uint64_t total = totalViolations();
        if (total == 0 || !stream) return;
        std::fprintf(stream, "Brezel ECS wall: %llu entity accesses without a scope, %zu sites (%llu unrecorded)\n",
                     static_cast<unsigned long long>(total), siteCount(),
                     static_cast<unsigned long long>(unrecordedViolations()));
        SiteInfo info;
        for (std::size_t i = 0; i < kMaxSites; ++i) {
            if (!site(i, info)) continue;
            std::fprintf(stream, "  ECS wall site: %s:%u (%s) thread '%s' x%llu\n", info.file,
                         static_cast<unsigned>(info.line), info.function, info.thread,
                         static_cast<unsigned long long>(info.count));
        }
        std::fflush(stream);
    }

    /// @name Scope bookkeeping, called by `Project::ReadAccess` / `WriteAccess` (thread-local, wait-free)
    /// @{
    void enterScope() const noexcept {
        auto& scopes = detail::t_ecsWallScopes;
        if (auto* held = heldScope(this)) { ++held->depth; return; }
        if (scopes.count < detail::EcsWallThreadScopes::kMaxHeldProjects) {
            scopes.held[scopes.count++] = detail::EcsWallHeldScope{this, 1};
        } else {
            ++scopes.overflow; // more projects locked at once than tracked: treated as "holds a scope"
        }
    }

    void leaveScope() const noexcept {
        auto& scopes = detail::t_ecsWallScopes;
        for (std::uint32_t i = 0; i < scopes.count; ++i) {
            if (scopes.held[i].wall != this) continue;
            if (--scopes.held[i].depth == 0) scopes.held[i] = scopes.held[--scopes.count];
            return;
        }
        if (scopes.overflow > 0) --scopes.overflow;
    }
    /// @}

    /// @brief Name of the calling thread (or its id in hex), written into `out` without allocating.
    static void currentThreadName(char* out, std::size_t size) noexcept {
        if (!out || size == 0) return;
        out[0] = '\0';
#if defined(__APPLE__) || defined(__linux__)
        pthread_getname_np(pthread_self(), out, size);
        out[size - 1] = '\0';
#endif
        if (out[0] == '\0' && isProcessMainThread()) std::snprintf(out, size, "main");
        if (out[0] == '\0') {
            std::snprintf(out, size, "0x%zx", std::hash<std::thread::id>{}(std::this_thread::get_id()));
        }
    }

private:
    static bool isProcessMainThread() noexcept {
#if defined(__APPLE__)
        return pthread_main_np() != 0;
#elif defined(__linux__)
        return static_cast<pid_t>(::syscall(SYS_gettid)) == ::getpid();
#else
        return false;
#endif
    }

    static constexpr std::uint32_t kFree = 0, kClaimed = 1, kPublished = 2;
    struct Site {
        std::atomic<std::uint32_t> state{kFree};
        const char* file = "";
        const char* function = "";
        std::uint32_t line = 0;
        char thread[kThreadNameChars]{};
        std::uint64_t firstTimeNs = 0;
        std::atomic<std::uint64_t> count{0};
    };

    static detail::EcsWallHeldScope* heldScope(const EcsWall* wall) noexcept {
        auto& scopes = detail::t_ecsWallScopes;
        for (std::uint32_t i = 0; i < scopes.count; ++i) {
            if (scopes.held[i].wall == wall) return &scopes.held[i];
        }
        return nullptr;
    }

    static Mode modeFromEnvironment() noexcept {
#ifdef NDEBUG
        return Mode::Count;
#else
        const char* value = std::getenv("BREZEL_ECS_WALL");
        return (value && std::strcmp(value, "count") == 0) ? Mode::Count : Mode::Assert;
#endif
    }

    static std::uint64_t nowNs() noexcept {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    std::atomic<std::thread::id> m_writer{};
    std::atomic<std::uint64_t> m_total{0};
    std::atomic<std::uint64_t> m_unrecorded{0};
    Site m_sites[kMaxSites];
    const Mode m_mode;
};

} // namespace Brezel
