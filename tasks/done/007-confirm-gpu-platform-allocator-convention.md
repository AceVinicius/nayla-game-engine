# TASK-007: Confirm the GPU and platform APIs adopt the allocator convention

## Agent
`gpu-architect` (platform half: `platform-engineer`)

## Context

### Required
- `docs/spec/core/allocator.md` — §7 (CORE-ALLOC-060..070, especially -067), §13 ("Out of scope")
- `.claude/rules/gpu.md`
- `.claude/rules/platform.md`
- `src/gpu/api.h` and `src/platform/api.h` (both still placeholders — nothing has been designed into them yet)
- `src/core/allocator.h` (from TASK-003 — the concrete convention being adopted)

### Optional
- `docs/adr/ADR-009-allocator-ownership.md` — "Decision" §2 and §3, and follow-up #7
- `src/renderer/renderer.c` after TASK-005, as the worked example of a subsystem adopting the convention

## Dependencies
- TASK-003 (the header being confirmed against must exist). The analysis could be done from `allocator.md` alone if this task needs to start earlier; the header makes the confirmation concrete rather than hypothetical.

## Objective

A recorded, defensible answer to whether `const EngineAllocator *` as a construction parameter works unchanged for the GPU API (Vulkan, D3D12, Metal) and the platform API (Windows, macOS, Linux) — before either has an allocating entry point to relitigate it against.

## Requirements

1. **The default answer is already written.** `allocator.md` §12 states follow-up #7's expected outcome: CORE-ALLOC-067's `const EngineAllocator *` construction parameter, with no per-subsystem variation. This task exists to try to break that answer against three graphics SDKs and three OSes, not to redesign it. Confirming it is a valid and expected result.
2. **Scope the question to host memory.** Device memory is the GPU API's own subject and is explicitly out of scope for `EngineAllocator` (`allocator.md` §13); device heap sizes are `uint64_t` under CORE-TYPES-018, not `size_t`. State the boundary explicitly so a future reader does not read this confirmation as covering device allocation.
3. **Test the convention against the specific things most likely to break it**, and record the finding for each rather than a blanket "it fits":
   - Vulkan's `VkAllocationCallbacks` requires a **reallocation** callback; `EngineAllocator` deliberately has none (`allocator.md` §13). Determine whether a Vulkan backend can satisfy it by allocate-copy-free through `EngineAllocator`, or whether this is a genuine gap.
   - `VkAllocationCallbacks` also carries an allocation **scope** enum that `EngineAllocator` has no field for. Decide whether that is discarded at the boundary or needs representing.
   - D3D12 and Metal expose no host-allocator hook of this shape. Confirm that is a non-problem (the engine's own host allocations still route through `EngineAllocator`) rather than an asymmetry.
   - Metal's Objective-C object allocation is outside any C allocator's reach. Confirm the boundary sits where CLAUDE.md invariant #4 already puts it.
   - Platform backends: confirm that OS-owned allocations (window handles, `dlopen` images, OS-returned buffers freed by an OS call) are released by their OS counterpart and never by `engine_free`, and that this does not violate CORE-ALLOC-030's same-instance rule — it is outside the allocator's domain, not an exception to it.
4. **Record the outcome where the future API authors will read it.** If confirmed, add a normative comment block to `src/gpu/api.h` and `src/platform/api.h` stating that every allocating create/init entry point takes `const EngineAllocator *allocator` as a construction parameter under CORE-ALLOC-067/068/070, and citing `docs/spec/core/allocator.md`. Comments only — no types, no function declarations.
5. **If any item in requirement 3 is a genuine gap**, that is an architecture gap, not something to patch locally: stop, record it, and escalate per CLAUDE.md § Escalation Rules. A gap in the GPU half is `gpu-architect`'s own ADR; a gap in the platform half escalates to `architect`. Do not amend `allocator.md` or ADR-009 from inside this task.
6. **The report is the deliverable.** State, per SDK and per OS family, whether the convention holds, what evidence supports it, and what was discarded at the boundary.

## Acceptance
- [ ] Each of the five checks in requirement 3 has an explicit recorded finding — confirmed, or named as a gap with its escalation target.
- [ ] The host-memory versus device-memory boundary is stated explicitly.
- [ ] If confirmed: `src/gpu/api.h` and `src/platform/api.h` each carry a comment stating the CORE-ALLOC-067 convention and citing the spec. Both files still declare no types and no functions.
- [ ] No change to `docs/spec/core/allocator.md`, `docs/adr/ADR-009-allocator-ownership.md`, or any `src/core` or `src/renderer` file.
- [ ] `tools/factory build debug` and `tools/factory test all` remain green (comment-only edits must be inert).

## Tests
- None. This task produces a decision and two comment blocks; there is no behaviour to test. The existing suite must remain green as evidence the edits are inert.

## Non-Goals
- Designing the GPU API or the platform API. No `GpuDevice`, `GpuBuffer`, `PlatformWindow`, or any other type is introduced (that work is ADR-003 and ADR-004 territory).
- Writing any backend, or adding a GPU/platform SDK dependency to the build.
- Specifying device-memory allocation, a GPU upload allocator, or arena/pool allocators (`allocator.md` §13).
- Adding `realloc` to `EngineAllocator` — if the Vulkan finding demands it, that is a spec amendment to escalate, never an edit to make here.
- Amending ADR-009 or any accepted spec.

## Review
Required: yes
Reviewer: `reviewer`
