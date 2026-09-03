#include "allocator.h"

#include <limits.h>
#include <stdlib.h>
#include <stdint.h>

/* TYPE-INV-5: Validate target floating-point and size_t sizes (static assertions).
 * This is the single place in the engine where these are asserted.
 */
static_assert(sizeof(float) == 4, "float must be exactly 4 bytes (IEEE-754 binary32)");
static_assert(sizeof(double) == 8, "double must be exactly 8 bytes (IEEE-754 binary64)");
static_assert(CHAR_BIT == 8, "CHAR_BIT must be 8");
static_assert(sizeof(size_t) == sizeof(void *), "size_t must match pointer size");

/* Default heap allocator: wraps malloc/free with over-alignment support
 * via the offset-header technique (CORE-ALLOC-052).
 * Thread-safety: THREAD_SAFE (inherits malloc/free guarantees per CORE-ALLOC-043).
 */

/* Forward declarations */
static void *default_alloc_impl(const EngineAllocator *allocator,
                                size_t                 size,
                                size_t                 alignment);
static void default_free_impl(const EngineAllocator *allocator, void *memory);

/* The singleton default allocator */
static const EngineAllocator g_default_allocator = {
    .alloc     = default_alloc_impl,
    .free      = default_free_impl,
    .user_data = NULL  /* stateless */
};

const EngineAllocator *engine_default_allocator(void)
{
    return &g_default_allocator;
}

/* Default allocator's alloc implementation.
 * Implements the offset-header algorithm from CORE-ALLOC-052.
 */
static void *default_alloc_impl(const EngineAllocator *allocator,
                                size_t                 size,
                                size_t                 alignment)
{
    (void)allocator;  /* unused: stateless allocator */

    /* Validation: CORE-ALLOC-012/015 */
    if (size == 0) {
        return NULL;  /* CORE-ALLOC-015 */
    }

    if (alignment == 0) {
        return NULL;  /* CORE-ALLOC-012 edge case 2 */
    }

    /* Check if alignment is a power of two */
    if ((alignment & (alignment - 1)) != 0) {
        return NULL;  /* CORE-ALLOC-012 edge case 3 */
    }

    /* Check if alignment is within bounds */
    if (alignment > ENGINE_ALLOCATOR_MAX_ALIGNMENT) {
        return NULL;  /* CORE-ALLOC-012 edge case 4 */
    }

    /* Clamp effective alignment to at least _Alignof(void *) so the header
     * slot below the returned address is itself naturally aligned.
     * CORE-ALLOC-052: "The clamp to _Alignof(void *) is required, not an optimization."
     */
    size_t effective = (alignment < _Alignof(void *)) ? _Alignof(void *) : alignment;

    /* Calculate overhead: space for the header pointer plus padding to reach
     * the effective alignment.
     * CORE-ALLOC-054: check overflow BEFORE the addition.
     */
    size_t overhead = sizeof(void *) + (effective - 1);
    if (size > SIZE_MAX - overhead) {
        return NULL;  /* overflow check before addition */
    }

    /* Allocate raw block */
    void *raw = malloc(size + overhead);
    if (raw == NULL) {
        return NULL;
    }

    /* Compute aligned address and store header below it.
     * base: first byte after the header slot
     * aligned: base rounded up to effective alignment
     * header: pointer at aligned[-1] pointing to raw
     */
    uintptr_t base = (uintptr_t)raw + sizeof(void *);
    uintptr_t aligned = (base + (effective - 1)) & ~(uintptr_t)(effective - 1);

    /* Store the original malloc pointer in the header (the word before aligned) */
    ((void **)aligned)[-1] = raw;

    return (void *)aligned;
}

/* Default allocator's free implementation.
 * Recovers the original malloc pointer from the header and frees it.
 */
static void default_free_impl(const EngineAllocator *allocator, void *memory)
{
    (void)allocator;  /* unused: stateless allocator */

    if (memory == NULL) {
        return;  /* CORE-ALLOC-031 */
    }

    /* Recover the original malloc pointer from the header word below the
     * returned address.
     */
    void *raw = ((void **)memory)[-1];
    (free)(raw);  /* Parentheses protect against free being a macro (CORE-ALLOC-093) */
}

/* engine_alloc: wrapper that centralizes validation.
 * Implements CORE-ALLOC-090..093.
 */
void *engine_alloc(const EngineAllocator *allocator, size_t size, size_t alignment)
{
    if (allocator == NULL) {
        return NULL;  /* CORE-ALLOC-091 */
    }

    /* Validate size and alignment before calling through.
     * CORE-ALLOC-012/015: validation happens here, not in alloc.
     * CORE-ALLOC-016: validation before calling prevents misreporting size/alignment
     * violations as out-of-memory.
     */
    if (size == 0) {
        return NULL;
    }

    if (alignment == 0) {
        return NULL;
    }

    if ((alignment & (alignment - 1)) != 0) {
        return NULL;  /* not a power of two */
    }

    if (alignment > ENGINE_ALLOCATOR_MAX_ALIGNMENT) {
        return NULL;
    }

    /* All arguments valid; call the allocator */
    return (allocator->alloc)(allocator, size, alignment);  /* CORE-ALLOC-093 */
}

/* engine_free: wrapper that handles NULL inputs gracefully.
 * Implements CORE-ALLOC-031 and CORE-ALLOC-092..093.
 */
void engine_free(const EngineAllocator *allocator, void *memory)
{
    if (memory == NULL) {
        return;  /* CORE-ALLOC-031 */
    }

    if (allocator == NULL) {
        return;  /* CORE-ALLOC-092: no-op, leaks rather than crashes */
    }

    /* Call the allocator's free */
    (allocator->free)(allocator, memory);  /* CORE-ALLOC-093 */
}
