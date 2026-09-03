# TASK-003: Implement the `EngineResult` and `EngineAllocator` foundation modules

## Agent
`c-engineer`

## Context

### Required
- `docs/spec/core/allocator.md` — §2, §3, §4, §5, §6, §9, §10 (the normative contract)
- `docs/spec/core/result.md` — §2, §3, §4, §5 (CORE-RESULT-009/010/011), §6, §8
- `.claude/rules/core.md`
- `src/core/allocator.h`, `src/core/allocator.c`, `src/core/result.h`, `src/core/result.c` (placeholders from TASK-002)
- `src/core/CMakeLists.txt` (`engine_foundation` target)

### Optional
- `docs/spec/core/types.md` — CORE-TYPES-014/016/017/021/023/024/025 and TYPE-INV-5 only
- `docs/adr/ADR-009-allocator-ownership.md` — "Decision" §1–§5, for the reasoning behind the shape

## Dependencies
- TASK-002 (the `engine_foundation` target must exist before `allocator.h` can be linked by anyone; ADR-009 follow-up #1)

## Objective

`engine_foundation` contains the real, dependency-free `result` and `allocator` modules: `EngineResult` with `engine_result_name`, and `EngineAllocator` with a conforming default heap allocator plus the `engine_alloc`/`engine_free` helpers.

## Requirements

1. **`result.h` lands first.** `engine_init`'s new signature (TASK-004) returns `EngineResult`, and `allocator.md` §12 sequences the result header ahead of the allocator work inside this task. Implement result, get it compiling, then allocator.
2. **`result.h`** — the five-code enum and its spelling per CORE-RESULT-001..007, the closed-set and value-stability rules CORE-RESULT-004/005, `ENGINE_NODISCARD` per CORE-RESULT-015, the `engine_result_name` declaration per CORE-RESULT-009. Static assertions for RESULT-INV-1, RESULT-INV-2, RESULT-INV-3. Only the five codes in result.md §4 — the deferred codes in that section stay deferred (CORE-RESULT-008).
3. **`result.c`** — `engine_result_name` as a `switch` returning string literals, honouring CORE-RESULT-010 (static storage duration) and CORE-RESULT-011 (never `NULL`; `"ENGINE_RESULT_UNKNOWN"` for an out-of-range value, which the `default` arm must handle).
4. **`allocator.h`** — the struct, the two callback typedefs, the two constants, and the three function declarations exactly as §2 fixes them: CORE-ALLOC-001 (field order), -002 (naming), -003, -004, -005 (no `result.h` include), -006/-007 (`const EngineAllocator *` in both callbacks). Static assertions for ALLOC-INV-1 and ALLOC-INV-2. `<stddef.h>` is the only include (CORE-ALLOC-081, ALLOC-INV-8).
5. **`allocator.c`** — `engine_default_allocator()` per CORE-ALLOC-040/041/043 (a `static const` singleton, `user_data == NULL`, documented `THREAD_SAFE` per CORE-ALLOC-021's fixed vocabulary), and the offset-header algorithm exactly as CORE-ALLOC-052 writes it, including the `_Alignof(void *)` clamp, the unconditional header, and CORE-ALLOC-054's before-the-addition overflow check. Validation per CORE-ALLOC-012/015. `uintptr_t` is confined to this file (CORE-ALLOC-051). Member calls are parenthesized per CORE-ALLOC-093.
6. **`engine_alloc` / `engine_free`** per CORE-ALLOC-090/091/092 — in particular `engine_alloc(NULL, …)` returns `NULL` and does **not** resolve to the default (CORE-ALLOC-091), and `engine_free(NULL, p)` is a leaking no-op (CORE-ALLOC-092).
7. **The `<limits.h>`/float/size static assertion block for TYPE-INV-5** lands here, in the foundation module, and nowhere else.
8. **`src/core/allocator.c` is the only file in `src/core` and `src/renderer` naming `malloc`/`free`/`calloc`/`realloc`/`aligned_alloc`** (CORE-ALLOC-042). `<stdlib.h>` is included by this file alone.
9. **Delete the `extern int engine_foundation_placeholder;` declarations** TASK-002 added to both headers.
10. Memory is not zeroed (CORE-ALLOC-017) and no debug poison is added (CORE-ALLOC-018 permits, does not require).

## Acceptance
- [ ] `allocator.h` includes `<stddef.h>` and nothing else; mentions neither `uintptr_t` nor `EngineResult` (ALLOC-INV-8).
- [ ] `result.h` and `allocator.h` include no engine header, and `engine_foundation` still links no engine target (CORE-ALLOC-081).
- [ ] No engine-defined type name ends in `_t`; enumerators are `ENGINE_RESULT_*` (CORE-TYPES-023/024, CORE-RESULT-007).
- [ ] Every `EngineResult`-returning declaration in `result.h` carries `ENGINE_NODISCARD` (CORE-RESULT-015).
- [ ] The static assertions for RESULT-INV-1/2/3, ALLOC-INV-1/2, and TYPE-INV-5 are present and the build passes them.
- [ ] `tools/factory build debug` and `build release` succeed with zero warnings from `engine_foundation`.
- [ ] `tools/factory test all` is green (the pre-existing suite is untouched and must not regress).
- [ ] `git diff --stat` touches only the four `src/core` foundation files.

## Tests
- Compile-time only in this task: the static assertions listed in requirement 2, 4, and 7.
- Runtime coverage of ALLOC-INV-3..6, ALLOC-INV-12/13, RESULT-INV-7/8 is **TASK-006's**, per `allocator.md` §12's migration table (`tests/` row → follow-up #5). Do not add test files here; if local verification is wanted, keep it throwaway and uncommitted.

## Non-Goals
- Editing `engine.h`, `engine.c`, `main.c`, `renderer.c`, `api.h`, or any test — that is TASK-004 and TASK-005.
- Arena, pool, stack, frame, scratch, or GPU-upload allocators; `realloc`; allocation statistics or leak tracking (allocator.md §13).
- `ENGINE_ASSERT` and the assertion module (CORE-003) — where the spec says "a debug build MAY assert", do nothing.
- Adding `ENGINE_NODISCARD` to `engine_alloc` — CORE-ALLOC-094 records that gap as accepted; do not close it by including `result.h` from `allocator.h`.
- Any `#ifdef _WIN32` or platform conditional in `allocator.c` (CORE-ALLOC-050 rationale 3).

## Review
Required: yes
Reviewer: `reviewer`
