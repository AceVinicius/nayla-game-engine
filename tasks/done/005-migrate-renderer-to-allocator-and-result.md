# TASK-005: Migrate the renderer to `EngineResult` and an explicit `EngineAllocator`

## Agent
`renderer-engineer`

## Context

### Required
- `docs/spec/core/allocator.md` — §7 (CORE-ALLOC-060..070), §8 (CORE-ALLOC-082)
- `.claude/rules/renderer.md`
- `src/renderer/api.h`, `src/renderer/renderer.c` (the code being migrated)
- `src/renderer/CMakeLists.txt` (already links `engine_foundation` after TASK-002)
- `src/core/allocator.h`, `src/core/result.h` (from TASK-003 — the contract being adopted)

### Optional
- `docs/spec/core/result.md` — CORE-RESULT-001 and RESULT-INV-4/5 only
- `src/core/engine.c` after TASK-004, as a worked example of the same migration

## Dependencies
- TASK-003 (hard — `allocator.h` and `result.h` must exist; the renderer links `engine_foundation`, never `engine_core`)
- TASK-004 (soft, ordering only). `allocator.md` §12 sequences #4 after #3, but the two touch disjoint files — `src/core/engine.*` + `main.c` + `tests/` versus `src/renderer/*` — and the renderer links no core target, so this task is parallel-safe with TASK-004. Run it after TASK-004 only if a single linear merge order is preferred.

## Objective

`renderer_init` takes and stores a borrowed `const EngineAllocator *`, both renderer entry points return `EngineResult`, and no direct `malloc`/`free` remains in `src/renderer`.

## Requirements

1. **`api.h`** — `struct renderer` becomes `typedef struct Renderer Renderer;` (CORE-TYPES-021/022). `renderer_init` gains `const EngineAllocator *allocator`. Both entry points return `EngineResult`, not `int32_t`, each with `ENGINE_NODISCARD`.
2. **One result type, engine-wide.** `types.md` §9's table leaves `RendererResult` open; `result.md` CORE-RESULT-001 closes it — there is exactly one result type and a `RendererResult` is prohibited (RESULT-INV-5). Use `EngineResult`.
3. **Include the foundation headers by their repo-root-relative path** (`"src/core/result.h"`, `"src/core/allocator.h"`), matching the include directory `engine_foundation` exports. This is a permitted dependency direction: CORE-ALLOC-082 lets `engine_renderer` link `engine_foundation` directly, and forbids it from linking `engine_core`.
4. **`renderer.c`** — `struct Renderer` gains a stored `const EngineAllocator *allocator`, set once at construction and never reassigned (CORE-ALLOC-067). `malloc` at line 13 and `free` at line 27 are replaced by `engine_alloc(allocator, sizeof(Renderer), _Alignof(Renderer))` and `engine_free` on the stored pointer. `<stdlib.h>` must no longer be included (CORE-ALLOC-042).
5. **`renderer_init` resolves and validates the allocator once** — `NULL` becomes `engine_default_allocator()` (CORE-ALLOC-068), then `alloc`/`free` are checked non-`NULL` with `ENGINE_RESULT_INVALID_ARGUMENT` on failure (CORE-ALLOC-070). The resolved, non-`NULL` pointer is stored.
6. **Return codes**, replacing the four sites: `-1` at lines 10 and 25 → `ENGINE_RESULT_INVALID_ARGUMENT`; `1` at line 15 → `ENGINE_RESULT_OUT_OF_MEMORY` (CORE-RESULT-019); `0` at lines 19 and 28 → `ENGINE_RESULT_SUCCESS`. Argument validation precedes state inspection (CORE-RESULT-012), and a failing `renderer_init` releases everything it acquired and leaves the out-parameter unusable-by-contract (CORE-RESULT-013).
7. **`src/renderer/CMakeLists.txt` should need no edit.** TASK-002 already added the `engine_foundation` link. If a link error appears, fix it there rather than adding `engine_core` — that link is exactly what ALLOC-INV-11 forbids.
8. **No GPU SDK types, headers, or `#ifdef VULKAN`/`D3D12`/`METAL` branching** is introduced (`rules/renderer.md`, CLAUDE.md invariant #2). This migration is allocator and result only.

## Acceptance
- [ ] No `malloc`, `free`, `calloc`, `realloc`, or `aligned_alloc` in `src/renderer` (ALLOC-INV-7).
- [ ] `src/renderer/renderer.c` does not include `<stdlib.h>`.
- [ ] `src/renderer/api.h` declares no function returning a bare `int32_t` as a status (RESULT-INV-4) and defines no `RendererResult` (RESULT-INV-5).
- [ ] No `return -1;` or bare `return 1;` remains in `src/renderer` (RESULT-INV-6).
- [ ] No file-scope mutable variable in `src/renderer` holds an allocator (ALLOC-INV-10).
- [ ] `src/renderer/CMakeLists.txt` contains no reference to `engine_core` (ALLOC-INV-11).
- [ ] `tools/factory build debug` and `build release` succeed with zero warnings from `engine_renderer`.
- [ ] `tools/factory test all` is green.

## Tests
- No test currently links `engine_renderer` — the library is built but never exercised. That gap is real and is recorded here rather than fixed silently; closing it is out of scope for this task.
- Verification for this task is the build plus the grep-shaped acceptance items above; their automated form lands in TASK-006.

## Non-Goals
- Adding renderer functionality: no frame graph, no draw submission, no GPU API surface.
- Defining anything in `src/gpu/api.h` — it is still a placeholder and stays one.
- Adding a renderer test target or linking `engine_renderer` into an executable.
- Touching `src/core/*`, `main.c`, or any existing test source.
- The `forbid_direct_allocation` CMake macro and the architecture checks — TASK-006.

## Review
Required: yes
Reviewer: `reviewer`
