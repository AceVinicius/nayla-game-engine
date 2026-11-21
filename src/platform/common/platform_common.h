#ifndef PLATFORM_COMMON_H
#define PLATFORM_COMMON_H

/* Internal to the platform backends: OS-independent helpers compiled into
 * every backend (src/platform/common/). Not part of the Platform API. */

#include <stdint.h>

#include "src/core/allocator.h"

/* PLATFORM-022: converts a tick count at `frequency` ticks per second to
 * nanoseconds without overflowing intermediate values. Precondition:
 * frequency > 0 (every OS counter reports a non-zero frequency). */
uint64_t platform_ticks_to_ns(uint64_t ticks, uint64_t frequency);

/* PLATFORM-031 / CORE-ALLOC-068, -070: resolves a NULL allocator to
 * engine_default_allocator() and returns the pointer to store. Returns NULL
 * when the resolved allocator has a NULL callback, which the caller reports as
 * ENGINE_RESULT_INVALID_ARGUMENT. Pure: it allocates nothing. */
const EngineAllocator *platform_resolve_allocator(const EngineAllocator *allocator);

#endif /* PLATFORM_COMMON_H */
