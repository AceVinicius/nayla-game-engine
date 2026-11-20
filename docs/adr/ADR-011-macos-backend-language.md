# ADR-011: macOS Platform Backend Language

**Status:** Accepted
**Date:** 2026-11-20
**Deciders:** `architect`
**Resolves:** `docs/spec/core/platform.md` §11 item 2
**Amends:** `docs/spec/core/platform.md` PLAT-INV-3(c) (also matches `@import` of the Metal/QuartzCore modules and their submodules) and PLAT-INV-14 (also scans `.m`)
**Related:** `docs/spec/core/platform.md` PLATFORM-031, -036, -044, -053, -072, -080, -082, -090, -092, -093, -094, PLAT-INV-3, -9, -14; ADR-010; `rules/platform.md`; CLAUDE.md invariants #1, #3, #5

---

## Context

The Platform API is a C contract (`src/platform/api.h`, `native.h`). The macOS backend (`src/platform/macos/`, target `engine_platform_macos`) has to drive AppKit: create `NSWindow`s, receive delegate callbacks, pump `NSEvent`s, and hand an `NSView *` to GPU backends (PLATFORM-072). AppKit is an Objective-C API. There are two ways to call it:

- **Objective-C translation units (`.m`)**, compiled by clang as Objective-C, with either ARC or manual retain/release (MRR).
- **C translation units (`.c`)** that call the runtime directly: `objc_getClass`, `sel_registerName`, `objc_msgSend` cast to an exact function-pointer type for every message, and delegate classes built at run time with `objc_allocateClassPair`/`class_addMethod` and type-encoding strings.

The choice affects review cost, how PLATFORM-044's release order ("OS handles released first, then the struct through the stored allocator") is expressed, the build, and how the architecture checks see these files.

## Options considered

1. **C + Objective-C runtime (`objc_msgSend`).** **Rejected.**
   - Every message send is an unchecked cast, e.g. `((void (*)(id, SEL, BOOL))objc_msgSend)(win, sel_registerName("setReleasedWhenClosed:"), NO)`. The compiler checks neither selector names nor argument types, so a typo or a wrong cast is a run-time crash. On x86_64, struct returns (`NSRect`) need `objc_msgSend_stret`, which is an architecture branch inside the backend.
   - Delegate classes are assembled from strings (`"v@:@"`) at run time. Nobody can review that against the AppKit headers.
   - Run-time class registration is process-global. Sequential re-init (PLATFORM-036) would have to look up the class before registering it again, which is extra code that `.m` files never need.
   - The only benefit is that files stay `.c`. That buys nothing: OS headers are allowed under `src/platform/<os>/` anyway (PLATFORM-094), and the C17 build check does not cover platform backends (see Decision 6).
2. **Objective-C with ARC.** **Rejected.**
   - `Platform` and `PlatformWindow` are C structs allocated through the stored `EngineAllocator` (PLATFORM-031), which hands back uninitialised memory. ARC `__strong` fields in such a struct must be zeroed before their first store, because ARC releases the old value. Before `engine_free` they must also be set to `nil` by hand, or they leak. So ARC brings back the explicit release steps and adds a trap if they are forgotten.
   - Every crossing of the `native.h` boundary (`NSView *` ↔ `PlatformNativeObject *`) needs a `__bridge` cast, which spreads ownership annotations into code whose only job is to hand over a borrowed pointer.
   - Under ARC, the "OS handles first, then the struct" order in PLATFORM-044 comes from where the compiler inserts releases, not from lines a reviewer can read.
3. **Objective-C with manual retain/release (MRR).** **Chosen.**

## Decision

