#ifndef PLATFORM_LINUX_H
#define PLATFORM_LINUX_H

/* Internal to the Linux backend (ADR-012 Decision 1). Window-system dispatch:
 * platform_init picks one PlatformOps table, and every Platform and
 * PlatformWindow of that choice is that path's struct with the base struct
 * below as its first member. The base holds everything the two paths share;
 * a path's own struct adds its handles. No window-system header here. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "src/platform/api.h"
#include "src/platform/native.h"
#include "src/platform/common/platform_common.h"

typedef struct PlatformOps PlatformOps;

/* A window-system connection made before anything is allocated (PLATFORM-033):
 * the path's own handle, kept as an engine-defined opaque type. */
typedef struct PlatformConnection {
    PlatformNativeObject *display;
} PlatformConnection;

struct Platform {
    const PlatformOps     *ops;         /* static const table of the chosen path */
    const EngineAllocator *allocator;   /* resolved once at init (PLATFORM-031) */
    const EngineLogger    *logger;      /* stored unchanged (PLATFORM-032) */
    PlatformEventQueue     queue;       /* PLATFORM-052 */
    PlatformWindow        *windows;     /* live windows, doubly linked */
    bool                   lost;        /* terminal state after connection loss (PLATFORM-043) */
};

struct PlatformWindow {
    const EngineAllocator *allocator;   /* resolved once at create (PLATFORM-031) */
    Platform              *platform;    /* non-owning */
    PlatformWindow        *prev;
    PlatformWindow        *next;
    uint32_t               pixel_width;   /* last pixel size seen (PLATFORM-042) */
    uint32_t               pixel_height;
    bool                   minimized;   /* reports 0 x 0 (PLATFORM-042) */
    bool                   creating;    /* absorbs creation notifications (PLATFORM-041) */
    bool                   should_close; /* sticky (PLATFORM-043) */
};

/* One table per window system. Allocation, linking, validation, the lost-state
 * guards and the generic queries stay in platform_linux.c; a path implements
 * only the window-system calls. All run on the platform thread. */
struct PlatformOps {
    const char *name;                   /* for logs */
    size_t platform_size;               /* sizeof the path's Platform struct */
    size_t window_size;                 /* sizeof the path's PlatformWindow struct */
    /* PLATFORM-033: probes the window system without any EngineAllocator
     * allocation. Logs the detail at WARN and returns NOT_SUPPORTED on failure,
     * leaving nothing open. */
    EngineResult (*connect)(const EngineLogger *logger, PlatformConnection *out_connection);
    /* Closes a connection that was never adopted (a later allocation failed). */
    void (*disconnect)(PlatformConnection *connection);
    /* Moves the connection into the allocated Platform (the base is
     * initialised). NOT_SUPPORTED on failure; the caller then disconnects. */
    EngineResult (*adopt)(Platform *platform, PlatformConnection *connection);
    /* Releases an adopted connection. Performs no I/O in the lost state. */
    void (*close)(Platform *platform);
    /* `title` is sanitised UTF-8. The base is linked and `creating` is true.
     * The window is visible on SUCCESS; sets its size with platform_linux_set_window_state. */
    EngineResult (*window_open)(PlatformWindow *window, const PlatformWindowDesc *desc, const char *title);
    void (*window_close)(PlatformWindow *window);   /* OS objects only */
    EngineResult (*get_native)(const PlatformWindow *window, PlatformNativeWindow *out_native);
    void (*pump)(Platform *platform);
    void (*wait)(Platform *platform, uint64_t timeout_ns);
};

extern const PlatformOps platform_x11_ops;
extern const PlatformOps platform_wayland_ops;

/* PLATFORM-042, PLATFORM-052: records the window's pixel size and minimized
 * state; a change in the reported size queues one coalesced FRAMEBUFFER_RESIZED
 * unless the window is still being created (PLATFORM-041). */
void platform_linux_set_window_state(PlatformWindow *window, uint32_t width, uint32_t height, bool minimized);

/* PLATFORM-043: sets the sticky flag; queues CLOSE_REQUESTED the first time only. */
void platform_linux_request_close(PlatformWindow *window);

/* PLATFORM-043, ADR-012 Decision 6: enters the terminal lost state (once).
 * Logs ERROR with `detail`, then treats it as an application-level quit. */
void platform_linux_enter_lost(Platform *platform, const char *detail);

#endif /* PLATFORM_LINUX_H */
