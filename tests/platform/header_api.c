/* Self-containment check for src/platform/api.h (PLATFORM-094). This file
 * includes api.h and nothing else, so a missing include in api.h fails the
 * build. CMake compiles it as C17 and as C23 (tests/platform/CMakeLists.txt). */

#include "src/platform/api.h"

int main(void) {
    return 0;
}
