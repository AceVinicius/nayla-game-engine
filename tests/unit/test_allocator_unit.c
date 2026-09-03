#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <assert.h>
#include "src/core/allocator.h"

/* Test ALLOC-INV-3: engine_default_allocator() never returns NULL, returns the same
 * pointer on repeated calls, and its alloc/free fields are non-NULL. */
static int test_allocator_singleton(void) {
    printf("Testing ALLOC-INV-3: allocator singleton...\n");

    const EngineAllocator *a1 = engine_default_allocator();
    const EngineAllocator *a2 = engine_default_allocator();

    if (a1 == NULL) {
        fprintf(stderr, "✗ engine_default_allocator() returned NULL\n");
        return 1;
    }

    if (a1 != a2) {
        fprintf(stderr, "✗ engine_default_allocator() returned different pointers\n");
        return 1;
    }

    if (a1->alloc == NULL || a1->free == NULL) {
        fprintf(stderr, "✗ engine_default_allocator()->alloc or ->free is NULL\n");
        return 1;
    }

    printf("✓ engine_default_allocator() singleton verified (ALLOC-INV-3)\n");
    return 0;
}

/* Test ALLOC-INV-4: For every power of two A in [1, ENGINE_ALLOCATOR_MAX_ALIGNMENT],
 * the default allocator's alloc(a, 1, A) returns a pointer p with (uintptr_t)p % A == 0,
 * and free(a, p) returns cleanly. */
static int test_allocator_alignment(void) {
    printf("Testing ALLOC-INV-4: alignment...\n");

    const EngineAllocator *alloc = engine_default_allocator();

    for (size_t alignment = 1; alignment <= ENGINE_ALLOCATOR_MAX_ALIGNMENT; alignment <<= 1) {
        void *p = engine_alloc(alloc, 1, alignment);
        if (p == NULL) {
            fprintf(stderr, "✗ engine_alloc(_, 1, %zu) returned NULL\n", alignment);
            return 1;
        }

        uintptr_t addr = (uintptr_t)p;
        if ((addr % alignment) != 0) {
            fprintf(stderr, "✗ engine_alloc(_, 1, %zu) returned misaligned pointer: %zu %% %zu = %zu\n",
                    alignment, addr, alignment, addr % alignment);
            engine_free(alloc, p);
            return 1;
        }

        engine_free(alloc, p);
    }

    printf("✓ Allocator handles all powers of two up to MAX_ALIGNMENT (ALLOC-INV-4)\n");
    return 0;
}

/* Test ALLOC-INV-5: The default allocator's alloc returns NULL for:
 * size == 0; alignment == 0; alignment == 3 (non-power-of-two);
 * alignment == ENGINE_ALLOCATOR_MAX_ALIGNMENT * 2; and size == SIZE_MAX. */
static int test_allocator_rejection(void) {
    printf("Testing ALLOC-INV-5: rejection cases...\n");

    const EngineAllocator *alloc = engine_default_allocator();

    /* size == 0 should return NULL */
    void *p = engine_alloc(alloc, 0, ENGINE_ALLOCATOR_DEFAULT_ALIGNMENT);
    if (p != NULL) {
        fprintf(stderr, "✗ engine_alloc(_, 0, DEFAULT_ALIGNMENT) should return NULL\n");
        return 1;
    }
    printf("  ✓ Rejects size == 0\n");

    /* alignment == 0 should return NULL */
    p = engine_alloc(alloc, 1, 0);
    if (p != NULL) {
        fprintf(stderr, "✗ engine_alloc(_, 1, 0) should return NULL\n");
        return 1;
    }
    printf("  ✓ Rejects alignment == 0\n");

    /* alignment == 3 (non-power-of-two) should return NULL */
    p = engine_alloc(alloc, 1, 3);
    if (p != NULL) {
        fprintf(stderr, "✗ engine_alloc(_, 1, 3) should return NULL\n");
        return 1;
    }
    printf("  ✓ Rejects non-power-of-two alignment\n");

    /* alignment == MAX_ALIGNMENT * 2 should return NULL */
    p = engine_alloc(alloc, 1, ENGINE_ALLOCATOR_MAX_ALIGNMENT * 2);
    if (p != NULL) {
        fprintf(stderr, "✗ engine_alloc(_, 1, MAX*2) should return NULL\n");
        return 1;
    }
    printf("  ✓ Rejects alignment > MAX_ALIGNMENT\n");

    /* size == SIZE_MAX should return NULL (even with valid alignment) */
    p = engine_alloc(alloc, SIZE_MAX, ENGINE_ALLOCATOR_DEFAULT_ALIGNMENT);
    if (p != NULL) {
        fprintf(stderr, "✗ engine_alloc(_, SIZE_MAX, _) should return NULL\n");
        return 1;
    }
    printf("  ✓ Rejects size == SIZE_MAX\n");

    printf("✓ All rejection cases verified (ALLOC-INV-5)\n");
    return 0;
}

/* Test ALLOC-INV-6: engine_free(a, NULL) and engine_free(NULL, p) return normally.
 * engine_alloc(NULL, 1, 1) returns NULL. */
static int test_allocator_null_handling(void) {
    printf("Testing ALLOC-INV-6: NULL handling...\n");

    const EngineAllocator *alloc = engine_default_allocator();

    /* Allocate a pointer to test freeing with NULL allocator */
    void *p = engine_alloc(alloc, 1, ENGINE_ALLOCATOR_DEFAULT_ALIGNMENT);
    if (p == NULL) {
        fprintf(stderr, "✗ Could not allocate test pointer\n");
        return 1;
    }

    /* engine_free(alloc, NULL) should be a no-op and return normally */
    engine_free(alloc, NULL);
    printf("  ✓ engine_free(alloc, NULL) is a no-op\n");

    /* engine_free(NULL, p) should be a no-op and leak (but return normally) */
    engine_free(NULL, p);
    printf("  ✓ engine_free(NULL, p) is a no-op (leaks)\n");

    /* engine_alloc(NULL, 1, 1) should return NULL */
    void *result = engine_alloc(NULL, 1, 1);
    if (result != NULL) {
        fprintf(stderr, "✗ engine_alloc(NULL, 1, 1) should return NULL\n");
        engine_free(alloc, p);
        return 1;
    }
    printf("  ✓ engine_alloc(NULL, 1, 1) returns NULL\n");

    /* We leaked p via engine_free(NULL, p), but that's the test; clean up the other one */
    /* (Actually p is already leaking, so we're done) */

    printf("✓ NULL handling verified (ALLOC-INV-6)\n");
    return 0;
}

int main(void) {
    printf("Running allocator unit tests...\n\n");

    int failures = 0;
    failures += test_allocator_singleton();
    failures += test_allocator_alignment();
    failures += test_allocator_rejection();
    failures += test_allocator_null_handling();

    if (failures) {
        printf("\n✗ %d test(s) failed\n", failures);
        return 1;
    }

    printf("\n✅ All allocator unit tests passed!\n");
    return 0;
}
