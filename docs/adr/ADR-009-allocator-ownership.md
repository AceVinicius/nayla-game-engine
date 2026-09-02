# ADR-009: Allocator Ownership and Lifetime Model

**Status:** Accepted (design only — implementation deferred to follow-up tasks)
**Date:** 2026-08-20
**Deciders:** `architect`
**Supersedes:** the `engine_set_allocators()` mechanism introduced in task 001
**Related:** specs.md §8 (Core Runtime), §12 (Memory Architecture), §37 (Engine Lifecycle), §49 (API Stability), §4.1 (Dependency Direction); CLAUDE.md invariant #5; `rules/core.md`

> Numbering note: specs.md §61 reserves ADR-001..ADR-008 for named topics (001 language, 002 build system, 003 platform abstraction, 004 GPU abstraction, 005 opaque GPU resources, 006 dynamic backend loading, 007 shader representation, 008 resource lifetime model). Memory/allocation is not in that list, so this decision takes the first free number rather than colliding with ADR-001.

---

## Context

`src/core/` and `src/renderer/` were tracked for the first time in task 001 (previously gitignored by accident). Reviewing the newly visible code exposed the memory-ownership model as undecided rather than merely unimplemented.

Current state:

- `src/core/engine.h` declares two bare function-pointer typedefs (`engine_alloc_fn`, `engine_free_fn`) and a configuration entry point `engine_set_allocators(alloc, free)`.
- `src/core/engine.c` stores them in file-scope mutable globals (`g_alloc`, `g_free`) defaulted to `malloc`/`free` wrappers. `engine_init()` reads `g_alloc` implicitly; `engine_shutdown()` reads `g_free`.
- `src/renderer/renderer.c` does not participate at all — it calls `malloc`/`free` directly.
- No `engine_alloc()` / `engine_free()` functions exist; only the typedefs.

Three separate normative sources are violated by this state:

1. CLAUDE.md invariant #5 — "Ownership/lifetime: explicit; no hidden global state."
2. `rules/core.md` — "No hidden globals: all state passed explicitly" and "Use engine allocators: `engine_alloc()` / `engine_free()`, not `malloc`/`free`."
3. specs.md §12, which **already specifies** the intended interface: a struct-based `EngineAllocator` carrying `alloc`, `free`, and `user_data`, with alignment in the allocation signature, sized for future `Arena`/`Pool`/`Frame`/`Scratch`/`GPUUpload` allocators.

The current code is therefore not a partial implementation of §12 — it is a different, incompatible design that predates anyone reading §12. That reframes the question from "should we refactor?" to "which model do we commit to before more subsystems copy the wrong one?"

## Problem

Who owns an allocator, how does a subsystem obtain one, and how does the object it allocated find the same allocator at destruction time?

Three properties must hold simultaneously:

- **P1 — Explicitness.** Allocation behavior must be visible in the call signature, not configured out-of-band.
- **P2 — Symmetry.** Whatever frees a block must provably be the allocator that produced it, without global lookup.
- **P3 — Multiplicity.** Distinct allocators must coexist in one process (per-subsystem arenas, per-frame scratch, a leak-tracking allocator under test) — §12's future-allocator list is meaningless otherwise.

Secondary: the allocator must be reachable from `core`, `renderer`, and both backend families without inverting the dependency direction of §4.1.

## Options considered

### Option A — Keep the configured global singleton (`engine_set_allocators`)

Two mutable file-scope pointers, set once before `engine_init()`.

- Cheapest: zero code churn, header already public, task 001 acceptance criteria already green against it.
- Fails P1: the allocator never appears in any signature.
- Fails P3 outright: one process has exactly one allocator, so arenas/frame/scratch allocators are unreachable without a second, parallel mechanism.
- Fails P2 in the interesting case: a "before `engine_init`" ordering rule is documentation, not enforcement; re-setting mid-run silently mismatches alloc/free pairs.
- Adds hidden thread-safety surface (unsynchronized mutable globals) that §49 requires be documented and would have to be documented as "not thread-safe, unenforced".
- Contradicts CLAUDE.md #5 and `rules/core.md` as literally written. Adopting it means amending the invariants, not just the code.

### Option B — Explicit allocator handle threaded through construction, stored in the owning object

`const EngineAllocator*` is a parameter of every create/init entry point; the created object stores the pointer it was built with and uses that same pointer to release its own memory. `engine_alloc()`/`engine_free()` become thin free functions whose **first parameter is the allocator** — never ambient lookup.

