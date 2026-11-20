# Platform API — Contract Specification

**Status:** Proposed
**Date:** 2026-11-20
**Author:** `spec-writer`
**Scope:** the Platform API contract for the first milestone (specs.md §47/§48): platform lifecycle, windows, events, monotonic time, thread identity, and the native-handle boundary to the future GPU surface. Binding on `src/platform/api.h`, `src/platform/native.h`, and every OS backend.
**Supersedes:** specs.md §13 ("Platform Abstraction"), §14 ("Platform Interface"), §15 ("Platform Services"), §16 ("Platform Backend Contract"), §17 ("Native Handle Boundary"), and the `platform_initialize`/`platform_shutdown` names in §37. specs.md remains a historical draft; where it disagrees with this document, **this document wins**.
**Depends on:** `types.md` (CORE-TYPES-011/012 no `bool` in cross-backend structs, flags are `uint32_t`; CORE-TYPES-013/014 no pointer-through-integer; CORE-TYPES-016 `void *` limits; CORE-TYPES-018 `uint64_t` timestamps; CORE-TYPES-021..025 naming and enum width). `result.md` (CORE-RESULT-005/008 closed code set, -012 precedence, -013 out-parameters, -015 `ENGINE_NODISCARD`, -017 infallible returns `void`). `allocator.md` (CORE-ALLOC-021 thread-safety vocabulary, -060..070 borrow-and-store, -080..083 targets). `logging.md` (CORE-LOG-003/004/005 borrowed logger and `NULL` discards, CORE-LOG-040..043 context sources, CORE-LOG-082 lifecycle, CORE-LOG-084 construction parameters).
**Related:** CLAUDE.md invariants #1–#6; `rules/platform.md`; `rules/core.md` ("Keep it simple"); ADR-009; the normative allocator block in `src/platform/api.h` (TASK-007), which this spec incorporates unchanged.

> **Numbering.** specs.md §67 uses `PLATFORM-001..008` as *task* identifiers, and logging.md cites them that way (PLATFORM-005 = time, PLATFORM-008 = threads). Rule identifiers in this spec start at `PLATFORM-010` so a rule ID never collides with a task ID.

---

## 1. Problem

specs.md §14 sketches a `Platform` with window functions but leaves open everything an implementer needs: who allocates `Platform`, which allocator and logger it uses, what thread it may be called from, what happens to the OS close button, what an event looks like, how the GPU backend reaches an `HWND` without the renderer seeing one, and in what units sizes are. Its sketch also uses `u32` and `bool` fields that types.md forbids (TYPE-INV-1, CORE-TYPES-011).

logging.md created a concrete debt: CORE-LOG-040..043 accept a monotonic clock and a thread-id source via `engine_log_set_context_source`, and name the platform layer as the only place they can come from. Nothing provides them. This spec provides them, decides how they plug in, and fixes the rest of the first-slice contract.

---

## 2. Decision 1 — Milestone scope

The first vertical slice (specs.md §48) is `Application → Engine → Platform → Window → GPU Instance → … → Surface → Swapchain → Present`. Each candidate service is in or out by whether that slice calls it.

| Service | In this milestone | Reason |
|---|---|---|
| Lifecycle (`platform_init`/`platform_shutdown`) | **Yes** | Owns process-level window-system state. |
| Window | **Yes** | The slice presents into one. |
| Events | **Yes** | Close and framebuffer resize drive the loop and swapchain recreation. Input (keyboard, mouse, gamepad, touch) is **out**: the triangle takes no input. |
| Monotonic time | **Yes** | Logger timestamps (CORE-LOG-041) and frame deltas. |
| Thread identity | **Yes** | Logger thread ids (CORE-LOG-043). |
| Thread creation, mutex, condition variable, atomics | **No** | specs.md §38's loop is single-threaded and nothing in the tree starts a thread. A thread API is not usable without a synchronization contract (mutex, memory ordering), which is a spec of its own size. Added by amendment when the first consumer (asynchronous logging, a render thread) exists. |
| Filesystem | **No** | The slice loads no runtime assets. Whether shaders are files or embedded bytes is ADR-007's decision; if it chooses runtime files, that call site triggers an amendment adding a whole-file read. |
| Dynamic libraries | **No** | Backends are selected at build time for the first milestone (§9). Runtime backend loading is ADR-006; the Vulkan loader is the Vulkan backend's own concern. |

**PLATFORM-010 (closed scope).** The Platform API for this milestone exposes exactly the services marked "Yes". A new service is added by amending this spec, naming the call site that needs it (the CORE-RESULT-008 discipline, applied to services).

---

## 3. Decision 2 — Time and thread identity are stateless functions

### 3.1 The tension, resolved

Should the clock require an initialized `Platform` (`platform_time_now_ns(const Platform *)`) or be a free function?

