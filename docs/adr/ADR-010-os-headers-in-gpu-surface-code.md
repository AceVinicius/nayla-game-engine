# ADR-010: OS Headers in GPU Backend Surface Code

**Status:** Accepted
**Date:** 2026-11-20
**Deciders:** `architect`
**Amends:** `.claude/rules/platform.md` ("OS headers stay inside platform backends"); specs.md §70 ("Native Apple APIs MUST remain isolated in the macOS backend"), for the one case below
**Related:** `docs/spec/core/platform.md` PLATFORM-071..074, PLATFORM-094; specs.md §17, §18; CLAUDE.md invariants #1–#4; `rules/gpu.md`

---

## Context

`platform.md` §7 hands GPU backends a `PlatformNativeWindow` whose fields are, by contract, `HINSTANCE`/`HWND`, an `NSView *`, an X11 `Display *` + XID, or a `wl_display *`/`wl_surface *`, stored as `PlatformNativeObject *`. PLATFORM-072 also lets the GPU backend install a `CAMetalLayer` on the `NSView`. To use any of these, a GPU backend has to cast back to the OS type and call an OS- or SDK-platform entry point: `vkCreateWin32SurfaceKHR` (`<vulkan/vulkan_win32.h>`, which needs `<windows.h>`), `IDXGIFactory2::CreateSwapChainForHwnd`, `-[NSView setWantsLayer:]` (AppKit), and the Xlib/XCB/Wayland surface calls.

PLATFORM-094 and `rules/platform.md` say OS headers appear only in platform backends. As written, no conforming GPU backend can create a surface. specs.md §18 already gives the GPU backend this conversion job, so the contract and the header rule contradict each other.

## Options considered

1. **Platform backend exposes a richer, GPU-ready handle.** The platform backend would create the `CAMetalLayer`, the `VkSurfaceKHR`, or similar, so GPU backends need no OS headers. **Rejected.**
   - The platform backend would have to include GPU SDK headers (QuartzCore/Metal, Vulkan). That breaks `rules/gpu.md` ("SDK headers stay inside backends", meaning GPU backends) and PLATFORM-094 ("never include a GPU SDK header"), and it reverses the dependency direction.
   - The coupling becomes N×M: each OS backend grows one hook per GPU API, and adding a GPU backend changes every platform backend.
   - It does not remove the need for OS types anyway. `CreateSwapChainForHwnd` takes an `HWND`, and D3D12/DXGI headers pull in `<windows.h>` themselves. Something on the GPU side still names the OS type.
2. **Allow OS headers anywhere in a GPU backend.** This is simple, but it can't be checked narrowly: window-system code spreads through a backend, and nothing marks where the native boundary is. **Rejected.**
3. **Allow OS headers in GPU backends only in a fixed, named surface file per native window type.** **Chosen.**

## Decision

1. **Where OS headers may be included directly:**
   - (a) anywhere under `src/platform/<os>/`;
   - (b) in GPU backends, only in files named exactly `src/gpu/<backend>/surface_<type>.c` or `.m`, where `<type>` is one of `win32`, `cocoa`, `x11`, `wayland`, matching `PlatformNativeWindowType`. A backend has one such file for each native type it supports.
2. **What a surface file may do.** It includes `src/platform/native.h`, converts the `PlatformNativeWindow` fields to OS types, and creates the backend's surface object (plus, for `cocoa`, the sanctioned `CAMetalLayer` installation from PLATFORM-072). It exposes the result to the rest of its backend only through a backend-private header that uses engine types and the backend's own SDK types, and never an OS type. Surface files are never headers, and no other file includes them.
3. **Platform-specific GPU-SDK surface headers and macros** (`vulkan_win32.h`, `vulkan_metal.h`, `vulkan_xlib.h`, `vulkan_xcb.h`, `vulkan_wayland.h`, and any `VK_USE_PLATFORM_*` definition) follow the same rule as OS headers.
4. **Direct includes only.** OS headers that a GPU SDK header pulls in transitively, such as `<windows.h>` via `<d3d12.h>`/`<dxgi*.h>` or Foundation via `<Metal/Metal.h>`, are part of that SDK. They are contained by the existing rule that SDK headers stay inside GPU backends. QuartzCore is classed as a GPU SDK header (Metal presentation), not an OS header.
5. **OS headers are never included by** `src/core/**`, `src/renderer/**`, `src/platform/api.h`, `src/platform/native.h`, `src/gpu/api.h`, or any GPU backend file other than those in 1(b). Platform backends still never include a GPU SDK header.
6. **Check:** a single forbidden-include list (the OS list plus the platform-specific Vulkan headers and `VK_USE_PLATFORM_`, matching `#include` and `#import`, case-insensitive) is enforced at configure time:
   - with no exemption over `src/core`, `src/renderer`, `src/platform/api.h`, `src/platform/native.h`, and `src/gpu/api.h`;
   - over each `src/gpu/<backend>/` with the sole exemption being basenames matching `^surface_(win32|cocoa|x11|wayland)\.(c|m)$`.

   The existing `forbid-pattern.sh` exemption takes one literal basename, so it needs a regex-exemption variant. That is a build-engineer task.

## Consequences

- GPU backends can implement PLATFORM-071/072, and the contract is satisfiable on every OS without platform↔GPU-SDK coupling.
- The native boundary in GPU code becomes visible: a reviewer or a grep can list every file that touches OS window types.
- Adding a native window type means adding one `surface_<type>` file per GPU backend that supports it. There are no core, renderer, or platform-backend changes, so CLAUDE.md's "switching backends MUST NOT require core/renderer changes" still holds.
- The forbidden-include list is the one source for this check and for PLAT-INV-3. It replaces the current lowercase-only `windows|unistd|vulkan|d3d12|metal` patterns for OS headers.
- Decided in platform.md PLATFORM-093: how GPU backends resolve the `platform_window_get_native` symbol at link time.
