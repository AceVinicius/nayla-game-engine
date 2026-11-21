/* Lifecycle, window and event entry points for backends that have no window
 * system implementation yet, compiled only into those backends (win32, linux).
 * The same behaviour on every such OS, written once here:
 *   - TEMPORARY for Win32, until its window task (#34) replaces it;
 *   - PERMANENT for Linux, until the Linux windowing ADR (#28) lands.
 * macOS links its real implementation instead (src/platform/macos/).
 * No Platform or PlatformWindow is ever constructed here. Every entry point
 * therefore validates its arguments, and any handle it receives is one it
 * must reject or ignore. */

#include "src/platform/api.h"
#include "src/platform/native.h"
#include "src/platform/common/platform_common.h"

EngineResult platform_init(Platform **out_platform,
                           const EngineAllocator *allocator,
                           const EngineLogger *logger) {
    if (!out_platform) {
        return ENGINE_RESULT_INVALID_ARGUMENT;
    }
    *out_platform = NULL;

    if (!platform_resolve_allocator(allocator)) {
        return ENGINE_RESULT_INVALID_ARGUMENT;
    }

    (void)logger; /* ENGINE_LOG_ERROR compiles away when ENGINE_LOG_COMPILE_MIN_LEVEL > 4 */
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
        (desc->flags & ~PLATFORM_WINDOW_RESIZABLE) != 0 || !platform_resolve_allocator(allocator)) {
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

void platform_wait_events(Platform *platform, uint64_t timeout_ns) {
    /* PLATFORM-055/PLAT-INV-13: no Platform exists, so the call returns at once. */
    (void)platform;
    (void)timeout_ns;
}

bool platform_next_event(Platform *platform, PlatformEvent *out_event) {
    /* PLATFORM-051: an empty queue, or a NULL argument, returns false and writes nothing. */
    (void)platform;
    (void)out_event;
    return false;
}
