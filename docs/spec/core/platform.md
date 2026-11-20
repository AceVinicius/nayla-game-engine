# Platform API — Contract Specification

**Status:** Proposed
**Date:** 2026-11-20
**Author:** `spec-writer`
**Scope:** the Platform API contract for the first milestone (specs.md §47/§48): platform lifecycle, windows, events, monotonic time, thread identity, and the native-handle boundary to the future GPU surface. Binding on `src/platform/api.h`, `src/platform/native.h`, and every OS backend.
**Supersedes:** specs.md §13 ("Platform Abstraction"), §14 ("Platform Interface"), §15 ("Platform Services"), §16 ("Platform Backend Contract"), §17 ("Native Handle Boundary"), the `platform_initialize`/`platform_shutdown` names in §37, and `platform_poll_events` in §38. specs.md remains a historical draft; where it disagrees with this document, **this document wins**. **Amends** `logging.md` CORE-LOG-082 (installing the context sources after platform initialization is no longer required, and records before that point need not carry the "unknown" sentinels, PLATFORM-026/083) and its edge case 12; both carry a note pointing here. **Widens** `result.md` §4's meaning of `ENGINE_RESULT_NOT_SUPPORTED` to OS-call failures (PLATFORM-034, issue #18); result.md carries a note.
**Depends on:** `types.md` (CORE-TYPES-011/012 no `bool` in cross-backend structs, flags are `uint32_t`; CORE-TYPES-013/014 no pointer-through-integer; CORE-TYPES-016 `void *` limits; CORE-TYPES-018 `uint64_t` timestamps; CORE-TYPES-021..025 naming and enum width). `result.md` (CORE-RESULT-005/008 closed code set, -012 precedence, -013 out-parameters, -015 `ENGINE_NODISCARD`, -017 infallible returns `void`). `allocator.md` (CORE-ALLOC-021 thread-safety vocabulary, -060..070 borrow-and-store, -080..083 targets). `logging.md` (CORE-LOG-003/004/005 borrowed logger and `NULL` discards, CORE-LOG-040..043 context sources, CORE-LOG-082 lifecycle, CORE-LOG-084 construction parameters).
**Related:** CLAUDE.md invariants #1–#6; `rules/platform.md`; `rules/core.md` ("Keep it simple"); ADR-009; ADR-010 (OS headers in GPU surface files); the normative allocator block in `src/platform/api.h` (TASK-007), which this spec incorporates unchanged.

> **Numbering.** specs.md §67 uses `PLATFORM-001..008` as *task* identifiers, and logging.md cites them that way (PLATFORM-005 = time, PLATFORM-008 = threads). Rule identifiers in this spec start at `PLATFORM-010` so a rule ID never collides with a task ID. Rules are grouped by decade per section; `PLATFORM-06x` is unused.

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

**PLATFORM-022 (overflow-safe conversion).** A backend converting a tick counter `c` at frequency `f` MUST compute `(c / f) * 1000000000 + ((c % f) * 1000000000) / f`, or an equivalent that cannot overflow. That formula is itself safe only while `f < 2^64 / 10^9` (about 1.8 × 10^10 Hz), which every real counter satisfies (QPC is typically 10 MHz). The naive `c * 1000000000 / f` overflows `uint64_t` after about 30 minutes at 10 MHz.

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

**PLATFORM-030 (signature shape).** Constructors (`platform_init`, `platform_window_create`) take the out-parameter first, matching `engine_init` and `renderer_init`. Queries take the object first and out-parameters last. The names are `platform_init`/`platform_shutdown` to match those, replacing specs.md's `platform_initialize`.

**PLATFORM-031 (allocator: `NULL` selects the default).** Every Platform API function that creates an engine-owned object (`platform_init`, `platform_window_create`) takes `const EngineAllocator *allocator`. `NULL` is resolved **once**, at that call, to `engine_default_allocator()` and the resolved pointer is stored (CORE-ALLOC-068). After resolution, `alloc`/`free` are verified non-`NULL` or the call returns `ENGINE_RESULT_INVALID_ARGUMENT` (CORE-ALLOC-070). Every allocation and release the object performs goes through its stored pointer (CORE-ALLOC-067). The `src/platform/api.h` block governs OS-owned memory: OS handles are released by their OS calls first, then the struct is released through the stored allocator.

