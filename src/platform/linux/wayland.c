/* Wayland path of the Linux backend (ADR-012 Decisions 1 to 4 and 6):
 * libwayland-client plus libdecor, the viewporter sizing scheme, `suspended` as
 * minimized, the pump-first wait and connection-loss detection.
 *
 * Upstream references. Versions: libwayland 1.22.0 and libdecor 0.2.2; the
 * claims were read from these tags, not from memory.
 *   libwayland, https://gitlab.freedesktop.org/wayland/wayland/-/blob/1.22.0/src/wayland-client.c
 *   libdecor,   https://gitlab.freedesktop.org/libdecor/libdecor/-/tree/0.2.2/src
 *   [1] wayland-client.c wl_display_get_error: an error is fatal, the display
 *       cannot be used afterwards; wl_display_get_protocol_error returns the
 *       code, interface and object id of a protocol error (error == EPROTO).
 *   [2] wayland-client.c wl_proxy_marshal_array_flags: once display->last_error
 *       is set, a request is dropped client side (nothing is written).
 *       Loss that arrives as a failed flush with EPIPE does NOT set it ([3]),
 *       so there, destroy requests are marshalled and may be written to the
 *       dead socket (a harmless failed send). wl_display_disconnect only closes
 *       the fd and frees the display.
 *   [3] wayland-client.c wl_display_flush: EPIPE and EAGAIN do not set the
 *       display error, "so that we may try to read events after the failed
 *       flush"; wl_display_read_events fails with the display error once one is
 *       set, and display_fatal_error records read errors and EOF.
 *   [4] libdecor src/plugins/cairo and src/plugins/gtk, function libdecor_plugin_<name>_dispatch (and
 *       src/libdecor-fallback.c): prepare_read/dispatch_pending loop, flush
 *       (-errno unless EAGAIN), poll(timeout), read_events and
 *       dispatch_pending. Returns the dispatched count, or -errno from flush or
 *       poll. A negative count can also leak out of dispatch_pending after a
 *       fatal error, so loss is decided by wl_display_get_error, not the sign.
 *       With display->last_error set, dispatch_pending fails at once while
 *       prepare_read keeps failing with EAGAIN as long as events are queued, so
 *       the plugins' prepare_read loop would spin forever: pump and wait
 *       therefore check the display error before every libdecor_dispatch. A
 *       residual race remains inside libdecor: an error set by another thread
 *       (a GPU WSI thread) between that check and the plugin's loop can still
 *       spin it; libdecor offers no hook to close that window. The local
 *       wayland_dispatch below deviates on purpose: it returns on the first
 *       failed dispatch_pending instead of looping. It otherwise copies the
 *       libdecor-free setup phase.
 *   [5] libdecor src/libdecor.c libdecor_new: when no plugin loads it prints
 *       "No plugins found, falling back on no decorations" to the standard error stream and
 *       installs the fallback plugin. libdecor_interface.error is called only
 *       for LIBDECOR_ERROR_COMPOSITOR_INCOMPATIBLE (no xdg_wm_base) and
 *       LIBDECOR_ERROR_INVALID_FRAME_CONFIGURATION (min size > max size), and
 *       its signature carries no user data. Neither is "no decorations": the
 *       missing-decoration limit of ADR-012 Decision 2 is NOT observable through
 *       libdecor's API, so the WARN is dropped as Decision 2 provides. With no
 *       plugin and no server-side decorations the window has no title bar or
 *       close button; NAYLA_LINUX_WINDOW_SYSTEM=x11 is the workaround.
 *   [6] libdecor.h: LIBDECOR_WINDOW_STATE_SUSPENDED exists from libdecor 0.2.0
 *       (absent in 0.1.1, checked on both tags); libdecor.c binds xdg_wm_base at
 *       min(server version, 6) when built with xdg-shell v6. A libdecor built
 *       without xdg-shell v6 never reports `suspended`, and the API cannot say
 *       so: only the compositor's xdg_wm_base version is observable here.
 *   [7] libdecor.c libdecor_frame_commit acks the configure serial;
 *       libdecor_configuration_get_content_size returns false when the
 *       configure carried no or a zero size; libdecor_frame_unref destroys the
 *       frame's xdg_toplevel and xdg_surface, which must precede wl_surface
 *       destruction.
 *   [8] wl_display_connect(NULL): WAYLAND_SOCKET, when set, is consumed (unset)
 *       once the fd parses and is valid, even when the connection setup on that
 *       fd then fails (it returns NULL without unsetting it if the value does
 *       not parse or the fd is EBADF), so a later probe or re-init in one
 *       process cannot use it again.
 *   [9] libdecor_new (called from adopt) runs the plugin's init, which in the
 *       cairo plugin may do an unbounded roundtrip. The probe already proved
 *       the compositor answers, so the risk is low; it is not bounded here.
 *
 * Every listener and libdecor callback runs inside a dispatch call on the
 * platform thread (nothing here is reached from another thread), so loss is
 * entered directly with platform_linux_enter_lost instead of through the atomic
 * PlatformLoss record that the X11 path needs for its I/O-error callback. */

