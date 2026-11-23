/* Wayland path placeholder (ADR-012 follow-up 3). The path exists in the
 * dispatch table so selection is final, but connecting always fails with
 * NOT_SUPPORTED until follow-up 5 implements it. Auto selection then falls
 * back to X11; NAYLA_LINUX_WINDOW_SYSTEM=wayland fails. The other operations
 * are never reached, because no Platform of this path can exist. */

#include "src/platform/linux/platform_linux.h"

static EngineResult wayland_open(Platform *platform) {
    ENGINE_LOG_WARN(platform->logger, "Platform", "Wayland path is not implemented yet (ADR-012 follow-up 5)");
    return ENGINE_RESULT_NOT_SUPPORTED;
}

const PlatformOps platform_wayland_ops = {
    .name = "Wayland",
    .platform_size = sizeof(Platform),
    .window_size = sizeof(PlatformWindow),
    .open = wayland_open,
};
