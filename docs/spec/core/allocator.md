# Core Allocator — Contract Specification

**Status:** Accepted
**Date:** 2026-08-25
**Author:** `spec-writer`
**Scope:** the allocator interface used by every engine subsystem that owns heap memory — core, renderer, platform API, GPU API, and all backends — plus the built-in default heap allocator.
**Implements:** ADR-009 ("Allocator Ownership and Lifetime Model", Accepted). ADR-009 decided *which model* and *why*; this spec fixes *exactly what* — struct layout, signatures, valid inputs, failure behaviour, invariants. **This spec does not supersede or amend ADR-009**, and where a reader wants the reasoning behind the ownership model, ADR-009 remains the record.
**Supersedes:** specs.md §12 ("Memory Architecture") and specs.md §66 CORE-005 / CORE-006. specs.md remains a historical draft; where its §12 sketch disagrees with this document — on `const`-qualification and on type spelling — **this document wins**. See §3.4.
**Depends on:** `docs/spec/core/types.md` (Accepted) — CORE-TYPES-016 (`void *` sanctioned for allocation results and callback `user_data`), CORE-TYPES-017 (`size_t` for in-memory sizes and alignments), CORE-TYPES-014 (`uintptr_t` sanctioned for alignment arithmetic, `.c` only), CORE-TYPES-021/022/023/024 (naming). `docs/spec/core/result.md` (Accepted) — CORE-RESULT-018 (`alloc` returns `void *`, not `EngineResult`), CORE-RESULT-019 (`NULL` is translated to `ENGINE_RESULT_OUT_OF_MEMORY` at the operation boundary).
**Related:** CLAUDE.md invariants #3, #4, #5; `rules/core.md`.

---

## 1. Problem

ADR-009 is Accepted, but it is a decision record, not a header contract. It fixes the shape (`alloc(allocator, size, alignment)`, `free(allocator, memory)`, `user_data`), the ownership model (§3), the `NULL`-selects-default rule (§4), and the foundation-leaf placement (§"Placement and dependency direction"). It deliberately leaves unspecified everything an implementer must know before writing a line:

- Is `alignment == 0` valid? Is `alignment == 3`? Is there an upper bound?
- What does `size == 0` do?
- Is returned memory zeroed?
- Does `free(allocator, NULL)` work?
- Does the default heap allocator honour an alignment larger than `malloc` guarantees, or reject it?
- Does the `const` in ADR-009 §3's `const EngineAllocator *` survive contact with specs.md §12's non-`const` function-pointer signatures? (It does not — §3.4.)

Shipped code answers none of this, because it implements a different design: `engine.h:13` declares `void *(*engine_alloc_fn)(uint64_t size)` with no alignment parameter, no allocator parameter, and a `uint64_t` size that `engine.c:5` immediately narrows with `malloc((size_t)size)` — the exact narrowing CORE-TYPES-017 exists to remove. `renderer.c:13,27` bypasses the mechanism entirely with direct `malloc`/`free`. ADR-009 deletes all of it; this spec says what replaces it.

---

## 2. Decision 1 — The interface

```c
/* src/core/allocator.h */
#ifndef ENGINE_ALLOCATOR_H
#define ENGINE_ALLOCATOR_H

#include <stddef.h>   /* size_t, max_align_t */

typedef struct EngineAllocator EngineAllocator;

/* Returns a block of at least `size` bytes aligned to at least `alignment`,
 * or NULL. See CORE-ALLOC-010..021. */
typedef void *(*EngineAllocatorAllocFn)(const EngineAllocator *allocator,
                                        size_t                 size,
                                        size_t                 alignment);

/* Releases a block previously returned by the SAME allocator's alloc.
 * `memory == NULL` is a no-op. See CORE-ALLOC-030..036. */
typedef void (*EngineAllocatorFreeFn)(const EngineAllocator *allocator,
                                      void                  *memory);

struct EngineAllocator {
    EngineAllocatorAllocFn  alloc;      /* required, never NULL */
    EngineAllocatorFreeFn   free;       /* required, never NULL */
    void                   *user_data;  /* implementation-owned, opaque to the engine */
};

#define ENGINE_ALLOCATOR_DEFAULT_ALIGNMENT ((size_t)_Alignof(max_align_t))
#define ENGINE_ALLOCATOR_MAX_ALIGNMENT     ((size_t)4096)

const EngineAllocator *engine_default_allocator(void);

void *engine_alloc(const EngineAllocator *allocator, size_t size, size_t alignment);
void  engine_free (const EngineAllocator *allocator, void *memory);

#endif /* ENGINE_ALLOCATOR_H */
```

### Rules

**CORE-ALLOC-001 (layout).** `EngineAllocator` is a struct with exactly three fields, in this order: `alloc`, `free`, `user_data`. It is a **defined** (not opaque) type: callers construct their own instances, so the layout is public API. Fields MUST NOT be reordered or removed; a field added later goes at the end, and the existing three keep their meaning.

**CORE-ALLOC-002 (naming).** The type is `EngineAllocator`, the callbacks `EngineAllocatorAllocFn` / `EngineAllocatorFreeFn`, per CORE-TYPES-021/022 (PascalCase, subsystem-prefixed, no `_t` — CORE-TYPES-023). Fields are `snake_case` and the two constants are `UPPER_CASE` (CORE-TYPES-024). specs.md §12's unprefixed `AllocatorAllocFn` / `AllocatorFreeFn` are **not** adopted: they are global identifiers without the namespace CORE-TYPES-022 requires.

