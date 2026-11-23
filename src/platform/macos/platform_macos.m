/* macOS platform backend: lifecycle, windows, events and the Cocoa native
 * handle (PLATFORM-030..054, -070..074, -080..083). Objective-C with manual
 * retain/release (ADR-011, -fno-objc-arc). Every Platform API function that
 * calls AppKit wraps those calls in @autoreleasepool, and destroy/shutdown end
 * the pool before the struct goes back to its allocator (PLATFORM-044).
 *
 * No file-scope mutable state (PLATFORM-090): per-window lookup goes through
 * each window's own delegate, and the Platform owns the window list. */

#import <AppKit/AppKit.h>
#include <math.h>

#include "src/platform/api.h"
#include "src/platform/native.h"
#include "src/platform/common/platform_common.h"

@class NaylaAppDelegate;
@class NaylaWindowDelegate;

struct Platform {
    const EngineAllocator *allocator;   /* resolved once at init (PLATFORM-031) */
    const EngineLogger    *logger;      /* stored unchanged (PLATFORM-032) */
    NaylaAppDelegate      *app_delegate; /* +1, owned (ADR-011 Decision 4) */
    PlatformEventQueue     queue;       /* fixed array, allocated at init (PLATFORM-052) */
    PlatformWindow        *windows;     /* live windows, doubly linked */
};

struct PlatformWindow {
    const EngineAllocator *allocator;   /* resolved once at create (PLATFORM-031) */
    Platform              *platform;    /* non-owning */
    NSWindow              *ns_window;   /* +1, owned */
    NaylaWindowDelegate   *delegate;    /* +1, owned */
    NSView                *view;        /* content view, owned by ns_window */
    PlatformWindow        *prev;
    PlatformWindow        *next;
    uint32_t               reported_width;   /* last pixel size seen (PLATFORM-042) */
    uint32_t               reported_height;
    bool                   minimized;   /* tracked here: AppKit keeps the view's bounds */
    bool                   creating;    /* absorbs creation notifications (PLATFORM-041) */
    bool                   should_close; /* sticky (PLATFORM-043) */
};

/* ---- Delegates (ADR-011 Decision 4) -------------------------------------
 * Each holds a non-owning back pointer. A callback with a NULL back pointer
 * (detached during destroy or shutdown) does nothing. */

@interface NaylaAppDelegate : NSObject <NSApplicationDelegate> {
    Platform *_platform;
}
- (instancetype)initWithPlatform:(Platform *)platform;
- (void)detach;
@end

@interface NaylaWindowDelegate : NSObject <NSWindowDelegate> {
    PlatformWindow *_window;
}
- (instancetype)initWithWindow:(PlatformWindow *)window;
- (void)detach;
@end

/* ---- Window state (PLATFORM-042, PLATFORM-043) --------------------------- */

/* Physical pixels: the content view's bounds converted through its backing
 * scale. A minimized or otherwise zero-area window reports 0 x 0 on both axes
 * (PLATFORM-042), never e.g. 640 x 0. */
static void window_pixel_size(const PlatformWindow *window, uint32_t *out_width, uint32_t *out_height) {
    uint32_t width = 0;
    uint32_t height = 0;
    if (!window->minimized) {
        NSRect pixels;
        @autoreleasepool {
            pixels = [window->view convertRectToBacking:[window->view bounds]];
        }
        width = pixels.size.width > 0 ? (uint32_t)lround(pixels.size.width) : 0;
        height = pixels.size.height > 0 ? (uint32_t)lround(pixels.size.height) : 0;
        if (width == 0 || height == 0) {
            width = 0;
            height = 0;
        }
    }
    *out_width = width;
    *out_height = height;
}

/* PLATFORM-042, PLATFORM-052: any change in the reported pixel size queues one
 * coalesced FRAMEBUFFER_RESIZED. While the window is creating, the size is
 * recorded but not queued (PLATFORM-041). */
static void window_sync_size(PlatformWindow *window) {
    uint32_t width = 0;
    uint32_t height = 0;
    window_pixel_size(window, &width, &height);
    if (width == window->reported_width && height == window->reported_height) {
        return;
    }
    window->reported_width = width;
    window->reported_height = height;
    if (!window->creating) {
        platform_queue_push(&window->platform->queue, window->platform->logger, window, PLATFORM_EVENT_WINDOW_FRAMEBUFFER_RESIZED, width, height);
    }
}

