/* OS-independent platform code, compiled into every backend. It includes no
 * OS or GPU SDK header (PLATFORM-092, PLAT-INV-3(c)).
 *
 * Holds the logger adapters (PLATFORM-025), the overflow-safe tick conversion
 * (PLATFORM-022), and the lifecycle, window and event entry points. Those
 * entry points are a stub with the same behaviour on every OS, written once
 * here:
 *   - TEMPORARY for macOS and Win32, until their window tasks (#32, #34)
 *     replace them per backend;
 *   - PERMANENT for Linux, until the Linux windowing ADR (#28) lands.
 * No Platform or PlatformWindow is ever constructed here. Every entry point
 * therefore validates its arguments, and any handle it receives is one it
 * must reject or ignore. */

#include "src/platform/api.h"
#include "src/platform/native.h"
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

/* PLATFORM-031 / CORE-ALLOC-068, -070: NULL resolves to the default once, and
 * the resolved allocator must have both callbacks. */
static bool allocator_is_valid(const EngineAllocator *allocator) {
    const EngineAllocator *resolved = allocator ? allocator : engine_default_allocator();
    return resolved->alloc && resolved->free;
}

EngineResult platform_init(Platform **out_platform,
                           const EngineAllocator *allocator,
                           const EngineLogger *logger) {
    if (!out_platform) {
        return ENGINE_RESULT_INVALID_ARGUMENT;
    }
    *out_platform = NULL;

    if (!allocator_is_valid(allocator)) {
        return ENGINE_RESULT_INVALID_ARGUMENT;
    }

    /* PLATFORM-033: the window-system probe fails before anything is allocated. */
    ENGINE_LOG_ERROR(logger, "Platform", "platform_init: no window system backend is implemented yet");
    return ENGINE_RESULT_NOT_SUPPORTED;
}

void platform_shutdown(Platform *platform) {
    (void)platform; /* PLATFORM-035: no Platform exists, so this is a no-op */
}

EngineResult platform_window_create(PlatformWindow **out_window,
                                    Platform *platform,
                                    const EngineAllocator *allocator,
                                    const PlatformWindowDesc *desc) {
    if (!out_window) {
        return ENGINE_RESULT_INVALID_ARGUMENT;
    }
    *out_window = NULL;

    /* PLATFORM-041 validation. */
    if (!platform || !desc || !desc->title || desc->width == 0 || desc->height == 0 ||
        (desc->flags & ~PLATFORM_WINDOW_RESIZABLE) != 0 || !allocator_is_valid(allocator)) {
        return ENGINE_RESULT_INVALID_ARGUMENT;
    }
    return ENGINE_RESULT_NOT_SUPPORTED;
}

void platform_window_destroy(PlatformWindow *window) {
    (void)window; /* PLATFORM-044: NULL is a no-op; no window exists */
}

bool platform_window_should_close(const PlatformWindow *window) {
    (void)window; /* PLAT-INV-13: NULL gives false; no window exists */
    return false;
}

EngineResult platform_window_get_framebuffer_size(const PlatformWindow *window,
                                                  uint32_t *out_width,
                                                  uint32_t *out_height) {
    /* No PlatformWindow exists in this backend, so every window argument is
     * invalid. Nothing is written (PLATFORM-042). */
    (void)window;
    (void)out_width;
    (void)out_height;
    return ENGINE_RESULT_INVALID_ARGUMENT;
}

EngineResult platform_window_get_native(const PlatformWindow *window,
                                        PlatformNativeWindow *out_native) {
    /* As above: no window exists, so the call is invalid and writes nothing (PLATFORM-073). */
    (void)window;
    (void)out_native;
    return ENGINE_RESULT_INVALID_ARGUMENT;
}

void platform_pump_events(Platform *platform) {
    (void)platform; /* PLATFORM-050: NULL is a no-op; the queue is always empty */
}

bool platform_next_event(Platform *platform, PlatformEvent *out_event) {
    /* PLATFORM-051: an empty queue, or a NULL argument, returns false and writes nothing. */
    (void)platform;
    (void)out_event;
    return false;
}
