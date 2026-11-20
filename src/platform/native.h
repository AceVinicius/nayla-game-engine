#ifndef PLATFORM_NATIVE_H
#define PLATFORM_NATIVE_H

/* Native-handle boundary (PLATFORM-070..074). Included ONLY by platform
 * backends, GPU backends' surface files, and tests/platform/ (PLAT-INV-2).
 * OS and SDK types appear only in the comment table below; the declarations
 * use opaque engine-defined types.
 *
 *   type            display            window                 window_id
 *   WIN32           HINSTANCE          HWND                   0
 *   COCOA           NULL               content NSView *       0
 *   X11             Display *          NULL                   X11 Window (XID)
 *   WAYLAND         struct wl_display *  struct wl_surface *    0
 */

#include <stdint.h>

#include "src/platform/api.h"

/* PLATFORM-070: incomplete type, never defined anywhere. */
typedef struct PlatformNativeObject PlatformNativeObject;

typedef enum PlatformNativeWindowType {
    PLATFORM_NATIVE_WINDOW_NONE = 0,
    PLATFORM_NATIVE_WINDOW_WIN32,     /* 1 */
    PLATFORM_NATIVE_WINDOW_COCOA,     /* 2 */
    PLATFORM_NATIVE_WINDOW_X11,       /* 3 */
    PLATFORM_NATIVE_WINDOW_WAYLAND    /* 4 */
} PlatformNativeWindowType;

/* PLAT-INV-1: fixed values (PLATFORM-071). */
_Static_assert(PLATFORM_NATIVE_WINDOW_NONE == 0, "PLAT-INV-1: PLATFORM_NATIVE_WINDOW_NONE must be 0");
_Static_assert(PLATFORM_NATIVE_WINDOW_WIN32 == 1, "PLAT-INV-1: PLATFORM_NATIVE_WINDOW_WIN32 must be 1");
_Static_assert(PLATFORM_NATIVE_WINDOW_COCOA == 2, "PLAT-INV-1: PLATFORM_NATIVE_WINDOW_COCOA must be 2");
_Static_assert(PLATFORM_NATIVE_WINDOW_X11 == 3, "PLAT-INV-1: PLATFORM_NATIVE_WINDOW_X11 must be 3");
_Static_assert(PLATFORM_NATIVE_WINDOW_WAYLAND == 4, "PLAT-INV-1: PLATFORM_NATIVE_WINDOW_WAYLAND must be 4");

typedef struct PlatformNativeWindow {
    int32_t               type;       /* a PlatformNativeWindowType */
    PlatformNativeObject *display;    /* see the table above */
    PlatformNativeObject *window;     /* see the table above */
    uint64_t              window_id;  /* see the table above */
} PlatformNativeWindow;

/* PLATFORM-073: stable for the life of the window; valid until
 * platform_window_destroy begins. */
ENGINE_NODISCARD EngineResult platform_window_get_native(const PlatformWindow *window,
                                                         PlatformNativeWindow *out_native);

#endif /* PLATFORM_NATIVE_H */
