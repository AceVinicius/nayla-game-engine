/* X11 path of the Linux backend (ADR-012 Decisions 2 to 4 and 6): Xlib windows,
 * the XPending pump, the poll-based wait and connection-loss detection.
 *
 * Upstream references (libX11, https://gitlab.freedesktop.org/xorg/lib/libx11;
 * tags libX11-1.7.0 and libX11-1.8.1, read through cgit.freedesktop.org):
 *   [1] src/XlibInt.c _XDefaultIOError ends in exit(1) in every release checked
 *       (1.7.0, 1.8.1, 1.8.12), so its default never reaches the per-Display
 *       exit handler.
 *   [2] src/XlibInt.c _XIOError: sets XlibDisplayIOError, takes the user lock
 *       (dpy->lock->user_lock_display), calls UnlockDisplay, then the global
 *       I/O-error handler, then dpy->exit_handler. The user lock is never
 *       released: the code assumes the exit handler exits the process.
 *   [3] src/locking.c _XUserLockDisplay / _XUserUnlockDisplay: a nesting count
 *       (locking_level) plus an owner thread, released by XUnlockDisplay.
 *   [4] src/xcb_io.c: _XSend and _XReply return at once, and _XEventsQueued
 *       returns 0, when dpy->flags has XlibDisplayIOError.
 *   [5] src/locking.c XInitThreads: `if (_Xglobal_lock) return 1;`, so a repeat
 *       call is harmless (checked on 1.7.0, the minimum).
 *
 * Neither path installs a process-global Xlib *error* handler (Decision 4). The
 * one process-global handler installed here is the I/O-error handler of
 * Decision 6's fallback, installed in x11_adopt and restored in x11_close. It
 * is needed because of [1]. At most one X11 Platform is live at a time
 * (PLATFORM-036), so the saved handler has one owner. */

/* Feature-test macro before any include: strict C23 mode hides poll(). */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "src/platform/linux/platform_linux.h"

/* PLATFORM-041: how long a window may take to report MapNotify. Backend-private. */
#define X11_MAP_TIMEOUT_NS ((uint64_t)1000000000u)
/* A request longer than the server's maximum fails with BadLength, and Xlib's
 * default error handler then exits the process, so the title is capped to
 * XMaxRequestSize (the length without BIG-REQUESTS, in 4-byte units) less the
 * margin. It only matters for absurdly long titles. */
/* ChangeProperty header (24 bytes) plus margin kept out of the title cap. */
#define X11_REQUEST_MARGIN_BYTES 256

typedef struct X11Platform {
    Platform       base;                 /* first member: Platform * casts to X11Platform * */
    Display       *display;
    int            connection_fd;
    Atom           wm_protocols;
    Atom           wm_delete_window;
    Atom           wm_state;
    Atom           net_wm_name;
    Atom           utf8_string;
    XIOErrorHandler previous_io_handler;  /* restored in x11_close (Decision 6 fallback) */
    atomic_bool    io_lost;              /* set by the exit handler, any thread */
    atomic_int     io_errno;
} X11Platform;

typedef struct X11Window {
    PlatformWindow base;                 /* first member */
    Window         xid;
    uint32_t       width;                /* raw size from ConfigureNotify */
    uint32_t       height;
    bool           minimized;
    bool           mapped;
} X11Window;

/* ---- Connection loss (Decision 6) ---------------------------------------- */

/* Process-global fallback handler: returns at once and touches no engine state,
 * so Xlib continues to the per-Display exit handler instead of exiting. */
static int x11_io_error_handler(Display *display) {
    (void)display;
    return 0;
}

/* Runs inside an Xlib call, possibly on a presentation thread: it records the
 * loss and nothing else. The platform thread does the rest in x11_check_lost. */
static void x11_io_exit_handler(Display *display, void *user_data) {
    X11Platform *x11 = user_data;
    atomic_store(&x11->io_errno, errno);
    atomic_store(&x11->io_lost, true);
    /* [2]: _XIOError took the display's user lock and never releases it, since
     * libX11 expects this handler to exit. We return instead, so balance it
     * ([3]), or another thread's XPending, XFlush or XCloseDisplay would block
     * forever on a lock whose owner has moved on. This runs on the thread that
     * made _XIOError take it. */
    XUnlockDisplay(display);
}

