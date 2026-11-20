/* ALLOC-INV-12: a leak-tracking allocator injected into engine_init sees
 * exactly as many frees as allocs after engine_shutdown, and every freed
 * pointer is one it returned (CORE-ALLOC-030/067). */

#include "src/core/allocator.h"
#include "src/core/engine.h"
#include "tests/check.h"

#define MAX_TRACKED 16

/* All tracker state lives in the allocator's user_data. */
typedef struct {
    void *blocks[MAX_TRACKED];
    int allocs;
    int frees;
    int foreign_frees;  /* frees of a pointer this tracker never returned */
} Tracker;

static void *tracked_alloc(const EngineAllocator *allocator, size_t size, size_t alignment) {
    Tracker *t = allocator->user_data;
    const EngineAllocator *base = engine_default_allocator();
    void *p = (base->alloc)(base, size, alignment);

    if (p != NULL && t->allocs < MAX_TRACKED) {
        t->blocks[t->allocs++] = p;
    }
    return p;
}

static void tracked_free(const EngineAllocator *allocator, void *memory) {
    Tracker *t = allocator->user_data;
    const EngineAllocator *base = engine_default_allocator();

    if (memory != NULL) {
        int known = 0;
        for (int i = 0; i < t->allocs; i++) {
            known |= t->blocks[i] == memory;
        }
        t->foreign_frees += !known;
        t->frees++;
    }
    (base->free)(base, memory);
}

static int test_no_leaks(void) {
    Tracker tracker = {0};
    EngineAllocator allocator = {.alloc = tracked_alloc, .free = tracked_free, .user_data = &tracker};
    Engine *engine = NULL;

    EXPECT(engine_init(&engine, &allocator, NULL), ENGINE_RESULT_SUCCESS);
    EXPECT(engine_tick(engine), ENGINE_RESULT_SUCCESS);
    EXPECT(engine_shutdown(engine), ENGINE_RESULT_SUCCESS);

    CHECK(tracker.allocs > 0, "the injected allocator was never used");
    CHECK(tracker.allocs == tracker.frees, "%d allocations but %d frees", tracker.allocs, tracker.frees);
    CHECK(tracker.foreign_frees == 0, "%d freed pointer(s) this allocator never returned",
          tracker.foreign_frees);
    return 0;
}

int main(void) {
    static const TestCase tests[] = {
        {"ALLOC-INV-12 balanced alloc/free through an injected allocator", test_no_leaks},
    };
    return RUN_TESTS(tests);
}