**CORE-ALLOC-003 (`alloc` and `free` are required).** Both function pointers MUST be non-`NULL` in any `EngineAllocator` handed to the engine. The engine does not check them on every call — that would put a branch on the hottest path in the engine for a condition that is a caller bug, detectable once at construction. A `NULL` callback is undefined behaviour, and the mitigation is CORE-ALLOC-070's construction-time validation, not a per-call test.

**CORE-ALLOC-004 (`user_data`).** `void *user_data` is the implementation's own state pointer: an arena cursor, a pool freelist head, a leak-tracking counter block, a mutex. It is **opaque to the engine**: the engine never reads it, writes it, copies it, frees it, or reasons about it, and it never crosses any other API boundary. It is one of the three sanctioned uses of `void *` under CORE-TYPES-016. It MAY be `NULL` — a stateless allocator (the default heap allocator, §5) sets it to `NULL`.

**CORE-ALLOC-005 (no `EngineResult` in this header).** `alloc` returns `void *`; `free` returns `void`. Neither returns `EngineResult`, per CORE-RESULT-018. `allocator.h` therefore does **not** include `result.h`, keeping the leaf minimal (§8). The obligation to translate a `NULL` allocation into `ENGINE_RESULT_OUT_OF_MEMORY` belongs to the calling operation (CORE-RESULT-019, restated as CORE-ALLOC-022).

### 2.4 The `const` correction to specs.md §12

specs.md §12 spells both callbacks with a non-`const` first parameter (`EngineAllocator* allocator`). ADR-009 §3 requires the engine to store and use a `const EngineAllocator *`. **These cannot both hold**: an object storing `const EngineAllocator *a` cannot call `a->alloc(a, …)` if `alloc` expects `EngineAllocator *` without casting away `const` at every allocation site — which would make the `const` decorative and would be the single most-repeated cast in the engine.

**CORE-ALLOC-006.** Both callbacks take `const EngineAllocator *allocator`. The `const` is load-bearing and is the honest description of the model: **the `EngineAllocator` struct itself is immutable from first use onward; all mutable allocator state lives behind `user_data`.**

This costs stateful allocators nothing, and the reason is worth stating because it is not obvious: `user_data` is a `void *` *value* inside a `const` struct. `const` applies to the pointer, not the pointee. An arena implementation writes `((Arena *)allocator->user_data)->cursor` freely; what it may not do is reassign `allocator->user_data` or swap out `allocator->alloc` mid-life. Forbidding exactly that is desirable — an allocator that rewrites its own vtable while objects built from it are alive breaks ADR-009's P2 (alloc/free symmetry) in a way no test can catch.

