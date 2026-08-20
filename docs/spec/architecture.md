# Architecture

For the two hard boundaries and the seven Core Invariants, see `.claude/CLAUDE.md` — not duplicated here.

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