/* Moves a recorded loss into the shared lost state. True once the Platform is lost. */
static bool x11_check_lost(X11Platform *x11) {
    if (!x11->base.lost && atomic_load(&x11->io_lost)) {
        char detail[64];
        (void)snprintf(detail, sizeof detail, "I/O error, errno %d", atomic_load(&x11->io_errno));
        platform_linux_enter_lost(&x11->base, detail);
    }
    return x11->base.lost;
}

/* ---- Events (PLATFORM-043, -050, -052) ------------------------------------ */

static X11Window *x11_find_window(X11Platform *x11, Window xid) {
    for (PlatformWindow *window = x11->base.windows; window; window = window->next) {
        if (((X11Window *)window)->xid == xid) {
            return (X11Window *)window;
        }
    }
    return NULL;
}

static void x11_sync_size(X11Window *window) {
    platform_linux_set_window_state(&window->base, window->width, window->height, window->minimized);
}

/* Decision 3: WM_STATE == IconicState is minimized. */
static bool x11_is_iconic(X11Platform *x11, Window xid) {
    Atom type = None;
    int format = 0;
    unsigned long count = 0;
    unsigned long remaining = 0;
    unsigned char *data = NULL;
    bool iconic = false;
    if (XGetWindowProperty(x11->display, xid, x11->wm_state, 0, 2, False, x11->wm_state,
                           &type, &format, &count, &remaining, &data) == Success && data) {
        if (type == x11->wm_state && format == 32 && count >= 1) {
            long state;
            memcpy(&state, data, sizeof state);
            iconic = state == IconicState;
        }
        XFree(data);
    }
    return iconic;
}

static void x11_handle_event(X11Platform *x11, const XEvent *event) {
    X11Window *window;
    switch (event->type) {
    case ConfigureNotify:
        window = x11_find_window(x11, event->xconfigure.window);
        if (window) {
            window->width = event->xconfigure.width > 0 ? (uint32_t)event->xconfigure.width : 0;
            window->height = event->xconfigure.height > 0 ? (uint32_t)event->xconfigure.height : 0;
            x11_sync_size(window);
        }
        break;
    case MapNotify:
        window = x11_find_window(x11, event->xmap.window);
        if (window) {
            window->mapped = true;
        }
        break;
    case PropertyNotify:
        if (event->xproperty.atom == x11->wm_state) {
            window = x11_find_window(x11, event->xproperty.window);
            if (window) {
                window->minimized = x11_is_iconic(x11, window->xid);
                x11_sync_size(window);
            }
        }
        break;
    case ClientMessage:
        window = x11_find_window(x11, event->xclient.window);
        if (window && event->xclient.message_type == x11->wm_protocols && event->xclient.format == 32 &&
            (Atom)event->xclient.data.l[0] == x11->wm_delete_window) {
            platform_linux_request_close(&window->base);   /* never destroys (PLATFORM-043) */
        }
        break;
    default:
        break;
    }
}

/* XPending flushes the output buffer when nothing is queued. After an I/O
 * error the flush and the read return at once ([4]) and no new event arrives,
 * so the loop ends. */
static void x11_drain(X11Platform *x11) {
    while (XPending(x11->display) > 0) {
        XEvent event;
        XNextEvent(x11->display, &event);
        x11_handle_event(x11, &event);
    }
}

static void x11_pump(Platform *platform) {
    X11Platform *x11 = (X11Platform *)platform;
    if (x11_check_lost(x11)) {   /* a presentation thread may have hit the loss */
        return;
    }
    x11_drain(x11);
    (void)x11_check_lost(x11);
}

/* Blocks until the connection is readable or the timeout elapses (PLATFORM-056):
 * rounded up to whole milliseconds, never to 0; UINT64_MAX is infinite; a long
 * finite timeout is clamped to INT_MAX ms, never to 0 or infinite. An EINTR
 * return is an early return, which PLATFORM-055 allows. */
static void x11_poll(int fd, uint64_t timeout_ns) {
    int timeout_ms = -1;
    if (timeout_ns != UINT64_MAX) {
        uint64_t ms = timeout_ns / 1000000u + (timeout_ns % 1000000u != 0 ? 1u : 0u);
        timeout_ms = ms > (uint64_t)INT_MAX ? INT_MAX : (int)ms;
    }
    struct pollfd entry = {.fd = fd, .events = POLLIN, .revents = 0};
    (void)poll(&entry, 1, timeout_ms);
}