**CORE-ALLOC-007.** An implementation MUST NOT cast away the `const` to mutate the `EngineAllocator` struct. Doing so is undefined behaviour if the struct was defined `const` (the default allocator's singleton is), and a contract violation regardless.

---

## 3. Decision 2 — `alloc()`'s contract

**CORE-ALLOC-010 (success).** On success `alloc` returns a non-`NULL` pointer to a block of **at least** `size` bytes, aligned to **at least** `alignment` bytes. The block is valid until passed to the same allocator's `free` (CORE-ALLOC-030). Accessing beyond `size` bytes is undefined behaviour even where the implementation over-allocated (§6 always does).

**CORE-ALLOC-011 (failure).** On failure `alloc` returns `NULL` and has no other observable effect: nothing is allocated, no state is modified, no diagnostic is required. `NULL` is the *only* failure signal (CORE-RESULT-018).

**CORE-ALLOC-012 (valid alignment).** `alignment` is valid if and only if all three hold:
1. `alignment >= 1`;
2. `alignment` is an integral power of two;
3. `alignment <= ENGINE_ALLOCATOR_MAX_ALIGNMENT`.

An invalid `alignment` MUST return `NULL`. `alignment == 0` is invalid — it is not a power of two, and it is explicitly **not** a magic "use the default" value (§9, edge case 2).

**CORE-ALLOC-013 (`ENGINE_ALLOCATOR_DEFAULT_ALIGNMENT`).** Callers with no specific requirement pass `ENGINE_ALLOCATOR_DEFAULT_ALIGNMENT`, defined as `_Alignof(max_align_t)` — the alignment `malloc` would have given. `_Alignof` is used rather than `alignof` so the header needs no `<stdalign.h>` under C17; both spell the same operator. Callers with a requirement pass `_Alignof(T)` for the type they are storing, or a literal for a hardware constraint (`64` for a cache line, `32` for AVX). Callers MUST NOT assume a blanket `max_align_t` guarantee: the contract delivers what was asked for and nothing more.

**CORE-ALLOC-014 (`ENGINE_ALLOCATOR_MAX_ALIGNMENT` is 4096).** The bound is a real number, not a formality, because §6's portable over-alignment wastes up to `alignment - 1` bytes per allocation. 4096 covers every alignment the engine can currently justify — cache lines (64/128), AVX-512 (64), the 256-byte constant-buffer and staging alignments the three GPU backends impose on host-visible memory, and page-granular host staging. A request above it is not a malloc-shaped problem: page-or-larger alignment wants a virtual-memory reservation, which belongs to a platform-backed allocator with its own spec, not to a `malloc` wrapper. Raising the constant requires superseding this rule and re-examining the waste bound.

**CORE-ALLOC-015 (`size == 0` is invalid).** `alloc` with `size == 0` MUST return `NULL`. It does **not** return a unique non-`NULL` pointer, as `malloc(0)` is permitted to.

*Rationale.* result.md §4 already classifies "a zero size" as an `ENGINE_RESULT_INVALID_ARGUMENT` condition, so treating it as a successful allocation would contradict an Accepted spec. The alternative — returning a distinct freeable pointer — forces every allocator, including arenas that hand out cursor offsets, to reserve a byte it will never use, and it makes `NULL` mean "exhausted" for some sizes and "impossible" for none, which is harder to reason about than one rule.

**CORE-ALLOC-016 (validate before calling — reconciling with CORE-RESULT-019).** CORE-RESULT-019 requires an `EngineResult`-returning operation that receives `NULL` from an allocation to return `ENGINE_RESULT_OUT_OF_MEMORY`. That would misreport a zero-size or bad-alignment request as memory exhaustion. The resolution is a caller obligation, not a second return channel:

> An `EngineResult`-returning operation MUST validate `size` and `alignment` (CORE-ALLOC-012/015) **before** calling `alloc`, and MUST return `ENGINE_RESULT_INVALID_ARGUMENT` for an invalid request without calling through. At the point where `alloc` is actually called, every argument is already valid, so `NULL` can only mean exhaustion, and CORE-RESULT-019's translation is exact.

In practice core and renderer call `alloc` with `sizeof(SomeStruct)` and `_Alignof(SomeStruct)`, both compile-time non-zero, so the validation is usually discharged by construction. It becomes a live obligation the moment a size reaches an allocator from caller data (a buffer length, an asset size narrowed from `uint64_t` under CORE-TYPES-019).

**CORE-ALLOC-017 (memory is NOT zeroed).** Returned memory is **uninitialized**; its contents are indeterminate. Callers MUST write every byte they read.

*Rationale.* Zeroing is a per-byte cost on the engine's hottest path, against `rules/core.md`'s "keep hot paths allocation-light"; a large fraction of engine allocations are immediately fully overwritten; and arena and pool allocators cannot zero cheaply without defeating the reason they exist. The asymmetry decides it: **promising zeroing later is a source-compatible strengthening; withdrawing it is not.** Not promising is the reversible choice. Callers needing zeroed memory call `memset` explicitly at the allocation site, where the cost is visible; if that pattern becomes common, the sanctioned addition is a separate `engine_alloc_zeroed()` helper, never a change to this rule.

**CORE-ALLOC-018 (debug poison is not a guarantee).** A debug build MAY fill returned memory with a poison pattern to make use-before-initialize visible. Code MUST NOT depend on it, and it MUST NOT be present in release builds.

**CORE-ALLOC-019 (no partial success).** `alloc` either satisfies the full request or returns `NULL`. It never returns a smaller block, and it never blocks, retries, or invokes an out-of-memory handler. There is no engine-wide new-handler equivalent.

**CORE-ALLOC-020 (no reentrancy into the engine).** An implementation MUST NOT call back into engine APIs from within `alloc` or `free`. It may call the platform C library, the OS, or its own helpers.

### Thread safety

**CORE-ALLOC-021 (the interface imposes nothing; every implementation MUST document one of three levels).** ADR-009 records that thread-safety is a per-allocator property each implementation must document. This spec makes that a contract with a fixed vocabulary, so "documented" cannot mean a sentence of prose that callers must interpret. Every `EngineAllocator` implementation MUST state, at its construction/definition site, exactly one of:

- **`THREAD_SAFE`** — `alloc` and `free` may be called concurrently from any threads on the same allocator instance. (The default heap allocator, §5.)
- **`EXTERNALLY_SYNCHRONIZED`** — calls on one instance must not overlap; the caller provides mutual exclusion. Distinct instances are independent.
- **`THREAD_AFFINE`** — calls must come from the thread that constructed the allocator. (The expected level for a frame/scratch allocator.)

**CORE-ALLOC-022 (what no level ever implies).** None of the three makes an *allocated block* thread-safe. Handing a block from one thread to another requires the caller's own synchronization to establish happens-before; the allocator provides no ordering for block contents. And concurrent `free` of the *same* pointer is a double free (CORE-ALLOC-033) under every level, including `THREAD_SAFE`.

---

## 4. Decision 3 — `free()`'s contract

**CORE-ALLOC-030 (what may be freed).** `free(allocator, memory)` accepts exactly one thing: a pointer previously returned by **this same allocator instance's** `alloc`, not yet freed. "Same instance" means pointer identity of the `EngineAllocator *`, not structural equality of the struct's bytes (CORE-ALLOC-053).

**CORE-ALLOC-031 (`memory == NULL` is a no-op).** `free(allocator, NULL)` MUST do nothing and MUST return normally. The C standard library convention is kept deliberately: CORE-RESULT-013 requires a failing constructor to release everything it acquired, and partial-construction cleanup paths are written as an unconditional sequence of frees over fields that may or may not have been allocated yet. Without the no-op, every such path grows a null test per field — the branch is not avoided, only moved to the noisier place, and forgetting one is a crash rather than a no-op.

**CORE-ALLOC-032 (foreign pointer — caller obligation).** Passing a pointer not obtained from this allocator instance is **undefined behaviour**. This is stated as a caller obligation because it is not enforceable: an allocator has no portable way to test whether an arbitrary address came from it. An arena could bounds-check its own region and a debug heap could consult a registry, but the *interface* cannot promise what a plain `malloc` wrapper cannot deliver, and a guarantee honoured by only some implementations is worse than none — callers would write code that depends on it.

**CORE-ALLOC-033 (double free — caller obligation).** Freeing the same pointer twice is **undefined behaviour**, for the same unenforceability reason. Callers null the pointer after freeing, so a repeat becomes CORE-ALLOC-031's no-op.

**CORE-ALLOC-034 (exact pointer only).** The pointer passed to `free` MUST be exactly the value `alloc` returned. Interior pointers, offset pointers, and pointers adjusted by the caller are undefined behaviour. §6's default implementation stores bookkeeping immediately below the returned address, so an offset pointer corrupts the heap rather than failing cleanly.

**CORE-ALLOC-035 (`free` never fails and never reports).** `free` returns `void`, MUST NOT abort, `longjmp`, or block on valid input, and has no failure mode a caller could handle. A debug implementation MAY assert on a detected violation of CORE-ALLOC-032/033; release builds MUST NOT.

**CORE-ALLOC-036 (`free` receives no size).** The signature carries no `size` or `alignment`. An implementation that needs them at release time MUST record them itself — in a header below the block (§6), in `user_data`, or not at all (arenas do not free individually). This is a constraint on future allocator implementations and is stated here so it is discovered at design time, not after a pool allocator's signature has been written.

---

## 5. Decision 4 — The default heap allocator

**CORE-ALLOC-040 (accessor).** The built-in allocator is obtained from:

```c
const EngineAllocator *engine_default_allocator(void);
```

It returns a pointer to a single `static const EngineAllocator` with static storage duration, defined in `src/core/allocator.c`. It **never returns `NULL`**, is callable at any point in the program including before any engine initialization, and returns the identical pointer on every call, from every thread, forever.

*Why a function and not `extern const EngineAllocator ENGINE_DEFAULT_ALLOCATOR;`.* A function keeps the definition private to one translation unit, so the singleton's `const` cannot be cast away by a consumer holding its address as a non-`const` object; exported *data* symbols are more fragile than function symbols across shared-library boundaries (a concern specs.md ADR-006's dynamic backend loading will make real); and a function leaves room for debug-build instrumentation without an API change. There is no lazy initialization and no synchronization — the object is a compile-time constant aggregate — so the accessor is trivially safe.