**PLATFORM-032 (logger: `NULL` discards).** `platform_init` takes `const EngineLogger *logger`, stores it unchanged, and **never** resolves `NULL` to anything. `NULL` means the platform's diagnostics are discarded (CORE-LOG-005). This is the opposite of PLATFORM-031 on purpose: a program cannot run without an allocator, but it runs fine without a logger. The logger is per-subsystem (this spec's choice, consistent with CORE-LOG-003): windows log through their `Platform`'s stored logger and take no logger parameter. The allocator is per-object (`api.h` block), so windows take one.

**PLATFORM-033 (`platform_init` results).** In CORE-RESULT-012 precedence order:
- `ENGINE_RESULT_INVALID_ARGUMENT`: `out_platform == NULL`, or the resolved allocator has a `NULL` callback;
- `ENGINE_RESULT_NOT_SUPPORTED`: no window system is reachable (no display server, a headless session, an OS version below the backend's minimum), or any other OS failure (PLATFORM-034);
- `ENGINE_RESULT_OUT_OF_MEMORY`: an `EngineAllocator` allocation returned `NULL` (CORE-RESULT-019);
- `ENGINE_RESULT_SUCCESS`: `*out_platform` is a valid `Platform`.

This order fixes the backend's sequence: validate arguments, then probe the window system without allocating, then allocate. A system with no reachable window system therefore returns `NOT_SUPPORTED` even with an always-`NULL` allocator. On any failure, `*out_platform` is `NULL` (when `out_platform` is non-`NULL`) and everything acquired has been released (CORE-RESULT-013).

**PLATFORM-034 (OS failures map to `NOT_SUPPORTED`, logged at the failure point).** An OS call that fails during `platform_init` or `platform_window_create` for any reason other than an `EngineAllocator` returning `NULL` is reported as `ENGINE_RESULT_NOT_SUPPORTED`. Queries and `platform_pump_events` return no OS errors: they report state the backend already holds (PLATFORM-042, PLATFORM-073). The backend logs at `ERROR` with the OS's own error detail (`GetLastError`, `errno`, an `NSError` description) converted to text. That error detail never crosses the boundary as a value (CORE-RESULT-005/006). This **widens** result.md §4's meaning ("capability unavailable, not a bug"): here `NOT_SUPPORTED` also covers an OS refusal that may be an environment or backend fault. *Rationale:* result.md deferred a `_PLATFORM` code until a caller needs to act differently on it (CORE-RESULT-008). In this milestone every production caller does the same thing on both conditions: log and exit. The conformance suite must tell them apart and does so by environment declaration (PLATFORM-101), not by result code. Decision and revisit trigger: §11 item 3, issue #18.

**PLATFORM-035 (`platform_shutdown`).** `platform_shutdown` returns `void` (CORE-RESULT-017) and is a no-op for `NULL`. Its precondition is that every `PlatformWindow` created from this `Platform` has been destroyed. If windows are still live, the backend logs at `ERROR` and returns without releasing anything, so it leaks rather than invalidating pointers the caller still holds (the CORE-ALLOC-092 trade). It logs through the stored logger but never reconfigures or disposes of the allocator or logger (CORE-ALLOC-062, CORE-LOG-002).

**PLATFORM-036 (one live `Platform` per process).** Window-system state is process-global in the OS (the `NSApplication` singleton, Win32 window-class registration, the display connection). At most one `Platform` may be live at a time. Violating this is a caller error and the behaviour is **undefined**. Backends MUST NOT add a process-global flag to detect it (PLATFORM-090). Sequential re-initialization (`platform_init` after `platform_shutdown` in one process) is supported: backends tolerate once-per-process OS setup that persists (the `NSApp` singleton, Win32 DPI awareness, an already-registered window class).

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

ENGINE_NODISCARD EngineResult platform_window_create(PlatformWindow          **out_window,
                                                     Platform                 *platform,
                                                     const EngineAllocator    *allocator,
                                                     const PlatformWindowDesc *desc);
void platform_window_destroy(PlatformWindow *window);
bool platform_window_should_close(const PlatformWindow *window);
ENGINE_NODISCARD EngineResult platform_window_get_framebuffer_size(const PlatformWindow *window,
                                                                   uint32_t *out_width,
                                                                   uint32_t *out_height);
```

**PLATFORM-040 (desc layout).** `PlatformWindowDesc` crosses into a backend, so it has no `bool` field (CORE-TYPES-011) and options are `uint32_t` flag bits (CORE-TYPES-012). specs.md's `bool resizable` becomes `PLATFORM_WINDOW_RESIZABLE`. specs.md's `fullscreen` is dropped for this milestone: the slice does not need it, and display-mode switching is a separate contract. Fields are appended, never reordered.

**PLATFORM-041 (`platform_window_create` results).** In precedence order: `INVALID_ARGUMENT` for a `NULL` `platform`, `desc`, `desc->title`, or `out_window`, a zero `width` or `height`, any unknown `flags` bit, or a resolved allocator with a `NULL` callback. Then `NOT_SUPPORTED` for any OS failure (PLATFORM-034). Then `OUT_OF_MEMORY`. Failure leaves `*out_window == NULL` (when `out_window` is non-`NULL`) and the `Platform` unchanged. On success the window is visible, and `desc` and `desc->title` may be released immediately: the backend copies or consumes the title during the call. Creation queues **no** event: OS notifications delivered during creation (Win32 `WM_SIZE` on create/show) are absorbed, and the initial size is read with the query. Invalid UTF-8 in `title` is a caller error that MUST NOT crash. The backend MAY substitute U+FFFD, and the call does not fail because of it.

**PLATFORM-042 (sizes are requests; the framebuffer is authoritative).** `width`/`height` request a client-area size in the OS's logical (DPI-independent) units. The OS may clamp or scale them. The GPU surface size is `platform_window_get_framebuffer_size`, which is **always physical pixels**. How a backend obtains DPI-correct pixels (macOS backing scale, Windows DPI awareness) is backend-internal. The query returns `INVALID_ARGUMENT` for any `NULL` pointer, writing nothing, and `SUCCESS` otherwise. A minimized or zero-area window MUST report `0 × 0` on every backend, which is a valid result and not an error (§13 edge case 3); where the OS keeps the view's bounds (macOS), the backend tracks miniaturize/deminiaturize itself. Every change in the value this query reports (an OS resize, a DPI or backing-scale change that alters the pixel size, minimize, restore) queues `FRAMEBUFFER_RESIZED` with the new value (PLATFORM-052).

**PLATFORM-043 (the close button never destroys).** When the user or OS asks to close a window (the close button, Alt+F4, Cmd+W, a window-manager close), the backend MUST NOT destroy it. It sets the window's sticky close flag and, the first time the flag is set, queues `PLATFORM_EVENT_WINDOW_CLOSE_REQUESTED` (§6). An application-level quit (Cmd+Q, the Quit menu) does this for every live window. Only `platform_window_destroy` destroys a window. The application decides whether to honour the request, but cannot cancel it: `platform_window_should_close` returns the sticky flag, which is never cleared (a confirm-on-close flow needs an amendment), and returns `false` for `NULL`.

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

**PLATFORM-052 (coalescing bounds the queue).** The queue is a fixed array allocated at `platform_init`; its capacity is backend-private. Pumping allocates nothing. At most one `FRAMEBUFFER_RESIZED` per window is queued at a time: a newer resize updates the queued entry's size in place. `CLOSE_REQUESTED` is queued at most once per window lifetime, when the close flag is first set (PLATFORM-043); later close requests queue nothing. If the queue is full, the new event is dropped and the backend logs at `WARN`. This loses nothing permanently, because **window state is authoritative and events are only notifications**: `platform_window_should_close` and `platform_window_get_framebuffer_size` always report the current truth.

**PLATFORM-053 (window pointer validity).** `event.window` is borrowed. Because destroying a window purges its events (PLATFORM-044), every event `platform_next_event` returns refers to a live window.

**PLATFORM-054 (closed event set; values stable).** `PlatformEventType` values are fixed as written, appended only, and never renumbered. `PLATFORM_EVENT_NONE` is never queued. Input events are a future amendment (PLATFORM-010).

---

## 7. Decision 6 — The native-handle boundary

### 7.1 Who sees native handles

Only **platform backends** (which produce them), **GPU backends** (which create a surface from them; OS types only in their surface files, ADR-010), and the platform conformance tests under `tests/platform/` (read-only, PLATFORM-100) see native handles. The renderer, core, `api.h`, the GPU API header, and every other test never do. The GPU API's future surface-creation entry point takes the opaque `PlatformWindow *`, and the GPU *backend* extracts native handles internally. specs.md §17's alternative, where the renderer fetches a `NativeWindowHandle` and passes it to the GPU API, is rejected: it would put a native-handle type in renderer code, which `rules/renderer.md` forbids ("no backend-native types").

### 7.2 Shape

```c
/* src/platform/native.h — included ONLY by platform backends, GPU backends, and tests/platform/ */
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

**PLATFORM-072 (ownership and permitted use).** The platform owns every native object. A GPU backend MUST NOT destroy, release, retain beyond the window's life, re-parent, subclass, or replace the window procedure or delegate of any of them. Exactly one mutation is sanctioned: on `COCOA`, the GPU backend MAY install a `CAMetalLayer` as the view's layer (`wantsLayer`/`layer`). This keeps QuartzCore/Metal headers out of the platform backend (`rules/gpu.md`: "SDK headers stay inside backends", meaning GPU backends), and it serves both the Metal backend and Vulkan-over-MoltenVK (`VK_EXT_metal_surface`).

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
| `engine_platform_win32` / `engine_platform_macos` / `engine_platform_linux` (STATIC) | `src/platform/win32/`, `src/platform/macos/`, `src/platform/linux/` (these names replace specs.md's `windows/`) | `engine_platform_api`, and `engine_foundation` directly and PUBLIC (CORE-ALLOC-082) |
| `engine_platform` (ALIAS) | the backend selected by the cache variable `NAYLA_PLATFORM_BACKEND`, which defaults to the host OS | — |

Until the §11 Linux ADR lands, `engine_platform_linux` implements only time and thread identity (§3). Its `platform_init` logs at `ERROR` and returns `ENGINE_RESULT_NOT_SUPPORTED` after argument validation; every other entry point is defined and follows its `NULL`-argument rule. This keeps the default Linux configure and link working.

**PLATFORM-093 (who may link what).** Dependencies flow downward only (specs.md §4.1):
- only executables link `engine_platform`: the application and the platform conformance tests (PLATFORM-100). No library target links `engine_platform` or a backend;
- `engine_core` MAY link `engine_platform_api` and MUST NOT link `engine_platform` or a backend;
- `engine_renderer` MUST NOT link `engine_platform_api`, `engine_platform`, or any backend, even transitively;
- GPU backend libraries declare a dependency on `engine_platform_api` only (for `native.h`) and MUST NOT link `engine_platform` or a backend. Their reference to `platform_window_get_native` stays unresolved in the library and is resolved by the final executable's link, which links both. That executable lists the GPU backend before `engine_platform` in `target_link_libraries`, so single-pass linkers (GNU ld) resolve it. `engine_gpu_api` MAY forward-declare `PlatformWindow` and MUST NOT include `native.h`.

**PLATFORM-094 (header discipline).** `api.h` includes exactly `<stdbool.h>`, `<stddef.h>`, `<stdint.h>`, `src/core/result.h`, `src/core/allocator.h`, and `src/core/logging.h`. `native.h` includes `api.h` and `<stdint.h>`. Neither names an OS or SDK type: native types appear only as comments in the table. OS headers are included directly only under `src/platform/<os>/` and in a GPU backend's surface files, `src/gpu/<backend>/surface_<type>.c` or `.m` with `<type>` one of `win32`, `cocoa`, `x11`, `wayland` (ADR-010). They are never included by `src/core/`, `src/renderer/`, `api.h`, `native.h`, or `src/gpu/api.h`. Backends are subject to `forbid_direct_allocation` and `forbid_direct_output` with no exemption, and platform backends never include a GPU SDK header.

---

## 10. Conformance

**PLATFORM-100 (one suite, every backend).** A backend conforms when two CTest tests pass on its target OS: `contract/platform_stateless` (`tests/platform/test_platform_stateless.c`, the invariants tagged *(stateless)* in §12) and `contract/platform_window` (`tests/platform/test_platform_window.c`, those tagged *(window)*). The `contract/` name prefix makes `tools/factory test contract` run them. Both use `tests/check.h`, are written only against `api.h`/`native.h`, link `engine_platform`, and are built unchanged for every backend. They inject a leak-tracking allocator (ALLOC-INV-12 pattern) and a capture-sink logger. `tests/platform/` is the only test directory permitted to link a platform backend or include `native.h`, and it reads the native record only to check it (PLAT-INV-11); this relaxes nothing for core, renderer, or `api.h`.

**PLATFORM-101 (skip only on a declared headless environment).** `contract/platform_stateless` never skips. `contract/platform_window` exits with CTest's skip code (`SKIP_RETURN_CODE 77`), printing the reason, only when `platform_init` returns `NOT_SUPPORTED` **and** the environment variable `NAYLA_TEST_NO_DISPLAY` is set to `1`. Without that declaration a display is required, and `NOT_SUPPORTED` from `platform_init` is a FAIL, so a broken backend on a machine with a display cannot pass as a skip (`rules/testing.md`). CI jobs declare it: the Ubuntu jobs set it until the §11 Linux ADR lands (as must anyone running the suite on Linux, whose pre-ADR backend has no windowing); the macOS jobs do not (GitHub's macOS runners have a window session; if that proves false, the job sets it and the gap is reported, not hidden). There is no Windows CI runner, so the win32 suite runs only on developer machines until one is added.

**PLATFORM-102 (no backend-specific assertions).** A conformance assertion MUST NOT branch on the backend. Harness code is not an assertion: starting the second thread for PLAT-INV-6/7 MAY select host thread primitives (C11 `<threads.h>`, pthreads, Win32) by `#if` in one test-only helper. A behaviour that cannot be asserted identically on all backends is either specified here as unspecified (PLATFORM-021) or is a spec gap to escalate. Weakening the test is never the fix.

---

## 11. Open ADR dependencies and deferred decisions

1. **Linux windowing strategy: X11, Wayland, or both.** specs.md §69 requires an ADR, and this spec deliberately does not decide it. The contract already accommodates every outcome: `X11` and `WAYLAND` native types are reserved (PLATFORM-071), and GPU backends degrade with `NOT_SUPPORTED` on a type they lack. `engine_platform_linux`'s windowing (it ships time and thread identity only, PLATFORM-092), its CI display environment, Linux window conformance (PLATFORM-101), and the behaviour when the window-system connection is lost mid-run (X server or compositor exit; `platform_pump_events` returns `void` and cannot report it) are blocked on that ADR. Nothing else in this spec is.
2. **macOS backend language.** The contract is C. Whether `src/platform/macos/` uses Objective-C translation units or C with the Objective-C runtime is decided by ADR when that backend is written. Either way, nothing Objective-C appears in `api.h` or `native.h`, and the contract is unaffected.
3. **A dedicated result code for OS failures (decided: not now).** PLATFORM-034 reuses `ENGINE_RESULT_NOT_SUPPORTED` for every OS-call failure rather than amending `result.md`. Reasons: CORE-RESULT-008 adds a code only when callers must tell two cases apart, and in this milestone every production caller handles both the same way (log and exit); the OS detail is logged where it happens. The one caller that must tell them apart, the conformance suite, does so by environment declaration (PLATFORM-101), not by a new code. Known cost: "the OS refused" and "this platform lacks the feature" are indistinguishable from the code alone. Revisit when a caller must branch on the difference (retry vs. degrade, a headless case beyond PLATFORM-101, or a GPU backend separating "no capability" from "driver call failed"). The change is then a `result.md` amendment (e.g. `ENGINE_RESULT_PLATFORM_ERROR`) plus switching the call sites; the cost grows with the number of callers, so decide before many backends exist. Tracked in issue #18.

---

## 12. Invariants

**PLAT-INV-1.** `PLATFORM_EVENT_NONE == 0`, `WINDOW_CLOSE_REQUESTED == 1`, `WINDOW_FRAMEBUFFER_RESIZED == 2`; `PLATFORM_NATIVE_WINDOW_NONE == 0` … `WAYLAND == 4`. *(static_assert)*

**PLAT-INV-2.** No file under `src/core`, `src/renderer`, `src/platform/api.h`, `src/gpu/api.h`, or `tests/` outside `tests/platform/` includes `platform/native.h` or names `PlatformNativeWindow`, `PlatformNativeObject`, or `platform_window_get_native`. *(grep)*

**PLAT-INV-3.** (a) The `#include` lines of `src/platform/api.h` and `src/platform/native.h` are exactly PLATFORM-094's sets (the LOG-INV-10 pattern), and the headers contain no `void *` other than callback `user_data` parameters (TYPE-INV-8, CORE-TYPES-016), no `bool` struct field, no `u32`, and no `_t`-suffixed engine type. (b) One forbidden-include list, matched case-insensitively on `#include` and `#import` lines: `windows.h`, `win*.h`, `unistd.h`, `pthread.h`, `dlfcn.h`, `<sys/`, `<mach/`, `<objc/`, `<AppKit/`, `<Cocoa/`, `<Foundation/`, `<CoreFoundation/`, `<X11/`, `<xcb/`, `<wayland-` (angle-bracket form only, so an engine-relative path such as `"src/core/foundation/..."` never matches), `vulkan_win32.h`, `vulkan_metal.h`, `vulkan_xlib.h`, `vulkan_xcb.h`, `vulkan_wayland.h`, plus the token `VK_USE_PLATFORM_` anywhere. It applies with zero exemptions to `src/core`, `src/renderer`, `src/platform/api.h`, `src/platform/native.h`, and `src/gpu/api.h`, and to each `src/gpu/<backend>/` with only basenames matching `^surface_(win32|cocoa|x11|wayland)\.(c|m)$` exempt (ADR-010). (c) No file under `src/platform/<os>/` includes `vulkan`, `d3d1`, `dxgi`, `Metal/`, or `QuartzCore/` (case-insensitive). *(grep — TYPE-INV-1/3/8, CORE-TYPES-011; this list replaces `forbid_os_gpu_includes`' lowercase-only OS patterns)*

