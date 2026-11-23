/* Linux platform entry points (ADR-012): window-system selection, dispatch
 * through a static const PlatformOps table, and everything the Wayland and X11
 * paths share (struct allocation and linking, the lost state, state queries).
 * The window-system calls live in x11.c and wayland.c.
 *
 * No file-scope mutable state (PLATFORM-090): the ops tables are const and the
 * environment is read once, inside platform_init. */

#include <stdalign.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "src/platform/linux/platform_linux.h"

#define NAYLA_LINUX_WINDOW_SYSTEM_ENV "NAYLA_LINUX_WINDOW_SYSTEM"

void platform_linux_set_window_state(PlatformWindow *window, uint32_t width, uint32_t height, bool minimized) {
    /* PLATFORM-042: a minimized window reports 0 x 0 on both axes, never 640 x 0.
     * The path passes the raw size every time, so a restore needs no new size from the OS. */
    window->minimized = minimized;
    uint32_t reported_width = minimized ? 0 : width;
    uint32_t reported_height = minimized ? 0 : height;
    if (width == 0 || height == 0) {
        reported_width = 0;
        reported_height = 0;
    }
    uint32_t old_width = window->pixel_width;
    uint32_t old_height = window->pixel_height;
    window->pixel_width = reported_width;
    window->pixel_height = reported_height;
    if (!window->creating && (old_width != reported_width || old_height != reported_height)) {
        platform_queue_push(&window->platform->queue, window->platform->logger, window,
                            PLATFORM_EVENT_WINDOW_FRAMEBUFFER_RESIZED, reported_width, reported_height);
    }
}

void platform_linux_request_close(PlatformWindow *window) {
    if (window->should_close) {
        return;
    }
    window->should_close = true;
    platform_queue_push(&window->platform->queue, window->platform->logger, window,
                        PLATFORM_EVENT_WINDOW_CLOSE_REQUESTED, 0, 0);
}

void platform_linux_enter_lost(Platform *platform, const char *detail) {
    if (platform->lost) {
        return;
    }
    platform->lost = true;
    ENGINE_LOG_ERROR(platform->logger, "Platform",
                     "%s connection lost (%s): treating it as an application quit (PLATFORM-043)",
                     platform->ops->name, detail);
    for (PlatformWindow *window = platform->windows; window; window = window->next) {
        platform_linux_request_close(window);
    }
}

/* ---- Lifecycle (PLATFORM-033..036, ADR-012 Decision 1) ------------------- */

/* PLATFORM-033: probe first (no EngineAllocator allocation), then allocate and
 * adopt the connection. A failed attempt releases everything it acquired. */
static EngineResult platform_try_open(const PlatformOps *ops, const EngineAllocator *allocator,
                                      const EngineLogger *logger, Platform **out_platform) {
    PlatformConnection connection = {NULL};
    EngineResult result = ops->connect(logger, &connection);
    if (result != ENGINE_RESULT_SUCCESS) {
        return result;
    }
    Platform *platform = engine_alloc(allocator, ops->platform_size, alignof(max_align_t));
    if (!platform) {
        ops->disconnect(&connection);
        return ENGINE_RESULT_OUT_OF_MEMORY;
    }
    memset(platform, 0, ops->platform_size);
    platform->ops = ops;
    platform->allocator = allocator;
    platform->logger = logger;
    if (platform_queue_init(&platform->queue, allocator) != ENGINE_RESULT_SUCCESS) {
        ops->disconnect(&connection);
        engine_free(allocator, platform);
        return ENGINE_RESULT_OUT_OF_MEMORY;
    }
    result = ops->adopt(platform, &connection);
    if (result != ENGINE_RESULT_SUCCESS) {
        ops->disconnect(&connection);
        platform_queue_destroy(&platform->queue, allocator);
        engine_free(allocator, platform);
        return result;
    }
    *out_platform = platform;
    return ENGINE_RESULT_SUCCESS;
}

EngineResult platform_init(Platform **out_platform,
                           const EngineAllocator *allocator,
                           const EngineLogger *logger) {
    if (!out_platform) {
        return ENGINE_RESULT_INVALID_ARGUMENT;
    }
    *out_platform = NULL;

    const EngineAllocator *resolved = platform_resolve_allocator(allocator);
    if (!resolved) {
        return ENGINE_RESULT_INVALID_ARGUMENT;
    }

    /* Decision 1: NAYLA_LINUX_WINDOW_SYSTEM forces one path with no fallback;
     * otherwise Wayland (when the session advertises one) and then X11. */
    const PlatformOps *candidates[2];
    size_t count = 0;
    const char *forced = getenv(NAYLA_LINUX_WINDOW_SYSTEM_ENV);
    if (forced && *forced) {
        if (strcmp(forced, "wayland") == 0) {
            candidates[count++] = &platform_wayland_ops;
        } else if (strcmp(forced, "x11") == 0) {
            candidates[count++] = &platform_x11_ops;
        } else {
            ENGINE_LOG_ERROR(logger, "Platform", "platform_init: %s must be \"wayland\" or \"x11\"",
                             NAYLA_LINUX_WINDOW_SYSTEM_ENV);
            return ENGINE_RESULT_NOT_SUPPORTED;
        }
    } else {
        if (getenv("WAYLAND_DISPLAY") || getenv("WAYLAND_SOCKET")) {
            candidates[count++] = &platform_wayland_ops;
        }
        candidates[count++] = &platform_x11_ops;
    }

    for (size_t i = 0; i < count; i++) {
        EngineResult result = platform_try_open(candidates[i], resolved, logger, out_platform);
        if (result == ENGINE_RESULT_SUCCESS) {
            ENGINE_LOG_INFO(logger, "Platform", "platform_init: using %s", candidates[i]->name);
            return ENGINE_RESULT_SUCCESS;
        }
        if (result != ENGINE_RESULT_NOT_SUPPORTED) {
            return result;
        }
    }
    /* PLATFORM-033: each attempt logged its own detail at WARN. */
    ENGINE_LOG_ERROR(logger, "Platform", "platform_init: no window system is reachable");
    return ENGINE_RESULT_NOT_SUPPORTED;
}