/* Feature-test macro before any include: strict C23 mode hides poll() and
 * memfd_create(). */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <libdecor.h>
#include <wayland-client.h>

#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "src/platform/linux/platform_linux.h"

/* PLATFORM-041: how long setup (the registry round-trips) and a window may take
 * to map. Backend-private, like the X11 value. */
#define WAYLAND_SETUP_TIMEOUT_NS ((uint64_t)1000000000u)
/* Outputs are tracked in a fixed array so that hot-plug never allocates in
 * pump/wait (PLATFORM-052). An output beyond the capacity is not bound and
 * counts as scale 1. */
#define WAYLAND_MAX_OUTPUTS 16
#define WAYLAND_XDG_WM_BASE_SUSPENDED_VERSION 6u
#define WAYLAND_FRACTIONAL_DENOMINATOR 120u   /* wp_fractional_scale_v1: scale is numerator / 120 */
/* wl_surface.preferred_buffer_scale arrived with wl_compositor v6 (libwayland 1.22). */
#ifdef WL_SURFACE_PREFERRED_BUFFER_SCALE_SINCE_VERSION
#define WAYLAND_COMPOSITOR_BIND_VERSION WL_SURFACE_PREFERRED_BUFFER_SCALE_SINCE_VERSION
#else
#define WAYLAND_COMPOSITOR_BIND_VERSION 1u
#endif
/* wl_output.scale is in version 2; later events are not used. */
#define WAYLAND_OUTPUT_BIND_VERSION 2u

typedef struct WaylandPlatform WaylandPlatform;

typedef struct WaylandOutput {
    struct wl_output *output;   /* NULL: free slot */
    uint32_t          name;     /* the registry name, for global_remove */
    int32_t           scale;
} WaylandOutput;

/* What the registry advertises and the backend bound. During the probe it lives
 * on the stack (owner == NULL); an adopted one lives in the Platform. */
typedef struct WaylandGlobals {
    WaylandPlatform                       *owner;
    struct wl_registry                    *registry;
    struct wl_compositor                  *compositor;
    struct wl_shm                         *shm;
    struct wp_viewporter                  *viewporter;
    struct wp_fractional_scale_manager_v1 *fractional_manager;
    uint32_t                               xdg_wm_base_version;   /* 0: absent; libdecor binds it itself */
    WaylandOutput                          outputs[WAYLAND_MAX_OUTPUTS];
} WaylandGlobals;

struct WaylandPlatform {
    Platform                      base;                /* first member: Platform * casts to WaylandPlatform * */
    struct wl_display            *display;
    WaylandGlobals                globals;
    struct wl_buffer             *placeholder;         /* the 1x1 opaque buffer of PLATFORM-041 */
    struct libdecor              *decor;
    struct libdecor_interface     decor_interface;     /* libdecor takes non-const pointers: they live here (Decision 2) */
    struct libdecor_frame_interface frame_interface;
    uint32_t                      activity;            /* bumped when a listener queued an event or changed window state */
};

typedef struct WaylandWindow {
    PlatformWindow                    base;            /* first member */
    struct wl_surface                *surface;
    struct wp_viewport               *viewport;
    struct wp_fractional_scale_v1    *fractional;
    struct libdecor_frame            *frame;
    int32_t                           logical_width;   /* the size the viewport shows */
    int32_t                           logical_height;
    uint32_t                          fractional_scale;   /* numerator over 120; 0 until the event */
    int32_t                           preferred_scale;    /* wl_surface.preferred_buffer_scale; 0 until the event */
    uint32_t                          entered_outputs;    /* bit i: outputs[i] */
    bool                              suspended;
    bool                              configured;
} WaylandWindow;

/* ---- Dispatch and loss (Decision 4, Decision 6) ----------------------------- */

/* The dispatch of [4] without libdecor, for the setup phase before a libdecor
 * context exists. Returns >= 0 on success (the count is not meaningful to the
 * callers) or -errno. */
static int wayland_dispatch(struct wl_display *display, int timeout_ms) {
    int count = 0;
    while (wl_display_prepare_read(display) != 0) {
        if (wl_display_dispatch_pending(display) < 0) {
            return -errno;   /* deliberate deviation from [4]: no spin after a fatal error */
        }
    }
    if (wl_display_flush(display) < 0 && errno != EAGAIN) {
        int error = errno;
        wl_display_cancel_read(display);
        return -error;
    }
    struct pollfd entry = {.fd = wl_display_get_fd(display), .events = POLLIN, .revents = 0};
    int ready = poll(&entry, 1, timeout_ms);
    if (ready < 0) {
        int error = errno;
        wl_display_cancel_read(display);
        return -error;
    }
    if (ready > 0 && (entry.revents & POLLIN)) {
        (void)wl_display_read_events(display);   /* a failure is the display error, read below */
        count += wl_display_dispatch_pending(display);
    } else {
        wl_display_cancel_read(display);
    }
    return count;
}

