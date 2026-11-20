# TASK-013: Thread the logger through `renderer_init`

## Agent
`renderer-engineer`

## Context

### Required
- `docs/spec/core/logging.md` — CORE-LOG-003, -004, -005, -084; CORE-LOG-090 (why the logger is reachable from the renderer at all); §13.2's `src/renderer/api.h`, `renderer.c` row
- `.claude/rules/renderer.md`
- `src/renderer/api.h`, `src/renderer/renderer.c` (the code being migrated)
- `src/core/logging.h` (from TASK-008 — the contract being adopted)

### Optional
- `tasks/done/005-migrate-renderer-to-allocator-and-result.md` — the same migration one parameter earlier
- `src/core/engine.c` after TASK-012, as the worked example of the identical change in core

## Dependencies
- TASK-008 (`logging.h` must exist and be reachable from `engine_renderer`)

Independent of TASK-012: `renderer_init` has **no call site anywhere outside `src/renderer`** — the root executable links `engine_core` only, and no test calls it. Verified against the tree, not assumed. The two tasks share no file and can run concurrently.

## Objective

`renderer_init` takes and stores a borrowed `const EngineLogger *` and logs at its three failure points, matching core's convention exactly.

## Requirements

1. **This is TASK-012's change, applied to two files.** `renderer_init` gains a third parameter `const EngineLogger *logger` after the allocator (CORE-LOG-084); `struct Renderer` (`renderer.c:3-6`) gains `const EngineLogger *logger;`, set once beside `renderer->allocator = resolved;` at `renderer.c:31` and never reassigned (CORE-LOG-003). Deliberately mirror core's wording and structure — a divergence here is a second convention, which is what CORE-LOG-084 exists to prevent.
2. **No CMake change is needed, and that is the architectural point.** `engine_renderer` already links `engine_foundation` PUBLIC, and CORE-LOG-090 puts logging in `engine_foundation` precisely so the renderer can log without linking `engine_core` (CORE-ALLOC-082, ALLOC-INV-11). If reaching `logging.h` appears to require `engine_core`, stop — that is an architecture problem, not a build fix. Do not add a link.
3. **Document `NULL` as *discard*, and contrast it with the allocator's `NULL`.** `api.h`'s existing comment documents `allocator == NULL` as "use `engine_default_allocator()`"; the logger's `NULL` discards and MUST NOT resolve to any default (CORE-LOG-005). The opposite semantics sit on adjacent parameters — say so. Document CORE-LOG-004's outlives-everything obligation.
4. **Store as received: no validation, no resolution, no `resolved` local for the logger** (CORE-LOG-084). `NULL` is legal; a non-`NULL` logger's validity is the caller's obligation.
5. **`renderer_init`'s three failure returns each gain one log line** — `renderer.c:10` (`!owned_renderer`), `renderer.c:21` (a callback is `NULL`), `renderer.c:28` (allocation returned `NULL`). Use `ENGINE_LOG_ERROR` with the subsystem literal `"Renderer"` (CORE-LOG-021). Log arguments are pure expressions (CORE-LOG-062).
6. **No backend-native type and no GPU SDK branching enters the renderer** (`.claude/rules/renderer.md`). `EngineLogger` is an engine-defined type from the foundation leaf; nothing in this task touches the GPU API, and `#ifdef VULKAN`-style branching would fail `forbid_gpu_branching` regardless.
7. **Do not name a stream function in `renderer.c` or `api.h`, including in a comment** — LOG-INV-9's grep (TASK-009) covers all of `src/renderer` with no exemption there.
8. **The diff is exactly two files.** No test, no `CMakeLists.txt`, no `main.c`. If a third file needs editing, a call site was missed — report it.

## Acceptance
- [ ] `renderer_init(Renderer **, const EngineAllocator *, const EngineLogger *)` is the declared signature, with `ENGINE_NODISCARD` retained.
- [ ] `renderer_init(&r, NULL, NULL)` succeeds and produces no output (CORE-LOG-005, LOG-INV-4).
- [ ] `struct Renderer`'s stored logger is assigned exactly once and never reassigned (CORE-LOG-003).
- [ ] `src/renderer/CMakeLists.txt` is unmodified and `engine_renderer` still does not link `engine_core` — `forbid_transitive_link` still passes (ALLOC-INV-11).
- [ ] `grep -rE '(printf|fprintf|puts|fputs|fwrite|stdout|stderr)' src/renderer` returns nothing (LOG-INV-9).
- [ ] No file-scope mutable logger, sink, or level in `src/renderer` (LOG-INV-5, CORE-LOG-001).
- [ ] `tools/factory build debug` and `build release` succeed with zero warnings.
- [ ] `tools/factory test all` is green.
- [ ] `git diff --stat` touches only `src/renderer/api.h` and `src/renderer/renderer.c`.

## Tests
- No new test files. `renderer_init` has no test coverage today and this task does not add any — the renderer's own contract tests are not in scope for the logging spec, and adding them here would mix two reviews.
- Verified by the existing suite staying green plus the acceptance items above.

## Non-Goals
- Any core change (`src/core/*`), `main.c`, or any test — TASK-012 owns those.
- Adding a renderer test suite, or wiring `engine_renderer` into the root executable.
- Adding renderer functionality of any kind: no GPU API calls, no resources, no frame graph.
- Installing a time source or thread-id source (CORE-LOG-041, `logging.md` §14).
- Adding a renderer-specific logging macro family — CORE-LOG-068 forbids `RENDERER_LOG_*`; the subsystem is a runtime string argument.
- Adding any sink, or a per-subsystem filter for `"Renderer"` (`logging.md` §14 — implementable as a caller-written filtering sink, not a contract change).

## Review
Required: yes
Reviewer: `reviewer`