static void x11_wait(Platform *platform, uint64_t timeout_ns) {
    X11Platform *x11 = (X11Platform *)platform;
    if (x11_check_lost(x11)) {
        return;
    }
    XFlush(x11->display);
    /* Events already read into Xlib's queue would never wake the poll. */
    int queued = XEventsQueued(x11->display, QueuedAfterReading);
    if (x11_check_lost(x11)) {
        return;   /* nothing to wait on (PLATFORM-043) */
    }
    if (queued == 0) {
        x11_poll(x11->connection_fd, timeout_ns);
    }
    x11_pump(platform);
}

/* ---- Lifecycle (Decisions 2, 4, 6) ----------------------------------------- */

/* PLATFORM-033: the probe allocates nothing from an EngineAllocator. */
static EngineResult x11_connect(const EngineLogger *logger, PlatformConnection *out_connection) {
    (void)logger;   /* ENGINE_LOG_WARN compiles away at high ENGINE_LOG_COMPILE_MIN_LEVEL */
    /* Decision 4: before the first Xlib call, XOpenDisplay included, because the
     * Vulkan Xlib WSI may use the Display from presentation threads. Repeat
     * calls across sequential re-init return 1 at once ([5]). */
    if (!XInitThreads()) {
        ENGINE_LOG_WARN(logger, "Platform", "X11: XInitThreads failed");
        return ENGINE_RESULT_NOT_SUPPORTED;
    }
    Display *display = XOpenDisplay(NULL);
    if (!display) {
        ENGINE_LOG_WARN(logger, "Platform", "X11: XOpenDisplay failed (no X server reachable through DISPLAY)");
        return ENGINE_RESULT_NOT_SUPPORTED;
    }
    out_connection->display = (PlatformNativeObject *)display;
    return ENGINE_RESULT_SUCCESS;
}

static void x11_disconnect(PlatformConnection *connection) {
    XCloseDisplay((Display *)connection->display);
}

static EngineResult x11_adopt(Platform *platform, PlatformConnection *connection) {
    X11Platform *x11 = (X11Platform *)platform;
    Display *display = (Display *)connection->display;
    x11->wm_protocols = XInternAtom(display, "WM_PROTOCOLS", False);
    x11->wm_delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);
    x11->wm_state = XInternAtom(display, "WM_STATE", False);
    x11->net_wm_name = XInternAtom(display, "_NET_WM_NAME", False);
    x11->utf8_string = XInternAtom(display, "UTF8_STRING", False);
    if (!x11->wm_protocols || !x11->wm_delete_window || !x11->wm_state || !x11->net_wm_name || !x11->utf8_string) {
        ENGINE_LOG_WARN(platform->logger, "Platform", "X11: the server did not intern the required atoms");
        return ENGINE_RESULT_NOT_SUPPORTED;
    }
    x11->display = display;
    atomic_init(&x11->io_lost, false);
    atomic_init(&x11->io_errno, 0);
    x11->connection_fd = ConnectionNumber(display);
    x11->previous_io_handler = XSetIOErrorHandler(x11_io_error_handler);
    XSetIOErrorExitHandler(display, x11_io_exit_handler, x11);
    return ENGINE_RESULT_SUCCESS;
}

/* Safe in the lost state: with XlibDisplayIOError set, _XSend and _XReply
 * return at once ([4]), so the XSync calls inside XCloseDisplay do no I/O. This
 * relies on x11_io_exit_handler having released the user lock ([2], [3]). */
static void x11_close(Platform *platform) {
    X11Platform *x11 = (X11Platform *)platform;
    XCloseDisplay(x11->display);
    XSetIOErrorHandler(x11->previous_io_handler);
}

/* ---- Windows (PLATFORM-041..044, Decision 3) -------------------------------- */

