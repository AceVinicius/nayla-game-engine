#include <stdio.h>
#include "src/core/engine.h"
#include "src/core/logging.h"

int main(void) {
    /* Construct logger on stack and initialize it before engine_init (CORE-LOG-082).
     * The logger is the outermost resource in the lifecycle.
     */
    EngineLogger logger;
    EngineResult log_err = engine_log_init(&logger, ENGINE_LOG_LEVEL_INFO);
    if (log_err != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "Failed to initialize logger (%s)\n", engine_result_name(log_err));
        return 1;
    }

    /* Install the console sink to output diagnostics to stderr (CORE-LOG-050/052) */
    EngineResult sink_err = engine_log_add_sink(&logger, engine_console_log_sink());
    if (sink_err != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "Failed to add console sink (%s)\n", engine_result_name(sink_err));
        engine_log_shutdown(&logger);
        return 1;
    }

    Engine *engine = NULL;
    EngineResult err;

    /* Pass the logger to engine_init (CORE-LOG-003) */
    err = engine_init(&engine, NULL, &logger);
    if (err != ENGINE_RESULT_SUCCESS) {
        ENGINE_LOG_ERROR(&logger, "App", "Failed to initialize engine (%s)", engine_result_name(err));
        engine_log_shutdown(&logger);
        return 1;
    }

    printf("Engine initialized. Ticking...\n");
    err = engine_tick(engine);
    if (err != ENGINE_RESULT_SUCCESS) {
        ENGINE_LOG_ERROR(&logger, "App", "Engine tick failed (%s)", engine_result_name(err));
        EngineResult shutdown_err = engine_shutdown(engine);
        if (shutdown_err != ENGINE_RESULT_SUCCESS) {
            ENGINE_LOG_ERROR(&logger, "App", "Engine shutdown also failed during cleanup (%s)", engine_result_name(shutdown_err));
        }
        engine_log_shutdown(&logger);
        return 1;
    }

    printf("Hello from the game engine!\n");

    err = engine_shutdown(engine);
    if (err != ENGINE_RESULT_SUCCESS) {
        ENGINE_LOG_ERROR(&logger, "App", "Engine shutdown failed (%s)", engine_result_name(err));
        engine_log_shutdown(&logger);
        return 1;
    }

    printf("Engine shut down successfully.\n");
    engine_log_shutdown(&logger);
    return 0;
}