**PLAT-INV-4.** `engine_renderer` does not link `engine_platform_api`, `engine_platform`, `engine_platform_win32`, `engine_platform_macos`, or `engine_platform_linux`. `engine_core` and every GPU backend library do not link `engine_platform` or any of those three backends. *(build-graph — `forbid_transitive_link`, called once per forbidden name, the alias included, because it compares target-name strings and does not see through an `ALIAS`. It is a call-time snapshot, so the calls are placed after every target exists, at the end of `src/CMakeLists.txt`.)*

**PLAT-INV-5.** The only Platform API function declarations without a `Platform` or `PlatformWindow` pointer parameter (`Platform *`, `Platform **`, `PlatformWindow *`, `PlatformWindow **`) are the four functions of PLATFORM-024/025. *(grep over `api.h`)*

**PLAT-INV-6.** `platform_time_now_ns` is non-decreasing across 10⁶ consecutive calls on one thread and across a happens-before edge between two threads. It never returns `UINT64_MAX`. A `sleep`-free busy loop eventually observes an increase. *(conformance: stateless)*

**PLAT-INV-7.** `platform_thread_current_id` is non-zero, equal across calls on one thread, and different between two concurrently live threads. *(conformance: stateless — thread start via the PLATFORM-102 helper)*

**PLAT-INV-8.** With the PLATFORM-025 adapters installed and no `Platform` created, a captured record has `timestamp_ns != ENGINE_LOG_TIMESTAMP_UNKNOWN` and `thread_id != ENGINE_LOG_THREAD_UNKNOWN`. *(conformance: stateless)*

