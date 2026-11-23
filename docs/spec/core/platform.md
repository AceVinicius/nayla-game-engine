# Platform API — Contract Specification

**Status:** Accepted (2026-11-21). Implemented and conformance-tested on macOS (PRs #37–#43); the Win32 backend (#34) and Linux windowing (decided by ADR-012; #57, #59) remain.
**Date:** 2026-11-20
**Amended:** 2026-11-21 (#45): `platform_wait_events` (PLATFORM-055/056, PLAT-INV-13 NULL clause, PLAT-INV-16).
**Amended:** 2026-11-22 (#55): ADR-012 Linux windowing (PLATFORM-036, -042, -043, -052, -071, -072, -090, -092, -101, PLAT-INV-11, §6 occlusion note, §11 item 1, §13 edge cases 3, 14, 19, §14).
**Author:** `spec-writer`
**Scope:** the Platform API contract for the first milestone (specs.md §47/§48): platform lifecycle, windows, events, monotonic time, thread identity, and the native-handle boundary to the future GPU surface. Binding on `src/platform/api.h`, `src/platform/native.h`, and every OS backend.
**Supersedes:** specs.md §13 ("Platform Abstraction"), §14 ("Platform Interface"), §15 ("Platform Services"), §16 ("Platform Backend Contract"), §17 ("Native Handle Boundary"), the `platform_initialize`/`platform_shutdown` names in §37, and `platform_poll_events` in §38. specs.md remains a historical draft; where it disagrees with this document, **this document wins**. **Amends** `logging.md` CORE-LOG-082 (installing the context sources after platform initialization is no longer required, and records before that point need not carry the "unknown" sentinels, PLATFORM-026/083) and its edge case 12; both carry a note pointing here. **Widens** `result.md` §4's meaning of `ENGINE_RESULT_NOT_SUPPORTED` to OS-call failures (PLATFORM-034, issue #18); result.md carries a note.
**Depends on:** `types.md` (CORE-TYPES-011/012 no `bool` in cross-backend structs, flags are `uint32_t`; CORE-TYPES-013/014 no pointer-through-integer; CORE-TYPES-016 `void *` limits; CORE-TYPES-018 `uint64_t` timestamps; CORE-TYPES-021..025 naming and enum width). `result.md` (CORE-RESULT-005/008 closed code set, -012 precedence, -013 out-parameters, -015 `ENGINE_NODISCARD`, -017 infallible returns `void`). `allocator.md` (CORE-ALLOC-021 thread-safety vocabulary, -060..070 borrow-and-store, -080..083 targets). `logging.md` (CORE-LOG-003/004/005 borrowed logger and `NULL` discards, CORE-LOG-040..043 context sources, CORE-LOG-082 lifecycle, CORE-LOG-084 construction parameters).
**Related:** CLAUDE.md invariants #1–#6; `rules/platform.md`; `rules/core.md` ("Keep it simple"); ADR-009; ADR-010 (OS headers in GPU surface files); ADR-011 (macOS backend language, resolves §11 item 2, amends PLAT-INV-3(c) and PLAT-INV-14); ADR-012 (Linux windowing, resolves §11 item 1); the normative allocator block in `src/platform/api.h` (TASK-007), which this spec incorporates unchanged.

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
| Events | **Yes** | Close and framebuffer resize drive the loop and swapchain recreation. Includes the blocking wait (PLATFORM-055, #45), called by `main.c`'s loop so an idle or minimized window does not spin a core. Input (keyboard, mouse, gamepad, touch) is **out**: the triangle takes no input. |
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

**PLATFORM-036 (one live `Platform` per process).** Window-system state is process-global in the OS (the `NSApplication` singleton, Win32 window-class registration, the display connection). At most one `Platform` may be live at a time. Violating this is a caller error and the behaviour is **undefined**. Backends MUST NOT add a process-global flag to detect it (PLATFORM-090). Sequential re-initialization (`platform_init` after `platform_shutdown` in one process) is supported: backends tolerate once-per-process OS setup that persists (the `NSApp` singleton, Win32 DPI awareness, an already-registered window class, GTK loaded by libdecor's plugin, which stays until process exit, ADR-012 Decision 2).

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

**PLATFORM-042 (sizes are requests; the framebuffer is authoritative).** `width`/`height` request a client-area size in the OS's logical (DPI-independent) units. The OS may clamp or scale them. The GPU surface size is `platform_window_get_framebuffer_size`, which is **always physical pixels**. How a backend obtains DPI-correct pixels (macOS backing scale, Windows DPI awareness) is backend-internal. The query returns `INVALID_ARGUMENT` for any `NULL` pointer, writing nothing, and `SUCCESS` otherwise. A minimized or zero-area window MUST report `0 × 0` on every backend, which is a valid result and not an error (§13 edge case 3); where the OS keeps the view's bounds (macOS), the backend tracks miniaturize/deminiaturize itself. X11 has no logical units: the requested size is in pixels. "Minimized" is X11 `WM_STATE == IconicState`, and on Wayland, which has no minimized state, an `xdg_toplevel` in the `suspended` state (xdg-shell v6). `suspended` also covers full occlusion, screen lock, and outputs turned off, so on Wayland those report `0 × 0` too. Without xdg-shell v6 a minimized Wayland window keeps a non-zero size (degraded, ADR-012 Decision 3). Every change in the value this query reports (an OS resize, a DPI or backing-scale change that alters the pixel size, minimize, restore) queues `FRAMEBUFFER_RESIZED` with the new value (PLATFORM-052).

**PLATFORM-043 (the close button never destroys).** When the user or OS asks to close a window (the close button, Alt+F4, Cmd+W, a window-manager close), the backend MUST NOT destroy it. It sets the window's sticky close flag and, the first time the flag is set, queues `PLATFORM_EVENT_WINDOW_CLOSE_REQUESTED` (§6). An application-level quit (Cmd+Q, the Quit menu) does this for every live window. Loss of the window-system connection (X server or compositor exit, a fatal display error) is an application-level quit: the backend logs `ERROR` once with the detail, sets every live window's sticky close flag, and queues `CLOSE_REQUESTED` for each window whose flag was not already set. The `Platform` is then in a terminal **lost** state, in which the backend makes no further window-system protocol call: `platform_pump_events` and `platform_wait_events` return at once without blocking; `platform_window_create` logs `ERROR` and returns `NOT_SUPPORTED` (PLATFORM-034); the size and native queries return the last values (PLATFORM-073), and those native values must not be used to create new surfaces; `platform_window_destroy` and `platform_shutdown` still release every engine allocation (PLAT-INV-9). The rule binds every backend; Win32 and Cocoa never trigger it (ADR-012 Decision 6). Only `platform_window_destroy` destroys a window. The application decides whether to honour the request, but cannot cancel it: `platform_window_should_close` returns the sticky flag, which is never cleared (a confirm-on-close flow needs an amendment), and returns `false` for `NULL`.

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
void platform_wait_events(Platform *platform, uint64_t timeout_ns);
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

**PLATFORM-052 (coalescing bounds the queue).** The queue is a fixed array allocated at `platform_init`; its capacity is backend-private. Pumping allocates nothing: it makes no `EngineAllocator` allocation, which happen only in `platform_init` and `platform_window_create`. Memory the window-system library allocates internally (AppKit autoreleased objects, Xlib/libwayland/libdecor event buffers and proxies) is library-owned and outside the engine's allocator domain and PLAT-INV-9 (ADR-011, ADR-012 Decision 4). At most one `FRAMEBUFFER_RESIZED` per window is queued at a time: a newer resize updates the queued entry's size in place. `CLOSE_REQUESTED` is queued at most once per window lifetime, when the close flag is first set (PLATFORM-043); later close requests queue nothing. If the queue is full, the new event is dropped and the backend logs at `WARN`. This loses nothing permanently, because **window state is authoritative and events are only notifications**: `platform_window_should_close` and `platform_window_get_framebuffer_size` always report the current truth.

**PLATFORM-053 (window pointer validity).** `event.window` is borrowed. Because destroying a window purges its events (PLATFORM-044), every event `platform_next_event` returns refers to a live window.

**PLATFORM-054 (closed event set; values stable).** `PlatformEventType` values are fixed as written, appended only, and never renumbered. `PLATFORM_EVENT_NONE` is never queued. Input events are a future amendment (PLATFORM-010).

**PLATFORM-055 (wait).** If no OS event is pending, `platform_wait_events` blocks until one arrives or `timeout_ns` elapses, whichever comes first. It then does exactly what `platform_pump_events` does (PLATFORM-050/052), including running for the length of a modal interaction. `timeout_ns == 0` behaves as `platform_pump_events`. `UINT64_MAX` means no limit. It MAY return early for any reason, such as an OS wakeup that queues no engine event, so a caller re-checks window state after it returns (PLATFORM-052) and never infers from the return that an event is queued or that the timeout elapsed. It reports no OS errors (PLATFORM-034). It is a no-op for `NULL`: it returns at once without blocking. It allocates nothing, in PLATFORM-052's sense. It is `THREAD_AFFINE` to the platform thread (PLATFORM-081). PLATFORM-050 is unchanged: `platform_pump_events` never waits. Every backend can implement the wait unconditionally, so there is no capability query. *Rationale (#45):* vsync cannot pace the idle case, because a minimized window reports `0 × 0` (PLATFORM-042) and must not present (§13 edge case 3), so nothing else would block the loop. A sleep adds input latency, and a timeout parameter on the pump would change PLATFORM-050 and every caller.

**PLATFORM-056 (timeout granularity).** A non-zero `timeout_ns` is rounded **up** to the OS wait granularity (milliseconds on Win32 and `poll`), never down to `0`: rounding to `0` turns the wait into a pump and reintroduces the spin. The bound applies to the **blocking phase** only: outside a modal interaction (PLATFORM-050), the call stops waiting within `timeout_ns` + one granularity unit + scheduler latency. The pump that follows (PLATFORM-055) then processes the pending events exactly as `platform_pump_events` would, and that processing time is not part of the bound: a backend MUST NOT truncate event processing to meet the timeout. The contract gives no numeric bound for scheduler latency or for pump processing. A finite timeout larger than the OS wait argument can hold is clamped to that range, and clamping MUST NOT turn a finite timeout into an infinite wait or into `0`.

Recommended loop shape. Until rendering exists, the wait is the tick. With rendering, the loop waits only while there is nothing to present, and otherwise pumps and lets present pace it:

```c
/* until GPU rendering lands */
platform_wait_events(platform, TICK_NS);

/* with rendering */
if (fb_width == 0 || fb_height == 0) platform_wait_events(platform, IDLE_NS);   /* minimized */
else                                 platform_pump_events(platform);            /* present paces */
```

On Wayland with xdg-shell v6, a fully occluded window is `suspended` and reports `0 × 0` (PLATFORM-042), so the idle wait paces it (ADR-012 Decision 3). On Win32, Cocoa, X11, and Wayland without v6, a window that is hidden but not minimized (occluded) keeps a non-zero framebuffer, so this amendment does not pace it; pacing it is the GPU contract's job, as an occluded or skipped present result (#45). `IDLE_NS` and `TICK_NS` are the application's constants, not contract. The values chosen in #45 are 100 ms idle and 16.7 ms pre-GPU tick.

---

## 7. Decision 6 — The native-handle boundary

### 7.1 Who sees native handles

Only **platform backends** (which produce them), **GPU backends** (which create a surface from them; OS types only in their surface files, ADR-010), and the tests under `tests/platform/` (read-only, PLATFORM-100) see native handles. The renderer, core, `api.h`, the GPU API header, and every other test never do. The GPU API's future surface-creation entry point takes the opaque `PlatformWindow *`, and the GPU *backend* extracts native handles internally. specs.md §17's alternative, where the renderer fetches a `NativeWindowHandle` and passes it to the GPU API, is rejected: it would put a native-handle type in renderer code, which `rules/renderer.md` forbids ("no backend-native types").

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

**PLATFORM-071 (the table is the contract).** Each `type` populates exactly the fields in the table, and the others are `NULL`/`0`. Win32 produces `WIN32` and macOS produces `COCOA`. Linux produces `X11` or `WAYLAND`: `platform_init` chooses one window system per ADR-012 Decision 1, and every window of that `Platform` reports that one `type` for the `Platform`'s whole life. The backend-specific environment variable `NAYLA_LINUX_WINDOW_SYSTEM` (PLATFORM-092) can force the choice; it is not part of the API. A GPU backend that receives a `type` it does not implement returns `ENGINE_RESULT_NOT_SUPPORTED` (CLAUDE.md invariant #6).

**PLATFORM-072 (ownership and permitted use).** The platform owns every native object. A GPU backend MUST NOT destroy, release, retain beyond the window's life, re-parent, subclass, or replace the window procedure or delegate of any of them. Exactly one mutation is sanctioned: on `COCOA`, the GPU backend MAY install a `CAMetalLayer` as the view's layer (`wantsLayer`/`layer`). This keeps QuartzCore/Metal headers out of the platform backend (`rules/gpu.md`: "SDK headers stay inside backends", meaning GPU backends), and it serves both the Metal backend and Vulkan-over-MoltenVK (`VK_EXT_metal_surface`). Presenting through the window system's swapchain mechanism (WSI buffer attach/commit on the `wl_surface`, Present/DRI3 on the X window) is use, not a mutation. The `wp_viewport`, the `buffer_scale`, and the `xdg_toplevel` state stay the platform's (ADR-012 Decision 6).

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
| every other Platform API function, including `platform_window_get_native`, `platform_pump_events`, and `platform_wait_events` | `THREAD_AFFINE` to the platform thread |

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

**PLATFORM-090 (no engine state at file scope).** No backend translation unit holds file-scope mutable engine state. Per-window lookup from an OS callback uses OS per-window storage (Win32 `GWLP_USERDATA`, a Cocoa delegate object, a list owned by the `Platform`), never a global table. OS-owned process singletons (`NSApp`) are the OS's state and are outside this rule, as is library-owned process state that ADR-012 accepts: the state `XInitThreads` sets, the fallback process-global `XSetIOErrorHandler` if #57 needs it (it touches no engine state), and GTK loaded by libdecor's plugin (ADR-012 Decisions 2, 4, 6). No engine state lives at file scope either way.

**PLATFORM-091 (nothing joins `engine_foundation`).** `engine_foundation` still depends on nothing (CORE-ALLOC-081). The logger receives time and thread ids through callbacks (CORE-LOG-041). No clock or thread-id code moves into foundation, and foundation does not link any platform target.

**PLATFORM-092 (targets).**

| Target | Contents | Links |
|---|---|---|
| `engine_platform_api` (INTERFACE, exists) | `src/platform/api.h`, `src/platform/native.h` | `engine_foundation` |
| `engine_platform_win32` / `engine_platform_macos` / `engine_platform_linux` (STATIC) | `src/platform/win32/`, `src/platform/macos/`, `src/platform/linux/` (these names replace specs.md's `windows/`), each plus the shared OS-free `src/platform/common/` | `engine_platform_api`, and `engine_foundation` directly and PUBLIC (CORE-ALLOC-082) |
| `engine_platform` (ALIAS) | the backend selected by the cache variable `NAYLA_PLATFORM_BACKEND`, which defaults to the host OS | — |

Per ADR-012 Decision 1, `engine_platform_linux` contains both a Wayland path and an X11 path, with no build option to drop either, and `platform_init` selects one. The environment variable `NAYLA_LINUX_WINDOW_SYSTEM` is backend-specific, not API: `wayland` or `x11` tries only that path, with no fallback; any other non-empty value logs `ERROR` and returns `NOT_SUPPORTED`; unset or empty selects automatically (Wayland first, then X11). It is read once, inside `platform_init`. Until the ADR-012 implementation lands (Linux selection and the X11 path in #57, the Wayland path in #59), `engine_platform_linux` implements only time and thread identity (§3). Its `platform_init` logs at `ERROR` and returns `ENGINE_RESULT_NOT_SUPPORTED` after argument validation; every other entry point is defined and follows its `NULL`-argument rule. This keeps the default Linux configure and link working.

**PLATFORM-093 (who may link what).** Dependencies flow downward only (specs.md §4.1):
- only executables link `engine_platform`: the application and the tests under `tests/platform/` (PLATFORM-100). No library target links `engine_platform` or a backend;
- `engine_core` MAY link `engine_platform_api` and MUST NOT link `engine_platform` or a backend;
- `engine_renderer` MUST NOT link `engine_platform_api`, `engine_platform`, or any backend, even transitively;
- GPU backend libraries declare a dependency on `engine_platform_api` only (for `native.h`) and MUST NOT link `engine_platform` or a backend. Their reference to `platform_window_get_native` stays unresolved in the library and is resolved by the final executable's link, which links both. That executable lists the GPU backend before `engine_platform` in `target_link_libraries`, so single-pass linkers (GNU ld) resolve it. `engine_gpu_api` MAY forward-declare `PlatformWindow` and MUST NOT include `native.h`.

**PLATFORM-094 (header discipline).** `api.h` includes exactly `<stdbool.h>`, `<stddef.h>`, `<stdint.h>`, `src/core/result.h`, `src/core/allocator.h`, and `src/core/logging.h`. `native.h` includes `api.h` and `<stdint.h>`. Neither names an OS or SDK type: native types appear only as comments in the table. OS headers are included directly only under `src/platform/<os>/` and in a GPU backend's surface files, `src/gpu/<backend>/surface_<type>.c` or `.m` with `<type>` one of `win32`, `cocoa`, `x11`, `wayland` (ADR-010). They are never included by `src/core/`, `src/renderer/`, `api.h`, `native.h`, or `src/gpu/api.h`. Backends are subject to `forbid_direct_allocation` and `forbid_direct_output` with no exemption, and platform backends never include a GPU SDK header.

---

## 10. Conformance

**PLATFORM-100 (one suite, every backend).** A backend conforms when two CTest tests pass on its target OS: `contract/platform_stateless` (`tests/platform/test_platform_stateless.c`, the invariants tagged *(stateless)* in §12) and `contract/platform_window` (`tests/platform/test_platform_window.c`, those tagged *(window)*). The `contract/` name prefix makes `tools/factory test contract` run them. Both use `tests/check.h`, are written only against `api.h`/`native.h`, link `engine_platform`, and are built unchanged for every backend. They inject a leak-tracking allocator (ALLOC-INV-12 pattern) and a capture-sink logger. `tests/platform/` is the only test directory permitted to link a platform backend or include `native.h`, and it reads the native record only to check it (PLAT-INV-11); this relaxes nothing for core, renderer, or `api.h`. For that reason `tests/platform/` also holds the other tests that need `native.h` or a backend: the header compile checks `architecture/platform_header_{api,native}_c{17,23}` (each header compiles alone as C17 and C23, PLATFORM-094; PR #37) and `unit/platform_smoke` (links the selected backend, PLATFORM-092; PR #39). Neither is part of conformance.

**PLATFORM-101 (skip only on a declared headless environment).** `contract/platform_stateless` never skips. `contract/platform_window` exits with CTest's skip code (`SKIP_RETURN_CODE 77`), printing the reason, only when `platform_init` returns `NOT_SUPPORTED` **and** the environment variable `NAYLA_TEST_NO_DISPLAY` is set to `1`. Without that declaration a display is required, and `NOT_SUPPORTED` from `platform_init` is a FAIL, so a broken backend on a machine with a display cannot pass as a skip (`rules/testing.md`). Linux CI jobs provide a display server and do not declare headless (ADR-012 Decision 5): Xvfb from #58, plus headless Weston from #60. Until #58 lands, the Ubuntu jobs set it, as must anyone running the suite on Linux before #57, whose backend has no windowing until then. After that, only a person running the suite on a Linux machine with no display server declares it. A window system forced with `NAYLA_LINUX_WINDOW_SYSTEM` (PLATFORM-092) whose server is not running is a FAIL unless declared. The macOS jobs do not declare it (GitHub's macOS runners have a window session; if that proves false, the job sets it and the gap is reported, not hidden). There is no Windows CI runner, so the win32 suite runs only on developer machines until one is added.

**PLATFORM-102 (no backend-specific assertions).** A conformance assertion MUST NOT branch on the backend. Harness code is not an assertion: starting the second thread for PLAT-INV-6/7 MAY select host thread primitives (C11 `<threads.h>`, pthreads, Win32) by `#if` in one test-only helper. A behaviour that cannot be asserted identically on all backends is either specified here as unspecified (PLATFORM-021) or is a spec gap to escalate. Weakening the test is never the fix.

---

## 11. Open ADR dependencies and deferred decisions

1. **Linux windowing strategy (decided: ADR-012).** Both Wayland and X11 in one `engine_platform_linux`, chosen at run time by `platform_init` (PLATFORM-071, -092). ADR-012 also fixes the Linux CI display environment (PLATFORM-101) and connection loss mid-run (PLATFORM-043). The contract, `api.h`, and `native.h` are unchanged; GPU backends degrade with `NOT_SUPPORTED` on a type they lack.
2. **macOS backend language (decided: ADR-011).** `src/platform/macos/` is Objective-C (`.m`) with manual retain/release. Nothing Objective-C appears in `api.h` or `native.h`, and the contract is unaffected. ADR-011 amends PLAT-INV-3(c) and PLAT-INV-14.
3. **A dedicated result code for OS failures (decided: not now).** PLATFORM-034 reuses `ENGINE_RESULT_NOT_SUPPORTED` for every OS-call failure rather than amending `result.md`. Reasons: CORE-RESULT-008 adds a code only when callers must tell two cases apart, and in this milestone every production caller handles both the same way (log and exit); the OS detail is logged where it happens. The one caller that must tell them apart, the conformance suite, does so by environment declaration (PLATFORM-101), not by a new code. Known cost: "the OS refused" and "this platform lacks the feature" are indistinguishable from the code alone. Revisit when a caller must branch on the difference (retry vs. degrade, a headless case beyond PLATFORM-101, or a GPU backend separating "no capability" from "driver call failed"). The change is then a `result.md` amendment (e.g. `ENGINE_RESULT_PLATFORM_ERROR`) plus switching the call sites; the cost grows with the number of callers, so decide before many backends exist. Tracked in issue #18.

---

## 12. Invariants

**PLAT-INV-1.** `PLATFORM_EVENT_NONE == 0`, `WINDOW_CLOSE_REQUESTED == 1`, `WINDOW_FRAMEBUFFER_RESIZED == 2`; `PLATFORM_NATIVE_WINDOW_NONE == 0` … `WAYLAND == 4`. *(static_assert)*

**PLAT-INV-2.** No file under `src/core`, `src/renderer`, `src/platform/api.h`, `src/gpu/api.h`, or `tests/` outside `tests/platform/` includes `platform/native.h` or names `PlatformNativeWindow`, `PlatformNativeObject`, or `platform_window_get_native`. *(grep)*

**PLAT-INV-3.** (a) The `#include` lines of `src/platform/api.h` and `src/platform/native.h` are exactly PLATFORM-094's sets (the LOG-INV-10 pattern), and the headers contain no `void *` other than callback `user_data` parameters (TYPE-INV-8, CORE-TYPES-016), no `bool` struct field, no `u32`, and no `_t`-suffixed engine type. (b) One forbidden-include list, matched case-insensitively on `#include` and `#import` lines: `windows.h`, `win*.h`, `unistd.h`, `pthread.h`, `dlfcn.h`, `<sys/`, `<mach/`, `<objc/`, `<AppKit/`, `<Cocoa/`, `<Foundation/`, `<CoreFoundation/`, `<X11/`, `<xcb/`, `<wayland-` (angle-bracket form only, so an engine-relative path such as `"src/core/foundation/..."` never matches), `vulkan_win32.h`, `vulkan_metal.h`, `vulkan_xlib.h`, `vulkan_xcb.h`, `vulkan_wayland.h`, plus the token `VK_USE_PLATFORM_` anywhere. It applies with zero exemptions to `src/core`, `src/renderer`, `src/platform/api.h`, `src/platform/native.h`, and `src/gpu/api.h`, and to each `src/gpu/<backend>/` with only basenames matching `^surface_(win32|cocoa|x11|wayland)\.(c|m)$` exempt (ADR-010). (c) No `#include` or `#import` line in a file under `src/platform/<os>/` contains `vulkan`, `d3d1`, `dxgi`, `Metal/`, or `QuartzCore/`, and no `@import` line names the module `Metal`, `QuartzCore`, `MetalKit`, or `IOSurface`, submodules included (`@import QuartzCore.CAMetalLayer;`), all case-insensitive (ADR-011 Decision 6). *(grep — TYPE-INV-1/3/8, CORE-TYPES-011; this list replaces `forbid_os_gpu_includes`' lowercase-only OS patterns)*

**PLAT-INV-4.** `engine_renderer` does not link `engine_platform_api`, `engine_platform`, `engine_platform_win32`, `engine_platform_macos`, or `engine_platform_linux`. `engine_core` and every GPU backend library do not link `engine_platform` or any of those three backends. *(build-graph — `forbid_transitive_link`, called once per forbidden name, the alias included, because it compares target-name strings and does not see through an `ALIAS`. It is a call-time snapshot, so the calls are placed after every target exists, at the end of `src/CMakeLists.txt`.)*

**PLAT-INV-5.** The only Platform API function declarations without a `Platform` or `PlatformWindow` pointer parameter (`Platform *`, `Platform **`, `PlatformWindow *`, `PlatformWindow **`) are the four functions of PLATFORM-024/025. *(grep over `api.h`)*

**PLAT-INV-6.** `platform_time_now_ns` is non-decreasing across 10⁶ consecutive calls on one thread and across a happens-before edge between two threads. It never returns `UINT64_MAX`. A `sleep`-free busy loop eventually observes an increase. *(conformance: stateless)*

**PLAT-INV-7.** `platform_thread_current_id` is non-zero, equal across calls on one thread, and different between two concurrently live threads. *(conformance: stateless — thread start via the PLATFORM-102 helper)*

**PLAT-INV-8.** With the PLATFORM-025 adapters installed and no `Platform` created, a captured record has `timestamp_ns != ENGINE_LOG_TIMESTAMP_UNKNOWN` and `thread_id != ENGINE_LOG_THREAD_UNKNOWN`. *(conformance: stateless)*

**PLAT-INV-9.** `platform_init(NULL, …)` returns `INVALID_ARGUMENT`. Where the window-system probe succeeds, an always-`NULL` allocator makes `platform_init` return `OUT_OF_MEMORY` and leaves `*out_platform == NULL` (PLATFORM-033 order). A leak-tracking allocator sees equal alloc/free counts after `platform_window_destroy` + `platform_shutdown`, including across a second `platform_init`/`platform_shutdown` cycle (PLATFORM-036). A `NULL` logger produces no output and no crash. *(conformance: window)*

**PLAT-INV-10.** `platform_window_create` returns `INVALID_ARGUMENT` for each PLATFORM-041 case (zero width, zero height, `NULL` title, unknown flag bit) and leaves `*out_window == NULL`. *(conformance: window)*

**PLAT-INV-11.** A newly created window has `should_close == false`, a non-zero framebuffer size (unless minimized), and no queued event (PLATFORM-041). On Wayland "minimized" means `suspended`, which also covers occlusion (PLATFORM-042); a window is visible on success (PLATFORM-041), so a fresh window is not suspended and the non-zero check holds. A compositor that reports `suspended` already in the first configure (e.g. screen locked) is covered by "unless minimized"; headless Weston may never send `suspended` (ADR-012 Decision 5), so the conformance suite cannot distinguish that case. Without xdg-shell v6 the size is non-zero in every case. `platform_window_get_native` returns `SUCCESS` with `type != NONE`, and the fields match the table row for the returned `type`: every field the row gives as `NULL` or `0` holds that value, and every other field is non-`NULL`/non-zero. *(conformance: window)*

**PLAT-INV-12.** `platform_next_event` on an empty queue returns `false` without writing. After `platform_window_destroy`, no queued event refers to the destroyed window. The second clause is non-vacuous only when an event was pending, and the API has no portable way to synthesize one (no input or resize call), so PLATFORM-044's purge is also review-enforced. *(conformance: window)*

**PLAT-INV-13.** `platform_shutdown(NULL)`, `platform_window_destroy(NULL)`, and `platform_pump_events(NULL)` return normally. `platform_wait_events(NULL, t)` returns normally for a non-zero `t` without sleeping (PLATFORM-055), checked against a generous upper bound. `platform_window_should_close(NULL)` returns `false`. *(conformance: stateless; it runs on headless CI, so it also exercises the stub)*

**PLAT-INV-14.** No `.c`, `.m`, or `.h` file under `src/platform` declares a file-scope non-`const` variable (ADR-011 Decision 6; `.h` because a backend-private header carries its state into every includer, PR #38). *(grep — PLATFORM-090; `static const` permitted)*

**PLAT-INV-15.** GPU backends perform no native-object mutation other than PLATFORM-072's `CAMetalLayer` installation. *(review-enforced)*

**PLAT-INV-16.** On a live `Platform` with a window, a `0` timeout returns without blocking, and a short timeout with no pending input returns within a generous upper bound (PLATFORM-056; with no pending input the pump phase is negligible, so the bound is dominated by the blocking phase). The test asserts upper bounds only, never a lower bound, because early return is allowed (PLATFORM-055), and a wait that ignores its timeout fails within a bounded test time rather than hanging. "Wakes promptly when an event arrives" cannot be asserted portably, because the API cannot synthesize an OS event (PLAT-INV-12, PLATFORM-102), so it stays a manual check: an idle or minimized app uses near-0% CPU and still reacts to resize and close at once. *(conformance: window)*

---

## 13. Edge cases

1. **Logging before any `Platform` exists:** timestamps and thread ids are real, because the sources are stateless (PLATFORM-024/026).
2. **Logging from a non-platform thread:** the sources are `THREAD_SAFE`, so the record is correct. Whether the *sinks* are safe on that thread is CORE-LOG-071's question, not the platform's.
3. **Minimized window:** framebuffer `0 × 0` on every backend (on Wayland, while `suspended`, which also covers occlusion, screen lock, and outputs turned off; without xdg-shell v6 the size stays non-zero; ADR-012 Decision 3), a `FRAMEBUFFER_RESIZED` to `0 × 0` is queued on minimize and one with the restored size on restore (PLATFORM-042), and the swapchain must not be recreated at zero size. That is the renderer/GPU contract's obligation, signalled here.
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
14. **GPU backend given `X11` or `WAYLAND` it does not implement:** returns `NOT_SUPPORTED` (PLATFORM-071). There is no automatic re-selection; the user restarts with `NAYLA_LINUX_WINDOW_SYSTEM` set to the other system (ADR-012 Consequences).
15. **Invalid UTF-8 title:** no crash and no failure. The backend may substitute U+FFFD (PLATFORM-041).
16. **Minimized window in the loop:** the loop waits rather than pumps, because nothing presents at `0 × 0` to pace it (PLATFORM-055). The restore ends the wait, or at the latest the idle timeout does: on macOS a Dock restore may be handled inside the run loop without an `NSEvent` returning (Win32 `QS_ALLINPUT`, X11, and Wayland do wake). Either way the pump then queues `FRAMEBUFFER_RESIZED` with the restored size.
17. **Unbounded wait (`UINT64_MAX`):** returns only on an OS event or an early wakeup. No API wakes it from another thread, so a caller that needs to resume for any other reason passes a finite timeout. An unbounded wait is the caller's risk.
18. **Cross-thread wake:** a `platform_wake` that ends a wait from another thread is deferred until the engine starts threads (PLATFORM-010).
19. **Window-system connection lost mid-run (X server or compositor exit):** every live window's `should_close` becomes `true`, with one `CLOSE_REQUESTED` per window not already closing, and one `ERROR` log. Pump and wait then return at once, window creation returns `NOT_SUPPORTED`, queries report the last state, and destroy/shutdown release everything (PLATFORM-043). The GPU backend sees the loss on its own as surface-lost or out-of-date present results, which the GPU contract handles; the application stops presenting because `should_close` is set (ADR-012 Decision 6).

---

## 14. Migration impact

Done (PRs #37–#43) except the Win32 backend (#34) and Linux windowing (ADR-012: selection and X11 #57, CI Xvfb #58, Wayland #59, CI Weston #60). The #45 amendment (`platform_wait_events`) is implemented by #50, in the rows marked #50.

| File | Change |
|---|---|
| `src/platform/api.h` | Replace the placeholder with §3–§6 declarations. The TASK-007 normative block stays verbatim. |
| `src/platform/native.h` (new) | §7.2. |
| `src/platform/CMakeLists.txt` | `engine_platform_api` links `engine_foundation`. Add backend targets, `NAYLA_PLATFORM_BACKEND`, the `engine_platform` alias, and the build-time greps: PLAT-INV-3(c) over every `src/platform/<os>/` and `src/platform/common/`, and PLATFORM-094's OS-include ban over `src/platform/common/`. PLAT-INV-2, -3(a)/(b) on the top-level headers, -5, and -14 are rows in `tests/architecture/test_core_invariants.c` (a build check cannot attach to the INTERFACE `engine_platform_api`). |
| `src/CMakeLists.txt` | At the end, after every target exists: the PLAT-INV-4 `forbid_transitive_link` calls, one per forbidden name. |
| `cmake/Common.cmake`, `forbid-pattern.sh` (PR #38) | `forbid-pattern.sh` takes a basename-regex exemption and a case-insensitive flag (PLAT-INV-3(b)). `forbid_transitive_link` takes the invariant ID. |
| Root `CMakeLists.txt` (ADR-011) | `enable_language(OBJC)` guarded by `NAYLA_PLATFORM_BACKEND STREQUAL "macos"`. |
| `main.c` | Install the adapters after `engine_log_init`, then `platform_init`/window/loop/teardown per PLATFORM-083. On Linux the application logs and exits with `NOT_SUPPORTED` until the ADR-012 implementation lands (#57). |
| `tests/platform/` (new), `tests/CMakeLists.txt` | The two conformance tests, the header compile checks, and `unit/platform_smoke` (PLATFORM-100); `add_subdirectory(platform)`. |
| `src/platform/api.h` (#50) | Declare `platform_wait_events` (PLATFORM-055). |
| Platform backends (#50) | macOS implements PLATFORM-055/056. `platform_stub.c` (Win32 and Linux until their windowing work) defines a `NULL`-tolerant function that returns at once; no `Platform` exists there. |
| `main.c` (#50) | The loop calls `platform_wait_events` with `TICK_NS` (16.7 ms), or `IDLE_NS` (100 ms) while the framebuffer is `0 × 0`, instead of `platform_pump_events`. Each constant carries a comment citing #45. |
| `tests/platform/` (#50) | The PLAT-INV-13 `platform_wait_events(NULL, t)` row in `contract/platform_stateless`; the PLAT-INV-16 rows in `contract/platform_window`. |
| CI (`ci.yml`) | Until #58, Ubuntu jobs (including sanitizers) set `NAYLA_TEST_NO_DISPLAY=1`. From #58 they run under Xvfb (the sanitizer job with `x11` forced) and stop setting it; #60 adds headless Weston and a forced-`x11` conformance run (ADR-012 Decision 5). macOS jobs do not set it (PLATFORM-101). |

Backend mapping for PLATFORM-055 (informative, not normative; each backend still converts the timeout per PLATFORM-056 and then runs its pump). Backends clamp a large finite timeout to the OS argument range: Win32's `DWORD` milliseconds must stay below `INFINITE`, and `poll` takes `int` milliseconds. The one normative point is PLATFORM-056's: clamping never yields an infinite wait or `0`.

| Backend | Wait mechanism |
|---|---|
| Win32 | `MsgWaitForMultipleObjectsEx(0, NULL, ms, QS_ALLINPUT, MWMO_INPUTAVAILABLE)`. `MWMO_INPUTAVAILABLE` also returns for input already in the queue but not yet seen. |
| macOS | `nextEventMatchingMask:untilDate:dequeue:YES` with the timeout as the date (`distantPast` for `0`, `distantFuture` for `UINT64_MAX`). The event it dequeues is sent, then the normal pump runs. |
| X11 | `XFlush`; if `XEventsQueued(QueuedAfterReading)` is 0, `poll` on `ConnectionNumber(display)`; then pump (ADR-012 Decision 4). |
| Wayland | Pump first with a non-blocking `libdecor_dispatch(ctx, 0)`. If that queued an engine event or changed window state, return without blocking; only otherwise block in `libdecor_dispatch(ctx, ms)`, then pump. `EINTR`/`EAGAIN` are retried or ignored, not loss (ADR-012 Decisions 4 and 6). |

The suggested order is: header + `native.h` + static/grep checks (no backend needed), then the time/thread-only `engine_platform_linux` plus `engine_platform_win32` and `engine_platform_macos` with the conformance suite, then `main.c`. Linux windowing follows ADR-012's order: #57, #58, #59, #60. The one behaviour change is #50's: `main.c`'s loop waits instead of spinning.
