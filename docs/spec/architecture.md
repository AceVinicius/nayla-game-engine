# Architecture

## Core Concept

The game engine has **two hard boundaries** that ensure portability and clean separation of concerns:

```
Core (C code, no OS/GPU deps) ↔ Platform API ↔ OS Backends (Windows/Mac/Linux)
                           ↔ GPU API ↔ Graphics Backends (Vulkan/D3D12/Metal)
Renderer (GPU API only, no SDK types)
```

## Invariants

1. **Core**: Portable C code with no OS or GPU SDK dependencies.
2. **Renderer**: Uses only GPU API; no direct Vulkan/D3D12/Metal branching.
3. **Public APIs**: Expose only engine-defined types, never SDK types.
4. **Backends**: Opaque to core and renderer.
5. **Ownership**: Explicit and passed through APIs.
6. **Capabilities**: Queried, never assumed.
7. **Architecture Changes**: Require ADRs; no implementation hacks.

## Subsystems

### Core
- **Location**: `src/core/`
- **Language**: C17/C23
- **Dependencies**: None (no OS/GPU)
- **Responsibility**: Engine logic, frame timing, event handling
- **Output**: `libengine_core.a`

### Platform API
- **Location**: `src/platform/`
- **Language**: C17/C23 (abstract interface)
- **Backends**: Windows, macOS, Linux (separate implementations)
- **Responsibility**: OS abstraction (window, input, threading)
- **Output**: `libplatform_api.a` (defined by contract, implemented by backends)

### GPU API
- **Location**: `src/gpu/`
- **Language**: C17/C23 (abstract interface)
- **Backends**: Vulkan, D3D12, Metal (separate implementations)
- **Responsibility**: GPU abstraction (buffers, shaders, rendering)
- **Output**: `libgpu_api.a` (defined by contract, implemented by backends)

### Renderer
- **Location**: `src/renderer/` (TBD)
- **Language**: C17/C23
- **Dependencies**: GPU API only (no SDK types)
- **Responsibility**: High-level rendering (meshes, materials, scene graph)
- **Output**: `librenderer.a`

## Dependency Direction

```
main → core → [APIs]
            ← [Backends]

Renderer → GPU API → GPU Backends
Core → Platform API → Platform Backends
```

No circular dependencies. Backends do NOT depend on each other.
