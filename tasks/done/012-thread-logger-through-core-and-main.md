# TASK-012: Thread the logger through `engine_init` and adopt it in `main.c`

## Agent
`c-engineer`

## Context

### Required
- `docs/spec/core/logging.md` — CORE-LOG-003, -004, -005, -084 (construction parameters); §9's CORE-LOG-082 lifecycle diagram; §13.1 and §13.2's `engine.h`/`engine.c`/`main.c` rows
- `.claude/rules/core.md`
- `src/core/engine.h`, `src/core/engine.c` (the code being migrated)
- `src/core/logging.h` (from TASK-008 — the contract being adopted)
- `main.c` (the first consumer, and the owner of the logger)

### Optional
- `tasks/done/004-migrate-core-to-allocator-and-result.md` — the same migration one parameter earlier; the precedent for how call sites and comments were handled
- `docs/spec/core/allocator.md` — CORE-ALLOC-060..067 only, for the borrow-and-store pattern being repeated

## Dependencies
- TASK-008 (`logging.h` and `engine_console_log_sink()` must exist)

Not dependent on TASK-009, TASK-010, or TASK-011. **File-overlap advisory:** this task edits `tests/architecture/test_core_invariants.c:39` and `tests/integration/test_allocator_leaktrack.c:118`, files that TASK-009 and TASK-011 also extend. The edits are in different regions and conflict only textually — run this task before or after those, not concurrently in the same worktree.

## Objective

`engine_init` takes and stores a borrowed `const EngineLogger *`, its three failure paths log at the point of failure, and `main.c` owns a real logger constructed first and shut down last.

## Requirements

