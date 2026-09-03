# TASK-004: Migrate core to `EngineResult` and an explicit `EngineAllocator`

## Agent
`c-engineer`

## Context

### Required
- `docs/spec/core/result.md` — §6 (CORE-RESULT-012..017), §10's migration table
- `docs/spec/core/allocator.md` — §7 (CORE-ALLOC-060..070), §12's migration table
- `.claude/rules/core.md`
- `src/core/engine.h`, `src/core/engine.c` (the code being migrated)
- `src/core/result.h`, `src/core/allocator.h` (from TASK-003 — the contract being adopted)
- `main.c`, `tests/contract/test_api_contracts.c`, `tests/unit/test_core_isolation.c`, `tests/architecture/test_core_invariants.c` (the call sites that break)

### Optional
- `docs/spec/core/types.md` — CORE-TYPES-021/022/023 (the `Engine` / `EngineResult` renames folded in here)
- `docs/adr/ADR-009-allocator-ownership.md` — "Backward compatibility" section

## Dependencies
- TASK-003 (`result.h` and `allocator.h` must exist and compile)
- TASK-002 (transitively — `engine_core` must already link `engine_foundation` PUBLIC)

## Objective

`engine_init` takes and stores a borrowed `const EngineAllocator *`, every core entry point returns a named `EngineResult`, the `g_alloc`/`g_free` globals and `engine_set_allocators` are gone, and every in-repo call site compiles against the new signatures.

## Requirements

1. **This is one edit, not three.** `types.md` §9 and `result.md` §10 both fold their renames into this task rather than a standalone rename, because all three changes rewrite the same signatures in the same four files. Do the allocator threading, the `EngineResult` shape change, and the PascalCase renames in a single pass.
2. **`engine.h`** — delete `engine_alloc_fn` (line 13), `engine_free_fn` (line 14), `engine_set_allocators` (line 26), and `typedef int32_t engine_error_t;` (line 10). `struct engine` becomes `typedef struct Engine Engine;` (CORE-TYPES-021). `engine_init` gains `const EngineAllocator *allocator`; all three entry points return `EngineResult` with `ENGINE_NODISCARD` (CORE-RESULT-015). Rewrite the "Returns 0 on success" comments to name the codes each function can return, and document per CORE-ALLOC-060/061/065 that the allocator is borrowed, caller-owned, and must outlive the engine.
3. **Include the foundation headers by their repo-root-relative path** (`"src/core/result.h"`, `"src/core/allocator.h"`) — that is the include directory both targets export, and it matches how the tests already include `"src/core/engine.h"`.
4. **`engine.c`** — delete `default_alloc` (lines 4–6) and the `g_alloc`/`g_free` globals (lines 9–10); ALLOC-INV-10 forbids their return. Delete `engine_set_allocators` (lines 51–58). `struct Engine` gains a stored `const EngineAllocator *allocator` set once at construction and never reassigned (CORE-ALLOC-067). `<stdlib.h>` must no longer be included — `src/core/allocator.c` is the only sanctioned site (CORE-ALLOC-042).
5. **`engine_init` resolves and validates the allocator once** (CORE-ALLOC-068, CORE-ALLOC-070): `NULL` becomes `engine_default_allocator()`; then `alloc`/`free` are checked non-`NULL`, returning `ENGINE_RESULT_INVALID_ARGUMENT` if either is missing. The resolved, non-`NULL` pointer is what gets stored. No lazy or per-allocation resolution anywhere.
6. **Allocation goes through `engine_alloc(allocator, sizeof(Engine), _Alignof(Engine))`** and release through `engine_free` on the stored pointer (CORE-ALLOC-067). Both arguments are compile-time non-zero, so CORE-ALLOC-016's pre-validation is discharged by construction; a `NULL` return can therefore only mean exhaustion and translates to `ENGINE_RESULT_OUT_OF_MEMORY` (CORE-RESULT-019).
7. **Return codes**, replacing the eight `return` statements: `-1` at lines 18/32/44 → `ENGINE_RESULT_INVALID_ARGUMENT`; `1` at line 23 → `ENGINE_RESULT_OUT_OF_MEMORY`; `1` at line 36 → `ENGINE_RESULT_INVALID_STATE`; `0` at 27/39/48 → `ENGINE_RESULT_SUCCESS`. Validation order follows CORE-RESULT-012: arguments before state, always.
8. **`engine_init` on failure leaves `*owned_engine` untouched or `NULL` and releases anything it acquired** (CORE-RESULT-013).
9. **`main.c`** — note the ground-truth correction: it is at the **repository root**, not `src/main.c` as both spec tables state. `engine_error_t err` → `EngineResult err`; `struct engine *engine` → `Engine *engine`; `engine_init(&engine)` → `engine_init(&engine, NULL)`; `err != 0` → `!= ENGINE_RESULT_SUCCESS` (CORE-RESULT-014); `%d` → `%s` with `engine_result_name(err)`; the discarded `engine_shutdown(engine)` on the tick-failure path must be assigned and checked (CORE-RESULT-015).
10. **`tests/contract/test_api_contracts.c`** — update the declaration and comparisons; `engine_init(&e, NULL)`; assign and check the discarded `engine_shutdown(e)` at line 29; strengthen line 44 to assert exactly `ENGINE_RESULT_INVALID_ARGUMENT` (RESULT-INV-8).
11. **`tests/unit/test_core_isolation.c`** — not listed in either spec's migration table, but it breaks: line 9 declares `struct engine *`, line 12 calls `engine_init(&engine)` into an `int`, and lines 19/23 discard results that `ENGINE_NODISCARD` will now warn on. Migrate all four sites.
12. **`tests/architecture/test_core_invariants.c`** — update the declarations; retire the runtime `sizeof` check at lines 33–37 in favour of RESULT-INV-2's static assertion, which cannot fail once it exists; update the messages on lines 37/45/53/60 to name `EngineResult`. The two `forbidden_pattern_found` grep checks stay exactly as they are.

