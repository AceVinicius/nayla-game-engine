/* C17 build check (.claude/rules/core.md: C17/C23). CMake compiles this file
 * and every engine source as C17, so a C23-only construct (for example a bare
 * static_assert without <assert.h>) fails the build. Running it confirms the
 * C17 objects work, not just compile. */

#include "src/core/engine.h"
#include "src/renderer/api.h"
#include "tests/check.h"

_Static_assert(__STDC_VERSION__ == 201710L, "this target must build as C17");

static int test_c17_lifecycle(void) {
    Engine *engine = NULL;
    Renderer *renderer = NULL;
    EXPECT(engine_init(&engine, NULL, NULL), ENGINE_RESULT_SUCCESS);
    EXPECT(renderer_init(&renderer, NULL, NULL), ENGINE_RESULT_SUCCESS);
    EXPECT(renderer_shutdown(renderer), ENGINE_RESULT_SUCCESS);
    EXPECT(engine_shutdown(engine), ENGINE_RESULT_SUCCESS);
    return 0;
}

int main(void) {
    static const TestCase tests[] = {
        {"engine and renderer build and run as C17", test_c17_lifecycle},
    };
    return RUN_TESTS(tests);
}