/* Decision 6: EINTR and EAGAIN are retried or ignored and are not loss; the
 * display error ([1]) or any other errno is. Safe to call after every dispatch.
 * Returns whether the Platform is now lost. */
static bool wayland_check_dispatch(WaylandPlatform *wl, int result) {
    if (wl->base.lost) {
        return true;
    }
    char detail[128];
    int error = wl_display_get_error(wl->display);
    if (error == EPROTO) {
        const struct wl_interface *interface = NULL;
        uint32_t id = 0;
        uint32_t code = wl_display_get_protocol_error(wl->display, &interface, &id);
        (void)snprintf(detail, sizeof detail, "protocol error %u on %s, object %u", (unsigned)code,
                       interface ? interface->name : "unknown interface", (unsigned)id);
    } else if (error != 0) {
        (void)snprintf(detail, sizeof detail, "display error, errno %d", error);
    } else if (result < 0 && result != -EINTR && result != -EAGAIN) {
        (void)snprintf(detail, sizeof detail, "dispatch failed, errno %d", -result);   /* [3]: includes EPIPE */
    } else {
        return false;
    }
    platform_linux_enter_lost(&wl->base, detail);
    return true;
}

/* Waits for the server to process everything sent so far, bounded by `deadline`
 * (a roundtrip with no bound would hang on a compositor that never answers).
 * The callback is destroyed by its done event, or here when it never comes. */
static void wayland_sync_done(void *data, struct wl_callback *callback, uint32_t serial) {
    (void)serial;
    *(bool *)data = true;
    wl_callback_destroy(callback);
}

static bool wayland_roundtrip(struct wl_display *display, uint64_t deadline) {
    static const struct wl_callback_listener listener = {.done = wayland_sync_done};
    bool done = false;
    struct wl_callback *callback = wl_display_sync(display);
    if (!callback) {
        return false;
    }
    wl_callback_add_listener(callback, &listener, &done);
    while (!done && wl_display_get_error(display) == 0) {
        uint64_t now = platform_time_now_ns();
        if (now >= deadline) {
            break;
        }
        int result = wayland_dispatch(display, platform_linux_timeout_ms(deadline - now));
        if (result < 0 && result != -EINTR && result != -EAGAIN) {
            break;
        }
    }
    if (!done) {
        wl_callback_destroy(callback);
    }
    return done;
}

/* ---- Registry and outputs (Decision 1, Decision 3) --------------------------- */

static void wayland_apply_size(WaylandWindow *window);

static WaylandOutput *wayland_find_output(WaylandGlobals *globals, const struct wl_output *output) {
    for (size_t i = 0; i < WAYLAND_MAX_OUTPUTS; i++) {
        if (globals->outputs[i].output && globals->outputs[i].output == output) {
            return &globals->outputs[i];
        }
    }
    return NULL;
}

static void wayland_resize_all(WaylandGlobals *globals) {
    if (!globals->owner) {
        return;   /* the probe has no windows */
    }
    for (PlatformWindow *window = globals->owner->base.windows; window; window = window->next) {
        wayland_apply_size((WaylandWindow *)window);
    }
}

static void wayland_output_geometry(void *data, struct wl_output *output, int32_t x, int32_t y, int32_t width_mm,
                                    int32_t height_mm, int32_t subpixel, const char *make, const char *model,
                                    int32_t transform) {
    (void)data; (void)output; (void)x; (void)y; (void)width_mm; (void)height_mm;
    (void)subpixel; (void)make; (void)model; (void)transform;
}

static void wayland_output_mode(void *data, struct wl_output *output, uint32_t flags, int32_t width, int32_t height,
                                int32_t refresh) {
    (void)data; (void)output; (void)flags; (void)width; (void)height; (void)refresh;
}

static void wayland_output_done(void *data, struct wl_output *output) {
    (void)data; (void)output;
}

static void wayland_output_scale(void *data, struct wl_output *output, int32_t factor) {
    WaylandGlobals *globals = data;
    WaylandOutput *entry = wayland_find_output(globals, output);
    if (entry && factor > 0) {
        entry->scale = factor;
        wayland_resize_all(globals);
    }
}

static const struct wl_output_listener wayland_output_listener = {
    .geometry = wayland_output_geometry,
    .mode = wayland_output_mode,
    .done = wayland_output_done,
    .scale = wayland_output_scale,
};

