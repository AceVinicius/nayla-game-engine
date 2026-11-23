/* OS-independent platform code, compiled into every backend. It includes no
 * OS or GPU SDK header (PLATFORM-092, PLAT-INV-3(c)).
 *
 * Holds the logger adapters (PLATFORM-025), the overflow-safe tick conversion
 * (PLATFORM-022), the allocator resolution every create entry point shares, the
 * event queue, window-create validation and title sanitising. The lifecycle
 * stub for backends without a window system (Win32) lives in platform_stub.c. */

#include <string.h>

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

EngineResult platform_queue_init(PlatformEventQueue *queue, const EngineAllocator *allocator) {
    queue->count = 0;
    queue->items = engine_alloc(allocator, sizeof *queue->items * PLATFORM_EVENT_QUEUE_CAPACITY,
                                _Alignof(PlatformEvent));
    return queue->items ? ENGINE_RESULT_SUCCESS : ENGINE_RESULT_OUT_OF_MEMORY;
}

void platform_queue_destroy(PlatformEventQueue *queue, const EngineAllocator *allocator) {
    engine_free(allocator, queue->items);
    queue->items = NULL;
    queue->count = 0;
}

void platform_queue_push(PlatformEventQueue *queue, const EngineLogger *logger, PlatformWindow *window,
                         int32_t type, uint32_t width, uint32_t height) {
    (void)logger; /* ENGINE_LOG_WARN compiles away at high ENGINE_LOG_COMPILE_MIN_LEVEL */
    if (type == PLATFORM_EVENT_WINDOW_FRAMEBUFFER_RESIZED) {
        for (uint32_t i = 0; i < queue->count; i++) {
            PlatformEvent *queued = &queue->items[i];
            if (queued->window == window && queued->type == type) {
                queued->width = width;
                queued->height = height;
                return;
            }
        }
    }
    if (queue->count == PLATFORM_EVENT_QUEUE_CAPACITY) {
        ENGINE_LOG_WARN(logger, "Platform", "event queue full: dropped a window event (PLATFORM-052)");
        return;
    }
    queue->items[queue->count++] = (PlatformEvent){type, window, width, height};
}

void platform_queue_purge_window(PlatformEventQueue *queue, const PlatformWindow *window) {
    uint32_t kept = 0;
    for (uint32_t i = 0; i < queue->count; i++) {
        if (queue->items[i].window != window) {
            queue->items[kept++] = queue->items[i];
        }
    }
    queue->count = kept;
}

bool platform_queue_pop(PlatformEventQueue *queue, PlatformEvent *out_event) {
    if (!out_event || queue->count == 0) {
        return false;
    }
    *out_event = queue->items[0];
    memmove(&queue->items[0], &queue->items[1], (queue->count - 1) * sizeof queue->items[0]);
    queue->count--;
    return true;
}

EngineResult platform_window_create_check(PlatformWindow **out_window, const Platform *platform,
                                          const EngineAllocator *allocator, const PlatformWindowDesc *desc,
                                          const EngineAllocator **out_resolved) {
    if (!out_window) {
        return ENGINE_RESULT_INVALID_ARGUMENT;
    }
    *out_window = NULL;
    if (!platform || !desc || !desc->title || desc->width == 0 || desc->height == 0 ||
        (desc->flags & ~PLATFORM_WINDOW_RESIZABLE) != 0) {
        return ENGINE_RESULT_INVALID_ARGUMENT;
    }
    *out_resolved = platform_resolve_allocator(allocator);
    return *out_resolved ? ENGINE_RESULT_SUCCESS : ENGINE_RESULT_INVALID_ARGUMENT;
}

/* Length of the well-formed UTF-8 sequence at `s` (RFC 3629 table 3.7), or 0. */
static size_t utf8_valid_length(const uint8_t *s) {
    uint8_t b0 = s[0];
    if (b0 < 0x80u) {
        return 1;
    }
    if (b0 >= 0xC2u && b0 <= 0xDFu) {
        return (s[1] & 0xC0u) == 0x80u ? 2 : 0;
    }
    if (b0 >= 0xE0u && b0 <= 0xEFu) {
        uint8_t lo = b0 == 0xE0u ? 0xA0u : 0x80u;   /* no overlongs */
        uint8_t hi = b0 == 0xEDu ? 0x9Fu : 0xBFu;   /* no surrogates */
        return (s[1] >= lo && s[1] <= hi && (s[2] & 0xC0u) == 0x80u) ? 3 : 0;
    }
    if (b0 >= 0xF0u && b0 <= 0xF4u) {
        uint8_t lo = b0 == 0xF0u ? 0x90u : 0x80u;   /* no overlongs */
        uint8_t hi = b0 == 0xF4u ? 0x8Fu : 0xBFu;   /* <= U+10FFFF */
        return (s[1] >= lo && s[1] <= hi && (s[2] & 0xC0u) == 0x80u && (s[3] & 0xC0u) == 0x80u) ? 4 : 0;
    }
    return 0;
}

char *platform_title_sanitize(const EngineAllocator *allocator, const char *title) {
    static const char replacement[3] = {(char)0xEF, (char)0xBF, (char)0xBD};   /* U+FFFD */
    size_t length = strlen(title);
    if (length > (SIZE_MAX - 1) / 3) {
        return NULL;
    }
    /* Worst case: every byte is invalid and becomes a 3-byte U+FFFD. */
    char *out = engine_alloc(allocator, length * 3 + 1, 1);
    if (!out) {
        return NULL;
    }
    size_t used = 0;
    for (size_t i = 0; i < length;) {
        /* Within the NUL-terminated string, a sequence cut short meets the NUL
         * (not a continuation byte) and is rejected before reading past it. */
        size_t n = utf8_valid_length((const uint8_t *)title + i);
        if (n == 0) {
            memcpy(out + used, replacement, sizeof replacement);
            used += sizeof replacement;
            i++;
        } else {
            memcpy(out + used, title + i, n);
            used += n;
            i += n;
        }
    }
    out[used] = '\0';
    return out;
}