- Satisfies P1, P2, P3 directly. Matches specs.md §12 verbatim.
- Symmetry is structural: the allocator travels with the object, so destruction cannot pick the wrong one.
- Composes with §37's ordered lifecycle — each subsystem can be handed a different allocator at its own init step.
- Cost: every constructor signature gains a parameter, and every object gains one pointer field. `engine_set_allocators()` must be removed (see Backward compatibility).

### Option C — Hybrid: keep the global as an implicit default, allow explicit override per call

Explicit parameter where supplied, fall back to the global otherwise.

- Preserves the existing public API and permits arenas, so it superficially passes P1/P3.
- Fails P2 in the worst way: the same allocation site can resolve differently depending on initialization order, so an object allocated from an arena can be freed to the heap. This is the hardest class of bug to diagnose and the one an allocator abstraction exists to prevent.
- Keeps the hidden global that invariant #5 forbids, while adding the full cost of Option B's signature churn. Strictly dominated: it pays B's price and keeps A's defect.

### Option D — Per-call allocator with no storage in the object

Pass the allocator to both create and destroy; store nothing.

- Satisfies P1 and P3; no per-object pointer cost; genuinely useful for pure arena/scratch regions that are bulk-released and never individually freed.
- Fails P2 at the API boundary: it makes correct pairing the *caller's* obligation across arbitrary time and call-stack distance, which is exactly the discipline this abstraction is supposed to remove.
- Rejected as the general rule, but explicitly retained as a permitted local pattern for arena-scoped allocations where the arena itself is the owning object (there the arena handle *is* the stored allocator).

## Decision

**Adopt Option B.**

1. **The allocator interface is specs.md §12's `EngineAllocator`** — a struct of `alloc(allocator, size, alignment)`, `free(allocator, memory)`, and `user_data` — not the current bare `engine_alloc_fn`/`engine_free_fn` pair. The `user_data` field is what makes stateful allocators (arena cursor, pool freelist, leak-tracking counters) possible; the current typedefs structurally cannot carry state, which is the deeper reason they must go. Alignment is part of the signature from day one, because retrofitting alignment into an allocator interface after callers exist is a breaking change to every backend.

2. **The allocator is a construction parameter and a stored field.** Every subsystem that owns heap memory takes `const EngineAllocator *allocator` in its init/create entry point and stores it in its own (opaque, `.c`-private) struct. Its shutdown/destroy path frees exclusively through that stored pointer. An object's allocator is fixed at construction and immutable for its lifetime.

3. **The caller owns the allocator; the engine borrows it.** The `EngineAllocator` outlives every object built from it. The engine never copies, frees, or takes ownership of the allocator — hence `const *`. Lifetime is the caller's obligation and MUST be documented as such per §49.

4. **`NULL` selects the default heap allocator.** Passing `NULL` yields the built-in `malloc`/`free`-backed heap allocator (specs.md CORE-006). This keeps trivial call sites short (`engine_init(&e, NULL)`) without reintroducing ambient state: the resolution happens once, at construction, and the resolved pointer is stored. Two calls with `NULL` resolve to the same stateless singleton heap allocator, which is a constant, not mutable global state.

5. **`engine_alloc()` / `engine_free()` are allocator-first free functions**, e.g. `engine_alloc(allocator, size, alignment)`. They do **not** read from ambient or thread-local context — that would be Option A wearing Option B's signature. They exist to centralize null-checks, alignment defaults, and (in debug builds) instrumentation.

6. **The renderer routes through the same interface.** `renderer_init` takes an allocator and stores it; `renderer.c`'s direct `malloc`/`free` calls are removed. There is no second memory mechanism in the engine.

7. **Direct `malloc`/`free` outside the default heap allocator implementation is prohibited in `core`, `renderer`, and the API layers**, and this becomes a grep-based architecture test alongside the existing OS/GPU include checks. The one legitimate `malloc`/`free` call site in the engine is the default heap allocator itself.

### Placement and dependency direction

The allocator interface MUST NOT live in the engine-runtime translation unit. specs.md §4.1 puts `Engine → Renderer → GPU API`, so linking today's `engine_core` (which owns `engine_init`/`tick`/`shutdown`) from `engine_renderer` would be a prohibited inverse dependency.

Resolution: the allocator is **foundation**, not runtime. It belongs in a dependency-free leaf module (`src/core/allocator.h` plus the default heap implementation), built as its own target that depends on nothing but the freestanding C headers. Core runtime, renderer, GPU API, platform API, and every backend may all link that leaf without inverting anything.

This exposes a latent problem worth naming: the current single `engine_core` target conflates specs.md §8's core *foundation* modules (types, allocator, assert, result) with the engine *runtime lifecycle*. Those are different layers with different dependents. Splitting them is a prerequisite for this ADR, not an optional cleanup.

### Backward compatibility

