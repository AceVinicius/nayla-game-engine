# TASK-008: Implement the logging module in `engine_foundation`

## Agent
`c-engineer`

## Context

### Required
- `docs/spec/core/logging.md` — §2.2, §2.3 (CORE-LOG-001..009), §3 (CORE-LOG-010..018), §4 (CORE-LOG-020..027), §5 (CORE-LOG-030..043), §6 (CORE-LOG-050..055), §7 (CORE-LOG-070..075), §8 (CORE-LOG-060..068), §9 (CORE-LOG-080..084), §10 (CORE-LOG-090..092), §12 (edge cases). Skip §1, §2.1, §13 — rationale and migration accounting, not contract.
- `.claude/rules/core.md`
- `src/core/result.h` (the `EngineResult` / `ENGINE_NODISCARD` surface being consumed)
- `src/core/CMakeLists.txt` (`engine_foundation` — the target gaining three sources)

### Optional
- `src/core/allocator.h`, `src/core/allocator.c` — the shape CORE-LOG-030 and CORE-LOG-051 deliberately mirror (`EngineAllocator`'s defined struct + `user_data`; the `static const` singleton)
- `docs/spec/core/types.md` — CORE-TYPES-004, -011, -012, -016, -017, -018, -021..025 only

## Dependencies

None. ADR-009 follow-ups #1–#5, which `logging.md` §13.3 names as the prerequisite, are **complete**: TASK-002 through TASK-007 are all in `tasks/done/`. `logging.md` §13.3's "mid-flight" note is stale as of this task's authoring; there is nothing to wait for.

## Objective

`engine_foundation` builds `logging.h`, `logging.c`, and `log_console.c`: a caller-placed `EngineLogger` with an explicit borrowed handle, a fixed-capacity sink table, two-stage filtering, and a `static const` console sink — with no allocation anywhere and no file-scope mutable state.

## Requirements

1. **Three new files, one existing edit.** `src/core/logging.h`, `src/core/logging.c`, `src/core/log_console.c` per CORE-LOG-090, added to `engine_foundation`'s source list in `src/core/CMakeLists.txt`. No other file in the repository changes. `engine_foundation` still links no engine target (CORE-ALLOC-081).
2. **`logging.h`'s include set is closed at five** (CORE-LOG-091): `<stdarg.h>`, `<stdbool.h>`, `<stddef.h>`, `<stdint.h>`, `"src/core/result.h"`. Note that `result.h` already includes `<assert.h>`, so `static_assert` is available without a sixth include — adding one fails LOG-INV-10. `allocator.h`, `<stdio.h>`, `<stdatomic.h>`, and every OS/GPU header are forbidden (CORE-LOG-008, -073, -091).
3. **`EngineLogLevel`** exactly as §3 declares it, with the LOG-INV-1 and LOG-INV-2 `static_assert`s. The numeric values are load-bearing, not cosmetic: CORE-LOG-060's `#if` comparisons are what they bind to.
4. **`EngineLogRecord`, `EngineLogSink`, and the four constants** exactly as §4 and §5.1 declare them, field order included (CORE-LOG-020, -030, -032). `min_level` is `int32_t`, not `EngineLogLevel` (CORE-LOG-032). No `bool` in either layout (CORE-TYPES-011).
5. **`EngineLogger` is a defined, caller-placed struct** with a fixed `ENGINE_LOG_MAX_SINKS == 8` array of `const EngineLogSink *`, a minimum level, and the two context-source function pointers plus their shared `user_data` (CORE-LOG-008, -034, -040). It is defined rather than opaque specifically so CORE-LOG-063's `static inline` can read `min_level` — that is the only sanctioned direct field read outside `logging.c`.
6. **`engine_log_write`'s signature is pinned by CORE-LOG-065.** `ENGINE_PRINTF_FORMAT(6, 7)` fixes the parameter list as `(logger, level, subsystem, file, line, format, ...)` — format is the sixth parameter, varargs the seventh. `engine_log_write_v` takes a `va_list` in that last position (CORE-LOG-067). Define `ENGINE_PRINTF_FORMAT` in `logging.h` beside the macros, expanding to `__attribute__((format(printf, a, b)))` under GCC/Clang and to nothing elsewhere.
7. **The six macros** per §8: selected by `#if <literal 0..5> >= ENGINE_LOG_COMPILE_MIN_LEVEL` on integer literals, never on enumerator names (CORE-LOG-060); the enabled form is the `do { if (engine_log_is_enabled(...)) { ... } } while (0)` of §8; the disabled form is `((void)0)` (CORE-LOG-061, -066). No `##__VA_ARGS__` and no `__VA_OPT__` — the format string is part of `__VA_ARGS__`, so it is never empty (CORE-LOG-064). `ENGINE_LOG_COMPILE_MIN_LEVEL` defaults per CORE-LOG-015 and `ENGINE_LOG_SOURCE_LOCATION` per CORE-LOG-024, both overridable by the build (`#ifndef` guards).
8. **`engine_log_write` follows CORE-LOG-018's five steps in that order**, and steps 3 and 4 run once per record, not once per sink — LOG-INV-12 tests exactly that. The message buffer is a stack buffer of `ENGINE_LOG_MESSAGE_MAX` bytes (CORE-LOG-025). No allocation occurs in any function in this task (CORE-LOG-008).
9. **`NULL` tolerance is a hard requirement, not a courtesy** (CORE-LOG-005): `engine_log_write`, `engine_log_write_v`, `engine_log_flush`, `engine_log_shutdown`, and every macro accept `logger == NULL` and discard. `NULL` MUST NOT resolve to any default — this is deliberately not CORE-ALLOC-068's rule, and the difference is the point.
10. **Return types.** `engine_log_init`, `engine_log_add_sink`, `engine_log_remove_sink`, and `engine_log_set_context_source` return `EngineResult` with `ENGINE_NODISCARD` (CORE-RESULT-015), with the code sets CORE-LOG-036, -037, and -080 fix. `engine_log_write`, `engine_log_write_v`, `engine_log_flush`, and `engine_log_shutdown` return `void` (CORE-LOG-039, -081, -083; CORE-RESULT-017). **`engine_log_set_level`'s return type is not fixed by any CORE-LOG rule** — CORE-LOG-006 and -016 name the function without typing it. Adopt CORE-LOG-080's precedent: validate `min_level` against `[TRACE, OFF]` and return `ENGINE_NODISCARD EngineResult`, `ENGINE_RESULT_INVALID_ARGUMENT` for a `NULL` logger or an out-of-range level. Record that reading in the code comment. If review disagrees, that is a `spec-writer` question, not a redesign (CLAUDE.md § Escalation Rules).
11. **`engine_log_level_name` mirrors `engine_result_name` exactly** (CORE-LOG-014): a `switch` over string literals, static storage duration, never `NULL`, `"UNKNOWN"` from the `default` arm.
12. **Edge cases §12 items 2, 3, 4, and 14 are code, not commentary.** `subsystem == NULL` substitutes the literal `"?"` and still delivers; an over-long message truncates, stays NUL-terminated, and sets `ENGINE_LOG_RECORD_TRUNCATED`; a negative `vsnprintf` return yields an empty message with the truncation flag set and the record still delivered; a record whose level is `ENGINE_LOG_LEVEL_OFF` is discarded (CORE-LOG-011).
13. **`log_console.c` is the sole `<stdio.h>` consumer** in `src/core` and `src/renderer` (CORE-LOG-052). One `fprintf` per record (CORE-LOG-053), to `stderr` and never `stdout`. It renders `ENGINE_LOG_TIMESTAMP_UNKNOWN` and `ENGINE_LOG_THREAD_UNKNOWN` as "unknown", not as a number (CORE-LOG-040). Its exact rendering is not contractual (CORE-LOG-055) — do not let any other file depend on it.
14. **Do not name `printf`, `fprintf`, `puts`, `fputs`, `fwrite`, `stdout`, or `stderr` anywhere in `src/core` or `src/renderer` outside `log_console.c` — including inside comments.** LOG-INV-9's check (TASK-009) is a textual grep; a doc comment in `logging.h` saying "writes to stderr" fails it. Phrase such comments as "the standard error stream".
15. **Thread safety is declared, not implied.** Use CORE-ALLOC-021's fixed three-level vocabulary at each definition site: the console sink is `THREAD_SAFE` (CORE-LOG-054), `engine_log_write`/`engine_log_flush` are `THREAD_SAFE` iff every registered sink is (CORE-LOG-071), and the six mutating operations are `EXTERNALLY_SYNCHRONIZED` against all logging (CORE-LOG-072). The logger takes no lock of its own and `min_level` is a plain `int32_t` (CORE-LOG-073).
16. **Naming per CORE-LOG-009** throughout; no engine type name ends in `_t` (CORE-TYPES-023).

## Acceptance
- [ ] `src/core/logging.h` includes exactly the five headers of CORE-LOG-091 and names none of `EngineAllocator`, `malloc`, `<stdio.h>`, `_Atomic` (LOG-INV-10, verified by hand here; automated in TASK-011).
- [ ] No file-scope **mutable** variable in the three new files holds an `EngineLogger`, an `EngineLogSink *`, or an `EngineLogLevel`; the console sink is `static const` (LOG-INV-5, LOG-INV-14, CORE-LOG-001/050).
- [ ] The LOG-INV-1 and LOG-INV-2 `static_assert`s are present and the build passes them.
- [ ] `ENGINE_LOG_ERROR(NULL, "Core", "x")`, `engine_log_write(NULL, …)`, `engine_log_flush(NULL)`, and `engine_log_shutdown(NULL)` each compile and return normally (LOG-INV-4).
- [ ] No allocation function is named in any of the three files; `logging.h` does not include `allocator.h` (CORE-LOG-008).
- [ ] Every `EngineResult`-returning declaration in `logging.h` carries `ENGINE_NODISCARD` (CORE-RESULT-015).
- [ ] `grep -rE '(printf|fprintf|puts|fputs|fwrite|stdout|stderr)' src/core src/renderer` matches `log_console.c` and nothing else (requirement 14).
- [ ] `tools/factory build debug` and `build release` succeed with zero warnings from `engine_foundation`, under both `-DENGINE_LOG_COMPILE_MIN_LEVEL=0` and `=6`.
- [ ] `tools/factory test all` is green — the existing suite is untouched and must not regress.
- [ ] `git diff --stat` touches only the three new files and `src/core/CMakeLists.txt`.

## Tests
- Compile-time only in this task: the LOG-INV-1 and LOG-INV-2 `static_assert`s.
- Runtime coverage — LOG-INV-3/4/6/7/8/11/12/14 — is **TASK-010's**, and the grep and compile-gate invariants LOG-INV-5/9/10/13 are **TASK-009's and TASK-011's**. Do not add test files here; keep any local verification throwaway and uncommitted. Do leave `engine_log_write` structured so an injected time source can be observed being called exactly once (LOG-INV-12).

## Non-Goals
- Editing `src/core/engine.h`, `engine.c`, `src/renderer/api.h`, `renderer.c`, `main.c`, or any test — TASK-012 and TASK-013.
- The `forbid_direct_output` CMake macro — TASK-009.
- Any sink other than the console: file, rotating file, in-memory ring, remote, IDE (`logging.md` §14).
- A monotonic clock or a thread-id implementation. This task provides only the two callback slots (CORE-LOG-040..043); PLATFORM-005 and PLATFORM-008 own the implementations.
- An atomic `min_level` (CORE-LOG-073), a recursion guard (CORE-LOG-038), per-subsystem filtering, structured logging, or level configuration from the environment (`logging.md` §14).
- `ENGINE_ASSERT` / `ENGINE_VERIFY` (CORE-003). `FATAL` records a severity and does not terminate (CORE-LOG-013).
- Reshaping `engine_init`'s parameter list into an `EngineInitDesc` (CORE-LOG-084's note; `logging.md` §14).

## Review
Required: yes
Reviewer: `reviewer`

Reviewer note: LOG-INV-15 (no sink retains `record` or `record->message` past its `write` call) is review-enforced and not greppable — check `log_console.c` against CORE-LOG-022 explicitly.
