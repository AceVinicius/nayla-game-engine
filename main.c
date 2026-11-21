#include <stdio.h>
#include "src/core/engine.h"
#include "src/core/logging.h"
#include "src/platform/api.h"

/* Lifecycle follows PLATFORM-083 exactly:
 *   engine_log_init -> add sink -> set context source (PLATFORM-026)
 *   -> engine_init -> platform_init -> platform_window_create -> loop
 *   -> platform_window_destroy -> platform_shutdown -> engine_shutdown
 *   -> engine_log_shutdown
 * Each failure jumps to the label that releases what was already acquired;
 * the labels fall through in reverse acquisition order, so no path leaks. */
int main(void) {
    /* The logger is the outermost resource and lives on the stack (CORE-LOG-082).
     * It is the only resource whose failure cannot be reported through itself. */
    EngineLogger logger;
    EngineResult err = engine_log_init(&logger, ENGINE_LOG_LEVEL_INFO);
    if (err != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "Failed to initialize logger (%s)\n", engine_result_name(err));
        return 1;
    }

    Engine *engine = NULL;
    Platform *platform = NULL;
    PlatformWindow *window = NULL;
    bool ok = false;   /* set only by a clean close; any failure keeps it false */

    /* CORE-LOG-050/052: the console sink is the only output. Without it no
     * record can be shown, so this one failure is reported on stderr. */
    err = engine_log_add_sink(&logger, engine_console_log_sink());
    if (err != ENGINE_RESULT_SUCCESS) {
        fprintf(stderr, "Failed to add console sink (%s)\n", engine_result_name(err));
        goto release_logger;
    }

    /* PLATFORM-026: installed before any Platform exists, so records carry real
     * timestamps and thread ids from the first line on. */
    err = engine_log_set_context_source(&logger, platform_log_time_source,
                                        platform_log_thread_id_source, NULL);
    if (err != ENGINE_RESULT_SUCCESS) {
        ENGINE_LOG_ERROR(&logger, "App", "Failed to install log context sources (%s)", engine_result_name(err));
        goto release_logger;
    }

    err = engine_init(&engine, NULL, &logger);
    if (err != ENGINE_RESULT_SUCCESS) {
        ENGINE_LOG_ERROR(&logger, "App", "Failed to initialize engine (%s)", engine_result_name(err));
        goto release_logger;
    }

    err = platform_init(&platform, NULL, &logger);
    if (err == ENGINE_RESULT_NOT_SUPPORTED) {
        ENGINE_LOG_ERROR(&logger, "App", "No window system is available on this platform (%s); the application cannot run",
                         engine_result_name(err));
        goto release_engine;
    }
    if (err != ENGINE_RESULT_SUCCESS) {
        ENGINE_LOG_ERROR(&logger, "App", "Failed to initialize platform (%s)", engine_result_name(err));
        goto release_engine;
    }

    const PlatformWindowDesc desc = {
        .title  = "Nayla Game Engine",
        .width  = 1280,
        .height = 720,
        .flags  = PLATFORM_WINDOW_RESIZABLE,
    };
    err = platform_window_create(&window, platform, NULL, &desc);
    if (err != ENGINE_RESULT_SUCCESS) {
        ENGINE_LOG_ERROR(&logger, "App", "Failed to create window (%s)", engine_result_name(err));
        goto release_platform;
    }

    ENGINE_LOG_INFO(&logger, "App", "Window created; running the event loop");
    for (;;) {
        platform_pump_events(platform);
        PlatformEvent event;
        while (platform_next_event(platform, &event)) {
            if (event.type == PLATFORM_EVENT_WINDOW_FRAMEBUFFER_RESIZED) {
                ENGINE_LOG_INFO(&logger, "App", "Framebuffer resized to %ux%u", event.width, event.height);
            } else if (event.type == PLATFORM_EVENT_WINDOW_CLOSE_REQUESTED) {
                ENGINE_LOG_INFO(&logger, "App", "Window close requested");
            }
        }
        if (platform_window_should_close(window)) {
            ok = true;
            break;
        }
        err = engine_tick(engine);
        if (err != ENGINE_RESULT_SUCCESS) {
            ENGINE_LOG_ERROR(&logger, "App", "Engine tick failed (%s)", engine_result_name(err));
            break;
        }
    }

    platform_window_destroy(window);
release_platform:
    platform_shutdown(platform);
release_engine:
    err = engine_shutdown(engine);
    if (err != ENGINE_RESULT_SUCCESS) {
        ENGINE_LOG_ERROR(&logger, "App", "Engine shutdown failed (%s)", engine_result_name(err));
        ok = false;
    }
release_logger:
    engine_log_shutdown(&logger);
    return ok ? 0 : 1;
}