**PLAT-INV-9.** `platform_init(NULL, …)` returns `INVALID_ARGUMENT`. Where the window-system probe succeeds, an always-`NULL` allocator makes `platform_init` return `OUT_OF_MEMORY` and leaves `*out_platform == NULL` (PLATFORM-033 order). A leak-tracking allocator sees equal alloc/free counts after `platform_window_destroy` + `platform_shutdown`, including across a second `platform_init`/`platform_shutdown` cycle (PLATFORM-036). A `NULL` logger produces no output and no crash. *(conformance: window)*

**PLAT-INV-10.** `platform_window_create` returns `INVALID_ARGUMENT` for each PLATFORM-041 case (zero width, zero height, `NULL` title, unknown flag bit) and leaves `*out_window == NULL`. *(conformance: window)*

**PLAT-INV-11.** A newly created window has `should_close == false`, a non-zero framebuffer size (unless minimized), and no queued event (PLATFORM-041). `platform_window_get_native` returns `SUCCESS` with `type != NONE`, and the fields match the table row for the returned `type`: every field the row gives as `NULL` or `0` holds that value, and every other field is non-`NULL`/non-zero. *(conformance: window)*

**PLAT-INV-12.** `platform_next_event` on an empty queue returns `false` without writing. After `platform_window_destroy`, no queued event refers to the destroyed window. The second clause is non-vacuous only when an event was pending, and the API has no portable way to synthesize one (no input or resize call), so PLATFORM-044's purge is also review-enforced. *(conformance: window)*