/* PLATFORM-043: the sticky flag is set once; CLOSE_REQUESTED is queued the first time only. */
static void window_request_close(PlatformWindow *window) {
    if (window->should_close) {
        return;
    }
    window->should_close = true;
    platform_queue_push(&window->platform->queue, window->platform->logger, window, PLATFORM_EVENT_WINDOW_CLOSE_REQUESTED, 0, 0);
}

/* Releases the OS objects of a window in PLATFORM-044 order: the back pointer
 * first, then the delegate is detached from the window, the window closed and
 * released, and the delegate released. Callers run it inside an @autoreleasepool
 * that ends before the struct is freed. Nil arguments are no-ops. */
static void window_release_os(NaylaWindowDelegate *delegate, NSWindow *ns_window) {
    [delegate detach];
    [ns_window setDelegate:nil];
    [ns_window close];
    [ns_window release];
    [delegate release];
}

static void install_main_menu(void) {
    /* Cmd+Q quits the application (PLATFORM-043); Cmd+W closes the key window. */
    NSMenu *bar = [[[NSMenu alloc] init] autorelease];

    NSMenuItem *app_item = [bar addItemWithTitle:@"" action:nil keyEquivalent:@""];
    NSMenu *app_menu = [[[NSMenu alloc] init] autorelease];
    [app_menu addItemWithTitle:@"Quit" action:@selector(terminate:) keyEquivalent:@"q"];
    [app_item setSubmenu:app_menu];

    /* Only the first (application) menu gets an automatic title; this one needs
     * its own, or Close Window has no visible menu-bar entry. */
    NSMenuItem *window_item = [bar addItemWithTitle:@"Window" action:nil keyEquivalent:@""];
    NSMenu *window_menu = [[[NSMenu alloc] initWithTitle:@"Window"] autorelease];
    [window_menu addItemWithTitle:@"Close Window" action:@selector(performClose:) keyEquivalent:@"w"];
    [window_item setSubmenu:window_menu];

    [NSApp setMainMenu:bar];
}

/* ---- Delegate implementations ------------------------------------------- */

@implementation NaylaAppDelegate

- (instancetype)initWithPlatform:(Platform *)platform {
    self = [super init];
    if (self) {
        _platform = platform;
    }
    return self;
}

- (void)detach {
    _platform = NULL;
}

/* Cmd+Q and the Quit menu (PLATFORM-043): every live window is marked, and the
 * quit itself is cancelled, so the application decides when to close. */
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
    if (_platform) {
        for (PlatformWindow *window = _platform->windows; window; window = window->next) {
            window_request_close(window);
        }
    }
    /* PLATFORM-043: always cancel, so the application decides when to exit. This
     * also defers logout and system shutdown until the app destroys its windows
     * and quits. */
    return NSTerminateCancel;
}

@end

@implementation NaylaWindowDelegate

- (instancetype)initWithWindow:(PlatformWindow *)window {
    self = [super init];
    if (self) {
        _window = window;
    }
    return self;
}

- (void)detach {
    _window = NULL;
}

/* The close button and Cmd+W never destroy (PLATFORM-043): they only request. */
- (BOOL)windowShouldClose:(NSWindow *)sender {
    if (_window) {
        window_request_close(_window);
    }
    return NO;
}

- (void)windowDidResize:(NSNotification *)notification {
    if (_window) {
        window_sync_size(_window);
    }
}

- (void)windowDidChangeBackingProperties:(NSNotification *)notification {
    if (_window) {
        window_sync_size(_window);
    }
}

- (void)windowDidChangeScreen:(NSNotification *)notification {
    if (_window) {
        window_sync_size(_window);
    }
}

- (void)windowDidMiniaturize:(NSNotification *)notification {
    if (_window) {
        _window->minimized = true;
        window_sync_size(_window);
    }
}

- (void)windowDidDeminiaturize:(NSNotification *)notification {
    if (_window) {
        _window->minimized = false;
        window_sync_size(_window);
    }
}

@end