static uint32_t wayland_min_u32(uint32_t a, uint32_t b) {
    return a < b ? a : b;
}

static void wayland_registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface,
                                    uint32_t version) {
    WaylandGlobals *globals = data;
    if (strcmp(interface, "wl_compositor") == 0 && !globals->compositor) {
        globals->compositor = wl_registry_bind(registry, name, &wl_compositor_interface,
                                               wayland_min_u32(version, WAYLAND_COMPOSITOR_BIND_VERSION));
    } else if (strcmp(interface, "wl_shm") == 0 && !globals->shm) {
        globals->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, "wp_viewporter") == 0 && !globals->viewporter) {
        globals->viewporter = wl_registry_bind(registry, name, &wp_viewporter_interface, 1);
    } else if (strcmp(interface, "wp_fractional_scale_manager_v1") == 0 && !globals->fractional_manager) {
        globals->fractional_manager = wl_registry_bind(registry, name, &wp_fractional_scale_manager_v1_interface, 1);
    } else if (strcmp(interface, "xdg_wm_base") == 0) {
        globals->xdg_wm_base_version = version;   /* libdecor binds it on its own registry */
    } else if (strcmp(interface, "wl_output") == 0) {
        for (size_t i = 0; i < WAYLAND_MAX_OUTPUTS; i++) {
            if (!globals->outputs[i].output) {
                globals->outputs[i].output = wl_registry_bind(registry, name, &wl_output_interface,
                                                              wayland_min_u32(version, WAYLAND_OUTPUT_BIND_VERSION));
                globals->outputs[i].name = name;
                globals->outputs[i].scale = 1;
                wl_output_add_listener(globals->outputs[i].output, &wayland_output_listener, globals);
                break;
            }
        }
    }
}

static void wayland_registry_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    (void)registry;
    WaylandGlobals *globals = data;
    for (size_t i = 0; i < WAYLAND_MAX_OUTPUTS; i++) {
        WaylandOutput *entry = &globals->outputs[i];
        if (entry->output && entry->name == name) {
            if (globals->owner) {
                for (PlatformWindow *window = globals->owner->base.windows; window; window = window->next) {
                    ((WaylandWindow *)window)->entered_outputs &= ~(1u << i);
                }
            }
            wl_output_destroy(entry->output);   /* bound below version 3: no release request */
            memset(entry, 0, sizeof *entry);
            wayland_resize_all(globals);
            return;
        }
    }
}

static const struct wl_registry_listener wayland_registry_listener = {
    .global = wayland_registry_global,
    .global_remove = wayland_registry_global_remove,
};

/* Binds the globals of Decision 1 and the outputs, bounded. Two round-trips: the
 * first delivers the registry, the second the initial events of what it bound. */
static bool wayland_bind_globals(struct wl_display *display, WaylandGlobals *globals) {
    uint64_t deadline = platform_time_now_ns() + WAYLAND_SETUP_TIMEOUT_NS;
    globals->registry = wl_display_get_registry(display);
    if (!globals->registry) {
        return false;
    }
    wl_registry_add_listener(globals->registry, &wayland_registry_listener, globals);
    return wayland_roundtrip(display, deadline) && wayland_roundtrip(display, deadline);
}

/* Client-side destruction only: no request that waits for the server. In the
 * lost state the requests are dropped locally or fail harmlessly ([2]). NULL-safe. */
static void wayland_release_globals(WaylandGlobals *globals) {
    for (size_t i = 0; i < WAYLAND_MAX_OUTPUTS; i++) {
        if (globals->outputs[i].output) {
            wl_output_destroy(globals->outputs[i].output);
        }
    }
    if (globals->fractional_manager) {
        wp_fractional_scale_manager_v1_destroy(globals->fractional_manager);
    }
    if (globals->viewporter) {
        wp_viewporter_destroy(globals->viewporter);
    }
    if (globals->shm) {
        wl_shm_destroy(globals->shm);
    }
    if (globals->compositor) {
        wl_compositor_destroy(globals->compositor);
    }
    if (globals->registry) {
        wl_registry_destroy(globals->registry);
    }
    memset(globals, 0, sizeof *globals);
}

/* The required globals of Decision 1 (xdg_wm_base is libdecor's requirement). */
static bool wayland_has_required(const WaylandGlobals *globals) {
    return globals->compositor && globals->shm && globals->viewporter && globals->xdg_wm_base_version != 0;
}

/* ---- Sizes (Decision 3) ------------------------------------------------------- */

/* Framebuffer scale as a numerator over 120: fractional-scale when received,
 * else preferred_buffer_scale, else the largest scale among the outputs the
 * surface entered, else 1. */