**For requiring `Platform`:** Windows `QueryPerformanceFrequency` and macOS `mach_timebase_info` return a conversion factor, and the obvious place to cache it is the `Platform` object. It also gives one rule: "no platform call before `platform_init`".

**Against, decisively:**
1. **The only reason to cache is a hidden global, and no cache is needed.** Each backend has a stateless path: Linux `clock_gettime(CLOCK_MONOTONIC)`, macOS `clock_gettime_nsec_np(CLOCK_UPTIME_RAW)`, Windows `QueryPerformanceCounter` plus `QueryPerformanceFrequency`, which is constant from boot and cheap to call. Without a cache, a `Platform` parameter carries no information.
2. **The logger wants timestamps before any `Platform` exists.** `engine_init` failures and `platform_init`'s own failures are logged before a `Platform` exists. A stateless clock timestamps them. A `Platform`-bound clock leaves them `UNKNOWN`.
3. **Threads that do not hold a `Platform`.** Sinks, future job workers, and allocator callbacks need time or a thread id without access to the platform object. Under CORE-LOG-043 both sources must be callable concurrently from any thread. A free function with no state meets that without any extra work.
4. **Fewer objects in the adapter.** The logger callbacks need no `user_data` (§3.3).

The cost: these two functions are the only Platform API entry points callable without a `Platform`, so the "nothing before `platform_init`" rule has a named exception. That is accepted.

### 3.2 Rules

```c
uint64_t platform_time_now_ns(void);
uint64_t platform_thread_current_id(void);
```

**PLATFORM-020 (monotonic nanoseconds).** `platform_time_now_ns` returns nanoseconds since an unspecified origin that is fixed for the life of the process. Successive calls from any threads return non-decreasing values: a call that happens-after another call never returns a smaller value. It MUST NOT return `UINT64_MAX` (= `ENGINE_LOG_TIMESTAMP_UNKNOWN`). It is never wall-clock time: it MUST NOT step under NTP or user clock changes.

**PLATFORM-021 (resolution and suspend).** Resolution SHOULD be 1 µs or finer and is not exposed. Whether the clock advances during system suspend is **unspecified**. Consumers MUST NOT depend on it either way, and a frame-delta consumer MUST tolerate an arbitrarily large step between two frames.

**PLATFORM-022 (overflow-safe conversion).** A backend converting a tick counter `c` at frequency `f` MUST compute `(c / f) * 1000000000 + ((c % f) * 1000000000) / f`, or an equivalent that cannot overflow. The naive `c * 1000000000 / f` overflows `uint64_t` after about 30 minutes at 10 MHz.

**PLATFORM-023 (thread identity).** `platform_thread_current_id` returns a value that is stable for the life of the calling thread, distinct between concurrently live threads, and never `0` (= `ENGINE_LOG_THREAD_UNKNOWN`). A value MAY be reused after its thread exits. It is an opaque identifier. No arithmetic, ordering, or OS meaning may be inferred from it. It is `uint64_t` directly. A `PlatformThreadId` typedef would add a name with no semantics.

**PLATFORM-024 (no state, any thread, any time).** Both functions are **`THREAD_SAFE`** (CORE-ALLOC-021). They are callable from any thread, including before `platform_init`, after `platform_shutdown`, and with no `Platform` in existence. They read no engine state, write no state, allocate nothing, never log, and never fail. Together with §3.3's adapters they are the only Platform API functions with no `Platform` or `PlatformWindow` parameter.

### 3.3 How they plug into the logger

`EngineLogTimeFn` and `EngineLogThreadIdFn` take `void *user_data`. Calling `platform_time_now_ns` through an `EngineLogTimeFn` pointer would be a call through an incompatible function type, which is undefined behaviour. The Platform API therefore declares two adapters with the logger's exact signatures:

```c
uint64_t platform_log_time_source(void *user_data);       /* == platform_time_now_ns()       */
uint64_t platform_log_thread_id_source(void *user_data);  /* == platform_thread_current_id() */
```

**PLATFORM-025 (adapters).** Each adapter ignores `user_data` and returns its function's result. Each meets CORE-LOG-043 completely: monotonic nanoseconds and never `UNKNOWN`, stable non-zero thread ids, `THREAD_SAFE`, never logs. The application installs them with:

```c
EngineResult r = engine_log_set_context_source(&logger,
                                               platform_log_time_source,
                                               platform_log_thread_id_source,
                                               NULL);   /* user_data unused */
```

**PLATFORM-026 (installation point).** The owner of the non-`const` `EngineLogger *` (the application, CORE-LOG-006) installs the sources. The Platform API never calls `engine_log_set_context_source` itself, because it holds only `const EngineLogger *`. Installation MAY happen immediately after `engine_log_init` (PLATFORM-024 makes the sources valid before any `Platform` exists). CORE-LOG-082 places installation after platform initialization because it assumed the clock needed one. That ordering is still conforming, but it is no longer required. The recommended order installs the sources first, so every record carries a real timestamp (§8).