/* ---- Lifecycle (PLATFORM-033..036) ------------------------------------- */

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

    /* PLATFORM-033: probe the window system before anything is allocated. Without
     * a display, AppKit has no screen to put a window on. */
    @autoreleasepool {
        [NSApplication sharedApplication];
        if ([[NSScreen screens] count] == 0) {
            ENGINE_LOG_ERROR(logger, "Platform", "platform_init: no display is reachable (NSScreen reports none)");
            return ENGINE_RESULT_NOT_SUPPORTED;
        }
    }

    Platform *platform = engine_alloc(resolved, sizeof *platform, _Alignof(Platform));
    if (!platform) {
        return ENGINE_RESULT_OUT_OF_MEMORY;
    }
    if (platform_queue_init(&platform->queue, resolved) != ENGINE_RESULT_SUCCESS) {
        engine_free(resolved, platform);
        return ENGINE_RESULT_OUT_OF_MEMORY;
    }
    platform->allocator = resolved;
    platform->logger = logger;
    platform->app_delegate = nil;
    platform->windows = NULL;

    @autoreleasepool {
        /* PLATFORM-036: this runs on every init. Both calls are harmless when repeated
         * on the persistent NSApp singleton, so re-init needs no once-only guard.
         * The BOOL from setActivationPolicy: is intentionally ignored: a refusal
         * leaves the default policy, which still lets the windows be shown. */
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp finishLaunching];
        platform->app_delegate = [[NaylaAppDelegate alloc] initWithPlatform:platform];
        if (!platform->app_delegate) {
            platform_queue_destroy(&platform->queue, resolved);
            engine_free(resolved, platform);
            ENGINE_LOG_ERROR(logger, "Platform", "platform_init: NaylaAppDelegate allocation returned nil (AppKit gives no error detail, PLATFORM-034)");
            return ENGINE_RESULT_NOT_SUPPORTED;
        }
        /* NSApp does not retain its delegate; the +1 from alloc belongs to the Platform. */
        [NSApp setDelegate:platform->app_delegate];
        install_main_menu();
    }

    *out_platform = platform;
    return ENGINE_RESULT_SUCCESS;
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
    @autoreleasepool {
        [platform->app_delegate detach];
        [NSApp setDelegate:nil];
        [NSApp setMainMenu:nil];
        [platform->app_delegate release];
        platform->app_delegate = nil;
    }
    /* The pool above has ended, so the OS objects are gone before the structs (PLATFORM-044). */
    platform_queue_destroy(&platform->queue, allocator);
    engine_free(allocator, platform);
}

/* ---- Windows (PLATFORM-040..044) --------------------------------------- */

EngineResult platform_window_create(PlatformWindow **out_window,
                                    Platform *platform,
                                    const EngineAllocator *allocator,
                                    const PlatformWindowDesc *desc) {
    const EngineAllocator *resolved = NULL;
    EngineResult checked = platform_window_create_check(out_window, platform, allocator, desc, &resolved);
    if (checked != ENGINE_RESULT_SUCCESS) {
        return checked;
    }
    @autoreleasepool {
        NSWindowStyleMask mask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable;
        if (desc->flags & PLATFORM_WINDOW_RESIZABLE) {
            mask |= NSWindowStyleMaskResizable;
        }
        /* Width and height are logical units (PLATFORM-042). */
        NSRect content = NSMakeRect(0, 0, desc->width, desc->height);
        NSWindow *ns_window = [[NSWindow alloc] initWithContentRect:content
                                                          styleMask:mask
                                                            backing:NSBackingStoreBuffered
                                                              defer:NO];
        if (!ns_window) {
            ENGINE_LOG_ERROR(platform->logger, "Platform", "platform_window_create: NSWindow initialisation returned nil (AppKit gives no error detail, PLATFORM-034)");
            return ENGINE_RESULT_NOT_SUPPORTED;
        }
        [ns_window setReleasedWhenClosed:NO];

        PlatformWindow *window = engine_alloc(resolved, sizeof *window, _Alignof(PlatformWindow));
        if (!window) {
            window_release_os(nil, ns_window);
            return ENGINE_RESULT_OUT_OF_MEMORY;
        }
        window->allocator = resolved;
        window->platform = platform;
        window->ns_window = ns_window;
        window->delegate = nil;
        window->view = [ns_window contentView];
        window->prev = NULL;
        window->next = NULL;
        window->reported_width = 0;
        window->reported_height = 0;
        window->minimized = false;
        window->creating = true;
        window->should_close = false;

        window->delegate = [[NaylaWindowDelegate alloc] initWithWindow:window];
        if (!window->delegate) {
            window_release_os(nil, ns_window);
            engine_free(resolved, window);
            ENGINE_LOG_ERROR(platform->logger, "Platform", "platform_window_create: NaylaWindowDelegate allocation returned nil (AppKit gives no error detail, PLATFORM-034)");
            return ENGINE_RESULT_NOT_SUPPORTED;
        }

        /* PLATFORM-041: the title is copied by NSString. Invalid UTF-8 is replaced
         * with U+FFFD rather than failing the call. */
        NSString *title = [NSString stringWithUTF8String:desc->title];
        [ns_window setTitle:title ? title : @"\uFFFD"];
        [ns_window setDelegate:window->delegate];
        [ns_window center];
        window_sync_size(window);
        [ns_window makeKeyAndOrderFront:nil];
        window_sync_size(window);
        window->creating = false;

        window->next = platform->windows;
        if (platform->windows) {
            platform->windows->prev = window;
        }
        platform->windows = window;
        *out_window = window;
    }
    return ENGINE_RESULT_SUCCESS;
}

