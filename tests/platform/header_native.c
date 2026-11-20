/* Self-containment check for src/platform/native.h (PLATFORM-094). This file
 * includes native.h and nothing else. native.h pulls in api.h itself, so the
 * same check covers the api.h it depends on. CMake compiles it as C17 and as
 * C23 (tests/platform/CMakeLists.txt). */

#include "src/platform/native.h"

int main(void) {
    return 0;
}