**CORE-ALLOC-041 (composition).** Its `alloc` wraps `malloc`, its `free` wraps `free`, and its `user_data` is `NULL` (it is stateless). Per CORE-TYPES-017 the request size is already `size_t`, so it reaches `malloc` with **no cast and no narrowing** — this retires the `malloc((size_t)size)` narrowing at `engine.c:5`, which existed only because `engine_alloc_fn` took `uint64_t`.

**CORE-ALLOC-042 (the only sanctioned `malloc` site).** Per ADR-009 §7, `src/core/allocator.c` is the single translation unit in `src/core` and `src/renderer` permitted to name `malloc`, `free`, `calloc`, `realloc`, or `aligned_alloc`. ADR-009 follow-up #5's grep-based architecture test MUST whitelist exactly this file and no other.

**CORE-ALLOC-043 (thread safety — stated, not implied).** **The default heap allocator is `THREAD_SAFE` (CORE-ALLOC-021), inheriting exactly the guarantee the platform C library gives `malloc`/`free` — no more and no less.**

Concretely: C11 and later require the memory-management functions to behave, for data-race purposes, as though they access only memory reachable through their arguments, which makes concurrent `malloc`/`free` calls from different threads data-race free on every conforming C17 implementation the engine supports. §6's alignment wrapper adds **no shared mutable state** — its bookkeeping lives inside each block — so it neither strengthens nor weakens that guarantee.

