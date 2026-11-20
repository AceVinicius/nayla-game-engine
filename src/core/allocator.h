#ifndef ENGINE_ALLOCATOR_H
#define ENGINE_ALLOCATOR_H

#include <stddef.h>   /* size_t, max_align_t */

/* Allocator interface for engine heap memory allocation.
 * See docs/spec/core/allocator.md for the contract.
 */

typedef struct EngineAllocator EngineAllocator;

/* Allocation callback: returns a block of at least size bytes aligned to
 * at least alignment bytes, or NULL on failure.
 * See CORE-ALLOC-010..021.
 */
typedef void *(*EngineAllocatorAllocFn)(const EngineAllocator *allocator,
                                        size_t                 size,
                                        size_t                 alignment);

/* Deallocation callback: releases a block previously returned by the SAME
 * allocator's alloc. memory == NULL is a no-op.
 * See CORE-ALLOC-030..036.
 */
typedef void (*EngineAllocatorFreeFn)(const EngineAllocator *allocator,
                                      void                  *memory);

/* EngineAllocator struct: defined layout, not opaque.
 * Callers construct their own instances. Fields are immutable after first use.
 * See CORE-ALLOC-001..007.
 */
struct EngineAllocator {
    EngineAllocatorAllocFn  alloc;      /* required, never NULL */
    EngineAllocatorFreeFn   free;       /* required, never NULL */
    void                   *user_data;  /* implementation-owned, opaque to the engine */
};

/* Default alignment: the alignment malloc guarantees.
 * See CORE-ALLOC-013.
 */
#define ENGINE_ALLOCATOR_DEFAULT_ALIGNMENT ((size_t)_Alignof(max_align_t))

/* Maximum alignment the allocator will honour.
 * Requests for alignment above this will fail with NULL.
 * See CORE-ALLOC-014.
 */
#define ENGINE_ALLOCATOR_MAX_ALIGNMENT     ((size_t)4096)

/* _Static_assert, not static_assert: the latter is a C23 keyword but a C17
 * <assert.h> macro, and ALLOC-INV-8 limits this header to <stddef.h>. */

/* ALLOC-INV-1: alignment of EngineAllocator must be reasonable */
_Static_assert(_Alignof(EngineAllocator) <= _Alignof(max_align_t),
               "EngineAllocator alignment must not exceed max_align_t");

/* ALLOC-INV-2: the constants are powers of two */
_Static_assert((ENGINE_ALLOCATOR_DEFAULT_ALIGNMENT & (ENGINE_ALLOCATOR_DEFAULT_ALIGNMENT - 1)) == 0,
               "DEFAULT_ALIGNMENT must be a power of two");
_Static_assert((ENGINE_ALLOCATOR_MAX_ALIGNMENT & (ENGINE_ALLOCATOR_MAX_ALIGNMENT - 1)) == 0,
               "MAX_ALIGNMENT must be a power of two");
_Static_assert(ENGINE_ALLOCATOR_DEFAULT_ALIGNMENT <= ENGINE_ALLOCATOR_MAX_ALIGNMENT,
               "DEFAULT_ALIGNMENT must not exceed MAX_ALIGNMENT");

/* Get the default heap allocator singleton.
 * Returns a pointer to a static const EngineAllocator that wraps malloc/free.
 * Never returns NULL, is callable at any time from any thread.
 * See CORE-ALLOC-040..043.
 */
const EngineAllocator *engine_default_allocator(void);

/* Allocate memory through an allocator.
 * Returns NULL if allocator == NULL, size/alignment are invalid,
 * or the allocator runs out of memory.
 * See CORE-ALLOC-090..093.
 */
void *engine_alloc(const EngineAllocator *allocator, size_t size, size_t alignment);

/* Free memory previously allocated through an allocator.
 * Is a no-op if memory == NULL or allocator == NULL (leaking in the latter case).
 * See CORE-ALLOC-031, CORE-ALLOC-092..093.
 */
void engine_free(const EngineAllocator *allocator, void *memory);

#endif /* ENGINE_ALLOCATOR_H */