static uint32_t wayland_scale(const WaylandWindow *window) {
    if (window->fractional_scale != 0) {
        return window->fractional_scale;
    }
    int32_t scale = window->preferred_scale;
    if (scale <= 0) {
        scale = 1;
        const WaylandPlatform *wl = (const WaylandPlatform *)window->base.platform;
        for (size_t i = 0; i < WAYLAND_MAX_OUTPUTS; i++) {
            if ((window->entered_outputs & (1u << i)) && wl->globals.outputs[i].scale > scale) {
                scale = wl->globals.outputs[i].scale;
            }
        }
    }
    /* Clamp so the product cannot wrap; no real scale gets near it. */
    uint32_t maximum = UINT32_MAX / WAYLAND_FRACTIONAL_DENOMINATOR;
    return ((uint32_t)scale > maximum ? maximum : (uint32_t)scale) * WAYLAND_FRACTIONAL_DENOMINATOR;
}

/* PLATFORM-042: framebuffer = logical x scale, rounded; the buffer_scale stays 1
 * and the viewport destination is the logical size, so a buffer committed at a
 * stale size is never a protocol error. Reports minimized as 0 x 0. */
static void wayland_apply_size(WaylandWindow *window) {
    uint32_t scale = wayland_scale(window);
    uint64_t width = ((uint64_t)window->logical_width * scale + WAYLAND_FRACTIONAL_DENOMINATOR / 2) /
                     WAYLAND_FRACTIONAL_DENOMINATOR;
    uint64_t height = ((uint64_t)window->logical_height * scale + WAYLAND_FRACTIONAL_DENOMINATOR / 2) /
                      WAYLAND_FRACTIONAL_DENOMINATOR;
    if (width > UINT32_MAX) {
        width = UINT32_MAX;
    }
    if (height > UINT32_MAX) {
        height = UINT32_MAX;
    }
    ((WaylandPlatform *)window->base.platform)->activity++;
    platform_linux_set_window_state(&window->base, (uint32_t)width, (uint32_t)height, window->suspended);
}

/* ---- Window listeners --------------------------------------------------------- */

static void wayland_surface_enter(void *data, struct wl_surface *surface, struct wl_output *output) {
    (void)surface;
    WaylandWindow *window = data;
    WaylandPlatform *wl = (WaylandPlatform *)window->base.platform;
    const WaylandOutput *entry = wayland_find_output(&wl->globals, output);
    if (entry) {
        window->entered_outputs |= 1u << (unsigned)(entry - wl->globals.outputs);
        wayland_apply_size(window);
    }
}

static void wayland_surface_leave(void *data, struct wl_surface *surface, struct wl_output *output) {
    (void)surface;
    WaylandWindow *window = data;
    WaylandPlatform *wl = (WaylandPlatform *)window->base.platform;
    const WaylandOutput *entry = wayland_find_output(&wl->globals, output);
    if (entry) {
        window->entered_outputs &= ~(1u << (unsigned)(entry - wl->globals.outputs));
        wayland_apply_size(window);
    }
}

#ifdef WL_SURFACE_PREFERRED_BUFFER_SCALE_SINCE_VERSION
static void wayland_surface_preferred_buffer_scale(void *data, struct wl_surface *surface, int32_t factor) {
    (void)surface;
    WaylandWindow *window = data;
    window->preferred_scale = factor;
    wayland_apply_size(window);
}

static void wayland_surface_preferred_buffer_transform(void *data, struct wl_surface *surface, uint32_t transform) {
    (void)data; (void)surface; (void)transform;
}
#endif

static const struct wl_surface_listener wayland_surface_listener = {
    .enter = wayland_surface_enter,
    .leave = wayland_surface_leave,
#ifdef WL_SURFACE_PREFERRED_BUFFER_SCALE_SINCE_VERSION
    .preferred_buffer_scale = wayland_surface_preferred_buffer_scale,
    .preferred_buffer_transform = wayland_surface_preferred_buffer_transform,
#endif
};

static void wayland_fractional_preferred_scale(void *data, struct wp_fractional_scale_v1 *fractional, uint32_t scale) {
    (void)fractional;
    WaylandWindow *window = data;
    window->fractional_scale = scale;
    wayland_apply_size(window);
}

static const struct wp_fractional_scale_v1_listener wayland_fractional_listener = {
    .preferred_scale = wayland_fractional_preferred_scale,
};

/* PLATFORM-041, Decision 3: libdecor delivers the configure; commit acks it,
 * and the surface is committed afterwards. The first one also attaches the 1x1
 * placeholder so the toplevel maps before any GPU surface exists. */
