# ADR-012: Linux Windowing — Wayland and X11, Chosen at Run Time

**Status:** Accepted
**Date:** 2026-11-21
**Deciders:** `architect`
**Resolves:** `docs/spec/core/platform.md` §11 item 1; `docs/spec/specs.md` §69
**Requires amendments (spec-writer, follow-up 1):** `docs/spec/core/platform.md` PLATFORM-042, -043, -071, -072, -092, -101, §11 item 1, §13 edge case 14 and a new edge case (listed under Decision 6)
**Related:** `docs/spec/core/platform.md` PLATFORM-031, -033, -034, -036, -041, -050, -052, -055, -056, -070, -073, -074, -080, -082, -090, -094, -102, PLAT-INV-3, -9, -11, -14, -16; ADR-010; ADR-011; `rules/platform.md`; `rules/gpu.md`; CLAUDE.md invariants #1, #4, #5, #6

---

## Context

`engine_platform_linux` ships time and thread identity only; `platform_init` returns `NOT_SUPPORTED` (PLATFORM-092), and the Ubuntu CI jobs declare `NAYLA_TEST_NO_DISPLAY=1` (PLATFORM-101). `native.h` reserves two Linux rows: `X11` (`Display *` + XID) and `WAYLAND` (`wl_display *` + `wl_surface *`). The state of the ecosystem in 2026:

- **Sessions.** GNOME (49+) and the distributions that follow it (Fedora, Ubuntu 25.10+) ship Wayland-only sessions; KDE Plasma is dropping its X11 session. Xfce, Cinnamon, MATE, i3 and remote X sessions still run on X11. XWayland is present on every Wayland desktop, so an X11 client runs nearly everywhere.
- **What X11-through-XWayland costs.** No fractional scaling (the compositor upscales and blurs, or the window renders tiny), no "window not visible" signal, and none of the protocols that matter to a game engine and exist only on Wayland: tearing control, presentation timing (fifo/commit-timing), color management/HDR. X11 itself is in maintenance mode.
- **Vulkan WSI.** `VK_KHR_xlib_surface`/`VK_KHR_xcb_surface` and `VK_KHR_wayland_surface` are supported by Mesa and NVIDIA. The `X11` row's `Display *` is an Xlib type, so Xlib (not bare xcb) keeps the table unchanged.
- **Wayland specifics.** No server-side decorations on GNOME (Mutter refuses `xdg-decoration`), so a client without client-side decorations has no title bar or close button. No minimized state: `xdg_toplevel` reports `suspended` (xdg-shell v6, GNOME 46+, KWin 6.1+) when the surface is not visible, minimized or fully occluded alike. Scale is per surface (`wp_fractional_scale_v1` + `wp_viewporter`, or integer `preferred_buffer_scale`). A toplevel is not mapped until a buffer is committed. Connection loss is a `-1` return; nothing exits the process.
- **X11 specifics.** Xlib's default I/O-error path calls `exit()`. libX11 ≥ 1.7 provides a per-`Display` `XSetIOErrorExitHandler` that may return instead. Minimize is observable through ICCCM `WM_STATE == IconicState`. X11 has no logical units: window sizes are pixels.

## Options considered

1. **SDL-like abstraction (SDL3, GLFW) under the Platform API.** **Rejected.** It owns the event loop and process-global state (against PLATFORM-090), duplicates the contract we already implement natively on macOS and Win32, and puts a second layer between the native handles and their owner (PLATFORM-072).
2. **X11 only.** **Rejected.** Cheapest (one Xlib path, Xvfb in CI), but on the default desktops it always runs through XWayland, with blurry or tiny scaled output and none of the Wayland-only presentation features a game engine will need. We would end up writing Wayland anyway.
3. **Wayland only.** **Rejected.** It drops the X11 sessions still in use (Xfce, Cinnamon, MATE, tiling WMs, remote X, older NVIDIA drivers) for a small saving, since the contract is three non-trivial entry points (create, pump/wait, size).
4. **Both, selected at build time.** **Rejected.** A distributed binary must run on whatever session the user has, and every extra configuration needs its own CI coverage.
5. **Both in one target, selected at run time by `platform_init`.** **Chosen.** This is what SDL3, GLFW 3.4 and winit do.

