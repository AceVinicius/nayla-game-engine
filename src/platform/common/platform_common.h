#ifndef PLATFORM_COMMON_H
#define PLATFORM_COMMON_H

/* Internal to the platform backends: OS-independent helpers compiled into
 * every backend (src/platform/common/). Not part of the Platform API. */

#include <stdbool.h>
#include <stdint.h>

#include "src/core/allocator.h"
#include "src/platform/api.h"

/* PLATFORM-022: converts a tick count at `frequency` ticks per second to
 * nanoseconds without overflowing intermediate values. Precondition:
 * frequency > 0 (every OS counter reports a non-zero frequency). */
uint64_t platform_ticks_to_ns(uint64_t ticks, uint64_t frequency);

/* PLATFORM-031 / CORE-ALLOC-068, -070: resolves a NULL allocator to
 * engine_default_allocator() and returns the pointer to store. Returns NULL
 * when the resolved allocator has a NULL callback, which the caller reports as
 * ENGINE_RESULT_INVALID_ARGUMENT. Pure: it allocates nothing. */
const EngineAllocator *platform_resolve_allocator(const EngineAllocator *allocator);

/* ---- Event queue (PLATFORM-051..053) --------------------------------------
 * PLATFORM-052: a fixed array allocated once at platform_init. The owning
 * Platform supplies the logger and allocator on each call; the queue stores
 * neither. */

typedef struct PlatformEventQueue {
    PlatformEvent *items;   /* PLATFORM_EVENT_QUEUE_CAPACITY entries, owned */
    uint32_t       count;
} PlatformEventQueue;

#define PLATFORM_EVENT_QUEUE_CAPACITY 64u

/* OUT_OF_MEMORY on allocation failure; the queue is then unusable. */
EngineResult platform_queue_init(PlatformEventQueue *queue, const EngineAllocator *allocator);
void platform_queue_destroy(PlatformEventQueue *queue, const EngineAllocator *allocator);

/* PLATFORM-052: a newer FRAMEBUFFER_RESIZED updates the queued one for the same
 * window in place. A full queue drops the event with a WARN; state queries stay
 * correct because window state is authoritative. */
void platform_queue_push(PlatformEventQueue *queue, const EngineLogger *logger, PlatformWindow *window,
                         int32_t type, uint32_t width, uint32_t height);
/* PLATFORM-044 / PLATFORM-053: removes every queued event for `window`, keeping order. */
void platform_queue_purge_window(PlatformEventQueue *queue, const PlatformWindow *window);
/* PLATFORM-051: false and nothing written when empty or `out_event` is NULL. */
bool platform_queue_pop(PlatformEventQueue *queue, PlatformEvent *out_event);

/* ---- platform_window_create argument handling (PLATFORM-041) -------------
 * Sets *out_window to NULL when out_window is non-NULL, then validates in the
 * PLATFORM-041 precedence: INVALID_ARGUMENT for a NULL out_window, platform,
 * desc or title, a zero size, an unknown flag bit, or an unusable allocator.
 * On SUCCESS *out_resolved is the allocator to store (NULL resolved to the
 * default, PLATFORM-031). */
EngineResult platform_window_create_check(PlatformWindow **out_window, const Platform *platform,
                                          const EngineAllocator *allocator, const PlatformWindowDesc *desc,
                                          const EngineAllocator **out_resolved);

/* ---- Title sanitising (PLATFORM-041) ---------------------------------------
 * Returns a NUL-terminated copy of `title` allocated from `allocator`, with
 * every byte that is not part of a well-formed sequence (overlong forms,
 * surrogates, > U+10FFFF, truncated or stray bytes) replaced by one U+FFFD, so
 * one bad sequence can yield several. Free with engine_free.
 * NULL on allocation failure. */
char *platform_title_sanitize(const EngineAllocator *allocator, const char *title);

#endif /* PLATFORM_COMMON_H */
