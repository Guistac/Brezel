#pragma once

#include <string_view>
#include <string>

#if defined(__APPLE__)
#include <pthread.h>
#elif defined(__linux__)
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace Brezel {

/**
 * @brief Sets a human-readable name for the current executing thread across macOS, Linux, and Windows.
 *
 * This name will be displayed in LLDB/GDB debuggers, Xcode, VS Code, Instruments, and OS thread samplers.
 *
 * @param name The descriptive name of the thread.
 */
inline void setThreadName(std::string_view name) {
#if defined(__APPLE__)
    // macOS: pthread_setname_np takes 1 argument (const char* name for current thread)
    // Max length is 63 characters + null terminator
    std::string truncatedName(name.substr(0, 63));
    pthread_setname_np(truncatedName.c_str());
#elif defined(__linux__)
    // Linux: pthread_setname_np takes 2 arguments (pthread_t, const char*)
    // Max length is 15 characters + null terminator (TASK_COMM_LEN limit is 16)
    std::string truncatedName(name.substr(0, 15));
    pthread_setname_np(pthread_self(), truncatedName.c_str());
#elif defined(_WIN32)
    // Windows 10 1607+ / Windows Server 2016+
    std::wstring wideName(name.begin(), name.end());
    SetThreadDescription(GetCurrentThread(), wideName.c_str());
#endif
}

/**
 * @brief Makes the calling thread an ordinary time-shared thread that stays off one CPU.
 *
 * On Linux a new thread inherits its creator's scheduling policy, priority and CPU set: a helper started from a
 * real-time thread runs at real-time priority on the real-time core unless it calls this first. Sets SCHED_OTHER
 * (priority 0) and an affinity of every configured CPU except `avoidCpu` (negative: every CPU). Thread-agnostic: call
 * it from the thread to change. A no-op returning true on macOS and Windows.
 *
 * @param avoidCpu The CPU the thread must not run on, usually the real-time core; negative for none.
 * @return false if the policy or the affinity could not be set.
 */
inline bool makeNonRealtimeThread(int avoidCpu = -1) {
#if defined(__linux__)
    sched_param param{};
    param.sched_priority = 0;
    bool ok = pthread_setschedparam(pthread_self(), SCHED_OTHER, &param) == 0;
    const long cpuCount = sysconf(_SC_NPROCESSORS_CONF);
    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    for (long cpu = 0; cpu < cpuCount && cpu < CPU_SETSIZE; cpu++) {
        if (cpu != avoidCpu) CPU_SET(cpu, &cpus);
    }
    if (CPU_COUNT(&cpus) > 0) ok = pthread_setaffinity_np(pthread_self(), sizeof(cpus), &cpus) == 0 && ok;
    return ok;
#else
    (void)avoidCpu;
    return true;
#endif
}

} // namespace Brezel
