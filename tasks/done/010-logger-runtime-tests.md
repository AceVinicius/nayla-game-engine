# TASK-010: Cover the logger's runtime contract with a capture sink

## Agent
`test-engineer`

## Context

### Required
- `docs/spec/core/logging.md` — §11 (LOG-INV-3, -4, -6, -7, -8, -11, -12, -14); CORE-LOG-036 and -037 for the exact code sets; CORE-LOG-055 for what must not be asserted on
- `.claude/rules/testing.md`
- `.claude/rules/tooling.md`
- `src/core/logging.h` (from TASK-008 — the surface under test)
- `tests/unit/CMakeLists.txt`, `tests/contract/CMakeLists.txt` (the `add_test(NAME <layer>/<name> …)` convention to follow)

### Optional
- `tests/unit/test_allocator_unit.c`, `tests/contract/test_allocator_contracts.c` — the existing style, and the `user_data`-carrying test-double pattern to copy
- `docs/spec/core/logging.md` §4 and §5.1 for the record and sink layouts

## Dependencies
- TASK-008 (`logging.h`, `logging.c`, and `log_console.c` must exist and compile — there is no surface to test before that)

## Objective

The logger's observable behaviour — level gating, per-sink gating, delivery order, `NULL` tolerance, truncation, context sources, registration contracts, and the console singleton — is covered by unit and contract tests that can actually fail.

## Requirements