static void wayland_frame_configure(struct libdecor_frame *frame, struct libdecor_configuration *configuration,
                                    void *user_data) {
    WaylandWindow *window = user_data;
    WaylandPlatform *wl = (WaylandPlatform *)window->base.platform;
    int width = 0;
    int height = 0;
    if (!libdecor_configuration_get_content_size(configuration, frame, &width, &height) || width <= 0 ||
        height <= 0) {
        width = window->logical_width;   /* [7]: no or zero size: the client chooses */
        height = window->logical_height;
    }
    enum libdecor_window_state state_flags;
    if (libdecor_configuration_get_window_state(configuration, &state_flags)) {
        window->suspended = (state_flags & LIBDECOR_WINDOW_STATE_SUSPENDED) != 0;
    }

    struct libdecor_state *state = libdecor_state_new(width, height);
    libdecor_frame_commit(frame, state, configuration);
    libdecor_state_free(state);

    if (width != window->logical_width || height != window->logical_height || !window->configured) {
        window->logical_width = width;
        window->logical_height = height;
        wp_viewport_set_destination(window->viewport, width, height);
    }
    if (!window->configured) {
        window->configured = true;
        wl_surface_attach(window->surface, wl->placeholder, 0, 0);
    }
    wl_surface_commit(window->surface);
    wayland_apply_size(window);
}

static void wayland_frame_close(struct libdecor_frame *frame, void *user_data) {
    (void)frame;
    WaylandWindow *window = user_data;
    ((WaylandPlatform *)window->base.platform)->activity++;
    platform_linux_request_close(&window->base);   /* never destroys (PLATFORM-043) */
}

static void wayland_frame_commit(struct libdecor_frame *frame, void *user_data) {
    (void)frame;
    wl_surface_commit(((WaylandWindow *)user_data)->surface);   /* decoration subsurfaces are synchronous */
}

static void wayland_frame_dismiss_popup(struct libdecor_frame *frame, const char *seat_name, void *user_data) {
    (void)frame; (void)seat_name; (void)user_data;
}

/* libdecor_interface.error carries no user data [5], so it cannot reach the
 * Platform. Both errors it reports are excluded before they can happen
 * (xdg_wm_base is probed; min <= max by construction). It must still be
 * non-NULL: libdecor calls it unconditionally. */
static void wayland_decor_error(struct libdecor *context, enum libdecor_error error, const char *message) {
    (void)context; (void)error; (void)message;
}

/* ---- Lifecycle (Decisions 1, 2, 6) --------------------------------------------- */

/* PLATFORM-033: the probe allocates nothing from an EngineAllocator. It proves
 * the connection works and the required globals exist, then lets go of its
 * registry; adopt binds again into the Platform. */
static EngineResult wayland_connect(const EngineLogger *logger, PlatformConnection *out_connection) {
    (void)logger;   /* ENGINE_LOG_WARN compiles away at high ENGINE_LOG_COMPILE_MIN_LEVEL */
    struct wl_display *display = wl_display_connect(NULL);   /* [8] */
    if (!display) {
        ENGINE_LOG_WARN(logger, "Platform", "Wayland: wl_display_connect failed (no compositor reachable)");
        return ENGINE_RESULT_NOT_SUPPORTED;
    }
    WaylandGlobals globals;
    memset(&globals, 0, sizeof globals);
    bool ok = wayland_bind_globals(display, &globals) && wayland_has_required(&globals);
    if (!ok) {
        ENGINE_LOG_WARN(logger, "Platform",
                        "Wayland: the compositor did not answer in time or lacks wl_compositor, xdg_wm_base, wl_shm or wp_viewporter");
    }
    wayland_release_globals(&globals);
    if (!ok) {
        wl_display_disconnect(display);
        return ENGINE_RESULT_NOT_SUPPORTED;
    }
    out_connection->display = (PlatformNativeObject *)display;
    return ENGINE_RESULT_SUCCESS;
}

static void wayland_disconnect(PlatformConnection *connection) {
    wl_display_disconnect((struct wl_display *)connection->display);
}

/* A 1x1 opaque wl_shm buffer: a zero-filled memfd is black, and the X of
 * XRGB8888 means the alpha is ignored. wl_shm_pool needs only the fd and the
 * size, so nothing is mapped. */
static struct wl_buffer *wayland_create_placeholder(struct wl_shm *shm) {
    int fd = memfd_create("nayla-placeholder", MFD_CLOEXEC);
    if (fd < 0) {
        return NULL;
    }
    struct wl_buffer *buffer = NULL;
    if (ftruncate(fd, 4) == 0) {
        struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, 4);
        if (pool) {
            buffer = wl_shm_pool_create_buffer(pool, 0, 1, 1, 4, WL_SHM_FORMAT_XRGB8888);
            wl_shm_pool_destroy(pool);
        }
    }
    close(fd);
    return buffer;
}

