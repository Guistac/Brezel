#pragma once

#include <string_view>
#include <string>

#if defined(__APPLE__)
#include <pthread.h>
#elif defined(__linux__)
#include <pthread.h>
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

} // namespace Brezel
