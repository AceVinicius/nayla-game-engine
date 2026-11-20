/* RESULT-INV-7: engine_result_name() names every enumerator and is never NULL,
 * including for an out-of-range value (CORE-RESULT-010/011). */

#include <string.h>
#include "tests/check.h"

static int test_result_name(void) {
    static const struct {
        EngineResult code;
        const char *expected;
    } cases[] = {
        {ENGINE_RESULT_SUCCESS, "ENGINE_RESULT_SUCCESS"},
        {ENGINE_RESULT_INVALID_ARGUMENT, "ENGINE_RESULT_INVALID_ARGUMENT"},
        {ENGINE_RESULT_INVALID_STATE, "ENGINE_RESULT_INVALID_STATE"},
        {ENGINE_RESULT_OUT_OF_MEMORY, "ENGINE_RESULT_OUT_OF_MEMORY"},
        {ENGINE_RESULT_NOT_SUPPORTED, "ENGINE_RESULT_NOT_SUPPORTED"},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const char *name = engine_result_name(cases[i].code);
        CHECK(name != NULL, "engine_result_name(%d) returned NULL", (int)cases[i].code);
        CHECK(strcmp(name, cases[i].expected) == 0, "engine_result_name(%d) is '%s', expected '%s'",
              (int)cases[i].code, name, cases[i].expected);
    }

    CHECK(engine_result_name((EngineResult)999) != NULL, "out-of-range value has no name");
    return 0;
}

int main(void) {
    static const TestCase tests[] = {
        {"RESULT-INV-7 engine_result_name", test_result_name},
    };
    return RUN_TESTS(tests);
}