static EngineResult x11_window_open(PlatformWindow *base, const PlatformWindowDesc *desc, const char *title) {
    X11Window *window = (X11Window *)base;
    X11Platform *x11 = (X11Platform *)base->platform;
    Display *display = x11->display;
    int screen = DefaultScreen(display);

    /* Logical units are pixels on X11 (PLATFORM-042). */
    window->width = desc->width;
    window->height = desc->height;
    window->xid = XCreateSimpleWindow(display, RootWindow(display, screen), 0, 0, desc->width, desc->height, 0,
                                      BlackPixel(display, screen), BlackPixel(display, screen));
    if (!window->xid) {
        ENGINE_LOG_ERROR(base->platform->logger, "Platform", "platform_window_create: XCreateSimpleWindow failed");
        return ENGINE_RESULT_NOT_SUPPORTED;
    }
    XSelectInput(display, window->xid, StructureNotifyMask | PropertyChangeMask);
    Atom protocols[1] = {x11->wm_delete_window};
    (void)XSetWMProtocols(display, window->xid, protocols, 1);   /* WM_DELETE_WINDOW only: no _NET_WM_PING */

    if (!(desc->flags & PLATFORM_WINDOW_RESIZABLE)) {
        XSizeHints hints;
        memset(&hints, 0, sizeof hints);
        hints.flags = PMinSize | PMaxSize;
        hints.min_width = hints.max_width = (int)desc->width;
        hints.min_height = hints.max_height = (int)desc->height;
        XSetWMNormalHints(display, window->xid, &hints);
    }

    /* _NET_WM_NAME only: every current window manager reads it. */
    size_t title_length = strlen(title);
    size_t title_cap = (size_t)XMaxRequestSize(display) * 4u - X11_REQUEST_MARGIN_BYTES;
    if (title_length > title_cap) {
        title_length = title_cap;
        while (title_length > 0 && ((unsigned char)title[title_length] & 0xC0u) == 0x80u) {
            title_length--;   /* never cut a sequence in half */
        }
    }
    XChangeProperty(display, window->xid, x11->net_wm_name, x11->utf8_string, 8, PropModeReplace,
                    (const unsigned char *)title, (int)title_length);

    /* PLATFORM-041: a window manager redirects the map, so wait for this
     * window's MapNotify (bounded) instead of trusting XSync. */
    XMapWindow(display, window->xid);
    uint64_t deadline = platform_time_now_ns() + X11_MAP_TIMEOUT_NS;
    for (;;) {
        bool lost = x11_check_lost(x11);   /* before any Xlib call: a loss may already be recorded */
        if (!lost) {
            x11_drain(x11);
        }
        if (window->mapped) {
            break;
        }
        uint64_t now = platform_time_now_ns();
        if (lost || x11_check_lost(x11) || now >= deadline) {
            if (!x11->base.lost) {
                XDestroyWindow(display, window->xid);
                XFlush(display);
            }
            ENGINE_LOG_ERROR(base->platform->logger, "Platform", "platform_window_create: the window was not mapped in time");
            return ENGINE_RESULT_NOT_SUPPORTED;
        }
        x11_poll(x11->connection_fd, deadline - now);
    }
    x11_sync_size(window);   /* the size as the window manager left it; creating, so no event */
    return ENGINE_RESULT_SUCCESS;
}

static void x11_window_close(PlatformWindow *base) {
    X11Platform *x11 = (X11Platform *)base->platform;
    if (!x11->base.lost && !atomic_load(&x11->io_lost)) {
        XDestroyWindow(x11->display, ((X11Window *)base)->xid);
        XFlush(x11->display);
    }
}

static EngineResult x11_get_native(const PlatformWindow *base, PlatformNativeWindow *out_native) {
    /* X11 row (PLATFORM-071): Display * and the XID; both stay owned by the platform. */
    out_native->type = PLATFORM_NATIVE_WINDOW_X11;
    out_native->display = (PlatformNativeObject *)((const X11Platform *)base->platform)->display;
    out_native->window = NULL;
    out_native->window_id = (uint64_t)((const X11Window *)base)->xid;
    return ENGINE_RESULT_SUCCESS;
}

const PlatformOps platform_x11_ops = {
    .name = "X11",
    .platform_size = sizeof(X11Platform),
    .window_size = sizeof(X11Window),
    .connect = x11_connect,
    .disconnect = x11_disconnect,
    .adopt = x11_adopt,
    .close = x11_close,
    .window_open = x11_window_open,
    .window_close = x11_window_close,
    .get_native = x11_get_native,
    .pump = x11_pump,
    .wait = x11_wait,
};