## Decision

1. **Selection (PLATFORM-071).** `engine_platform_linux` always contains a Wayland path and an X11 path; there is no build option to drop either. `platform_init` picks one, and every window of that `Platform` reports that one `type` for the `Platform`'s whole life:
   - If `NAYLA_LINUX_WINDOW_SYSTEM` is `wayland` or `x11`, only that path is tried, with no fallback. Any other non-empty value: `ERROR` log, `NOT_SUPPORTED`. This is a backend-specific environment variable, not an `api.h` change. It exists for CI (Decision 5) and as a user workaround. It is read once, inside `platform_init`, and is not stored anywhere else.
   - Otherwise (auto): Wayland if `WAYLAND_DISPLAY` or `WAYLAND_SOCKET` is set and `wl_display_connect(NULL)` succeeds with the required globals (`wl_compositor`, `xdg_wm_base`, `wl_shm`). Else X11 if `XOpenDisplay(NULL)` succeeds. A failed Wayland attempt logs `WARN` before falling back. If nothing connects: `ERROR`, `NOT_SUPPORTED` (PLATFORM-033). The chosen system is logged at `INFO`.
   - Wayland comes first because on a Wayland session `DISPLAY` is also set (XWayland), and the native path is the better one.
   - Dispatch is internal: the `Platform` holds a pointer to a `static const` operations table (one per window system), so PLATFORM-090/PLAT-INV-14 hold. The OS-free parts (argument validation, the event queue and its coalescing, U+FFFD title sanitising) live in `src/platform/common/` and are shared by both paths. The macOS queue moves there too.
2. **Libraries.** Linked, not `dlopen`ed: every desktop distribution has both client libraries installed, and Wayland-only GNOME still pulls in libX11.
   - X11: **Xlib** (`libX11` ≥ 1.7). Xlib because of the native table's `Display *` and `XSetIOErrorExitHandler`. No xcb-only code.
   - Wayland: `libwayland-client`; protocol glue generated at build time by `wayland-scanner` from `wayland-protocols` (xdg-shell, viewporter, fractional-scale-v1) into the build tree, which is outside PLAT-INV-14's `src/platform` scope; **libdecor** (`libdecor-0`) for decorations. libdecor uses server-side decorations when the compositor offers them and draws client-side ones (GTK or cairo plugin) otherwise, so the close button exists on GNOME and PLATFORM-043 holds for the user, not only for Alt+F4.
   - xkbcommon is not needed until input events exist (PLATFORM-054).
   - Where a library takes a non-`const` listener or interface pointer, that struct lives in the `Platform`, never at file scope.
3. **Window behaviour per system.**
   - *Sizes (PLATFORM-042).* X11: logical units are pixels, so the requested size is the pixel size; the framebuffer comes from `ConfigureNotify`. Wayland: the requested size is logical; the first `configure`'s size wins when non-zero. Framebuffer = logical × scale, where the scale is fractional-scale (`round(logical × scale / 120)`) when available, else `preferred_buffer_scale`, else the largest scale among the outputs the surface has entered. The backend keeps `buffer_scale` at 1 and sets `wp_viewport` destination = logical size, so a buffer committed by the GPU at a stale size never causes a protocol error. Any change in the reported pixel size queues `FRAMEBUFFER_RESIZED` (PLATFORM-052).
   - *Minimized.* X11: `WM_STATE == IconicState` reports `0 × 0`, and `NormalState` restores the size. Wayland: `0 × 0` while the toplevel is `suspended`. This also covers fully occluded windows, and that is what the loop wants: presenting to a suspended surface can block FIFO present indefinitely, and the PLATFORM-055 idle wait avoids it. Without xdg-shell v6 a minimized window keeps its size (degraded, logged once at `INFO`). Both points need the PLATFORM-042 amendment in Decision 6.
   - *Close (PLATFORM-043).* X11: `WM_DELETE_WINDOW` in `WM_PROTOCOLS`; `_NET_WM_PING` is not advertised. Wayland: the libdecor/`xdg_toplevel` close callback.
   - *Visible on success (PLATFORM-041).* X11: `XMapWindow` + `XSync`. Wayland: commit, roundtrip until the first `configure`, ack it, then attach and commit a 1×1 opaque `wl_shm` buffer scaled to the logical size by the viewport, so the toplevel maps before any GPU surface exists. The GPU swapchain's first present replaces it. Events delivered during creation are absorbed.
   - *Titles.* Both protocols require valid UTF-8, and a Wayland compositor may kill the client over invalid UTF-8. The backend therefore always substitutes U+FFFD, using PLATFORM-041's MAY as a MUST here.
   - *Not resizable.* Min size = max size (WM normal hints / `libdecor` content limits).
