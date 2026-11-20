/* Linux time and thread identity (PLATFORM-020, PLATFORM-023). Stateless: no
 * file-scope state, no caching (PLATFORM-024, PLATFORM-090). */

/* Feature-test macro before any include. The strict C23 mode this project uses
 * (CMAKE_C_EXTENSIONS OFF) hides clock_gettime, CLOCK_MONOTONIC, syscall and
 * SYS_gettid otherwise. Only this file needs it. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>

#include "src/platform/api.h"

uint64_t platform_time_now_ns(void) {
    struct timespec ts;
    /* CLOCK_MONOTONIC has no error path for a valid clock id and a valid out
     * pointer, so the return value carries no information here. */
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

uint64_t platform_thread_current_id(void) {
    /* gettid(2): the kernel thread id, unique among live threads and never 0. */
    return (uint64_t)syscall(SYS_gettid);
}