---

## 4. Decision 3 — Lifecycle, allocator, logger

```c
typedef struct Platform Platform;   /* opaque; defined in the selected backend */

ENGINE_NODISCARD EngineResult platform_init(Platform              **out_platform,
                                            const EngineAllocator  *allocator,
                                            const EngineLogger     *logger);
void platform_shutdown(Platform *platform);
```

**PLATFORM-030 (signature shape).** The out-parameter comes first, then allocator, then logger, matching `engine_init` and `renderer_init`. The names are `platform_init`/`platform_shutdown` to match those, replacing specs.md's `platform_initialize`.

**PLATFORM-031 (allocator: `NULL` selects the default).** Every Platform API function that creates an engine-owned object (`platform_init`, `platform_window_create`) takes `const EngineAllocator *allocator`. `NULL` is resolved **once**, at that call, to `engine_default_allocator()` and the resolved pointer is stored (CORE-ALLOC-068). After resolution, `alloc`/`free` are verified non-`NULL` or the call returns `ENGINE_RESULT_INVALID_ARGUMENT` (CORE-ALLOC-070). Every allocation and release the object performs goes through its stored pointer (CORE-ALLOC-067). The `src/platform/api.h` block governs OS-owned memory: OS handles are released by their OS calls first, then the struct is released through the stored allocator.

**PLATFORM-032 (logger: `NULL` discards).** `platform_init` takes `const EngineLogger *logger`, stores it unchanged, and **never** resolves `NULL` to anything. `NULL` means the platform's diagnostics are discarded (CORE-LOG-005). This is the opposite of PLATFORM-031 on purpose: a program cannot run without an allocator, but it runs fine without a logger. The logger is per-subsystem (CORE-LOG-084): windows log through their `Platform`'s stored logger and take no logger parameter. The allocator is per-object (`api.h` block), so windows take one.

