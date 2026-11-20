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
#include <string.h>

#include "src/platform/api.h"
#include "src/platform/native.h"
#include "src/platform/common/platform_common.h"

/* PLATFORM-052: a fixed capacity; the queue is allocated once at platform_init. */
#define NAYLA_EVENT_QUEUE_CAPACITY 64u

@class NaylaAppDelegate;
@class NaylaWindowDelegate;

struct Platform {
    const EngineAllocator *allocator;   /* resolved once at init (PLATFORM-031) */
    const EngineLogger    *logger;      /* stored unchanged (PLATFORM-032) */
    NaylaAppDelegate      *app_delegate; /* +1, owned (ADR-011 Decision 4) */
    PlatformEvent         *queue;       /* fixed array, allocated at init (PLATFORM-052) */
    uint32_t               queue_count;
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

/* ---- Event queue (PLATFORM-051..053) ------------------------------------ */

/* PLATFORM-052: a newer FRAMEBUFFER_RESIZED updates the queued one in place.
 * A full queue drops the event with a WARN; state queries stay correct. */
static void queue_push(Platform *platform, PlatformWindow *window, int32_t type,
                       uint32_t width, uint32_t height) {
    if (type == PLATFORM_EVENT_WINDOW_FRAMEBUFFER_RESIZED) {
        for (uint32_t i = 0; i < platform->queue_count; i++) {
            PlatformEvent *queued = &platform->queue[i];
            if (queued->window == window && queued->type == type) {
                queued->width = width;
                queued->height = height;
                return;
            }
        }
    }
    if (platform->queue_count == NAYLA_EVENT_QUEUE_CAPACITY) {
        ENGINE_LOG_WARN(platform->logger, "Platform", "event queue full: dropped a window event (PLATFORM-052)");
        return;
    }
    platform->queue[platform->queue_count++] = (PlatformEvent){type, window, width, height};
}

/* PLATFORM-044 / PLATFORM-053: removes every queued event for `window`, keeping order. */
static void queue_purge_window(Platform *platform, const PlatformWindow *window) {
    uint32_t kept = 0;
    for (uint32_t i = 0; i < platform->queue_count; i++) {
        if (platform->queue[i].window != window) {
            platform->queue[kept++] = platform->queue[i];
        }
    }
    platform->queue_count = kept;
}

/* ---- Window state (PLATFORM-042, PLATFORM-043) --------------------------- */

/* Physical pixels: the content view's bounds converted through its backing
 * scale. A minimized window reports 0 x 0 (PLATFORM-042). */
static void window_pixel_size(const PlatformWindow *window, uint32_t *out_width, uint32_t *out_height) {
    if (window->minimized) {
        *out_width = 0;
        *out_height = 0;
        return;
    }
    NSRect pixels;
    @autoreleasepool {
        pixels = [window->view convertRectToBacking:[window->view bounds]];
    }
    *out_width = pixels.size.width > 0 ? (uint32_t)lround(pixels.size.width) : 0;
    *out_height = pixels.size.height > 0 ? (uint32_t)lround(pixels.size.height) : 0;
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
        queue_push(window->platform, window, PLATFORM_EVENT_WINDOW_FRAMEBUFFER_RESIZED, width, height);
    }
}

/* PLATFORM-043: the sticky flag is set once; CLOSE_REQUESTED is queued the first time only. */
static void window_request_close(PlatformWindow *window) {
    if (window->should_close) {
        return;
    }
    window->should_close = true;
    queue_push(window->platform, window, PLATFORM_EVENT_WINDOW_CLOSE_REQUESTED, 0, 0);
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

    NSMenuItem *window_item = [bar addItemWithTitle:@"" action:nil keyEquivalent:@""];
    NSMenu *window_menu = [[[NSMenu alloc] init] autorelease];
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
    PlatformEvent *queue = engine_alloc(resolved, sizeof *queue * NAYLA_EVENT_QUEUE_CAPACITY, _Alignof(PlatformEvent));
    if (!queue) {
        engine_free(resolved, platform);
        return ENGINE_RESULT_OUT_OF_MEMORY;
    }
    platform->allocator = resolved;
    platform->logger = logger;
    platform->app_delegate = nil;
    platform->queue = queue;
    platform->queue_count = 0;
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
            engine_free(resolved, queue);
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
    engine_free(allocator, platform->queue);
    engine_free(allocator, platform);
}

/* ---- Windows (PLATFORM-040..044) --------------------------------------- */

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
        (desc->flags & ~PLATFORM_WINDOW_RESIZABLE) != 0) {
        return ENGINE_RESULT_INVALID_ARGUMENT;
    }
    const EngineAllocator *resolved = platform_resolve_allocator(allocator);
    if (!resolved) {
        return ENGINE_RESULT_INVALID_ARGUMENT;
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
    queue_purge_window(platform, window);
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

bool platform_next_event(Platform *platform, PlatformEvent *out_event) {
    if (!platform || !out_event || platform->queue_count == 0) {
        return false;
    }
    *out_event = platform->queue[0];
    memmove(&platform->queue[0], &platform->queue[1], (platform->queue_count - 1) * sizeof(PlatformEvent));
    platform->queue_count--;
    return true;
}