/* Destroys what adopt acquired, client side, whether or not the Platform is lost. */
static void wayland_release(WaylandPlatform *wl) {
    if (wl->decor) {
        libdecor_unref(wl->decor);
    }
    if (wl->placeholder) {
        wl_buffer_destroy(wl->placeholder);
    }
    wayland_release_globals(&wl->globals);
    wl->decor = NULL;
    wl->placeholder = NULL;
}

static EngineResult wayland_adopt(Platform *platform, PlatformConnection *connection) {
    WaylandPlatform *wl = (WaylandPlatform *)platform;
    wl->display = (struct wl_display *)connection->display;
    wl->globals.owner = wl;
    if (!wayland_bind_globals(wl->display, &wl->globals) || !wayland_has_required(&wl->globals)) {
        goto fail;
    }
    wl->placeholder = wayland_create_placeholder(wl->globals.shm);
    if (!wl->placeholder) {
        goto fail;
    }
    wl->decor_interface.error = wayland_decor_error;
    wl->frame_interface.configure = wayland_frame_configure;
    wl->frame_interface.close = wayland_frame_close;
    wl->frame_interface.commit = wayland_frame_commit;
    wl->frame_interface.dismiss_popup = wayland_frame_dismiss_popup;
    wl->decor = libdecor_new(wl->display, &wl->decor_interface);
    if (!wl->decor || wl_display_get_error(wl->display) != 0) {
        goto fail;
    }
    if (wl->globals.xdg_wm_base_version < WAYLAND_XDG_WM_BASE_SUSPENDED_VERSION) {
        /* Decision 3: without xdg-shell v6 a minimized window keeps its size. */
        ENGINE_LOG_INFO(platform->logger, "Platform",
                        "Wayland: xdg_wm_base is version %u, below %u: no `suspended` state, so a minimized window keeps its size",
                        (unsigned)wl->globals.xdg_wm_base_version, WAYLAND_XDG_WM_BASE_SUSPENDED_VERSION);
    }
    return ENGINE_RESULT_SUCCESS;
fail:
    ENGINE_LOG_WARN(platform->logger, "Platform", "Wayland: setting up the connection failed");
    wayland_release(wl);
    return ENGINE_RESULT_NOT_SUPPORTED;   /* the caller disconnects */
}

/* Safe in the lost state: with the display error set, libwayland drops the
 * destroy requests locally; after an EPIPE flush they are marshalled and may be
 * sent to the dead socket, which fails harmlessly ([2]). wl_display_disconnect
 * does no I/O. */
static void wayland_close(Platform *platform) {
    WaylandPlatform *wl = (WaylandPlatform *)platform;
    wayland_release(wl);
    wl_display_disconnect(wl->display);
}

/* ---- Windows (PLATFORM-041..044, Decision 3) ----------------------------------- */

/* Frame before surface ([7]); the requests are dropped locally when lost. NULL-safe,
 * so it also cleans a window whose open failed half way. */
static void wayland_window_release(WaylandWindow *window) {
    if (window->frame) {
        libdecor_frame_unref(window->frame);
    }
    if (window->fractional) {
        wp_fractional_scale_v1_destroy(window->fractional);
    }
    if (window->viewport) {
        wp_viewport_destroy(window->viewport);
    }
    if (window->surface) {
        wl_surface_destroy(window->surface);
    }
    window->frame = NULL;
    window->fractional = NULL;
    window->viewport = NULL;
    window->surface = NULL;
}