void platform_window_destroy(PlatformWindow *window) {
    if (!window) {
        return;
    }
    Platform *platform = window->platform;
    const EngineAllocator *allocator = window->allocator;

    @autoreleasepool {
        window_release_os(window->delegate, window->ns_window);
    }

    if (window->prev) {
        window->prev->next = window->next;
    } else {
        platform->windows = window->next;
    }
    if (window->next) {
        window->next->prev = window->prev;
    }
    platform_queue_purge_window(&platform->queue, window);
    /* The pool above has ended, so the OS objects are gone before the struct (PLATFORM-044). */
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
    window_pixel_size(window, out_width, out_height);
    return ENGINE_RESULT_SUCCESS;
}

/* ---- Native handle (PLATFORM-070..073) --------------------------------- */

EngineResult platform_window_get_native(const PlatformWindow *window,
                                        PlatformNativeWindow *out_native) {
    if (!window || !out_native) {
        return ENGINE_RESULT_INVALID_ARGUMENT;
    }
    /* COCOA row (PLATFORM-071): the content view, no display, no window id. The
     * view stays owned by the window; this is a borrowed pointer (PLATFORM-072). */
    out_native->type = PLATFORM_NATIVE_WINDOW_COCOA;
    out_native->display = NULL;
    out_native->window = (PlatformNativeObject *)window->view;
    out_native->window_id = 0;
    return ENGINE_RESULT_SUCCESS;
}

/* ---- Events (PLATFORM-050..053) ---------------------------------------- */

void platform_pump_events(Platform *platform) {
    if (!platform) {
        return;
    }
    @autoreleasepool {
        /* distantPast: return at once when nothing is pending (PLATFORM-050). */
        NSEvent *event;
        while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                         untilDate:[NSDate distantPast]
                                            inMode:NSDefaultRunLoopMode
                                           dequeue:YES])) {
            [NSApp sendEvent:event];
        }
        [NSApp updateWindows];
        /* Catch size changes whose notification has not arrived yet. */
        for (PlatformWindow *window = platform->windows; window; window = window->next) {
            window_sync_size(window);
        }
    }
}

/* PLATFORM-055/056. The blocking phase is one untilDate wait. NSDate holds
 * seconds since 2001 as a double (~1e-7 s resolution today), so a timeout below
 * that would round to "now" and turn the wait into a pump: a non-zero timeout
 * is rounded up to 1 us (PLATFORM-056). UINT64_MAX means no limit. */
#define WAIT_MIN_NS ((uint64_t)1000u)

void platform_wait_events(Platform *platform, uint64_t timeout_ns) {
    if (!platform) {
        return;
    }
    if (timeout_ns != 0) {
        uint64_t wait_ns = timeout_ns < WAIT_MIN_NS ? WAIT_MIN_NS : timeout_ns;
        @autoreleasepool {
            NSDate *until = (timeout_ns == UINT64_MAX)
                ? [NSDate distantFuture]
                : [NSDate dateWithTimeIntervalSinceNow:(double)wait_ns / 1e9];
            NSEvent *event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                untilDate:until
                                                   inMode:NSDefaultRunLoopMode
                                                  dequeue:YES];
            if (event) {
                [NSApp sendEvent:event];
            }
        }
    }
    /* Timeout 0 is exactly the pump; otherwise the pump drains what arrived
     * and runs the size sync (PLATFORM-055). */
    platform_pump_events(platform);
}

bool platform_next_event(Platform *platform, PlatformEvent *out_event) {
    return platform && platform_queue_pop(&platform->queue, out_event);
}
