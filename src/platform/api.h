#ifndef PLATFORM_API_H
#define PLATFORM_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "src/core/result.h"
#include "src/core/allocator.h"
#include "src/core/logging.h"

/* Abstract Platform API. To be implemented by OS backends (Windows/Mac/Linux).
 * This is the contract between core/renderer and OS-specific code.
 * No OS headers should be included here.
 */

/* -------------------------------------------------------------------------
 * NORMATIVE: host-memory allocation convention.
 * TASK-007, discharging ADR-009 follow-up #7 for the platform half.
 * Contract: docs/spec/core/allocator.md, CORE-ALLOC-060..070.
 *
 * Every Platform API entry point that allocates host memory -- every
 * create/init function that returns or initializes an engine-owned object --
 * takes
 *
 *     const EngineAllocator *allocator
 *
 * as a construction parameter, identically on every OS backend. There is no
 * per-OS variation and no "change an object's allocator" operation.
 *
 *   CORE-ALLOC-067  The allocator is fixed at construction and stored in the
 *                   object's backend-private struct. Every allocation and
 *                   every release that object performs goes through that one
 *                   stored pointer, which is never reassigned.
 *   CORE-ALLOC-068  An entry point MAY document that allocator == NULL means
 *                   "use engine_default_allocator()". The substitution happens
 *                   once, at construction; NULL is never stored, and no
 *                   allocator is ever resolved lazily or per allocation.
 *   CORE-ALLOC-070  After resolving NULL, the entry point MUST verify that
 *                   both callbacks are non-NULL and return
 *                   ENGINE_RESULT_INVALID_ARGUMENT if either is not. That is
 *                   the one place the check is paid for.
 *   CORE-ALLOC-061..065
 *                   The reference is borrowed: never owned, never copied,
 *                   never mutated, never released by the engine, and it must
 *                   outlive every object constructed from it.
 *
 * OS-OWNED ALLOCATIONS ARE OUTSIDE THE ALLOCATOR'S DOMAIN -- the boundary.
 * Memory and objects the OS creates (window and event-loop handles,
 * dynamically loaded module images, buffers the OS allocates and expects to
 * reclaim through its own call) are released by their OS counterpart call and
 * NEVER by engine_free. Symmetrically, a block obtained from an
 * EngineAllocator is never handed to an OS release call. This is NOT an
 * exception to CORE-ALLOC-030's same-instance rule: such pointers were never
 * returned by any EngineAllocator's alloc, so they lie outside its domain
 * entirely, and passing one to engine_free would be the foreign-pointer
 * undefined behaviour of CORE-ALLOC-032.
 *
 * The resulting shape is the same on Windows, macOS and Linux:
 *   - the engine-visible struct is allocated with engine_alloc from the
 *     allocator stored at construction (CORE-ALLOC-067);
 *   - it holds OS handles as opaque, backend-private members that never cross
 *     this boundary except through an explicit interface (rules/platform.md);
 *   - destroy releases the OS handles by their OS calls FIRST, then releases
 *     the struct through the same stored allocator.
 * Two release disciplines, nested, each symmetric with its own acquisition.
 * ADR-009's alloc/release symmetry holds at the engine level regardless of
 * what the OS does underneath.
 *
 * A backend whose OS may invoke callbacks on threads other than the
 * constructing one MUST state, at its own entry point, the thread-safety level
 * it requires of the caller's allocator (CORE-ALLOC-021: THREAD_SAFE,
 * EXTERNALLY_SYNCHRONIZED, or THREAD_AFFINE). That is a documented backend
 * precondition, not a change to this contract.
 *
 * This block records a decision only: it declares no types and no functions.
 * ------------------------------------------------------------------------- */

/* ---- Time and thread identity (PLATFORM-020..025) -------------------------
 * Stateless: no Platform parameter, callable from any thread at any time. */

uint64_t platform_time_now_ns(void);
uint64_t platform_thread_current_id(void);

/* Logger adapters with the exact EngineLogTimeFn / EngineLogThreadIdFn
 * signatures. user_data is ignored (PLATFORM-025). */
uint64_t platform_log_time_source(void *user_data);
uint64_t platform_log_thread_id_source(void *user_data);

/* ---- Lifecycle (PLATFORM-030..037) ----------------------------------------
 * A NULL allocator selects engine_default_allocator() (PLATFORM-031); a NULL
 * logger discards diagnostics (PLATFORM-032). At most one live Platform per
 * process (PLATFORM-036). */

typedef struct Platform Platform;   /* opaque; defined in the selected backend */

ENGINE_NODISCARD EngineResult platform_init(Platform              **out_platform,
                                            const EngineAllocator  *allocator,
                                            const EngineLogger     *logger);
void platform_shutdown(Platform *platform);

/* ---- Windows (PLATFORM-040..044) ------------------------------------------ */

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

/* ---- Events (PLATFORM-050..054) -------------------------------------------
 * Pump, then drain. Window state is authoritative; events only notify. */

typedef enum PlatformEventType {
    PLATFORM_EVENT_NONE = 0,
    PLATFORM_EVENT_WINDOW_CLOSE_REQUESTED,      /* 1 */
    PLATFORM_EVENT_WINDOW_FRAMEBUFFER_RESIZED   /* 2 -- width/height are the new pixel size */
} PlatformEventType;

/* PLAT-INV-1: fixed values (PLATFORM-054). */
_Static_assert(PLATFORM_EVENT_NONE == 0, "PLAT-INV-1: PLATFORM_EVENT_NONE must be 0");
_Static_assert(PLATFORM_EVENT_WINDOW_CLOSE_REQUESTED == 1, "PLAT-INV-1: WINDOW_CLOSE_REQUESTED must be 1");
_Static_assert(PLATFORM_EVENT_WINDOW_FRAMEBUFFER_RESIZED == 2, "PLAT-INV-1: WINDOW_FRAMEBUFFER_RESIZED must be 2");

typedef struct PlatformEvent {
    int32_t         type;     /* a PlatformEventType (CORE-TYPES-025) */
    PlatformWindow *window;   /* borrowed; live whenever the event is returned */
    uint32_t        width;    /* FRAMEBUFFER_RESIZED only; 0 otherwise */
    uint32_t        height;
} PlatformEvent;

void platform_pump_events(Platform *platform);
bool platform_next_event(Platform *platform, PlatformEvent *out_event);

#endif /* PLATFORM_API_H */