`engine_set_allocators()` is public API, declared in `engine.h`, and task 001's acceptance criteria were signed off against a tree containing it. Removing it is a real, non-free cost and is recorded as such rather than waved away.

**It is removed outright, not deprecated.** Justification, and the honest limits of it:

- Its only in-repo caller is nothing. `main.c` and the test suite never call it; the sole consumers of the allocator are `engine_init`/`engine_shutdown` internally. The measured blast radius of deletion is one declaration and one definition.
- No external consumers exist. The project is pre-0.1 with no released API surface, and §49's stability rules govern the API being designed, not this stub.
- Task 001 was explicitly a structural/tooling task whose non-goals include "Implement core engine logic". `engine_set_allocators()` was a placeholder that satisfied a stub requirement; treating it as a stability commitment would freeze an accident.
- A deprecation shim is actively harmful here: any shim that keeps a global for the old path reintroduces exactly the Option C failure mode (mismatched alloc/free pairs) for the entire deprecation window, in the subsystem where that bug is least debuggable.

`engine_init`'s signature also changes (`engine_init(struct engine **owned, const EngineAllocator *allocator)`), which breaks `main.c` and `tests/contract/test_api_contracts.c`. Both are in-repo, both are one-line edits (`engine_init(&e, NULL)`). If the cost were larger, the honest alternative would be an `engine_create_with_allocator()` companion entry point — rejected here only because the migration is this cheap, and two constructors would be permanent API surface bought to avoid a two-line change.

## Consequences

### Positive

- CLAUDE.md invariant #5 and `rules/core.md` hold in `core` and `renderer` without amending either document.
- The codebase converges on specs.md §12 instead of drifting from it; §12's future allocator list becomes reachable rather than aspirational.
- Alloc/free symmetry is structural, not a documented convention.
- Testability: a leak-tracking or deliberately failing allocator can be injected per test, in-process and in parallel, enabling real coverage of the `engine_init` allocation-failure branch that is currently untestable.
- Per-subsystem and per-frame allocators become possible with no further API breakage.

### Negative / costs

- Public API break: `engine_set_allocators()` removed; `engine_init` and `renderer_init` gain a parameter. `main.c` and the contract test require edits.
- Every heap-owning struct grows one pointer, and every constructor grows one parameter. This is permanent ergonomic cost, paid at every future subsystem.
- The `engine_core` target must be split into foundation and runtime before the renderer can link the allocator, which is scope beyond a pure allocator change.
- The engine cannot detect a caller freeing an allocator while objects built from it are still alive. This is a documented contract, not an enforced one; a debug-build outstanding-allocation counter is the mitigation, not a guarantee.
- `NULL`-means-default is a small ergonomic concession that a purist reading of "explicit" would reject. It is bounded: it resolves once, at construction, to a stored pointer.

### Neutral / follow-up

- Thread-safety is now a per-allocator property that each implementation MUST document (§49). The default heap allocator is as thread-safe as the platform `malloc`. The interface itself imposes no synchronization.
- Type naming is unresolved: specs.md §50 prescribes PascalCase types (`EngineAllocator`), while existing `src/core/` code uses snake_case `_t` (`engine_error_t`). This ADR quotes §12's spelling; the codebase currently contradicts it. That conflict is real, out of scope here, and needs its own decision before more headers land — see follow-ups.

## Follow-up work (not performed in this ADR)

1. **Split `engine_core`** into a dependency-free foundation target (allocator, types) and the engine runtime target. Prerequisite for 2–4. — `build-engineer`
2. **Implement `EngineAllocator` + default heap allocator** (specs.md CORE-005, CORE-006) in the foundation module. — `c-engineer`
3. **Migrate core**: remove `engine_alloc_fn`/`engine_free_fn`/`engine_set_allocators` and the `g_alloc`/`g_free` globals; thread the allocator through `engine_init`/`engine_shutdown`; update `main.c` and `tests/contract/test_api_contracts.c`. — `c-engineer`
4. **Migrate renderer**: `renderer_init` takes and stores an allocator; delete direct `malloc`/`free` from `renderer.c`. — `renderer-engineer`
5. **Architecture test**: forbid `malloc`/`free`/`realloc`/`calloc` in `src/core` and `src/renderer` outside the default heap allocator, mirroring the existing OS/GPU include checks in `cmake/Common.cmake`. — `test-engineer`
6. **Resolve the type-naming conflict** between specs.md §50 (PascalCase) and existing `_t` snake_case core code; record as its own ADR. — `spec-writer`
7. **Confirm the GPU and platform API layers** adopt the same allocator parameter convention before their first allocating entry points are written, so this decision does not have to be relitigated per subsystem. — `gpu-architect`, `platform-engineer`
