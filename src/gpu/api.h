#ifndef GPU_API_H
#define GPU_API_H

/* Abstract GPU API. To be implemented by graphics backends (Vulkan/D3D12/Metal).
 * This is the contract between renderer and GPU-specific code.
 * Must be implementable equally by all three backends.
 * No SDK types should be exposed here.
 */

/* -------------------------------------------------------------------------
 * NORMATIVE: host-memory allocation convention.
 * TASK-007, discharging ADR-009 follow-up #7 for the GPU half.
 * Contract: docs/spec/core/allocator.md, CORE-ALLOC-060..070.
 *
 * Every GPU API entry point that allocates host memory -- every create/init
 * function that returns or initializes an engine-owned object -- takes
 *
 *     const EngineAllocator *allocator
 *
 * as a construction parameter. There is no per-backend and no per-subsystem
 * variation on this, and there is no "change an object's allocator" operation.
 *
 *   CORE-ALLOC-067  The allocator is fixed at construction and stored in the
 *                   object's backend-private struct. Every allocation and
 *                   every release that object performs goes through that one
 *                   stored pointer, which is never reassigned.
 *   CORE-ALLOC-068  An entry point MAY document that allocator == NULL means
 *                   "use engine_default_allocator()". The substitution happens
 *                   once, at construction; NULL is never stored, and no
 *                   allocator is ever resolved lazily or per allocation.
 *   CORE-ALLOC-070  After resolving NULL, the entry point MUST verify that
 *                   both callbacks are non-NULL and return
 *                   ENGINE_RESULT_INVALID_ARGUMENT if either is not. That is
 *                   the one place the check is paid for.
 *   CORE-ALLOC-061..065
 *                   The reference is borrowed: never owned, never copied,
 *                   never mutated, never released by the engine, and it must
 *                   outlive every object constructed from it.
 *
 * HOST MEMORY ONLY -- the boundary.
 * This convention governs HOST (CPU) memory only: the backend's own structs,
 * handle tables, command-recording bookkeeping, and the host side of any
 * staging allocation. DEVICE memory is the GPU API's own subject and is
 * explicitly out of scope for EngineAllocator (allocator.md section 13).
 * Device heaps and device allocation sizes are uint64_t under CORE-TYPES-018,
 * not size_t, and are governed by ADR-008. Do not read this block as covering
 * device allocation.
 *
 * Findings recorded by TASK-007 (all confirmed; no architecture gap found):
 *   - The convention is satisfiable identically by all three backends because
 *     it constrains only the ENGINE's own host allocations, never an SDK's
 *     internal ones.
 *   - A backend MAY forward the stored allocator to an SDK host-allocator hook
 *     where its SDK offers one (only one of the three does). Doing so is a
 *     backend-private implementation detail: it changes nothing observable at
 *     this API, so it is NOT an optional feature and MUST NOT acquire a
 *     capability query. A backend whose SDK has no such hook is not degraded.
 *   - An SDK hook that requires a reallocation callback is served by
 *     allocate-copy-release through the stored allocator, with the shim
 *     recording each block's size and alignment in its own block header or in
 *     user_data (CORE-ALLOC-036 already places that burden on the
 *     implementation). EngineAllocator gains no realloc (allocator.md
 *     section 13); adding one would be a spec amendment, never a backend
 *     decision.
 *   - An SDK allocation-scope / lifetime hint has no EngineAllocator field and
 *     is discarded at this boundary. A backend that wants it keeps it in its
 *     own per-block record or behind user_data (CORE-ALLOC-004). It MUST NOT
 *     be lifted into the engine interface: a field shaped by one SDK is the
 *     "renamed Vulkan/D3D12" that rules/gpu.md forbids.
 *   - SDK objects with their own runtime lifetime (COM reference counting,
 *     Objective-C retain/release) are never allocated by, and never released
 *     through, an EngineAllocator. They are backend detail under CLAUDE.md
 *     invariant #4: destroy releases the SDK objects by their own SDK calls
 *     first, then releases the engine-allocated struct through the stored
 *     allocator. Those pointers never came from an EngineAllocator's alloc, so
 *     this is outside CORE-ALLOC-030's domain, not an exception to it.
 *   - A backend whose SDK may invoke allocation callbacks from several threads
 *     MUST state at its own entry point that it requires a THREAD_SAFE
 *     allocator (CORE-ALLOC-021), or wrap the caller's allocator itself. That
 *     is a documented backend precondition, not a change to this contract.
 *
 * This block records a decision only: it declares no types and no functions.
 * ------------------------------------------------------------------------- */

/* Placeholder: API contract to be defined. */

#endif /* GPU_API_H */