1. **Language.** `src/platform/macos/` consists of Objective-C translation units (`.m`) compiled with **manual retain/release**. The target sets `-fno-objc-arc` explicitly, so a generator default (for example Xcode's `CLANG_ENABLE_OBJC_ARC`) cannot change it. No `.c` file in this directory calls `objc_msgSend`.
2. **Ownership (PLATFORM-031, PLATFORM-044).**
   - `Platform` and `PlatformWindow` are C structs allocated and freed only through their stored `EngineAllocator`. Objective-C objects (`NSWindow`, delegates) are OS-owned memory created with `+alloc`/`-init` and released with `-release`. They never come from the `EngineAllocator`, and PLAT-INV-9's leak counts do not include them.
   - Each struct holds a +1 reference to every Objective-C object it owns. `NSWindow` is created with `releasedWhenClosed = NO` so that its only owner is the struct.
   - Every Platform API function that calls AppKit wraps its AppKit calls in `@autoreleasepool { … }`, because the C caller has no pool. `-close` and other AppKit calls may autorelease objects, so an object is only truly released when that pool drains.
   - `platform_window_destroy` therefore runs in this order, with each step written out in the source. Inside one `@autoreleasepool` block: clear the delegate's back pointer and `[ns_window setDelegate:nil]`, `[ns_window close]`, `[ns_window release]`, `[delegate release]`. The block then ends, which drains the pool. Only after it: purge the window's queued events (PLATFORM-053), then `engine_free(window->allocator, window)`. The struct release is never inside the pool block, so OS objects are gone before the struct (PLATFORM-044). `platform_shutdown` follows the same shape for the objects the `Platform` owns, ending its pool block before `engine_free` on the `Platform`.
3. **Header isolation.** Nothing Objective-C appears in `api.h` or `native.h` (PLATFORM-094). The `Platform`/`PlatformWindow` struct definitions, the delegate classes, and any backend-private header live under `src/platform/macos/`. A backend-private header that names an Objective-C type is included only by `.m` files in that directory. `platform_window_get_native` returns the `NSView *` as `(PlatformNativeObject *)view`, a plain cast with no transfer of ownership (PLATFORM-072).
4. **Per-window lookup without file-scope state (PLATFORM-090, PLAT-INV-14).**
   - Each `PlatformWindow` owns one `NaylaWindowDelegate` (an `NSObject <NSWindowDelegate>`) whose only ivar is a non-owning `PlatformWindow *` back pointer. The delegate is set on its `NSWindow`, and AppKit callbacks (`windowShouldClose:`, `windowDidResize:`, focus, backing-scale changes) reach the window through `self->window`.
   - `NSEvent`s handled in `platform_pump_events` find their window through `[[event window] delegate]`, which leads to the same back pointer. That delegate may belong to a window the engine does not own (AppKit panels, menus), so the backend uses the back pointer only after `[delegate isKindOfClass:[NaylaWindowDelegate class]]` succeeds, and otherwise forwards the event to AppKit untouched. There is no global table.
   - The `Platform` owns an application delegate in the same way (back pointer to `Platform`), installs it in `platform_init`, and in `platform_shutdown` sets `[NSApp setDelegate:nil]` and releases it. A later `platform_init` installs a fresh one, while the `NSApp` singleton itself persists (PLATFORM-036).
   - Class declarations and `@implementation` blocks are not file-scope variables. `static` mutable variables stay forbidden in `.m` files exactly as in `.c` files. Class names carry the `Nayla` prefix because the Objective-C class namespace is process-wide.
5. **Main thread (PLATFORM-080, PLATFORM-082).**
   - On macOS the platform thread is the main thread, which is AppKit's own requirement. Every `THREAD_AFFINE` entry point (PLATFORM-081) must be called on it; calling one from another thread is undefined and is not detected (spec §13 item 9). The four stateless functions (`platform_time_now_ns`, `platform_thread_current_id` and their two logger adapters) stay `THREAD_SAFE` and callable from any thread with or without a `Platform` (PLATFORM-024/025): they touch no AppKit object, need no `@autoreleasepool`, and may live in a plain `.c` file in this directory. Apart from them, no backend code runs on another thread.
   - The backend runs the run loop only inside `platform_pump_events` (and implicitly inside `platform_window_create`/`destroy`), so delegate callbacks arrive on the platform thread inside a Platform API call. They may therefore use the stored allocator and logger. The backend subscribes to no notification that AppKit delivers off the main thread.
6. **CMake and the checks.**
   - `project(... C)` is unchanged. The root `CMakeLists.txt` calls `enable_language(OBJC)` at directory scope, guarded by `NAYLA_PLATFORM_BACKEND STREQUAL "macos"`, before `add_subdirectory(src)`. CMake requires this to happen in the highest directory common to every target that compiles or links Objective-C, and executables that link the backend (PLATFORM-093) link through it. Non-Apple configures never need an Objective-C compiler. The same guard covers a GPU backend's `surface_cocoa.m` (ADR-010), since `COCOA` windows exist only with this backend.
   - `OBJC_STANDARD`/`OBJC_EXTENSIONS` follow `C_STANDARD`/`C_EXTENSIONS` (CMake's default), so they are not set separately. The target uses `-Wall -Wextra -Wpedantic -fno-objc-arc`, with zero warnings in Debug and Release like every other target, and links `-framework AppKit`.
   - `forbid_direct_allocation` and `forbid_direct_output` (PLATFORM-094) use `grep -r` over the directory, which ignores file extensions, so they cover `.m` with no change. `[obj release]` does not match the `free(` pattern.
   - PLAT-INV-3(c) (no GPU SDK header in platform backends) must also match `@import` lines, and on those lines the module names themselves, submodules included: `Metal`, `QuartzCore`, `MetalKit`, `IOSurface`, matched case-insensitively as `@import[[:space:]]+(Metal|QuartzCore|MetalKit|IOSurface)\b` (so `@import QuartzCore.CAMetalLayer;` is caught too). The existing `Metal/`/`QuartzCore/` tokens match header paths only, so `@import Metal;` would otherwise get past both the directive and the token check. `@import` compiles only with clang modules (`-fmodules`), which are off by default for command-line and Makefile/Ninja builds but on by default in Xcode, so the Xcode generator would accept it silently. The backend never uses `@import` and includes headers only with `#import`.
   - PLAT-INV-14's grep scans `.m` as well as `.c`.
   - `test_c17_build` recompiles only `engine_foundation`, `engine_core`, and `engine_renderer`, and only sources matching `\.c$`. It does not cover `.m` files and must not: it checks portable C, and this backend is not portable C by design. If platform targets are ever added to that loop, the existing `\.c$` filter already skips `.m`.

## Consequences

- AppKit calls are type-checked by the compiler and read like Apple's documentation, so reviewers can check them against the headers. Review cost is that of ordinary Objective-C.
- PLATFORM-044's release order is spelled out line by line in `platform_window_destroy` and `platform_shutdown`, so a reviewer can check it, and PLAT-INV-9 can be verified with a leak-tracking allocator without counting AppKit objects.
- MRR puts retain/release correctness on the backend author. This is mitigated by the backend's small object graph (per window: `NSWindow` + delegate; per `Platform`: app delegate) and by clang's static analyzer, which understands MRR conventions.
- Follow-up tasks: `build-engineer` adds the guarded `enable_language(OBJC)`, the `engine_platform_macos` target flags, `@import` in the PLAT-INV-3(c) pattern, and `.m` coverage in the PLAT-INV-14 grep. `spec-writer` adds `@import` to the PLAT-INV-3 text and marks §11 item 2 decided by this ADR. `platform-engineer` implements the backend against Decisions 1–5.
- The contract is unaffected: `api.h`, `native.h`, core, renderer, and GPU backends do not change.

## Revisit trigger

Reopen this decision if any of the following happens:

- Apple's toolchain deprecates manual retain/release, or an AppKit API the backend needs becomes usable only under ARC or from Swift.
- A retain/release defect in this backend reaches `main` twice.
- The backend's owned Objective-C object graph grows beyond what one `destroy` function can release in a readable sequence.
