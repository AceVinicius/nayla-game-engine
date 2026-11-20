# TASK-009: Forbid direct stream output outside `log_console.c`

## Agent
`build-engineer`

## Context

### Required
- `docs/spec/core/logging.md` — CORE-LOG-052 and CORE-LOG-092; LOG-INV-9; §13.1 (why this passes today with zero source edits). Nothing else.
- `.claude/rules/tooling.md`
- `.claude/rules/testing.md`
- `cmake/Common.cmake` — `forbid_direct_allocation`, the macro this one is modelled on
- `cmake/forbid-pattern.sh` — already accepts the optional 5th "exempt filename" argument (added by TASK-006); **do not modify it**
- `src/core/CMakeLists.txt`, `src/renderer/CMakeLists.txt` (the three targets gaining the check)
- `tests/architecture/test_core_invariants.c` — the runtime mirror; see its existing `forbidden_pattern_found` helper and the `snprintf`/`system` shape at line 128 for an exempted grep

### Optional
- `tasks/done/006-architecture-test-forbid-direct-allocation.md` — the precedent this task follows

## Dependencies

None. `logging.md` §13.1 is verified correct against the tree: `src/core` and `src/renderer` name no stream function today, so this check passes on an unmodified tree and can land before TASK-008. It is deliberately ordered **first or in parallel** — closing the door before anyone walks through it is the cheap moment.

## Objective

`printf`, `fprintf`, `puts`, `fputs`, `fwrite`, `stdout`, or `stderr` anywhere under `src/core` or `src/renderer` other than `src/core/log_console.c` fails the build, and fails again independently at test time.

## Requirements

1. **Extend the existing mechanism; do not invent a second one.** Add `forbid_direct_output(target_name source_dir [exempt_filename])` to `cmake/Common.cmake` directly beside `forbid_direct_allocation`, in the same two-branch shape (exempt / no-exempt) and delegating to `${NAYLA_FORBID_PATTERN_SCRIPT}`. CORE-LOG-092 states the intended shape explicitly: one pattern, one exempt filename — which `forbid-pattern.sh` already supports.
2. **Seven patterns, one per `COMMAND`**, each with its own description string so a failure names which one fired: `printf`, `fprintf`, `puts`, `fputs`, `fwrite`, `stdout`, `stderr` (LOG-INV-9). Use the word-boundary form the allocation patterns already use (`\bfprintf\s*(` for the calls); `stdout` and `stderr` are identifiers rather than calls, so they need `\bstdout\b` / `\bstderr\b`. Note `\bprintf\s*(` also matches the tail of `fprintf(` under `grep -E`'s word boundary rules — verify the two patterns do not double-report, and if they do, keep both and accept the duplicate rather than weakening either.
3. **Apply to three targets** in the two existing `CMakeLists.txt` files, mirroring exactly how `forbid_direct_allocation` is already applied: `engine_foundation` and `engine_core` both pass `src/core` and both exempt `"log_console.c"` (their greps overlap, as they already do for `allocator.c` — expected, not a bug to optimize away, per CORE-ALLOC-083); `engine_renderer` passes `src/renderer` with **no** exemption.
4. **The exemption is safe to add before the file exists.** If this task lands before TASK-008, `src/core/log_console.c` is absent; `forbid-pattern.sh`'s `grep -v "/log_console.c:"` filter is then simply a no-op over a result set that is already empty. Do not gate this task on TASK-008 and do not create a placeholder file.
5. **Mirror the check at runtime**, per TASK-006 requirement 3's precedent and its stated reason: a regression in the build-time check (quoting, generator escaping, a dropped `COMMAND`) must not silently untest the invariant. Add LOG-INV-9 to `tests/architecture/test_core_invariants.c` as its own named check with its own failure message, covering both `NAYLA_CORE_SRC_DIR` and `NAYLA_RENDERER_SRC_DIR` (both compile definitions already exist) and excluding `log_console.c` the way line 128's check already excludes `const`.
6. **Do not weaken a failing check** (`.claude/rules/testing.md`). If a conforming file trips a pattern — most likely a doc comment that names `stderr` in prose — the fix is to correct the comment or tighten the pattern, never to narrow the scope or drop a pattern. TASK-008 requirement 14 carries the matching obligation on the source side.
7. **No source file under `src/` changes in this task.** If one has to, the check has found a real violation; report it rather than editing around it.

## Acceptance
- [ ] `forbid_direct_output` exists in `cmake/Common.cmake` and is applied to `engine_foundation`, `engine_core`, and `engine_renderer`.
- [ ] `cmake/forbid-pattern.sh` is unmodified, and every existing `forbid_os_gpu_includes` / `forbid_direct_allocation` caller still works.
- [ ] Adding `fprintf(stderr, "x\n");` to `src/core/engine.c` fails the build; adding the same line to `src/renderer/renderer.c` fails the build; adding it to a file named `src/core/log_console.c` does not. Verify all three by hand, then revert.
- [ ] Each of the seven patterns produces a distinct, greppable failure message.
- [ ] The runtime LOG-INV-9 check in `tests/architecture/test_core_invariants.c` fails when the build-time check is commented out and a violation is introduced — verify, then revert.
- [ ] `tools/factory build debug`, `build release`, and `test all` are green on the unmodified tree (§13.1: this creates no retroactive violation).
- [ ] `git diff --stat` touches only `cmake/Common.cmake`, `src/core/CMakeLists.txt`, `src/renderer/CMakeLists.txt`, and `tests/architecture/test_core_invariants.c`.

## Tests
- The runtime LOG-INV-9 check in requirement 5 is this task's only new assertion; it extends the existing architecture test rather than adding a file.
- LOG-INV-5 and LOG-INV-10's greps are **TASK-011's** — they need `logging.h` to exist before they mean anything, which is why they are not in this task despite being greps. LOG-INV-13 is also TASK-011's.

## Non-Goals
- Writing `logging.h`, `logging.c`, or `log_console.c` — TASK-008.
- Adding `src/core/log_console.c` as a placeholder to make the exemption "real" (requirement 4).
- Extending the check to `main.c`, `tests/`, `src/gpu`, or `src/platform`. LOG-INV-9's scope is exactly `src/core` and `src/renderer`; `main.c` is the application and is permanently out of scope (§13.1).
- Any runtime logger test — TASK-010.
- Changing `forbid_os_gpu_includes`, `forbid_gpu_branching`, `forbid_direct_allocation`, or `forbid_transitive_link`.
- Adopting a test framework, adding CI, or restructuring the existing tests.

## Review
Required: yes
Reviewer: `reviewer`
