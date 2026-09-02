# TASK-002: Split `engine_core` into `engine_foundation` + `engine_core`

## Agent
`build-engineer`

## Context

### Required
- `docs/spec/core/allocator.md` — §8 only (CORE-ALLOC-080..083). Do not read §2–§7; this task writes no allocator logic.
- `.claude/rules/tooling.md` (`tools/factory` contract)
- `src/core/CMakeLists.txt` (the target being split)
- `src/renderer/CMakeLists.txt` (gains the foundation link; must not gain `engine_core`)
- `cmake/Common.cmake` (`forbid_os_gpu_includes`)
- `CMakeLists.txt` (root — links `nayla_game_engine` to `engine_core`)

### Optional
- `docs/adr/ADR-009-allocator-ownership.md` — "Placement and dependency direction" section, for why the split exists
- `tests/{unit,contract,architecture}/CMakeLists.txt` (all currently link `engine_core`)

## Dependencies
None. This is ADR-009 follow-up #1 and the prerequisite for TASK-003, TASK-004, TASK-005.

## Objective

One CMake directory (`src/core/`) produces two libraries: a dependency-free `engine_foundation` and a narrowed `engine_core` that links it PUBLIC — so `engine_renderer` can reach the allocator without linking the engine runtime.

## Requirements

1. **Two targets from `src/core/CMakeLists.txt`**, exactly as CORE-ALLOC-080 tabulates: `engine_foundation` from `allocator.c` + `result.c`; `engine_core` narrowed to `engine.c` alone.
2. **Create the four foundation files as compile-clean placeholders.** TASK-003 authors their real contents; this task only makes the target buildable. Each `.c` includes its own header; each header carries an include guard (`ENGINE_ALLOCATOR_H`, `ENGINE_RESULT_H`) and the single declaration `extern int engine_foundation_placeholder;` so the translation unit is non-empty under `-Wpedantic` without emitting a symbol. TASK-003 deletes that declaration. Paths are exactly those CORE-ALLOC-080 names: `src/core/allocator.h`, `src/core/allocator.c`, `src/core/result.h`, `src/core/result.c`.
3. **`engine_foundation` links no engine target** (CORE-ALLOC-081). Give it the same repo-root PUBLIC include directory `engine_core` already has, so consumers keep including `"src/core/…"`.
4. **`engine_core` links `engine_foundation` PUBLIC** (CORE-ALLOC-080), so `main.c`, the tests, and the root executable reach the foundation headers transitively with no root-`CMakeLists.txt` edit.
5. **`engine_renderer` links `engine_foundation` PUBLIC and MUST NOT link `engine_core`** (CORE-ALLOC-082, ALLOC-INV-11). Its existing `PRIVATE engine_gpu_api` link stays.
6. **Both targets carry the checks** (CORE-ALLOC-083): `forbid_os_gpu_includes` and `-Wall -Wextra -Wpedantic` on `engine_foundation` as well as `engine_core`. Both pass `src/core` as the source dir, so the greps overlap — that is expected, not a bug to optimize away.
7. **No source file outside `src/core/CMakeLists.txt` and `src/renderer/CMakeLists.txt` changes.** If the root `CMakeLists.txt` or any `tests/*/CMakeLists.txt` needs an edit to keep building, that is a signal requirement 4 was implemented as PRIVATE instead of PUBLIC.

## Acceptance
- [ ] `src/core/CMakeLists.txt` defines both `engine_foundation` and `engine_core`; `engine_core`'s source list is `engine.c` only.
- [ ] `engine_foundation` has no `target_link_libraries` naming an engine target (CORE-ALLOC-081).
- [ ] `engine_renderer` links `engine_foundation`; no occurrence of `engine_core` in `src/renderer/CMakeLists.txt` (ALLOC-INV-11).
- [ ] `forbid_os_gpu_includes` and the three warning flags are applied to both targets (CORE-ALLOC-083).
- [ ] `tools/factory build debug` and `tools/factory build release` succeed with no new warnings.
- [ ] `tools/factory test all` is green — identical results to before the split.
- [ ] Root `CMakeLists.txt`, `main.c`, and all `tests/*/CMakeLists.txt` are unmodified.

## Tests
- No new test files. This task is verified by the existing suite continuing to pass through `tools/factory test all` plus the build-graph acceptance items above.
- Verify ALLOC-INV-11 by inspection here; its automated assertion is TASK-006's.

## Non-Goals
- Writing any allocator, result, or default-heap-allocator logic — that is TASK-003.
- Touching `src/core/engine.h`, `src/core/engine.c`, `src/renderer/renderer.c`, `src/renderer/api.h`, `main.c`, or any test source.
- Moving files between directories. CORE-ALLOC-080: the split is by target, not by directory.
- Introducing CMake presets, an install/export step, or shared-library variants.

## Review
Required: yes
Reviewer: `reviewer`
