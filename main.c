#include <stdio.h>
#include "src/core/engine.h"

int main(void) {
    Engine *engine = NULL;
    EngineResult err;

    err = engine_init(&engine, NULL);
    if (err != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "Failed to initialize engine (%s)\n", engine_result_name(err));
        return 1;
    }

    printf("Engine initialized. Ticking...\n");
    err = engine_tick(engine);
    if (err != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "Engine tick failed (%s)\n", engine_result_name(err));
        EngineResult shutdown_err = engine_shutdown(engine);
        if (shutdown_err != ENGINE_RESULT_SUCCESS) {
            fprintf(stderr, "Engine shutdown also failed during cleanup (%s)\n", engine_result_name(shutdown_err));
        }
        return 1;
    }

    printf("Hello from the game engine!\n");

    err = engine_shutdown(engine);
    if (err != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "Engine shutdown failed (%s)\n", engine_result_name(err));
        return 1;
    }

    printf("Engine shut down successfully.\n");
    return 0;
}
