#include "allocator.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

/* TYPE-INV-5: the one place the engine asserts its float and size_t sizes. */
_Static_assert(sizeof(float) == 4, "float must be exactly 4 bytes (IEEE-754 binary32)");
_Static_assert(sizeof(double) == 8, "double must be exactly 8 bytes (IEEE-754 binary64)");
_Static_assert(CHAR_BIT == 8, "CHAR_BIT must be 8");
_Static_assert(sizeof(size_t) == sizeof(void *), "size_t must match pointer size");

/* CORE-ALLOC-012/015: non-zero size, power-of-two alignment within bounds.
 * Checked by engine_alloc before calling through (CORE-ALLOC-016: so a caller
 * bug is never misreported as out-of-memory) and again by the default
 * allocator, which can be called directly through its function pointers. */
static bool request_is_valid(size_t size, size_t alignment) {
    return size != 0 &&
           alignment != 0 &&
           (alignment & (alignment - 1)) == 0 &&
           alignment <= ENGINE_ALLOCATOR_MAX_ALIGNMENT;
}

/* Default heap allocator: malloc/free with over-alignment via the
 * offset-header technique (CORE-ALLOC-052). The returned address is preceded
 * by one pointer slot holding the original malloc result, so free can recover
 * it. THREAD_SAFE: inherits the C library's malloc/free guarantee
 * (CORE-ALLOC-043). */
static void *default_alloc(const EngineAllocator *allocator, size_t size, size_t alignment) {
    (void)allocator;  /* stateless */

    if (!request_is_valid(size, alignment)) {
        return NULL;
    }

    /* The header slot must itself be naturally aligned (CORE-ALLOC-052). */
    size_t effective = alignment < _Alignof(void *) ? _Alignof(void *) : alignment;

    /* CORE-ALLOC-054: check for overflow before the addition. */
    size_t overhead = sizeof(void *) + (effective - 1);
    if (size > SIZE_MAX - overhead) {
        return NULL;
    }

    void *raw = malloc(size + overhead);
    if (raw == NULL) {
        return NULL;
    }

    uintptr_t base = (uintptr_t)raw + sizeof(void *);
    uintptr_t aligned = (base + (effective - 1)) & ~(uintptr_t)(effective - 1);
    ((void **)aligned)[-1] = raw;
    return (void *)aligned;
}

static void default_free(const EngineAllocator *allocator, void *memory) {
    (void)allocator;  /* stateless */

    if (memory == NULL) {
        return;  /* CORE-ALLOC-031 */
    }
    /* Parenthesized so a free() macro cannot expand here (CORE-ALLOC-093). */
    (free)(((void **)memory)[-1]);
}

static const EngineAllocator g_default_allocator = {
    .alloc = default_alloc,
    .free = default_free,
    .user_data = NULL,
};

const EngineAllocator *engine_default_allocator(void) {
    return &g_default_allocator;
}

/* CORE-ALLOC-090..093. A NULL allocator is not resolved to the default
 * (CORE-ALLOC-091): that is only valid at construction time. */
void *engine_alloc(const EngineAllocator *allocator, size_t size, size_t alignment) {
    if (allocator == NULL || !request_is_valid(size, alignment)) {
        return NULL;
    }
    return (allocator->alloc)(allocator, size, alignment);
}

/* free(NULL) is a no-op; so is a NULL allocator, which leaks rather than
 * crashes (CORE-ALLOC-031, CORE-ALLOC-092). */
void engine_free(const EngineAllocator *allocator, void *memory) {
    if (allocator == NULL || memory == NULL) {
        return;
    }
    (allocator->free)(allocator, memory);
}
