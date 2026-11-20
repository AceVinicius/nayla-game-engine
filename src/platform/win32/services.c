/* Win32 time and thread identity (PLATFORM-020..023). Stateless: no file-scope
 * state, no caching (PLATFORM-024, PLATFORM-090). Not compiled on the macOS or
 * Linux development hosts; it is checked only by a Windows toolchain. */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdint.h>

#include "src/platform/api.h"
#include "src/platform/common/platform_common.h"

uint64_t platform_time_now_ns(void) {
    /* The frequency is constant from boot, so it is read per call rather than
     * cached (PLATFORM-024). The conversion is overflow-safe (PLATFORM-022). */
    LARGE_INTEGER counter;
    LARGE_INTEGER frequency;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return platform_ticks_to_ns((uint64_t)counter.QuadPart, (uint64_t)frequency.QuadPart);
}

uint64_t platform_thread_current_id(void) {
    /* The thread id is a DWORD, widened here. It is never 0 for a live thread. */
    return (uint64_t)GetCurrentThreadId();
}