1. **Link `engine_foundation` only, never `engine_core`.** Every test in this task exercises the logger standalone. Keeping `engine_core` out of the link line is what makes this task immune to TASK-012's `engine_init` signature change, so the two can land in either order.
2. **Build one reusable capture sink** and reuse it across the cases. It keeps all of its state in `user_data` (CORE-LOG-007, CORE-LOG-030), and it **copies `record->message` before returning** (CORE-LOG-022) — a capture sink that stores the pointer is the exact defect LOG-INV-15 exists to catch, and this test must not model it. It MUST NOT be a file-scope mutable `EngineLogSink`, which would reintroduce the pattern LOG-INV-5 forbids.
3. **LOG-INV-3** — `engine_log_level_name` returns non-`NULL` for all seven enumerators including `OFF`, and for at least one out-of-range value. The direct analogue of RESULT-INV-7 in `tests/unit/test_result_unit.c`.
4. **LOG-INV-4** — `ENGINE_LOG_ERROR(NULL, "Core", "x")`, `engine_log_write(NULL, …)`, `engine_log_flush(NULL)`, and `engine_log_shutdown(NULL)` all return normally and produce no output (CORE-LOG-005). Assert the "no output" half by registering nothing and observing no crash, not by capturing `stderr`.
5. **LOG-INV-6** — a logger at `WARN` delivers `WARN`/`ERROR`/`FATAL` and delivers no `TRACE`/`DEBUG`/`INFO`; a logger at `OFF` delivers nothing (CORE-LOG-010/011/016). Build the test so it is not silently vacuous under a compile-time gate: assert the `ENGINE_LOG_COMPILE_MIN_LEVEL` in effect is `0` for this translation unit, or call `engine_log_write` directly, so a release-default gate cannot turn a pass into a no-op.
6. **LOG-INV-7** — two sinks at `TRACE` and `ERROR`: one `WARN` reaches the first only; one `ERROR` reaches both, and the capture order proves registration order (CORE-LOG-017, -035).
7. **LOG-INV-8 as a contract test** — `engine_log_add_sink` returns `ENGINE_RESULT_INVALID_ARGUMENT` for a `NULL` sink and for a sink whose `write` is `NULL`; `ENGINE_RESULT_INVALID_STATE` on the `ENGINE_LOG_MAX_SINKS + 1`-th distinct sink and on a duplicate pointer; `engine_log_remove_sink` returns `ENGINE_RESULT_INVALID_STATE` for an unregistered pointer. Assert the exact codes, not merely "not success" (RESULT-INV-8's standard). Cover CORE-LOG-036's out-of-range `min_level` rejection too. Include the two-structurally-identical-sinks case from §12 edge case 7: both receive, and `remove_sink` removes exactly the one whose pointer was passed (CORE-LOG-033).
8. **LOG-INV-11** — a message longer than `ENGINE_LOG_MESSAGE_MAX` is still delivered, is NUL-terminated, has `message_length < ENGINE_LOG_MESSAGE_MAX`, and carries `ENGINE_LOG_RECORD_TRUNCATED` (CORE-LOG-025). Truncation is never an error and never suppresses the record — assert delivery, not just the flag.
9. **LOG-INV-12** — with no context source installed, records carry `ENGINE_LOG_TIMESTAMP_UNKNOWN` and `ENGINE_LOG_THREAD_UNKNOWN` (CORE-LOG-040). With a deterministic injected time source, two sinks receiving one record observe **identical** `timestamp_ns`, and the source is called **exactly once** — count the calls in the source's own `user_data`. This is the assertion that pins CORE-LOG-018's "steps 3 and 4 happen once per record, not once per sink"; a per-sink implementation must fail it.
10. **LOG-INV-14** — `engine_console_log_sink()` never returns `NULL`, returns the identical pointer across repeated calls, and its `write` is non-`NULL` while its `user_data` is `NULL` (CORE-LOG-050/051). The analogue of ALLOC-INV-3.
11. **Do not assert on the console sink's rendered output** (CORE-LOG-055). The format is `log_console.c`'s implementation detail; every content assertion in this task goes through the capture sink and inspects the structured `EngineLogRecord`.
12. **Every new test is wired into CTest** through its directory's `CMakeLists.txt` using the existing `add_test(NAME <layer>/<name> COMMAND …)` convention.
13. **Do not weaken a failing assertion.** A failure here means TASK-008 is incomplete; report it rather than relaxing the check (`.claude/rules/testing.md`).

## Acceptance
- [ ] `tools/factory test unit` and `tools/factory test contract` pass and each runs at least one new assertion.
- [ ] Deliberately breaking one assertion in each new test file makes exactly that test fail — no silent passes. Verify, then revert.
- [ ] Every new test target links `engine_foundation` and not `engine_core` (requirement 1).
- [ ] LOG-INV-12's call-count assertion fails if the time source is moved inside the per-sink loop. Verify by hand against `logging.c`, then revert.
- [ ] No file-scope mutable `EngineLogSink`, `EngineLogger`, or `EngineLogLevel` appears in any new test file (LOG-INV-5).
- [ ] `tools/factory build debug` and `build release` succeed with zero warnings, including no `warn_unused_result` warnings from the `ENGINE_NODISCARD` entry points.
- [ ] `tools/factory test all` is green.

## Tests
This task *is* the test work. New sources under `tests/unit/` (LOG-INV-3, -4, -6, -7, -11, -12, -14) and `tests/contract/` (LOG-INV-8), each wired into CTest per requirement 12.

## Non-Goals
- LOG-INV-1 and LOG-INV-2 — `static_assert`s delivered by TASK-008.
- LOG-INV-5, LOG-INV-9, LOG-INV-10 (greps) and LOG-INV-13 (the compile-gate integration test) — TASK-009 and TASK-011.
- LOG-INV-15 — review-enforced and explicitly not greppable.
- Any concurrency test. CORE-LOG-071 states the logger takes no lock and is `THREAD_SAFE` only if its sinks are; a threading test would be testing the sinks, and the foundation leaf has no portable threading primitive to write one with (CORE-LOG-073's rationale).
- Editing any file under `src/` — including to make an assertion pass.
- Testing `engine_init`, `renderer_init`, or `main.c`'s logger threading — TASK-012 and TASK-013.
- Adopting a test framework or restructuring the existing tests.

## Review
Required: yes
Reviewer: `reviewer`