**PLAT-INV-13.** `platform_shutdown(NULL)`, `platform_window_destroy(NULL)`, and `platform_pump_events(NULL)` return normally. `platform_window_should_close(NULL)` returns `false`. *(conformance: stateless)*

**PLAT-INV-14.** No file under `src/platform` declares a file-scope non-`const` variable. *(grep — PLATFORM-090; `static const` permitted)*

**PLAT-INV-15.** GPU backends perform no native-object mutation other than PLATFORM-072's `CAMetalLayer` installation. *(review-enforced)*

---

## 13. Edge cases

1. **Logging before any `Platform` exists:** timestamps and thread ids are real, because the sources are stateless (PLATFORM-024/026).
2. **Logging from a non-platform thread:** the sources are `THREAD_SAFE`, so the record is correct. Whether the *sinks* are safe on that thread is CORE-LOG-071's question, not the platform's.
3. **Minimized window:** framebuffer `0 × 0` on every backend, a `FRAMEBUFFER_RESIZED` to `0 × 0` is queued on minimize and one with the restored size on restore (PLATFORM-042), and the swapchain must not be recreated at zero size. That is the renderer/GPU contract's obligation, signalled here.
4. **Resize storm during a drag:** coalesced to one queued event per window (PLATFORM-052), and the query returns the final size.
5. **Close requested, application ignores it:** the window stays and `should_close` stays `true` (PLATFORM-043). Only the first request queues `CLOSE_REQUESTED`; later ones queue nothing (PLATFORM-052).
6. **`platform_shutdown` with live windows:** logs `ERROR` and leaks (PLATFORM-035).
7. **Window destroyed while a GPU surface still uses it:** undefined behaviour and undetectable (PLATFORM-073). The lifecycle ordering (§8) prevents it.
8. **Second `Platform` in one process:** undefined behaviour, deliberately not detected (PLATFORM-036/090). Sequential re-initialization is supported.
9. **`platform_init` on a non-initial thread:** violates PLATFORM-080; behaviour is undefined.
10. **Event queue full after coalescing:** the event is dropped with a `WARN`. State queries stay correct (PLATFORM-052).
11. **Win32 modal move/size loop or macOS live resize:** `platform_pump_events` may not return until the interaction ends, and frames stall. Rendering during the interaction is out of scope (PLATFORM-050).
12. **Huge uptime, high-frequency counter:** conversion cannot overflow (PLATFORM-022). `UINT64_MAX` ns is about 584 years.
13. **Clock across system suspend:** unspecified, and frame code clamps the delta (PLATFORM-021).
14. **GPU backend given `X11` or `WAYLAND` before the Linux ADR lands:** returns `NOT_SUPPORTED` (PLATFORM-071).
15. **Invalid UTF-8 title:** no crash and no failure. The backend may substitute U+FFFD (PLATFORM-041).

