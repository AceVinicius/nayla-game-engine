# TASK-006: Forbid direct allocation, and cover the allocator with real tests

## Agent
`test-engineer`

## Context

### Required
- `docs/spec/core/allocator.md` — §10 (ALLOC-INV-3..13); CORE-ALLOC-042 for the single exemption
- `.claude/rules/testing.md`
- `.claude/rules/tooling.md`
- `cmake/Common.cmake` and `cmake/forbid-pattern.sh` (the existing grep-check mechanism to extend)
- `tests/architecture/test_core_invariants.c` and `tests/architecture/CMakeLists.txt`
- `tests/integration/CMakeLists.txt` (placeholder — no test source exists yet)

### Optional
- `docs/spec/core/result.md` — RESULT-INV-4/5/6/7/8/9 only
- `src/core/allocator.h` (from TASK-003 — the surface under test)
- `tests/contract/test_api_contracts.c`, `tests/unit/CMakeLists.txt`

## Dependencies
- TASK-003 (`allocator.c` must exist to be the exemption; the module must exist to be tested)
- TASK-004 (until core is migrated, the `malloc`/`engine_error_t` greps fail)
- TASK-005 (until the renderer is migrated, the `malloc` grep fails)

## Objective

Direct `malloc`/`free`/`calloc`/`realloc`/`aligned_alloc` outside `src/core/allocator.c` fails the build, and the allocator's contract is covered by unit, contract, and integration tests that can actually fail.

## Requirements

1. **Extend the grep mechanism, do not replace it.** Add a `forbid_direct_allocation(target_name source_dir)` macro to `cmake/Common.cmake` alongside `forbid_os_gpu_includes`. `cmake/forbid-pattern.sh` currently hard-fails on anything other than 4 arguments; give it an optional 5th "exempt this filename" argument and keep the 4-argument form working unchanged, so no existing caller breaks.
2. **`src/core/allocator.c` is the sole exemption** (CORE-ALLOC-042, ALLOC-INV-7). Exactly one file, named explicitly — not a directory, not a pattern. Apply the macro to `engine_foundation`, `engine_core`, and `engine_renderer`.
3. **Mirror the checks at runtime too.** `tests/architecture/test_core_invariants.c` already re-runs the OS/GPU greps independently of CMake, with a stated reason: a regression in the build-time check must not silently untest the invariant. Follow that precedent for the new checks, and add a `NAYLA_RENDERER_SRC_DIR` compile definition beside the existing `NAYLA_CORE_SRC_DIR` so `src/renderer` is in scope.
4. **Grep-shaped invariants to cover**, each as its own named check with its own failure message: ALLOC-INV-7 (allocation calls), ALLOC-INV-8 (`allocator.h` includes only `<stddef.h>`; mentions neither `uintptr_t` nor `EngineResult`), ALLOC-INV-9 (no surviving `engine_alloc_fn` / `engine_free_fn` / `engine_set_allocators`), ALLOC-INV-10 (no file-scope mutable allocator or allocator function pointer), RESULT-INV-4, RESULT-INV-5, RESULT-INV-6, RESULT-INV-9.
5. **ALLOC-INV-11 as a build-graph assertion** — `engine_renderer` must not link `engine_core`, directly or transitively (CORE-ALLOC-082). A CMake-level check is acceptable here; a grep over `src/renderer/CMakeLists.txt` is not sufficient on its own because the link could arrive transitively.
6. **Unit tests (`tests/unit/`, linked against `engine_foundation` only)** — ALLOC-INV-3 (singleton is non-`NULL`, stable across calls, callbacks non-`NULL`), ALLOC-INV-4 (every power of two in `[1, ENGINE_ALLOCATOR_MAX_ALIGNMENT]` yields a correctly aligned pointer that frees cleanly), ALLOC-INV-5 (the five rejection cases: `size == 0`, `alignment == 0`, `alignment == 3`, `alignment == MAX * 2`, `size == SIZE_MAX`), ALLOC-INV-6 (`engine_free(a, NULL)`, `engine_free(NULL, p)`, `engine_alloc(NULL, 1, 1)`), RESULT-INV-7 (`engine_result_name` non-`NULL` for every enumerator and for at least one out-of-range value).
7. **Contract test — ALLOC-INV-13.** A test allocator whose `alloc` always returns `NULL` makes `engine_init` return `ENGINE_RESULT_OUT_OF_MEMORY`. This is the branch at the old `engine.c:22-24` that was untestable before ADR-009. Initialize the out-parameter to `NULL` before the call and assert it is `NULL` afterwards — CORE-RESULT-013 permits either "set to `NULL`" or "left unmodified", and pre-initializing makes the assertion valid under both.
8. **Integration test — ALLOC-INV-12.** A leak-tracking allocator injected into `engine_init` observes exactly as many `free` calls as `alloc` calls after `engine_shutdown`, and every freed pointer is one it returned. `tests/integration/` has no source and no `add_test` today; create both. This is the end-to-end check of CORE-ALLOC-030/067 and the main testability gain ADR-009 was bought for.
9. **Test allocators keep their own state in `user_data`** (CORE-ALLOC-004) and must not be file-scope mutable allocator structs — the tests must not reintroduce the pattern ALLOC-INV-10 forbids.
10. **Do not weaken a failing check.** If a grep pattern produces false positives on conforming code, tighten the pattern; do not narrow the scope or drop the invariant (`rules/testing.md`).

## Acceptance
- [ ] Introducing a `malloc` call into `src/core/engine.c` or `src/renderer/renderer.c` fails the build; the same call in `src/core/allocator.c` does not. Verify both directions by hand, then revert.
- [ ] The 4-argument form of `cmake/forbid-pattern.sh` still works for every existing `forbid_os_gpu_includes` caller.
- [ ] All nine grep invariants in requirement 4 pass, and each has a distinct, greppable failure message.
- [ ] ALLOC-INV-11 is asserted at configure or build time, not only by inspection.
- [ ] `tools/factory test unit`, `test contract`, `test integration`, and `test architecture` all pass and each runs at least one new assertion.
- [ ] Deliberately breaking one assertion in each new test file makes exactly that test fail — no silent passes.
- [ ] `tools/factory build debug` and `build release` succeed with zero warnings.

## Tests
This task *is* the test work. New sources: an allocator unit test and a result-name unit test under `tests/unit/`, the failing-allocator case added to `tests/contract/`, and a leak-tracking integration test under `tests/integration/`, each wired into CTest through its directory's `CMakeLists.txt` using the existing `add_test(NAME <layer>/<name> …)` naming convention.

## Non-Goals
- Changing any `src/` source to make a check pass — a failing check means TASK-004 or TASK-005 is incomplete; escalate rather than edit around it.
- TYPE-INV-1/3/4 (banned short aliases, `_t` suffixes, `uintptr_t` in public headers). `types.md` §7 intends these as architecture tests but no ADR-009 follow-up owns them; they need their own task and are deliberately out of scope here.
- ALLOC-INV-1/2 and RESULT-INV-1/2/3 — static assertions delivered by TASK-003.
- TYPE-INV-7/8 and RESULT-INV-10, which the specs mark review-enforced and not greppable.
- Adopting a test framework, adding CI, or restructuring the existing tests beyond the additions above.

## Review
Required: yes
Reviewer: `reviewer`