4. **Pump, wait, threads (PLATFORM-050/055/056/080/082).**
   - X11: pump = `XPending` loop. Wait = `XFlush`; if `XEventsQueued(QueuedAfterReading)` is 0, `poll` on `ConnectionNumber` with the timeout rounded up to ms; then pump.
   - Wayland: pump = `libdecor_dispatch(ctx, 0)`; wait = `libdecor_dispatch(ctx, ms)`, which internally uses the `wl_display_prepare_read`/`read_events` protocol. A GPU WSI thread reading its own event queue therefore never steals or starves the platform's reads.
   - `UINT64_MAX` maps to an infinite poll. Finite timeouts are clamped to `INT_MAX` ms, never to 0 or infinite.
   - All listeners run inside dispatch calls on the platform thread, so they may log and allocate (PLATFORM-082). Threads a decoration plugin may start never call engine code.
   - Neither path installs a process-global Xlib error or I/O-error handler, because that would be engine state at process scope and would clobber the application's. X protocol errors therefore still reach Xlib's default handler: a protocol error is a backend bug.
   - `platform_shutdown` closes the connection, so sequential re-init (PLATFORM-036) opens a fresh one.
5. **CI display environment (PLATFORM-101).**
   - The Ubuntu jobs install the client libraries, `xvfb`, `weston` (≥ 10, headless backend) and a libdecor plugin. They start `Xvfb :99` and `weston --backend=headless --socket=<name>` (with a private `0700` `XDG_RUNTIME_DIR`), export `DISPLAY`/`WAYLAND_DISPLAY`, and **stop setting `NAYLA_TEST_NO_DISPLAY`**.
   - The full `ctest` run uses auto selection, so it exercises Wayland. One extra run of `^contract/platform_window$` sets `NAYLA_LINUX_WINDOW_SYSTEM=x11`. The test source does not branch on the backend (PLATFORM-102).
   - The sanitizer job runs under Xvfb with `x11` forced: no decoration plugin, no GTK. Any LeakSanitizer suppression for a third-party library is listed in the repo and reported, never broad.
   - Neither Xvfb nor headless Weston runs a window manager that minimizes or closes windows. That is fine, because the suite cannot synthesize those events anyway (PLAT-INV-12/16).
   - PLATFORM-101's rule is unchanged: `NOT_SUPPORTED` without the declaration is a FAIL. Only its "Ubuntu jobs set it" sentence changes: Linux jobs provide a display, and only a person running the suite on a Linux machine with no display server declares headless. A forced system whose server is not running is a FAIL unless declared.
