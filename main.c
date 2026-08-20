#include <stdio.h>
#include "src/core/engine.h"

int main(void) {
    struct engine *engine;
    engine_error_t err;

    err = engine_init(&engine);
    if (err != 0) {
        fprintf(stderr, "Failed to initialize engine (error %d)\n", err);
        return 1;
    }

    printf("Engine initialized. Ticking...\n");
    err = engine_tick(engine);
    if (err != 0) {
        fprintf(stderr, "Engine tick failed (error %d)\n", err);
        engine_shutdown(engine);
        return 1;
    }

    printf("Hello from the game engine!\n");

    err = engine_shutdown(engine);
    if (err != 0) {
        fprintf(stderr, "Engine shutdown failed (error %d)\n", err);
        return 1;
    }

    printf("Engine shut down successfully.\n");
    return 0;
}
