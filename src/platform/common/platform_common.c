/* OS-independent platform code, compiled into every backend. It includes no
 * OS or GPU SDK header (PLATFORM-092, PLAT-INV-3(c)).
 *
 * Holds the logger adapters (PLATFORM-025), the overflow-safe tick conversion
 * (PLATFORM-022) and the allocator resolution every create entry point shares.
 * The lifecycle stub for backends without a window system lives in
 * platform_stub.c. */

#include "src/platform/api.h"
#include "src/platform/common/platform_common.h"

uint64_t platform_ticks_to_ns(uint64_t ticks, uint64_t frequency) {
    /* (c / f) * 1e9 cannot overflow for f < 2^64 / 1e9 (PLATFORM-022). The
     * remainder is below f, so (c % f) * 1e9 is below 2^64 under the same bound. */
    return (ticks / frequency) * 1000000000u + ((ticks % frequency) * 1000000000u) / frequency;
}

uint64_t platform_log_time_source(void *user_data) {
    (void)user_data; /* PLATFORM-025: ignored */
    return platform_time_now_ns();
}

uint64_t platform_log_thread_id_source(void *user_data) {
    (void)user_data; /* PLATFORM-025: ignored */
    return platform_thread_current_id();
}

const EngineAllocator *platform_resolve_allocator(const EngineAllocator *allocator) {
    const EngineAllocator *resolved = allocator ? allocator : engine_default_allocator();
    return (resolved->alloc && resolved->free) ? resolved : NULL;
}
