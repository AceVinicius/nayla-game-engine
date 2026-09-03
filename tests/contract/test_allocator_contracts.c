#include <stdio.h>
#include "src/core/allocator.h"
#include "src/core/engine.h"

/* Allocator that always fails (alloc always returns NULL) */
static void *failing_alloc_impl(const EngineAllocator *allocator, size_t size, size_t alignment) {
    (void)allocator;  /* unused */
    (void)size;       /* unused */
    (void)alignment;  /* unused */
    return NULL;  /* Always fail */
}

static void failing_free_impl(const EngineAllocator *allocator, void *memory) {
    (void)allocator;  /* unused */
    (void)memory;     /* unused */
    /* no-op: we never allocated anything */
}

/* Test ALLOC-INV-13: A test allocator whose alloc always returns NULL makes
 * engine_init return ENGINE_RESULT_OUT_OF_MEMORY and leave its out-parameter
 * NULL or unmodified. */
int main(void) {
    printf("Testing allocator contracts (ALLOC-INV-13)...\n");

    /* Create a failing allocator in a local variable (test allocator state) */
    EngineAllocator failing_alloc = {
        .alloc = failing_alloc_impl,
        .free = failing_free_impl,
        .user_data = NULL
    };

    /* CORE-RESULT-013: Pre-initialize the out-parameter to NULL so we can verify
     * that it is either left unmodified or set to NULL after a failure. */
    Engine *engine = NULL;

    EngineResult result = engine_init(&engine, &failing_alloc);

    if (result != ENGINE_RESULT_OUT_OF_MEMORY) {
        fprintf(stderr, "✗ engine_init with failing allocator should return ENGINE_RESULT_OUT_OF_MEMORY, got %s\n",
                engine_result_name(result));
        return 1;
    }
    printf("✓ engine_init with failing allocator returns ENGINE_RESULT_OUT_OF_MEMORY\n");

    /* CORE-RESULT-013: Out-parameter must be NULL or unmodified (we pre-set it to NULL) */
    if (engine != NULL) {
        fprintf(stderr, "✗ engine_init failed but left engine non-NULL (%p)\n", (void *)engine);
        return 1;
    }
    printf("✓ engine_init failed and left out-parameter NULL\n");

    printf("\n✅ All allocator contract tests passed!\n");
    return 0;
}