## Acceptance
- [ ] No occurrence of `engine_error_t`, `engine_alloc_fn`, `engine_free_fn`, `engine_set_allocators`, `g_alloc`, or `g_free` anywhere in the repository (ALLOC-INV-9).
- [ ] No file-scope mutable variable in `src/core` holds an allocator or allocator function pointer (ALLOC-INV-10, CLAUDE.md invariant #5).
- [ ] `src/core/engine.c` does not include `<stdlib.h>` and names no allocation function (CORE-ALLOC-042).
- [ ] No `return -1;` or bare `return 1;` remains in `src/core` (RESULT-INV-6).
- [ ] `engine_init(&e, NULL)` succeeds and `engine_shutdown` frees through the stored allocator (CORE-ALLOC-067).
- [ ] `engine_tick(NULL)` returns exactly `ENGINE_RESULT_INVALID_ARGUMENT` (RESULT-INV-8).
- [ ] `tools/factory build debug` and `build release` succeed with zero warnings, including no `warn_unused_result` warnings anywhere.
- [ ] `tools/factory test all` is green.

## Tests
- Existing suite, updated in place per requirements 10–12; no new test files.
- The injected-allocator tests that this migration makes possible — ALLOC-INV-12 (alloc/free call balance) and ALLOC-INV-13 (a failing allocator drives `engine_init` to `ENGINE_RESULT_OUT_OF_MEMORY`) — are **TASK-006's**. Do not write them here, but do leave `engine_init` structured so an injected allocator can reach that branch.

## Non-Goals
- Any renderer change (`src/renderer/*`) — TASK-005.
- The `forbid_direct_allocation` CMake macro and the grep-based architecture checks — TASK-006.
- Adding result codes beyond result.md §4's five (CORE-RESULT-008), or a deprecation shim for `engine_set_allocators` (ADR-009 forbids one explicitly).
- Adding engine functionality: no logging, no assertions module, no lifecycle stages beyond init/tick/shutdown.
- Changing `main.c`'s program behaviour or its printed output beyond the `%s`/`engine_result_name` swap.

## Review
Required: yes
Reviewer: `reviewer`
