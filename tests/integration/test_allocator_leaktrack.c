#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "src/core/allocator.h"
#include "src/core/engine.h"

/* Leak tracking allocator state: maintained in user_data
 * This struct tracks all allocations so we can verify they're all freed. */
typedef struct {
    /* Simple tracking: store up to 256 allocations */
    void **allocations;
    int alloc_count;
    int free_count;
    int max_allocs;
    /* Set when leak_tracker_free() is asked to release a pointer this
     * allocator never returned. leak_tracker_free() has no return value
     * (it matches EngineAllocatorFreeFn), so a provenance violation can
     * only be reported by setting this flag for main() to check. */
    int provenance_violation;
} LeakTrackerState;

/* Allocate a new tracker state */
static LeakTrackerState *leak_tracker_create(void) {
    LeakTrackerState *state = malloc(sizeof(LeakTrackerState));
    if (!state) return NULL;

    state->max_allocs = 256;
    state->allocations = malloc(sizeof(void *) * state->max_allocs);
    if (!state->allocations) {
        free(state);
        return NULL;
    }

    state->alloc_count = 0;
    state->free_count = 0;
    state->provenance_violation = 0;
    return state;
}

/* Free the tracker state */
static void leak_tracker_destroy(LeakTrackerState *state) {
    if (!state) return;
    free(state->allocations);
    free(state);
}

/* Leak tracker alloc: wrap the default allocator and track allocations */
static void *leak_tracker_alloc(const EngineAllocator *allocator, size_t size, size_t alignment) {
    LeakTrackerState *state = (LeakTrackerState *)allocator->user_data;

    /* Get the default allocator's alloc function and call it */
    const EngineAllocator *default_alloc = engine_default_allocator();
    void *ptr = (default_alloc->alloc)(default_alloc, size, alignment);

    if (ptr != NULL) {
        /* Track this allocation */
        if (state->alloc_count < state->max_allocs) {
            state->allocations[state->alloc_count] = ptr;
            state->alloc_count++;
        }
    }

    return ptr;
}

/* Leak tracker free: track frees and verify pointer was allocated */
static void leak_tracker_free(const EngineAllocator *allocator, void *memory) {
    LeakTrackerState *state = (LeakTrackerState *)allocator->user_data;

    if (memory != NULL) {
        /* Verify this pointer was allocated (simple linear search) */
        int found = 0;
        for (int i = 0; i < state->alloc_count; i++) {
            if (state->allocations[i] == memory) {
                found = 1;
                break;
            }
        }

        if (!found) {
            fprintf(stderr, "✗ leak_tracker_free: pointer %p was not allocated\n", memory);
            state->provenance_violation = 1;
        }

        state->free_count++;
    }

    /* Call the default allocator's free */
    const EngineAllocator *default_alloc = engine_default_allocator();
    (default_alloc->free)(default_alloc, memory);
}

/* Test ALLOC-INV-12: A leak-tracking allocator injected into engine_init
 * observes exactly as many free calls as alloc calls after engine_shutdown,
 * and every freed pointer was one it returned. */
int main(void) {
    printf("Testing allocator leak tracking (ALLOC-INV-12)...\n");

    /* Create leak tracker state */
    LeakTrackerState *tracker = leak_tracker_create();
    if (!tracker) {
        fprintf(stderr, "✗ Could not create leak tracker state\n");
        return 1;
    }

    /* Create a custom allocator that wraps the default and tracks calls */
    EngineAllocator tracking_alloc = {
        .alloc = leak_tracker_alloc,
        .free = leak_tracker_free,
        .user_data = (void *)tracker
    };

    /* Initialize engine with the tracking allocator */
    Engine *engine = NULL;
    EngineResult result;
    EngineResult shutdown_result;
    result = engine_init(&engine, &tracking_alloc);
    if (result != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "✗ engine_init failed: %s\n", engine_result_name(result));
        leak_tracker_destroy(tracker);
        return 1;
    }
    printf("✓ engine_init succeeded with tracking allocator\n");

    /* Record alloc count after init */
    int allocs_after_init = tracker->alloc_count;
    int frees_after_init = tracker->free_count;
    printf("  Allocations during init: %d\n", allocs_after_init);
    printf("  Frees during init: %d\n", frees_after_init);

    /* Tick the engine */
    result = engine_tick(engine);
    if (result != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "✗ engine_tick failed: %s\n", engine_result_name(result));
        shutdown_result = engine_shutdown(engine);
        if (shutdown_result != ENGINE_RESULT_SUCCESS) {
            fprintf(stderr, "  (engine_shutdown also failed: %s)\n", engine_result_name(shutdown_result));
        }
        leak_tracker_destroy(tracker);
        return 1;
    }
    printf("✓ engine_tick succeeded\n");

    /* Shutdown the engine */
    shutdown_result = engine_shutdown(engine);
    if (shutdown_result != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "✗ engine_shutdown failed: %s\n", engine_result_name(shutdown_result));
        leak_tracker_destroy(tracker);
        return 1;
    }
    printf("✓ engine_shutdown succeeded\n");

    /* Verify leak tracking: all allocations must be freed */
    printf("  Total allocations: %d\n", tracker->alloc_count);
    printf("  Total frees: %d\n", tracker->free_count);

    if (tracker->alloc_count != tracker->free_count) {
        fprintf(stderr, "✗ Allocation/free count mismatch: %d allocations, %d frees\n",
                tracker->alloc_count, tracker->free_count);
        leak_tracker_destroy(tracker);
        return 1;
    }
    printf("✓ All allocations were freed (ALLOC-INV-12)\n");

    if (tracker->provenance_violation) {
        fprintf(stderr, "✗ At least one freed pointer was not one this allocator returned (ALLOC-INV-12 provenance)\n");
        leak_tracker_destroy(tracker);
        return 1;
    }
    printf("✓ Every freed pointer was one this allocator returned (ALLOC-INV-12 provenance)\n");

    leak_tracker_destroy(tracker);

    printf("\n✅ Leak tracking test passed!\n");
    return 0;
}
