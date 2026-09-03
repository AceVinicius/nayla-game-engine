#ifndef PLATFORM_API_H
#define PLATFORM_API_H

/* Abstract Platform API. To be implemented by OS backends (Windows/Mac/Linux).
 * This is the contract between core/renderer and OS-specific code.
 * No OS headers should be included here.
 */

/* -------------------------------------------------------------------------
 * NORMATIVE: host-memory allocation convention.
 * TASK-007, discharging ADR-009 follow-up #7 for the platform half.
 * Contract: docs/spec/core/allocator.md, CORE-ALLOC-060..070.
 *
 * Every Platform API entry point that allocates host memory -- every
 * create/init function that returns or initializes an engine-owned object --
 * takes
 *
 *     const EngineAllocator *allocator
 *
 * as a construction parameter, identically on every OS backend. There is no
 * per-OS variation and no "change an object's allocator" operation.
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
 * OS-OWNED ALLOCATIONS ARE OUTSIDE THE ALLOCATOR'S DOMAIN -- the boundary.
 * Memory and objects the OS creates (window and event-loop handles,
 * dynamically loaded module images, buffers the OS allocates and expects to
 * reclaim through its own call) are released by their OS counterpart call and
 * NEVER by engine_free. Symmetrically, a block obtained from an
 * EngineAllocator is never handed to an OS release call. This is NOT an
 * exception to CORE-ALLOC-030's same-instance rule: such pointers were never
 * returned by any EngineAllocator's alloc, so they lie outside its domain
 * entirely, and passing one to engine_free would be the foreign-pointer
 * undefined behaviour of CORE-ALLOC-032.
 *
 * The resulting shape is the same on Windows, macOS and Linux:
 *   - the engine-visible struct is allocated with engine_alloc from the
 *     allocator stored at construction (CORE-ALLOC-067);
 *   - it holds OS handles as opaque, backend-private members that never cross
 *     this boundary except through an explicit interface (rules/platform.md);
 *   - destroy releases the OS handles by their OS calls FIRST, then releases
 *     the struct through the same stored allocator.
 * Two release disciplines, nested, each symmetric with its own acquisition.
 * ADR-009's alloc/release symmetry holds at the engine level regardless of
 * what the OS does underneath.
 *
 * A backend whose OS may invoke callbacks on threads other than the
 * constructing one MUST state, at its own entry point, the thread-safety level
 * it requires of the caller's allocator (CORE-ALLOC-021: THREAD_SAFE,
 * EXTERNALLY_SYNCHRONIZED, or THREAD_AFFINE). That is a documented backend
 * precondition, not a change to this contract.
 *
 * This block records a decision only: it declares no types and no functions.
 * ------------------------------------------------------------------------- */

/* Placeholder: API contract to be defined. */

#endif /* PLATFORM_API_H */