---

## 14. Migration impact

| File | Change |
|---|---|
| `src/platform/api.h` | Replace the placeholder with §3–§6 declarations. The TASK-007 normative block stays verbatim. |
| `src/platform/native.h` (new) | §7.2. |
| `src/platform/CMakeLists.txt` | `engine_platform_api` links `engine_foundation`. Add backend targets, `NAYLA_PLATFORM_BACKEND`, the `engine_platform` alias, and the checks for PLAT-INV-2/3/14. The OS-include check must scope to the top-level headers, not backend subdirectories (`forbid-pattern.sh` is recursive). |
| `src/CMakeLists.txt` | At the end, after every target exists: the PLAT-INV-4 `forbid_transitive_link` calls, one per forbidden name. |
| `cmake/Common.cmake`, `forbid-pattern.sh` (follow-up build task) | PLAT-INV-3(b) needs a regex-exemption variant of `forbid-pattern.sh`, which today exempts one literal basename. `forbid_transitive_link`'s message hard-codes `ALLOC-INV-11` and must be generalized (take the invariant ID) before reuse. |
| `main.c` | Install the adapters after `engine_log_init`, then `platform_init`/window/loop/teardown per PLATFORM-083. On Linux the application logs and exits with `NOT_SUPPORTED` until the ADR. |
| `tests/platform/` (new), `tests/CMakeLists.txt` | The two conformance tests (PLATFORM-100); `add_subdirectory(platform)`. |
| CI (`ci.yml`) | Ubuntu jobs set `NAYLA_TEST_NO_DISPLAY=1` (PLATFORM-101). |

The suggested order is: header + `native.h` + static/grep checks (no backend needed), then the time/thread-only `engine_platform_linux` plus `engine_platform_win32` and `engine_platform_macos` with the conformance suite, then `main.c`. Linux windowing waits for its ADR. No existing behaviour changes.
