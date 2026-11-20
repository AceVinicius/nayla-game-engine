/* ALLOC-INV-13: an allocator whose alloc always fails drives engine_init to
 * ENGINE_RESULT_OUT_OF_MEMORY, leaving the out-parameter NULL (CORE-RESULT-013,
 * CORE-RESULT-019). The branch was untestable before allocators were injectable. */

#include "src/core/engine.h"
#include "tests/check.h"

static void *always_fails(const EngineAllocator *allocator, size_t size, size_t alignment) {
    (void)allocator;
    (void)size;
    (void)alignment;
    return NULL;
}

static void never_called(const EngineAllocator *allocator, void *memory) {
    (void)allocator;
    (void)memory;
}

static int test_out_of_memory(void) {
    EngineAllocator failing = {.alloc = always_fails, .free = never_called};
    Engine *engine = NULL;  /* pre-set so "unmodified" and "set to NULL" are the same */

    EXPECT(engine_init(&engine, &failing, NULL), ENGINE_RESULT_OUT_OF_MEMORY);
    CHECK(engine == NULL, "engine_init failed but left the out-parameter non-NULL");
    return 0;
}

int main(void) {
    static const TestCase tests[] = {
        {"ALLOC-INV-13 engine_init under allocation failure", test_out_of_memory},
    };
    return RUN_TESTS(tests);
}