What this explicitly does **not** promise, restating CORE-ALLOC-022 at the site where callers will assume otherwise: it does not synchronize *block contents* (passing a block between threads still needs the caller's own happens-before edge), and it does not make concurrent `free` of the same pointer anything other than a double free. If a target's libc turns out not to provide the C11 guarantee, that target is unsupported until wrapped by an `EXTERNALLY_SYNCHRONIZED` allocator; it is not papered over here.

---

## 6. Decision 5 — Alignment arithmetic and over-alignment

`malloc` guarantees only `_Alignof(max_align_t)` (typically 8 or 16). `ENGINE_ALLOCATOR_MAX_ALIGNMENT` is 4096. Something must reconcile them.

**CORE-ALLOC-050 (over-alignment is IN scope, and the default heap allocator implements it).** Every conforming `EngineAllocator` — including the default — MUST honour any alignment valid under CORE-ALLOC-012. An allocator MUST NOT return `NULL` merely because an alignment exceeds what its underlying mechanism natively provides.

*Rationale, weighing the alternative honestly.* Scoping over-alignment out to a future arena/pool spec is the cheaper option and has a real argument: nothing in the tree today allocates anything needing more than `max_align_t`, and the offset-header technique below costs bytes on every allocation. It is rejected on three grounds:

1. **It would make `NULL` ambiguous.** With over-alignment out of scope, `NULL` means "exhausted" *or* "this allocator cannot do that alignment" — and CORE-RESULT-019 maps both to `ENGINE_RESULT_OUT_OF_MEMORY`, reporting a caller bug as memory pressure. The single-meaning `NULL` that CORE-RESULT-018 relies on is worth more than the per-block overhead.
2. **The default allocator is the one every trivial call site gets.** `NULL` resolves to it (ADR-009 §4), so a default that cannot serve a 64-byte-aligned request pushes the *first* subsystem needing SIMD or cache alignment into writing a custom allocator before arenas exist — which is precisely the retrofit ADR-009 §1 put alignment in the signature to avoid.
3. **The obvious alternative imports platform branching into the foundation leaf.** `aligned_alloc` is C11 but absent on MSVC (which spells it `_aligned_malloc`, paired with `_aligned_free`, so `free` stops working symmetrically) and under C17 additionally requires `size` to be a multiple of `alignment`. Using it means `#ifdef _WIN32` inside `src/core/allocator.c` — an OS-shaped conditional in the one module whose stated purpose is depending on nothing (`rules/platform.md`; ADR-009's placement section). The portable technique below needs no conditional at all.

**CORE-ALLOC-051 (`uintptr_t` is confined to `allocator.c`).** The align-up arithmetic is the sanctioned `uintptr_t` site named by CORE-TYPES-014 and types.md edge case 5. The round trip stays inside `src/core/allocator.c`, within one process, and is never stored, serialized, or transmitted. `uintptr_t` MUST NOT appear in `allocator.h` (TYPE-INV-4).

**CORE-ALLOC-052 (the algorithm).** The default heap allocator uses the offset-header technique. This shape is normative — an implementation may differ internally only if `free` remains able to recover the original `malloc` pointer from the returned pointer alone (CORE-ALLOC-036).

```
alloc(allocator, size, alignment):
    if size == 0                            -> return NULL          /* CORE-ALLOC-015 */
    if alignment == 0                       -> return NULL          /* CORE-ALLOC-012 */
    if (alignment & (alignment - 1)) != 0   -> return NULL          /* not a power of two */
    if alignment > ENGINE_ALLOCATOR_MAX_ALIGNMENT -> return NULL

    /* Clamp so the header slot below the returned address is itself
       naturally aligned; the result still meets or exceeds the request. */
    effective = (alignment < _Alignof(void *)) ? _Alignof(void *) : alignment

    overhead = sizeof(void *) + (effective - 1)
    if size > SIZE_MAX - overhead           -> return NULL          /* CORE-ALLOC-054 */

    raw = malloc(size + overhead)
    if raw == NULL                          -> return NULL

    base    = (uintptr_t)raw + sizeof(void *)
    aligned = (base + (effective - 1)) & ~(uintptr_t)(effective - 1)

    ((void **)aligned)[-1] = raw            /* header: the original malloc pointer */
    return (void *)aligned

free(allocator, memory):
    if memory == NULL                       -> return              /* CORE-ALLOC-031 */
    free(((void **)memory)[-1])
```

Three points that are easy to get wrong and are therefore normative:

- **The clamp to `_Alignof(void *)` is required, not an optimization.** Without it, `alignment == 1` can yield an `aligned` address at which storing a `void *` is misaligned — undefined behaviour on strict-alignment targets. Returning memory aligned *more* than requested is conforming (CORE-ALLOC-010 says "at least").
- **The header is unconditional.** It is written even when `effective <= _Alignof(max_align_t)` and the raw pointer would already have sufficed. `free` receives only a pointer (CORE-ALLOC-036) and so has no way to discover which scheme `alloc` chose; a conditional header makes `free` unimplementable. The cost is `sizeof(void *)` bytes on every allocation, accepted for that reason.
- **Overhead is bounded and worth knowing:** `sizeof(void *) + alignment - 1` bytes, so at most `4103` bytes on a 64-bit host at `ENGINE_ALLOCATOR_MAX_ALIGNMENT`. That bound is the reason CORE-ALLOC-014 caps the constant where it does.

**CORE-ALLOC-053 (pointers are not interchangeable between allocators).** Because the header records provenance implicitly rather than explicitly, a block from the default allocator passed to any other allocator's `free` — or to the C library's `free` directly — corrupts the heap. This is CORE-ALLOC-032 restated with its concrete mechanism, and it is why ADR-009 §7's prohibition on direct `malloc`/`free` is enforced by test rather than convention.

**CORE-ALLOC-054 (overflow).** `size + overhead` MUST be checked against `SIZE_MAX` **before** the addition, as written above. Computing the sum first and testing the result is wrong: unsigned wraparound is well-defined, so the test would silently pass and a small block would be allocated for a huge request. On overflow, `alloc` returns `NULL` — the request cannot be satisfied by this host, which is exactly what `NULL` means.

---

## 7. Ownership and lifetime invariants

ADR-009 §3 states this in prose. Restated as numbered obligations, each one either a caller obligation or an engine obligation, so a reviewer can check them one at a time.

**CORE-ALLOC-060 (caller owns the storage).** The `EngineAllocator` object is owned by the caller. The caller decides where it lives — static storage, the caller's stack, the caller's own heap — and is responsible for its lifetime.

**CORE-ALLOC-061 (engine borrows).** The engine receives `const EngineAllocator *` and holds a **borrowed** reference. It never takes ownership.

**CORE-ALLOC-062 (engine never frees the allocator).** No engine code path frees, destroys, finalizes, or otherwise disposes of an `EngineAllocator`. `engine_shutdown` releases the memory the engine allocated *through* the allocator; it does not touch the allocator.

**CORE-ALLOC-063 (engine never copies the allocator).** The engine stores the pointer, never a copy of the struct. Two structurally identical `EngineAllocator` values are two allocators, and freeing through the wrong one violates CORE-ALLOC-030 — harmlessly for the stateless default, and catastrophically for a tracking or arena allocator. Identity is the pointer.

**CORE-ALLOC-064 (engine never mutates the allocator).** Guaranteed by CORE-ALLOC-006/007's `const`.

**CORE-ALLOC-065 (outlives everything built from it).** The `EngineAllocator` MUST remain valid, and its backing state usable, until *after* the last object constructed with it has been destroyed and the last block allocated from it has been freed. Destroying an allocator with live blocks outstanding is undefined behaviour.

**CORE-ALLOC-066 (unenforceable, by design).** The engine cannot detect CORE-ALLOC-065's violation — ADR-009 records this as an accepted cost. A debug-build outstanding-allocation counter in a wrapping allocator is the available mitigation, not a guarantee, and it is not part of this contract.

**CORE-ALLOC-067 (fixed at construction, immutable for the object's life).** Every subsystem that owns heap memory takes `const EngineAllocator *allocator` in its init/create entry point and stores it in its own `.c`-private struct. That stored pointer is set once and never reassigned. Every allocation and every free that object performs goes through it. There is no "change an object's allocator" operation, and adding one would destroy ADR-009's structural alloc/free symmetry (P2).

**CORE-ALLOC-068 (`NULL` at construction, and only there).** Per ADR-009 §4, an init/create entry point that documents it MAY accept `allocator == NULL`, meaning "use the default heap allocator". The resolution happens **once, at construction**: the entry point immediately replaces `NULL` with `engine_default_allocator()` and stores the resolved, non-`NULL` pointer. No engine code stores `NULL` as an allocator, and no code path resolves an allocator lazily or per-allocation — that would be the ambient lookup ADR-009 §5 forbids, wearing a different signature.

**CORE-ALLOC-069 (`NULL` is never a valid allocator at use time).** Once construction is past, `allocator == NULL` is not "the default", it is a bug. Invoking `allocator->alloc(...)` through a `NULL` allocator is undefined behaviour and is not checkable at that site — the function pointer being called lives inside the struct being dereferenced. `engine_alloc`/`engine_free` (§9) handle it defensively; direct callback invocation does not.

**CORE-ALLOC-070 (construction-time validation).** An entry point taking an allocator MUST, after resolving `NULL` per CORE-ALLOC-068, verify that `allocator->alloc != NULL` and `allocator->free != NULL`, and return `ENGINE_RESULT_INVALID_ARGUMENT` if either is `NULL`. This is the one place the check is paid for, discharging CORE-ALLOC-003 for the object's whole lifetime.

---

## 8. Placement, target split, and dependency direction

ADR-009's "Placement and dependency direction" section requires the allocator to live in a dependency-free foundation leaf, not in the engine runtime, because `engine_renderer` linking today's `engine_core` would invert specs.md §4.1's `Engine → Renderer → GPU API` direction. This is the exact split that implies — ADR-009 follow-up #1, `build-engineer`.

**CORE-ALLOC-080 (the split is by target, not by directory).** ADR-009 names the path `src/core/allocator.h`; that path is kept. What splits is the CMake target: `src/core/CMakeLists.txt` produces **two** libraries from one directory.

| Target | Sources | Public headers | Depends on |
|---|---|---|---|
| `engine_foundation` (new) | `allocator.c`, `result.c` | `allocator.h`, `result.h` | nothing but the C standard library |
| `engine_core` (existing, narrowed) | `engine.c` | `engine.h` | `engine_foundation` (PUBLIC) |

**CORE-ALLOC-081 (`engine_foundation` depends on nothing).** It links no engine target. `allocator.h` includes `<stddef.h>` and nothing else — not `<stdlib.h>` (that belongs to `allocator.c` alone, CORE-ALLOC-042), not `<stdint.h>` (no fixed-width type appears in the interface), not `result.h` (CORE-ALLOC-005). `result.md` §10 places `result.h`/`result.c` in this same target for the same reason.

**CORE-ALLOC-082 (who may link it).** `engine_core`, `engine_renderer`, the future platform and GPU API targets, and every backend link `engine_foundation` directly and PUBLIC. `engine_renderer` MUST NOT link `engine_core` — that is the inversion the split exists to prevent, and it is the reason this split is a prerequisite for ADR-009 follow-up #4 rather than a cleanup after it.

**CORE-ALLOC-083 (checks apply to both targets).** `forbid_os_gpu_includes` is applied to `engine_foundation` as well as `engine_core` (`cmake/Common.cmake`), and both carry `-Wall -Wextra -Wpedantic`. ADR-009 follow-up #5's `malloc`/`free`/`calloc`/`realloc` prohibition covers `src/core` and `src/renderer` with `src/core/allocator.c` as its sole exemption.

---

## 9. The `engine_alloc` / `engine_free` helpers

ADR-009 §5 requires these to be allocator-first free functions that centralize null checks and alignment validation, and explicitly forbids them from reading ambient context.

**CORE-ALLOC-090.** `engine_alloc(allocator, size, alignment)`:
1. returns `NULL` if `allocator == NULL`;
2. returns `NULL` if `size` or `alignment` is invalid (CORE-ALLOC-012/015), **without** calling through;
3. otherwise returns `allocator->alloc(allocator, size, alignment)` unchanged.

**CORE-ALLOC-091 (`engine_alloc(NULL, …)` does NOT resolve to the default).** It returns `NULL`. Resolving here would place default-allocator resolution at *allocation* time rather than construction time, contradicting CORE-ALLOC-068 and ADR-009 §4's "the resolution happens once, at construction". `NULL`-means-default is a construction-parameter convenience, not an allocation-path fallback.

**CORE-ALLOC-092.** `engine_free(allocator, memory)`:
1. is a no-op if `memory == NULL` (CORE-ALLOC-031);
2. is a no-op if `allocator == NULL` — a caller bug that leaks rather than crashes, because the alternative on a cleanup path is a null dereference during error unwinding, and a leak is the more recoverable failure. A debug build MAY assert here;
3. otherwise calls `(allocator->free)(allocator, memory)`.

**CORE-ALLOC-093 (parenthesized member call).** `allocator->free` and `allocator->alloc` MUST be invoked as `(allocator->free)(…)` inside `allocator.c`, which includes `<stdlib.h>`. Some C libraries and sanitizer interposers define `free` as a function-like macro; `allocator->free(a, p)` would then be macro-expanded at the member-call site. The parentheses suppress the expansion. Everywhere else in the engine, code calls `engine_free` and never touches the member, so this rule is scoped to the one file that can be bitten.

**CORE-ALLOC-094 (no `ENGINE_NODISCARD` here — a known gap).** `ENGINE_NODISCARD` is defined in `result.h` (CORE-RESULT-015), which `allocator.h` deliberately does not include (CORE-ALLOC-005/081). `engine_alloc` is therefore **not** annotated, so a discarded allocation — an immediate leak — is not caught by the compiler. This is recorded as an accepted gap rather than resolved by adding a dependency to the leaf. If a third foundation leaf holding compiler attributes is ever created, annotating `engine_alloc` is its first job.

---

## 10. Invariants

Testable statements, in the style of `rules/testing.md`, types.md §7, and result.md §8.

**ALLOC-INV-1.** `sizeof(EngineAllocator) == 2 * sizeof(void (*)(void)) + sizeof(void *)` is *not* asserted — struct padding is the implementation's business. What is asserted is `_Alignof(EngineAllocator) <= _Alignof(max_align_t)`. *(static_assert)*

**ALLOC-INV-2.** `ENGINE_ALLOCATOR_DEFAULT_ALIGNMENT` and `ENGINE_ALLOCATOR_MAX_ALIGNMENT` are powers of two, and `DEFAULT <= MAX`. *(static_assert)*

**ALLOC-INV-3.** `engine_default_allocator()` never returns `NULL`, returns the same pointer on repeated calls, and its `alloc`/`free` fields are non-`NULL`. *(unit test)*

**ALLOC-INV-4.** For every power of two `A` in `[1, ENGINE_ALLOCATOR_MAX_ALIGNMENT]`, the default allocator's `alloc(a, 1, A)` returns a pointer `p` with `(uintptr_t)p % A == 0`, and `free(a, p)` returns cleanly. *(unit test — the direct check on §6's algorithm)*

**ALLOC-INV-5.** The default allocator's `alloc` returns `NULL` for: `size == 0`; `alignment == 0`; `alignment == 3` (and any other non-power-of-two); `alignment == ENGINE_ALLOCATOR_MAX_ALIGNMENT * 2`; and `size == SIZE_MAX`. *(unit test)*

**ALLOC-INV-6.** `engine_free(a, NULL)` and `engine_free(NULL, p)` return normally. `engine_alloc(NULL, 1, 1)` returns `NULL`. *(unit test)*

**ALLOC-INV-7.** No source under `src/core` or `src/renderer` other than `src/core/allocator.c` contains `malloc`, `calloc`, `realloc`, `free`, or `aligned_alloc` as a call. *(grep — ADR-009 follow-up #5)*

**ALLOC-INV-8.** `src/core/allocator.h` includes only `<stddef.h>`, and mentions neither `uintptr_t` (TYPE-INV-4) nor `EngineResult`. *(grep)*

**ALLOC-INV-9.** No engine source declares an allocator-shaped function pointer other than `EngineAllocatorAllocFn`/`EngineAllocatorFreeFn` — no surviving `engine_alloc_fn`, `engine_free_fn`, or `engine_set_allocators`. *(grep)*

**ALLOC-INV-10.** No file-scope mutable variable in `src/core` or `src/renderer` holds an allocator or an allocator function pointer — the direct check that ADR-009's Option A is gone and has not crept back. *(grep — CLAUDE.md invariant #5)*

**ALLOC-INV-11.** `engine_renderer` does not link `engine_core`. *(build-graph assertion — CORE-ALLOC-082)*

**ALLOC-INV-12.** A leak-tracking test allocator injected into `engine_init` observes exactly as many `free` calls as `alloc` calls after `engine_shutdown`, and every freed pointer was one it returned. *(integration test — the capability ADR-009 lists as its main testability gain, and the only end-to-end check of CORE-ALLOC-030/067)*

**ALLOC-INV-13.** A test allocator whose `alloc` always returns `NULL` makes `engine_init` return `ENGINE_RESULT_OUT_OF_MEMORY` and leave its out-parameter `NULL`. *(contract test — CORE-RESULT-013/019 through this interface; the branch at `engine.c:22-24` that is untestable today)*

---

## 11. Edge cases

1. **`size == 0`** — `alloc` returns `NULL` (CORE-ALLOC-015). Not a successful zero-byte allocation. An `EngineResult`-returning caller must have rejected it as `ENGINE_RESULT_INVALID_ARGUMENT` before calling (CORE-ALLOC-016), so this `NULL` is never mistranslated as out-of-memory.
2. **`alignment == 0`** — invalid, returns `NULL`. Explicitly **not** a magic "use default" value: a zero-initialized struct field would then silently produce a working allocation, hiding the fact that nobody chose an alignment. Callers that do not care pass `ENGINE_ALLOCATOR_DEFAULT_ALIGNMENT`, which says so.
3. **Non-power-of-two alignment (`3`, `24`, `100`)** — invalid, returns `NULL`. §6's align-up arithmetic is a mask operation that is only correct for powers of two, and no hardware alignment requirement is non-power-of-two.
4. **`alignment > ENGINE_ALLOCATOR_MAX_ALIGNMENT`** — invalid, returns `NULL` (CORE-ALLOC-014). Not silently clamped: clamping would return under-aligned memory to a caller who asked for a specific alignment and would report success.
5. **`allocator == NULL` at an init/create entry point** — valid where documented; means the default heap allocator; resolved once and stored (CORE-ALLOC-068).
6. **`allocator == NULL` passed to `engine_alloc`** — returns `NULL`; does **not** resolve to the default (CORE-ALLOC-091). Passed to `engine_free` — no-op, leaks (CORE-ALLOC-092). Passed to a raw `allocator->alloc(...)` callback invocation — undefined behaviour, unpreventable (CORE-ALLOC-069).
7. **`free(allocator, NULL)`** — no-op, always (CORE-ALLOC-031).
8. **`size_t` overflow in `size + overhead`** — checked *before* the addition; returns `NULL` (CORE-ALLOC-054). Unsigned wraparound is well-defined, so a post-hoc check would silently succeed with a dangerously small block.
9. **A block freed through a different `EngineAllocator` instance with identical fields** — undefined behaviour (CORE-ALLOC-030/063). It happens to work for the stateless default allocator; that accident MUST NOT be relied upon, and it is precisely why identity is the pointer, not the bytes.
10. **A pointer from the default allocator passed to the C library's `free`** — heap corruption, not a leak, because the returned address is offset past §6's header. This is the concrete reason ADR-009 §7's prohibition is grep-enforced (ALLOC-INV-7) rather than left to review.
11. **Allocator destroyed while blocks are live** — undefined behaviour, undetectable by the engine (CORE-ALLOC-065/066).
12. **`free` as a function-like macro in some libc or sanitizer** — mitigated by the parenthesized member call inside `allocator.c` (CORE-ALLOC-093). No other file names the member.
13. **`alignment == 1` on a strict-alignment target** — handled by the clamp to `_Alignof(void *)` in §6; without it, the header write itself would be undefined behaviour.
14. **A stateful allocator behind a `const EngineAllocator *`** — fully supported: the `const` applies to the struct, not to what `user_data` points at (§2.4).
15. **A 32-bit host and a size arriving as `uint64_t`** — narrowed under CORE-TYPES-019 with a range check, returning an explicit error, *before* it reaches `alloc`. The allocator interface never sees a `uint64_t`.

---

## 12. Migration impact on existing code

Recorded with the same rigour as types.md §9 and result.md §10. Every row is already owned by an ADR-009 follow-up; this spec adds no new task.

| File | Change | Follow-up |
|---|---|---|
| `src/core/allocator.h` (new) | The interface, two constants, `engine_default_allocator`, `engine_alloc`, `engine_free`. ~45 lines. | #1, #2 |
| `src/core/allocator.c` (new) | The default allocator singleton and §6's algorithm; the sole `<stdlib.h>` consumer in core. ~60 lines. | #2 |
| `src/core/CMakeLists.txt` | Split into `engine_foundation` + `engine_core` per §8. | #1 |
| `src/core/engine.h` | Delete `engine_alloc_fn` (line 13), `engine_free_fn` (line 14), `engine_set_allocators` (line 26). `engine_init` gains `const EngineAllocator *allocator`. | #3 |
| `src/core/engine.c` | Delete `default_alloc` (lines 4–6) and the `g_alloc`/`g_free` globals (lines 9–10) — ALLOC-INV-10. `struct engine` gains a stored `const EngineAllocator *`. Lines 21 and 47 route through it. Delete `engine_set_allocators` (lines 51–58). | #3 |
| `src/renderer/api.h`, `src/renderer/renderer.c` | `renderer_init` gains and stores an allocator; `malloc` (line 13) and `free` (line 27) replaced with `engine_alloc`/`engine_free`. | #4 |
| `src/main.c`, `tests/contract/test_api_contracts.c` | `engine_init(&e)` → `engine_init(&e, NULL)`. | #3 |
| `cmake/Common.cmake` | A `forbid_direct_allocation` macro alongside the existing include checks, exempting `allocator.c` (ALLOC-INV-7). | #5 |
| `tests/` (new) | ALLOC-INV-3..6 as unit tests; ALLOC-INV-12/13 as the injected-allocator tests. | #5 |

**Sequencing.** This spec, `types.md`, and `result.md` are the three prerequisites for ADR-009 follow-up #2, and follow-up #1 (the target split) must land first because `allocator.h` cannot be linked by the renderer until `engine_foundation` exists. The correct order is **#1 → (#2 with `result.h`) → #3 → #4 → #5**. Follow-ups #6 (type naming, discharged by `types.md`) and #7 (GPU/platform adoption) are unaffected: #7's answer is CORE-ALLOC-067 — the same `const EngineAllocator *` construction parameter, with no per-subsystem variation.

---

## 13. Out of scope

Explicitly not decided here:

- **Arena, pool, stack, frame, scratch, and GPU-upload allocators** (specs.md §12's future list) — each gets its own spec. They inherit every rule in §2, §3, §4, and §7 unchanged; what they add is bulk-release semantics and, for arenas, ADR-009 Option D's per-call pattern where the arena handle *is* the stored allocator.
- **Reallocation.** There is no `realloc` in this interface, and adding one is a spec amendment, not an implementation decision. Growable buffers allocate-copy-free explicitly. `realloc` is genuinely hard for arena and pool allocators to implement honestly, and an interface method half the implementations must fail is worse than none.
- **Allocation statistics, budgets, and leak tracking.** Implementable *as* an `EngineAllocator` that wraps another one — which is the point of the interface — but no engine-level statistics API is specified.
- **Debug instrumentation details** (poison patterns, guard bytes, outstanding-allocation counters). Permitted by CORE-ALLOC-018 and CORE-ALLOC-066; never guaranteed, never depended upon.
- **GPU memory allocation.** Device memory is the GPU API's subject, not this one. `EngineAllocator` allocates host memory only; a `GPUUploadAllocator` may present this interface for its host-visible staging side, but device heaps use `uint64_t` sizes per CORE-TYPES-018 and are governed by specs.md ADR-008.
- **Assertions** (`ENGINE_ASSERT` and friends) — CORE-003. Where this spec says "a debug build MAY assert", that mechanism is CORE-003's.