static EngineResult wayland_window_open(PlatformWindow *base, const PlatformWindowDesc *desc, const char *title) {
    WaylandWindow *window = (WaylandWindow *)base;
    WaylandPlatform *wl = (WaylandPlatform *)base->platform;

    /* Wayland sizes are int32_t. A backend limit is an OS failure (PLATFORM-034, -041). */
    if (desc->width > INT32_MAX || desc->height > INT32_MAX) {
        ENGINE_LOG_ERROR(base->platform->logger, "Platform",
                         "platform_window_create: %ux%u exceeds the Wayland limit of %u per axis",
                         (unsigned)desc->width, (unsigned)desc->height, (unsigned)INT32_MAX);
        return ENGINE_RESULT_NOT_SUPPORTED;
    }
    window->logical_width = (int32_t)desc->width;   /* requests are logical (PLATFORM-042) */
    window->logical_height = (int32_t)desc->height;

    window->surface = wl_compositor_create_surface(wl->globals.compositor);
    if (window->surface) {
        wl_surface_add_listener(window->surface, &wayland_surface_listener, window);
        window->viewport = wp_viewporter_get_viewport(wl->globals.viewporter, window->surface);
        if (wl->globals.fractional_manager) {
            window->fractional = wp_fractional_scale_manager_v1_get_fractional_scale(wl->globals.fractional_manager,
                                                                                    window->surface);
            if (window->fractional) {
                wp_fractional_scale_v1_add_listener(window->fractional, &wayland_fractional_listener, window);
            }
        }
        window->frame = libdecor_decorate(wl->decor, window->surface, &wl->frame_interface, window);
    }
    if (!window->frame || !window->viewport) {
        ENGINE_LOG_ERROR(base->platform->logger, "Platform", "platform_window_create: creating the Wayland surface failed");
        wayland_window_release(window);
        return ENGINE_RESULT_NOT_SUPPORTED;
    }
    libdecor_frame_set_title(window->frame, title);
    if (!(desc->flags & PLATFORM_WINDOW_RESIZABLE)) {
        libdecor_frame_set_min_content_size(window->frame, window->logical_width, window->logical_height);
        libdecor_frame_set_max_content_size(window->frame, window->logical_width, window->logical_height);
        libdecor_frame_unset_capabilities(window->frame, LIBDECOR_ACTION_RESIZE);
    }
    libdecor_frame_map(window->frame);

    /* PLATFORM-041: wait (bounded) for the first configure, which the listener
     * acks and answers with the placeholder commit. Events delivered meanwhile
     * are absorbed (`creating`). A loss is seen after each dispatch. */
    uint64_t deadline = platform_time_now_ns() + WAYLAND_SETUP_TIMEOUT_NS;
    while (!window->configured) {
        uint64_t now = platform_time_now_ns();
        bool lost = wayland_check_dispatch(wl, 0);   /* a loss may already be recorded */
        if (!lost && now < deadline) {
            lost = wayland_check_dispatch(wl, libdecor_dispatch(wl->decor, platform_linux_timeout_ms(deadline - now)));
        }
        if (lost || (!window->configured && platform_time_now_ns() >= deadline)) {
            ENGINE_LOG_ERROR(base->platform->logger, "Platform",
                             "platform_window_create: the window was not configured in time or the connection was lost");
            wayland_window_release(window);
            return ENGINE_RESULT_NOT_SUPPORTED;
        }
    }
    (void)wl_display_flush(wl->display);
    return ENGINE_RESULT_SUCCESS;
}

static void wayland_window_close(PlatformWindow *base) {
    WaylandPlatform *wl = (WaylandPlatform *)base->platform;
    wayland_window_release((WaylandWindow *)base);
    if (!wl->base.lost) {
        (void)wl_display_flush(wl->display);
    }
}

static EngineResult wayland_get_native(const PlatformWindow *base, PlatformNativeWindow *out_native) {
    /* WAYLAND row (PLATFORM-071): wl_display * and wl_surface *; both stay owned by the platform. */
    out_native->type = PLATFORM_NATIVE_WINDOW_WAYLAND;
    out_native->display = (PlatformNativeObject *)((const WaylandPlatform *)base->platform)->display;
    out_native->window = (PlatformNativeObject *)((const WaylandWindow *)base)->surface;
    out_native->window_id = 0;
    return ENGINE_RESULT_SUCCESS;
}

/* ---- Pump and wait (Decision 4) -------------------------------------------------- */

static void wayland_pump(Platform *platform) {
    WaylandPlatform *wl = (WaylandPlatform *)platform;
    if (wayland_check_dispatch(wl, 0)) {   /* a WSI thread may have set the display error; see [4] */
        return;
    }
    (void)wayland_check_dispatch(wl, libdecor_dispatch(wl->decor, 0));
}

/* Pump first: libdecor's dispatch drains what is queued and then polls for the
 * full timeout, so an event it just queued (a CLOSE_REQUESTED) would sit unseen
 * for a whole wait (PLATFORM-055). Block only if that pump queued nothing and
 * changed no window state. Rounding and clamping: platform_linux_timeout_ms
 * (PLATFORM-056). -EINTR is an early return (PLATFORM-055). */
static void wayland_wait(Platform *platform, uint64_t timeout_ns) {
    WaylandPlatform *wl = (WaylandPlatform *)platform;
    uint32_t activity = wl->activity;
    wayland_pump(platform);
    if (wl->base.lost || wl->activity != activity) {
        return;
    }
    if (wayland_check_dispatch(wl, 0)) {
        return;
    }
    if (!wayland_check_dispatch(wl, libdecor_dispatch(wl->decor, platform_linux_timeout_ms(timeout_ns)))) {
        wayland_pump(platform);   /* never in the lost state: see [4] */
    }
}

const PlatformOps platform_wayland_ops = {
    .name = "Wayland",
    .platform_size = sizeof(WaylandPlatform),
    .window_size = sizeof(WaylandWindow),
    .connect = wayland_connect,
    .disconnect = wayland_disconnect,
    .adopt = wayland_adopt,
    .close = wayland_close,
    .window_open = wayland_window_open,
    .window_close = wayland_window_close,
    .get_native = wayland_get_native,
    .pump = wayland_pump,
    .wait = wayland_wait,
};