**PLATFORM-033 (`platform_init` results).** In CORE-RESULT-012 precedence order:
- `ENGINE_RESULT_INVALID_ARGUMENT`: `out_platform == NULL`, or the resolved allocator has a `NULL` callback;
- `ENGINE_RESULT_NOT_SUPPORTED`: no window system is reachable (no display server, a headless session, an OS version below the backend's minimum), or any other OS failure (PLATFORM-034);
- `ENGINE_RESULT_OUT_OF_MEMORY`: an `EngineAllocator` allocation returned `NULL` (CORE-RESULT-019);
- `ENGINE_RESULT_SUCCESS`: `*out_platform` is a valid `Platform`.

On any failure, `*out_platform` is `NULL` (when `out_platform` is non-`NULL`) and everything acquired has been released (CORE-RESULT-013).

**PLATFORM-034 (OS failures map to `NOT_SUPPORTED`, logged at the failure point).** An OS call that fails for any reason other than an `EngineAllocator` returning `NULL` is reported as `ENGINE_RESULT_NOT_SUPPORTED`. The backend logs at `ERROR` with the OS's own error detail (`GetLastError`, `errno`, an `NSError` description) converted to text. That error detail never crosses the boundary as a value (CORE-RESULT-005/006). *Rationale:* result.md deferred a `_PLATFORM` code until a caller needs to act differently on it (CORE-RESULT-008). In this milestone every caller does the same thing on both conditions: log and exit. When a caller first needs to tell "windowing unavailable" from "the OS refused", that caller is the named call site for the amendment.

**PLATFORM-035 (`platform_shutdown`).** `platform_shutdown` returns `void` (CORE-RESULT-017) and is a no-op for `NULL`. Its precondition is that every `PlatformWindow` created from this `Platform` has been destroyed. If windows are still live, the backend logs at `ERROR` and returns without releasing anything, so it leaks rather than invalidating pointers the caller still holds (the CORE-ALLOC-092 trade). It never touches the allocator or logger objects (CORE-ALLOC-062, CORE-LOG-002).

**PLATFORM-036 (one live `Platform` per process).** Window-system state is process-global in the OS (the `NSApplication` singleton, Win32 window-class registration, the display connection). At most one `Platform` may be live at a time. Violating this is a caller error and the behaviour is **undefined**. Backends MUST NOT add a process-global flag to detect it (PLATFORM-090).

**PLATFORM-037 (lifetimes).** The allocator and logger passed to `platform_init` MUST outlive the `Platform` (CORE-ALLOC-065, CORE-LOG-004). The allocator passed to `platform_window_create` MUST outlive that window.

---

## 5. Decision 4 — Windows

```c
typedef struct PlatformWindow PlatformWindow;   /* opaque */

#define PLATFORM_WINDOW_RESIZABLE ((uint32_t)0x00000001u)

typedef struct PlatformWindowDesc {
    const char *title;    /* non-NULL, NUL-terminated UTF-8; not retained */
    uint32_t    width;    /* requested client-area size, logical units, > 0 */
    uint32_t    height;
    uint32_t    flags;    /* PLATFORM_WINDOW_* bits */
} PlatformWindowDesc;

ENGINE_NODISCARD EngineResult platform_window_create(Platform                 *platform,
                                                     const EngineAllocator    *allocator,
                                                     const PlatformWindowDesc *desc,
                                                     PlatformWindow          **out_window);
void platform_window_destroy(PlatformWindow *window);
bool platform_window_should_close(const PlatformWindow *window);
ENGINE_NODISCARD EngineResult platform_window_get_framebuffer_size(const PlatformWindow *window,
                                                                   uint32_t *out_width,
                                                                   uint32_t *out_height);
```

**PLATFORM-040 (desc layout).** `PlatformWindowDesc` crosses into a backend, so it has no `bool` field (CORE-TYPES-011) and options are `uint32_t` flag bits (CORE-TYPES-012). specs.md's `bool resizable` becomes `PLATFORM_WINDOW_RESIZABLE`. specs.md's `fullscreen` is dropped for this milestone: the slice does not need it, and display-mode switching is a separate contract. Fields are appended, never reordered.

**PLATFORM-041 (`platform_window_create` results).** In precedence order: `INVALID_ARGUMENT` for a `NULL` `platform`, `desc`, `desc->title`, or `out_window`, a zero `width` or `height`, any unknown `flags` bit, or a resolved allocator with a `NULL` callback. Then `NOT_SUPPORTED` for any OS failure (PLATFORM-034). Then `OUT_OF_MEMORY`. Failure leaves `*out_window == NULL` and the `Platform` unchanged. On success the window is visible, and `desc` and `desc->title` may be released immediately: the backend copies or consumes the title during the call. Invalid UTF-8 in `title` is a caller error that MUST NOT crash. The backend MAY substitute U+FFFD, and the call does not fail because of it.

**PLATFORM-042 (sizes are requests; the framebuffer is authoritative).** `width`/`height` request a client-area size in the OS's logical (DPI-independent) units. The OS may clamp or scale them. The GPU surface size is `platform_window_get_framebuffer_size`, which is **always physical pixels**. How a backend obtains DPI-correct pixels (macOS backing scale, Windows DPI awareness) is backend-internal. The query returns `INVALID_ARGUMENT` for any `NULL` pointer, writing nothing, and `SUCCESS` otherwise. A minimized or zero-area window reports `0 × 0`, which is a valid result and not an error (§13 edge case 3).

**PLATFORM-043 (the close button never destroys).** When the user or OS asks to close a window (the close button, Alt+F4, the Cmd+W/Quit menu, a window-manager close), the backend MUST NOT destroy it. It sets the window's sticky close flag and queues `PLATFORM_EVENT_WINDOW_CLOSE_REQUESTED` (§6). Only `platform_window_destroy` destroys a window. The application decides whether to honour the request. `platform_window_should_close` returns the sticky flag, which is never cleared, and returns `false` for `NULL`.

**PLATFORM-044 (`platform_window_destroy`).** Returns `void` and is a no-op for `NULL`. It releases OS resources first, then the struct through the window's stored allocator (`api.h` block). It purges every queued event that refers to this window (PLATFORM-053). Precondition: no GPU surface built from this window is still live (PLATFORM-073).

---

## 6. Decision 5 — Events: pump, then drain; state is authoritative

```c
typedef enum PlatformEventType {
    PLATFORM_EVENT_NONE = 0,
    PLATFORM_EVENT_WINDOW_CLOSE_REQUESTED,      /* 1 */
    PLATFORM_EVENT_WINDOW_FRAMEBUFFER_RESIZED   /* 2 — width/height are the new pixel size */
} PlatformEventType;

typedef struct PlatformEvent {
    int32_t         type;     /* a PlatformEventType (CORE-TYPES-025) */
    PlatformWindow *window;   /* borrowed; live whenever the event is returned */
    uint32_t        width;    /* FRAMEBUFFER_RESIZED only; 0 otherwise */
    uint32_t        height;
} PlatformEvent;

#define PLATFORM_EVENT_QUEUE_CAPACITY ((size_t)64)

void platform_pump_events(Platform *platform);
bool platform_next_event(Platform *platform, PlatformEvent *out_event);
```

Main loop shape (replaces specs.md §38's `platform_poll_events()`, which had no object to act on):

```c
for (;;) {
    platform_pump_events(platform);
    PlatformEvent ev;
    while (platform_next_event(platform, &ev)) { /* react */ }
    if (platform_window_should_close(window)) break;
    /* update, render, present */
}
```

**PLATFORM-050 (pump).** `platform_pump_events` processes every OS event pending at the time of the call. It translates them into the `Platform`'s queue and returns. It MUST NOT wait when nothing is pending. It MAY run for as long as the OS keeps control inside a user-driven modal interaction (Win32 move/size loop, macOS live resize). The contract does not require rendering during that interaction. It is a no-op for `NULL`.

**PLATFORM-051 (drain).** `platform_next_event` copies the oldest queued event into `*out_event`, removes it, and returns `true`. When the queue is empty, or either argument is `NULL`, it returns `false` and writes nothing. It never pumps. Events are delivered in the order the OS produced them, subject to PLATFORM-052.

**PLATFORM-052 (coalescing bounds the queue).** The queue is a fixed array of `PLATFORM_EVENT_QUEUE_CAPACITY` entries, allocated at `platform_init`. Pumping allocates nothing. At most one `FRAMEBUFFER_RESIZED` and one `CLOSE_REQUESTED` per window are queued at a time. A newer resize updates the queued entry's size in place, and a repeat close request is dropped. If the queue is still full, the new event is dropped and the backend logs at `WARN`. This loses nothing permanently, because **window state is authoritative and events are only notifications**: `platform_window_should_close` and `platform_window_get_framebuffer_size` always report the current truth.

**PLATFORM-053 (window pointer validity).** `event.window` is borrowed. Because destroying a window purges its events (PLATFORM-044), every event `platform_next_event` returns refers to a live window.

**PLATFORM-054 (closed event set; values stable).** `PlatformEventType` values are fixed as written, appended only, and never renumbered. `PLATFORM_EVENT_NONE` is never queued. Input events are a future amendment (PLATFORM-010).

---

## 7. Decision 6 — The native-handle boundary

### 7.1 Who sees native handles

Only **platform backends** (which produce them) and **GPU backends** (which create a surface from them) see native handles. The renderer, core, and the GPU API header never do. The GPU API's future surface-creation entry point takes the opaque `PlatformWindow *`, and the GPU *backend* extracts native handles internally. specs.md §17's alternative, where the renderer fetches a `NativeWindowHandle` and passes it to the GPU API, is rejected: it would put a native-handle type in renderer code, which `rules/renderer.md` forbids ("no backend-native types").

### 7.2 Shape

```c
/* src/platform/native.h — included ONLY by platform backends and GPU backends */
typedef struct PlatformNativeObject PlatformNativeObject;   /* never defined anywhere */

typedef enum PlatformNativeWindowType {
    PLATFORM_NATIVE_WINDOW_NONE = 0,
    PLATFORM_NATIVE_WINDOW_WIN32,     /* 1 */
    PLATFORM_NATIVE_WINDOW_COCOA,     /* 2 */
    PLATFORM_NATIVE_WINDOW_X11,       /* 3 */
    PLATFORM_NATIVE_WINDOW_WAYLAND    /* 4 */
} PlatformNativeWindowType;

typedef struct PlatformNativeWindow {
    int32_t               type;       /* a PlatformNativeWindowType */
    PlatformNativeObject *display;    /* see table */
    PlatformNativeObject *window;     /* see table */
    uint64_t              window_id;  /* see table */
} PlatformNativeWindow;

ENGINE_NODISCARD EngineResult platform_window_get_native(const PlatformWindow *window,
                                                         PlatformNativeWindow *out_native);
```

| `type` | `display` | `window` | `window_id` |
|---|---|---|---|
| `WIN32` | `HINSTANCE` | `HWND` | `0` |
| `COCOA` | `NULL` | the window's content `NSView *` | `0` |
| `X11` | `Display *` | `NULL` | the X11 `Window` (XID) |
| `WAYLAND` | `struct wl_display *` | `struct wl_surface *` | `0` |

**PLATFORM-070 (no `void *`, no pointer-as-integer).** Native pointers are stored as `PlatformNativeObject *`, a pointer to an incomplete type that is never defined. This is CORE-TYPES-016's own prescription for opaque values ("forward-declared incomplete types"), so TYPE-INV-8 holds without an amendment. Backends convert with an explicit cast at the boundary, and the round trip yields the original pointer. The X11 XID is an integer handle, not a pointer, so it travels in `uint64_t window_id`. No pointer is ever stored in `window_id` (CORE-TYPES-013).

**PLATFORM-071 (the table is the contract).** Each `type` populates exactly the fields in the table, and the others are `NULL`/`0`. `WIN32` and `COCOA` are the only types produced in this milestone. `X11` and `WAYLAND` are reserved now so that GPU backends can be written against a fixed shape. **Which of them a Linux backend produces is the open ADR in §11.** A GPU backend that receives a `type` it does not implement returns `ENGINE_RESULT_NOT_SUPPORTED` (CLAUDE.md invariant #6).

**PLATFORM-072 (ownership and permitted use).** The platform owns every native object. A GPU backend MUST NOT destroy, release, retain beyond the window's life, re-parent, subclass, or replace the window procedure or delegate of any of them. Exactly one mutation is sanctioned: on `COCOA`, the GPU backend MAY install a `CAMetalLayer` as the view's layer (`wantsLayer`/`layer`). This keeps QuartzCore/Metal headers out of the platform backend (`rules/gpu.md`: SDK headers stay inside GPU backends), and it serves both the Metal backend and Vulkan-over-MoltenVK (`VK_EXT_metal_surface`).

**PLATFORM-073 (validity and lifetime).** `platform_window_get_native` returns the same values on every call for the life of the window. It returns `INVALID_ARGUMENT` for a `NULL` argument and `SUCCESS` otherwise. The values are valid until `platform_window_destroy` begins. Every GPU surface built from a window MUST be destroyed before that window. This is the window-level form of specs.md §37's `gpu_shutdown → platform_shutdown` order.

**PLATFORM-074 (thread for native use).** Surface creation and every sanctioned mutation (PLATFORM-072) happen on the platform thread (§8). Whether a GPU backend may *present* from another thread is the GPU surface spec's decision. This spec guarantees only that the values are stable (PLATFORM-073).

---

## 8. Thread safety and lifecycle position

Stated in CORE-ALLOC-021's vocabulary.

**PLATFORM-080 (the platform thread).** `platform_init` MUST be called on the process's initial thread, the one that entered `main`. That thread is the **platform thread**. This is the strictest common requirement of the three OSes (AppKit requires the main thread, and Win32 binds a window's messages to its creating thread), so one rule holds on every backend.

**PLATFORM-081 (levels).**

| Functions | Level |
|---|---|
| `platform_time_now_ns`, `platform_thread_current_id`, `platform_log_time_source`, `platform_log_thread_id_source` | `THREAD_SAFE` |
| every other Platform API function, including `platform_window_get_native` | `THREAD_AFFINE` to the platform thread |

**PLATFORM-082 (what the platform calls, and from where).** A backend invokes its allocator and logger **only on the platform thread**, and only inside a Platform API call. OS callbacks the backend receives on other threads MUST NOT allocate through the `EngineAllocator` or log. The caller's allocator and sinks therefore need nothing stronger than `THREAD_AFFINE` to the platform thread on the platform's behalf. Per the `api.h` block, this is a uniform backend precondition, not a per-OS variation.

**PLATFORM-083 (recommended lifecycle).** This extends CORE-LOG-082:

```
engine_log_init → add sinks → engine_log_set_context_source(platform adapters)   (PLATFORM-026)
  → engine_init → platform_init → platform_window_create → gpu… → renderer… → loop
  → …renderer/gpu shutdown → platform_window_destroy → platform_shutdown → engine_shutdown → engine_log_shutdown
```

The application owns and sequences `Platform`, just as it owns the logger. Whether `engine_init` later absorbs `platform_init` is `engine.md`'s decision (CORE-LOG-084 note), not this spec's.

---

## 9. Placement, targets, and dependency direction

**PLATFORM-090 (no engine state at file scope).** No backend translation unit holds file-scope mutable engine state. Per-window lookup from an OS callback uses OS per-window storage (Win32 `GWLP_USERDATA`, a Cocoa delegate object, a list owned by the `Platform`), never a global table. OS-owned process singletons (`NSApp`) are the OS's state and are outside this rule.

**PLATFORM-091 (nothing joins `engine_foundation`).** `engine_foundation` still depends on nothing (CORE-ALLOC-081). The logger receives time and thread ids through callbacks (CORE-LOG-041). No clock or thread-id code moves into foundation, and foundation does not link any platform target.

**PLATFORM-092 (targets).**

| Target | Contents | Links |
|---|---|---|
| `engine_platform_api` (INTERFACE, exists) | `src/platform/api.h`, `src/platform/native.h` | `engine_foundation` |
| `engine_platform_win32` / `engine_platform_macos` / `engine_platform_linux` (STATIC) | `src/platform/<os>/` | `engine_platform_api` |
| `engine_platform` (ALIAS) | the backend selected by the cache variable `NAYLA_PLATFORM_BACKEND`, which defaults to the host OS | — |

**PLATFORM-093 (who may link what).** Dependencies flow downward only (specs.md §4.1):
- the application executable links `engine_platform`, and it is the only target that links a backend;
- `engine_core` MAY link `engine_platform_api` and MUST NOT link a backend;
- `engine_renderer` MUST NOT link `engine_platform_api` or any backend, even transitively;
- GPU backends MAY link `engine_platform_api` (for `native.h`) and MUST NOT link a platform backend. `engine_gpu_api` MAY forward-declare `PlatformWindow` and MUST NOT include `native.h`.

**PLATFORM-094 (header discipline).** `api.h` includes exactly `<stdbool.h>`, `<stddef.h>`, `<stdint.h>`, `src/core/result.h`, `src/core/allocator.h`, and `src/core/logging.h`. `native.h` includes `api.h` and `<stdint.h>`. Neither names an OS or SDK type: native types appear only as comments in the table. OS headers appear only under `src/platform/<os>/`. Backends are subject to `forbid_direct_allocation` and `forbid_direct_output` with no exemption, and they never include a GPU SDK header.

**PLATFORM-095 (macOS language is a backend detail).** The contract is C. Whether `src/platform/macos/` uses Objective-C translation units or C with the Objective-C runtime is decided by ADR when that backend is written (§11). Either way, nothing Objective-C appears in `api.h` or `native.h`.

---

## 10. Conformance

**PLATFORM-100 (one suite, every backend).** A backend conforms when `tests/platform/test_platform_conformance.c` passes on its target OS. The suite is written only against `api.h`/`native.h`, linked against `engine_platform`, and built unchanged for every backend. It covers every invariant tagged *(conformance)* in §12. It injects a leak-tracking allocator (ALLOC-INV-12 pattern) and a capture-sink logger.

**PLATFORM-101 (unavailable display is reported, not passed).** Time and thread-id cases run everywhere. If `platform_init` returns `NOT_SUPPORTED` because no window system is reachable, the window and event cases exit with CTest's skip code (`SKIP_RETURN_CODE 77`) and print the reason. They MUST NOT count as passed (`rules/testing.md`). CI for each backend runs where a display exists, which on Linux depends on the §11 ADR.

**PLATFORM-102 (no backend-specific assertions).** A conformance case MUST NOT branch on the backend. A behaviour that cannot be asserted identically on all backends is either specified here as unspecified (PLATFORM-021) or is a spec gap to escalate. Weakening the test is never the fix.

---

## 11. Open ADR dependencies (not decided here)

1. **Linux windowing strategy: X11, Wayland, or both.** specs.md §69 requires an ADR, and this spec deliberately does not decide it. The contract already accommodates every outcome: `X11` and `WAYLAND` native types are reserved (PLATFORM-071), and GPU backends degrade with `NOT_SUPPORTED` on a type they lack. `engine_platform_linux`, its CI display environment, and Linux conformance (PLATFORM-101) are blocked on that ADR. Nothing else in this spec is.
2. **macOS backend language** (PLATFORM-095). It does not affect the contract.

---

## 12. Invariants

**PLAT-INV-1.** `PLATFORM_EVENT_NONE == 0`, `WINDOW_CLOSE_REQUESTED == 1`, `WINDOW_FRAMEBUFFER_RESIZED == 2`; `PLATFORM_NATIVE_WINDOW_NONE == 0` … `WAYLAND == 4`; `PLATFORM_EVENT_QUEUE_CAPACITY >= 8`. *(static_assert)*

**PLAT-INV-2.** No file under `src/core`, `src/renderer`, or `src/gpu/api.h` includes `platform/native.h` or names `PlatformNativeWindow`, `PlatformNativeObject`, or `platform_window_get_native`. *(grep)*

**PLAT-INV-3.** `src/platform/api.h` and `src/platform/native.h` include no OS or GPU SDK header and contain no `void *`, `bool` struct field, `u32`, or `_t`-suffixed engine type. *(grep — TYPE-INV-1/3/8, CORE-TYPES-011)*

**PLAT-INV-4.** `engine_renderer` does not link `engine_platform_api` or any `engine_platform_*` backend. `engine_core` and every GPU backend do not link any `engine_platform_*` backend. *(build-graph — `forbid_transitive_link`)*

**PLAT-INV-5.** The only Platform API function declarations without a `Platform` or `PlatformWindow` pointer parameter (`Platform *`, `Platform **`, `PlatformWindow *`, `PlatformWindow **`) are the four functions of PLATFORM-024/025. *(grep over `api.h`)*

**PLAT-INV-6.** `platform_time_now_ns` is non-decreasing across 10⁶ consecutive calls on one thread and across a happens-before edge between two threads. It never returns `UINT64_MAX`. A `sleep`-free busy loop eventually observes an increase. *(conformance)*

**PLAT-INV-7.** `platform_thread_current_id` is non-zero, equal across calls on one thread, and different between two concurrently live threads. *(conformance — the test may use the C11 `<threads.h>` or the host's threads, test-only)*

**PLAT-INV-8.** With the PLATFORM-025 adapters installed and no `Platform` created, a captured record has `timestamp_ns != ENGINE_LOG_TIMESTAMP_UNKNOWN` and `thread_id != ENGINE_LOG_THREAD_UNKNOWN`. *(conformance)*

**PLAT-INV-9.** `platform_init(NULL, …)` returns `INVALID_ARGUMENT`. An always-`NULL` allocator makes `platform_init` return `OUT_OF_MEMORY` and leaves `*out_platform == NULL`. A leak-tracking allocator sees equal alloc/free counts after `platform_window_destroy` + `platform_shutdown`. A `NULL` logger produces no output and no crash. *(conformance)*

**PLAT-INV-10.** `platform_window_create` returns `INVALID_ARGUMENT` for each PLATFORM-041 case (zero width, zero height, `NULL` title, unknown flag bit) and leaves `*out_window == NULL`. *(conformance)*

**PLAT-INV-11.** A newly created window has `should_close == false`, a non-zero framebuffer size (unless minimized), and `platform_window_get_native` returns `SUCCESS` with a `type` and fields matching PLATFORM-071's table for the host. *(conformance)*

**PLAT-INV-12.** `platform_next_event` on an empty queue returns `false` without writing. After `platform_window_destroy`, no queued event refers to the destroyed window. *(conformance)*

**PLAT-INV-13.** `platform_shutdown(NULL)`, `platform_window_destroy(NULL)`, and `platform_pump_events(NULL)` return normally. `platform_window_should_close(NULL)` returns `false`. *(conformance)*

**PLAT-INV-14.** No file under `src/platform` declares a file-scope non-`const` variable. *(grep — PLATFORM-090; `static const` permitted)*

**PLAT-INV-15.** GPU backends perform no native-object mutation other than PLATFORM-072's `CAMetalLayer` installation. *(review-enforced)*

---

## 13. Edge cases

1. **Logging before any `Platform` exists:** timestamps and thread ids are real, because the sources are stateless (PLATFORM-024/026).
2. **Logging from a non-platform thread:** the sources are `THREAD_SAFE`, so the record is correct. Whether the *sinks* are safe on that thread is CORE-LOG-071's question, not the platform's.
3. **Minimized window:** framebuffer `0 × 0`, a `FRAMEBUFFER_RESIZED` to `0 × 0` is queued, and the swapchain must not be recreated at zero size. That is the renderer/GPU contract's obligation, signalled here.
4. **Resize storm during a drag:** coalesced to one queued event per window (PLATFORM-052), and the query returns the final size.
5. **Close requested, application ignores it:** the window stays and `should_close` stays `true` (PLATFORM-043). A repeat request queues no second event.
6. **Window-system connection lost mid-run** (X server or compositor exit): `platform_pump_events` cannot report it, because it returns `void`. The backend sets every window's close flag, queues `CLOSE_REQUESTED` for each, and logs at `ERROR`. The application's normal close path then runs.
7. **`platform_shutdown` with live windows:** logs `ERROR` and leaks (PLATFORM-035).
8. **Window destroyed while a GPU surface still uses it:** undefined behaviour and undetectable (PLATFORM-073). The lifecycle ordering (§8) prevents it.
9. **Second `Platform` in one process:** undefined behaviour, deliberately not detected (PLATFORM-036/090).
10. **`platform_init` on a non-initial thread:** violates PLATFORM-080. Backends MAY return `INVALID_STATE` when the OS can tell them (`pthread_main_np`, `NSThread isMainThread`). Otherwise behaviour is undefined.
11. **Event queue full after coalescing:** the event is dropped with a `WARN`. State queries stay correct (PLATFORM-052).
12. **Win32 modal move/size loop or macOS live resize:** `platform_pump_events` may not return until the interaction ends, and frames stall. Rendering during the interaction is out of scope (PLATFORM-050).
13. **Huge uptime, high-frequency counter:** conversion cannot overflow (PLATFORM-022). `UINT64_MAX` ns is about 584 years.
14. **Clock across system suspend:** unspecified, and frame code clamps the delta (PLATFORM-021).
15. **GPU backend given `X11` or `WAYLAND` before the Linux ADR lands:** returns `NOT_SUPPORTED` (PLATFORM-071).
16. **Invalid UTF-8 title:** no crash and no failure. The backend may substitute U+FFFD (PLATFORM-041).

---

## 14. Migration impact

| File | Change |
|---|---|
| `src/platform/api.h` | Replace the placeholder with §3–§6 declarations. The TASK-007 normative block stays verbatim. |
| `src/platform/native.h` (new) | §7.2. |
| `src/platform/CMakeLists.txt` | `engine_platform_api` links `engine_foundation`. Add backend targets, `NAYLA_PLATFORM_BACKEND`, the `engine_platform` alias, and the checks for PLAT-INV-2/3/4/14. The OS-include check must scope to the top-level headers, not backend subdirectories (`forbid-pattern.sh` is recursive). |
| `src/renderer/CMakeLists.txt` | `forbid_transitive_link(engine_renderer engine_platform_api)` (PLAT-INV-4). |
| `main.c` | Install the adapters after `engine_log_init`, then `platform_init`/window/loop/teardown per PLATFORM-083. |
| `tests/platform/` (new) | The conformance suite (PLATFORM-100). |

The suggested order is: header + `native.h` + static/grep checks (no backend needed), then `engine_platform_win32` and `engine_platform_macos` with the conformance suite, then `main.c`. Linux waits for its ADR. No existing behaviour changes.
