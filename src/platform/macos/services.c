/* macOS time and thread identity (PLATFORM-020, PLATFORM-023). Stateless: no
 * file-scope state, no caching (PLATFORM-024, PLATFORM-090). Plain C is enough
 * for these two functions (ADR-011); the Objective-C translation units arrive
 * with the window task (#32). */

#include <stdint.h>
#include <time.h>
#include <pthread.h>

#include "src/platform/api.h"

uint64_t platform_time_now_ns(void) {
    /* CLOCK_UPTIME_RAW is monotonic and not adjusted by NTP (PLATFORM-020). It
     * already counts nanoseconds. */
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
}

uint64_t platform_thread_current_id(void) {
    /* NULL names the calling thread. The id is stable for the thread's life and
     * never 0 on macOS. */
    uint64_t id = 0;
    (void)pthread_threadid_np(NULL, &id);
    return id;
}