6. **Connection loss mid-run.** X server or compositor exit; detected as Xlib's I/O-error exit handler (installed per `Display`, with the `Platform` as user data) or a `-1` from `libdecor_dispatch`/`wl_display_*`.
   - The backend enters a terminal **lost** state. It logs `ERROR` once with the detail (`errno`, or the Wayland protocol-error interface and code). It then treats the loss as an application-level quit (PLATFORM-043): every live window's sticky close flag is set, and `CLOSE_REQUESTED` is queued for each window whose flag was not already set (PLATFORM-052).
   - After that, it makes no further protocol call. `platform_pump_events` and `platform_wait_events` return at once; the wait does not block, because there is nothing to wait on. `platform_window_create` returns `NOT_SUPPORTED` with an `ERROR` log (PLATFORM-034). The size and native queries return the last values (PLATFORM-073), and the native values must not be used to create new surfaces. `platform_window_destroy` and `platform_shutdown` still release every engine allocation (PLAT-INV-9) and free the client-side connection object without I/O.
   - **This needs a spec amendment**, because PLATFORM-043 lists user/OS close sources but not connection loss, and the post-loss rules exist nowhere. spec-writer amends:
     - **PLATFORM-043**: add "loss of the window-system connection behaves as an application-level quit; afterwards pump/wait return at once, window creation returns `NOT_SUPPORTED`, queries report the last state, and destroy/shutdown release everything".
     - **§13**: a new edge case.
     - It is written for every backend; Win32 and Cocoa never trigger it.
   - **Other amendments:**
     - **PLATFORM-042**: on Wayland "minimized" means `suspended` (occluded included); without xdg-shell v6 the size stays non-zero.
     - **PLATFORM-071**: Linux produces `X11` or `WAYLAND`, one type per `Platform`, chosen by `platform_init` per this ADR.
     - **PLATFORM-072**: presenting through the window system's swapchain mechanism (WSI attach/commit on the `wl_surface`, Present/DRI3 on the X window) is use, not a sanctioned-list mutation. The `wp_viewport`, `buffer_scale` and xdg state stay the platform's.
     - **PLATFORM-092**: "until the §11 ADR lands" becomes "until the ADR-012 implementation lands"; document `NAYLA_LINUX_WINDOW_SYSTEM`.
     - **PLATFORM-101**: per Decision 5.
     - **§11 item 1** and **§13 edge case 14**: point here.
   - **Out of scope:** fullscreen and display modes (PLATFORM-040); input and xkbcommon (PLATFORM-054); a confirm-on-close flow; detecting a minimized window on pre-v6 compositors; an `api.h` hint to choose the window system (revisit when `platform_init` takes a descriptor); a `platform_wake` cross-thread wakeup (edge case 18).

## Consequences

- One binary runs natively on Wayland desktops, with correct fractional scaling and access to future presentation protocols, and on X11 sessions. The contract, `api.h`, `native.h`, core and renderer do not change. GPU backends implement `surface_wayland.c` and `surface_x11.c` (ADR-010) and degrade with `NOT_SUPPORTED` on a type they lack (PLATFORM-071), which the user can work around with `NAYLA_LINUX_WINDOW_SYSTEM`.
- The cost is two window-system paths to maintain plus libdecor. On GNOME, libdecor's GTK plugin loads GTK into the process; that is accepted for a working title bar and close button. CI runs window conformance on both paths, so neither rots.
- **Follow-up tasks.** Order: 1 → 2 → 3 → 5 → 4 → 6; 7 waits for the Vulkan backend.
  1. `spec-writer`: apply Decision 6's amendments to `platform.md`.
  2. `build-engineer`: `engine_platform_linux` dependencies (pkg-config `x11` ≥ 1.7, `wayland-client`, `wayland-scanner` code generation for xdg-shell/viewporter/fractional-scale, `libdecor-0`) with zero warnings in Debug and Release.
  3. `platform-engineer`: Linux selection and dispatch, the shared queue/validation/UTF-8 helpers moved to `src/platform/common/`, and the X11 path, against Decisions 1–4 and 6.
  4. `platform-engineer`: the Wayland path (libdecor, viewporter/fractional scale, `suspended`, initial shm buffer), against Decisions 1–4 and 6.
  5. `build-engineer`: Ubuntu CI with Xvfb and headless Weston per Decision 5; drop `NAYLA_TEST_NO_DISPLAY`; sanitizer job under Xvfb.
  6. `test-engineer`: a Linux-only integration test in `tests/platform/` that starts a private Xvfb (and Weston), creates a window, kills the server, and asserts the Decision 6 behaviour (`should_close`, `CLOSE_REQUESTED`, leak-free shutdown) on each path.
  7. `gpu-backend-engineer`: `surface_x11.c` (`VK_KHR_xlib_surface`) and `surface_wayland.c` (`VK_KHR_wayland_surface`), once a Vulkan backend exists.

## Revisit trigger

Reopen this decision if any of the following happens:

- Major X11-session desktops (Xfce, Cinnamon, MATE) ship Wayland by default and XWayland covers the rest. Then drop the X11 path.
- libdecor or its plugins cause a defect in this backend (threads, leaks, crashes) that cannot be fixed or suppressed narrowly. Then fall back to `xdg-decoration` only and accept undecorated windows on GNOME.
- A supported target lacks one of the client libraries. Then add a build option or `dlopen`.
- An application needs to pick the window system through the API rather than the environment.