1. **`engine_init` gains a third parameter**, `const EngineLogger *logger`, after the allocator (CORE-LOG-084). Include `"src/core/logging.h"` from `engine.h`, by the repo-root-relative path the other two foundation headers already use.
2. **Document `NULL` as *discard*, and say what it is not.** `engine.h`'s comment block currently documents `allocator == NULL` as "use `engine_default_allocator()`". The logger parameter's `NULL` means **discard**, and MUST NOT be resolved to any default because none exists (CORE-LOG-005). The two parameters therefore have deliberately opposite `NULL` semantics on the same line — state that explicitly in the comment, because a reader who pattern-matches from the allocator will get it wrong. Also document CORE-LOG-004: the logger and every sink on it must outlive the engine.
3. **No validation, no resolution.** Unlike the allocator at `engine.c:13-22`, the logger is stored as received (CORE-LOG-084): `NULL` is legal, and a non-`NULL` logger's validity is the caller's obligation under CORE-LOG-004. Do not add a `resolved` local, a callback check, or an `ENGINE_RESULT_INVALID_ARGUMENT` branch for it.
4. **`struct Engine` (`engine.c:3-6`) gains `const EngineLogger *logger;`**, set once beside `engine->allocator = resolved;` at `engine.c:32` and never reassigned (CORE-LOG-003). There is no operation to change an engine's logger.
5. **`engine_init`'s three failure returns each gain one log line** — `engine.c:10` (`!owned_engine`), `engine.c:21` (a callback is `NULL`), `engine.c:28` (allocation returned `NULL`). This is §13.2's "the three existing failure returns gain a log line each": `engine.c` has six failure returns in total, but only `engine_init` has exactly three, and §9's payoff bullet names `engine_init`'s out-of-memory branch specifically as the reason CORE-LOG-008 exists. Use `ENGINE_LOG_ERROR` with the subsystem literal `"Core"` (CORE-LOG-021: a string literal, static storage). Note the `engine.c:10` site logs through a parameter whose object does not exist yet — that is exactly the case CORE-LOG-005 makes safe.
6. **Log arguments are pure expressions** (CORE-LOG-062). No call, no increment, no assignment inside a logging macro's arguments; they are unevaluated in both filtered cases.
7. **Do not name a stream function in `engine.c` or `engine.h`, including in a comment** — LOG-INV-9's grep (TASK-009) is textual and covers all of `src/core`.
8. **`main.c` becomes the worked example of CORE-LOG-082's ordering.** Define an `EngineLogger` on `main`'s stack; `engine_log_init` it (checking the `ENGINE_NODISCARD` result) **before** `engine_init`; `engine_log_add_sink(&logger, engine_console_log_sink())`; pass `&logger` to `engine_init`; `engine_log_shutdown(&logger)` **after** `engine_shutdown`, on every exit path including the two early `return 1;`s. Logger first, destroyed last — outside the engine's own lifecycle. Do not install a context source: no monotonic clock exists yet, and records correctly carry the two "unknown" sentinels (CORE-LOG-040, §12 edge case 12).
9. **`main.c`'s four diagnostic sites MUST migrate; its three status sites are a judgment call.** The `fprintf(stderr, …)` calls at `main.c:10`, `:17`, `:20`, and `:29` are diagnostics and become `ENGINE_LOG_ERROR(&logger, "App", …)`. The `printf` calls at `main.c:14`, `:25`, and `:33` are the program's own stdout output; CORE-LOG-052's stdout/stderr rule governs the console sink, not the application, and §13.1 makes `main.c` permanently out of LOG-INV-9's scope — so either migrating them to `ENGINE_LOG_INFO` or leaving them as `printf` is correct. Pick one, apply it consistently, and state which and why in the review.
10. **Six `engine_init` call sites gain a third argument.** `NULL` is the correct value everywhere except `main.c` — CORE-LOG-005 makes it the right choice for a test that does not care about output (§13.2's `tests/contract/` row). Exactly: `main.c:8` → `&logger`; `tests/unit/test_core_isolation.c:12`, `tests/contract/test_api_contracts.c:14`, `tests/contract/test_allocator_contracts.c:36`, `tests/integration/test_allocator_leaktrack.c:118`, `tests/architecture/test_core_invariants.c:39` → `NULL`. No other change to those five test files.
11. **Weigh CORE-LOG-084's `EngineInitDesc` question before writing the signature, and do not answer it here.** `logging.md` §14 puts the shape of `engine_init`'s parameter list in `docs/spec/core/engine.md`, which does not exist. Three parameters is what this spec fixes. If the analysis concludes a descriptor struct should land now, that is a specification change: stop and escalate to `spec-writer` per CLAUDE.md § Escalation Rules. Do not invent `EngineInitDesc` inside this task.

## Acceptance
- [ ] `engine_init(Engine **, const EngineAllocator *, const EngineLogger *)` is the declared signature, with `ENGINE_NODISCARD` retained.
- [ ] `engine_init(&e, NULL, NULL)` succeeds and produces no output (CORE-LOG-005, LOG-INV-4).
- [ ] `engine_init(&e, &failing_allocator, &logger)` emits exactly one `ERROR` record and still returns `ENGINE_RESULT_OUT_OF_MEMORY` — the out-of-memory path logs without allocating (CORE-LOG-008, §12 edge case 11).
- [ ] `struct Engine`'s stored logger is assigned exactly once and never reassigned (CORE-LOG-003).
- [ ] `main.c` calls `engine_log_init` before `engine_init` and `engine_log_shutdown` after `engine_shutdown` on **every** exit path (CORE-LOG-082).
- [ ] `grep -rE '(printf|fprintf|puts|fputs|fwrite|stdout|stderr)' src/core` matches `log_console.c` and nothing else (LOG-INV-9).
- [ ] No `engine_default_logger`, `engine_log_set_global`, or file-scope mutable logger appears anywhere (LOG-INV-5, CORE-LOG-001).
- [ ] `tools/factory build debug` and `build release` succeed with zero warnings, including no `warn_unused_result` warnings from `engine_log_init` / `engine_log_add_sink`.
- [ ] `tools/factory test all` is green.
- [ ] `git diff --stat` touches only `src/core/engine.h`, `src/core/engine.c`, `main.c`, and the five test files of requirement 10.

## Tests
- Existing suite, updated in place per requirement 10; no new test files.
- The logger's own runtime coverage is **TASK-010's**. Do not duplicate it here.
- If `main.c`'s output changes, no test asserts on it today — verify by running `nayla_game_engine` by hand and confirming the diagnostics still reach the standard error stream.

## Non-Goals
- Any renderer change (`src/renderer/*`) — TASK-013.
- Introducing `EngineInitDesc` or otherwise reshaping the parameter list (requirement 11; `logging.md` §14).
- Adding log lines to `engine_tick` or `engine_shutdown`. §13.2 budgets ~12 lines across `engine.h` and `engine.c`; requirement 5's three sites are the scope.
- Installing a time source or thread-id source — PLATFORM-005 and PLATFORM-008 do not exist (CORE-LOG-041, §14).
- Adding any sink beyond `engine_console_log_sink()`, or reading a log level from the environment or the command line (`logging.md` §14).
- Changing `main.c`'s program behaviour, exit codes, or the engine's allocator handling.
- Adding engine functionality: no assertions module, no lifecycle stages beyond init/tick/shutdown.

## Review
Required: yes
Reviewer: `reviewer`

Reviewer note: requirement 9's choice for the three `printf` sites, and requirement 11's `EngineInitDesc` analysis, are both explicitly for review to weigh.