void platform_shutdown(Platform *platform) {
    if (!platform) {
        return;
    }
    if (platform->windows) {
        /* PLATFORM-035: release nothing; the caller still holds the windows. */
        ENGINE_LOG_ERROR(platform->logger, "Platform", "platform_shutdown: windows are still live; leaking them (PLATFORM-035)");
        return;
    }
    const EngineAllocator *allocator = platform->allocator;
    platform->ops->close(platform);
    platform_queue_destroy(&platform->queue, allocator);
    engine_free(allocator, platform);
}

/* ---- Windows (PLATFORM-040..044) --------------------------------------- */

EngineResult platform_window_create(PlatformWindow **out_window,
                                    Platform *platform,
                                    const EngineAllocator *allocator,
                                    const PlatformWindowDesc *desc) {
    const EngineAllocator *resolved = NULL;
    EngineResult result = platform_window_create_check(out_window, platform, allocator, desc, &resolved);
    if (result != ENGINE_RESULT_SUCCESS) {
        return result;
    }
    if (platform->lost) {
        ENGINE_LOG_ERROR(platform->logger, "Platform", "platform_window_create: the window-system connection is lost");
        return ENGINE_RESULT_NOT_SUPPORTED;
    }

    char *title = platform_title_sanitize(resolved, desc->title);
    PlatformWindow *window = title ? engine_alloc(resolved, platform->ops->window_size, alignof(max_align_t)) : NULL;
    if (!window) {
        engine_free(resolved, title);   /* NULL-safe */
        return ENGINE_RESULT_OUT_OF_MEMORY;
    }
    memset(window, 0, platform->ops->window_size);
    window->allocator = resolved;
    window->platform = platform;
    window->creating = true;
    /* Linked before the open call so that events for it, delivered while the
     * path waits for the window to map, find it. */
    window->next = platform->windows;
    if (platform->windows) {
        platform->windows->prev = window;
    }
    platform->windows = window;

    result = platform->ops->window_open(window, desc, title);
    engine_free(resolved, title);
    if (result != ENGINE_RESULT_SUCCESS) {
        platform->windows = window->next;
        if (window->next) {
            window->next->prev = NULL;
        }
        platform_queue_purge_window(&platform->queue, window);
        engine_free(resolved, window);
        return result;
    }
    window->creating = false;
    *out_window = window;
    return ENGINE_RESULT_SUCCESS;
}

void platform_window_destroy(PlatformWindow *window) {
    if (!window) {
        return;
    }
    Platform *platform = window->platform;
    const EngineAllocator *allocator = window->allocator;

    platform->ops->window_close(window);
    if (window->prev) {
        window->prev->next = window->next;
    } else {
        platform->windows = window->next;
    }
    if (window->next) {
        window->next->prev = window->prev;
    }
    platform_queue_purge_window(&platform->queue, window);
    /* The OS objects are gone before the struct (PLATFORM-044). */
    engine_free(allocator, window);
}

bool platform_window_should_close(const PlatformWindow *window) {
    return window ? window->should_close : false;
}

EngineResult platform_window_get_framebuffer_size(const PlatformWindow *window,
                                                  uint32_t *out_width,
                                                  uint32_t *out_height) {
    if (!window || !out_width || !out_height) {
        return ENGINE_RESULT_INVALID_ARGUMENT;
    }
    *out_width = window->pixel_width;
    *out_height = window->pixel_height;
    return ENGINE_RESULT_SUCCESS;
}

EngineResult platform_window_get_native(const PlatformWindow *window,
                                        PlatformNativeWindow *out_native) {
    if (!window || !out_native) {
        return ENGINE_RESULT_INVALID_ARGUMENT;
    }
    return window->platform->ops->get_native(window, out_native);
}

/* ---- Events (PLATFORM-050..056) ---------------------------------------- */

void platform_pump_events(Platform *platform) {
    /* PLATFORM-043: in the lost state nothing is left to pump. */
    if (platform && !platform->lost) {
        platform->ops->pump(platform);
    }
}

void platform_wait_events(Platform *platform, uint64_t timeout_ns) {
    if (!platform || platform->lost) {
        return;
    }
    if (timeout_ns == 0) {
        platform->ops->pump(platform);
    } else {
        platform->ops->wait(platform, timeout_ns);
    }
}

bool platform_next_event(Platform *platform, PlatformEvent *out_event) {
    return platform && platform_queue_pop(&platform->queue, out_event);
}
