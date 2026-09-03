# TASK-011: Assert the no-global-logger greps and the compile-time gate

## Agent
`test-engineer`

## Context

### Required
- `docs/spec/core/logging.md` — LOG-INV-5, LOG-INV-10, LOG-INV-13; CORE-LOG-001, -015, -061, -062, -091. Nothing else.
- `.claude/rules/testing.md`
- `.claude/rules/tooling.md`
- `tests/architecture/test_core_invariants.c` and `tests/architecture/CMakeLists.txt` (the `forbidden_pattern_found` helper; the `NAYLA_CORE_SRC_DIR` / `NAYLA_RENDERER_SRC_DIR` compile definitions already present)
- `tests/integration/CMakeLists.txt` (where LOG-INV-13's two build configurations are wired)
- `src/core/logging.h` (from TASK-008 — the file LOG-INV-10 greps and the macros LOG-INV-13 exercises)

### Optional
- `tasks/done/006-architecture-test-forbid-direct-allocation.md` — requirement 4's "one named check, one failure message" precedent and the `\bstatic\b.*Allocator | grep -v const` shape at `test_core_invariants.c:128`

## Dependencies
- TASK-008 (LOG-INV-10 greps `src/core/logging.h`, which must exist; LOG-INV-13 needs the macros and `ENGINE_LOG_COMPILE_MIN_LEVEL`. **This corrects `logging.md` §13.3's claim** that its step 3 "has no prerequisite beyond this spec" — that holds for LOG-INV-9 only, which is why LOG-INV-9 was split into TASK-009.)
- TASK-009 (both tasks add checks to `tests/architecture/test_core_invariants.c` and both extend the same forbid mechanism; the dependency serializes the edit, it is not a logical one)

## Objective

The absence of any global logger is asserted by grep, `logging.h`'s include set is asserted by grep, and the compile-time gate is proved to *remove* call sites rather than merely filter them.

## Requirements

1. **LOG-INV-5, as its own named check with its own failure message.** No file-scope **mutable** variable in `src/core` or `src/renderer` holds an `EngineLogger`, an `EngineLogger *`, an `EngineLogSink *`, or an `EngineLogLevel`; and no symbol matching `*default_logger*`, `*global_log*`, or `*log_set_global*` exists **anywhere in the repository** (CORE-LOG-001, CLAUDE.md invariant #5). Reuse the shape already at `test_core_invariants.c:128` — `grep -rE '\bstatic\b.*<Type>' … | grep -v const`. The `grep -v const` filter is load-bearing here: `log_console.c`'s `static const EngineLogSink` is required by CORE-LOG-050 and must pass. `\bstatic\b` is word-bounded so it does not match `static_assert`.
2. **The symbol half of LOG-INV-5 is repository-wide, not `src/`-scoped.** `engine_default_logger`, `engine_log_set_global`, or a thread-local logger appearing in a test, an example, or `main.c` would defeat the rule just as thoroughly. Scope that grep accordingly and say so in the failure message.
3. **LOG-INV-10, as its own named check.** `src/core/logging.h` includes exactly the five headers of CORE-LOG-091 — `<stdarg.h>`, `<stdbool.h>`, `<stddef.h>`, `<stdint.h>`, `"src/core/result.h"` — and mentions none of `EngineAllocator`, `malloc`, `<stdio.h>`, `_Atomic`. Assert **exactly five**, not "at least these five": a sixth include is the failure mode this exists to catch (the analogue of ALLOC-INV-8).
4. **LOG-INV-13 as a real integration test with two build configurations.** One new source file under `tests/integration/`, compiled into **two** executables from the same source: one with `target_compile_definitions(… ENGINE_LOG_COMPILE_MIN_LEVEL=4)` and one with `=0`. The source contains `ENGINE_LOG_TRACE(logger, "Core", "%u", counter_that_increments())`. At `=4` the counter is unchanged and no record is delivered; at `=0` it increments exactly once and exactly one record is delivered. Register both with `add_test(NAME integration/<name> …)`.
5. **LOG-INV-13 is the only end-to-end proof that the gate removes the call rather than filtering it** (CORE-LOG-015/061). A runtime-level test cannot distinguish the two, which is why this one is worth its build complexity. Assert the counter, not merely the absence of output.
6. **This test deliberately writes a side-effecting log argument, which CORE-LOG-062 forbids in engine code.** That is the point of the test — state it in a comment at the call site so a reviewer does not read it as a defect, and confine it to this one file.
7. **Do not weaken a failing check** (`.claude/rules/testing.md`). If a grep produces a false positive on conforming code, tighten the pattern; do not narrow the scope or drop the invariant. A genuine LOG-INV-5 or LOG-INV-10 failure means TASK-008 is incomplete — report it rather than editing `src/`.

## Acceptance
- [ ] LOG-INV-5 and LOG-INV-10 each pass, each as a separately named check with a distinct, greppable failure message.
- [ ] Adding `static EngineLogger g_logger;` to `src/core/engine.c` fails LOG-INV-5; `log_console.c`'s `static const EngineLogSink` still passes. Verify both directions, then revert.
- [ ] Adding `#include <stdio.h>` to `src/core/logging.h` fails LOG-INV-10. Verify, then revert.
- [ ] Both LOG-INV-13 executables build and pass, and their compile definitions differ only in `ENGINE_LOG_COMPILE_MIN_LEVEL`.
- [ ] Inverting either LOG-INV-13 expectation makes exactly that one of the two tests fail — the two configurations are not both passing for the same reason. Verify, then revert.
- [ ] `tools/factory test architecture` and `tools/factory test integration` pass; `tools/factory test all` is green.
- [ ] `tools/factory build debug` and `build release` succeed with zero warnings.
- [ ] No file under `src/` is modified.

## Tests
This task *is* the test work: additions to `tests/architecture/test_core_invariants.c`, one new source under `tests/integration/` built twice, and the `CMakeLists.txt` edits to wire them.

## Non-Goals
- LOG-INV-9 and the `forbid_direct_output` CMake macro — TASK-009.
- LOG-INV-3/4/6/7/8/11/12/14 — TASK-010's runtime suite over a capture sink.
- LOG-INV-1/2 — `static_assert`s delivered by TASK-008.
- LOG-INV-15 — review-enforced and explicitly not greppable.
- Editing any file under `src/` to make a check pass (requirement 7).
- Adding CI, adopting a test framework, or introducing CMake presets for the two LOG-INV-13 configurations — two `add_executable` calls in one `CMakeLists.txt` is the whole mechanism.
- Testing `engine_init` or `renderer_init` logger threading — TASK-012 and TASK-013.

## Review
Required: yes
Reviewer: `reviewer`
