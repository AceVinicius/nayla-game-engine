#ifndef PLATFORM_COMMON_H
#define PLATFORM_COMMON_H

/* Internal to the platform backends: OS-independent helpers compiled into
 * every backend (src/platform/common/). Not part of the Platform API. */

#include <stdint.h>

/* PLATFORM-022: converts a tick count at `frequency` ticks per second to
 * nanoseconds without overflowing intermediate values. Precondition:
 * frequency > 0 (every OS counter reports a non-zero frequency). */
uint64_t platform_ticks_to_ns(uint64_t ticks, uint64_t frequency);

#endif /* PLATFORM_COMMON_H */
