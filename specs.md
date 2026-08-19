# Portable Game Engine

## Initial Technical Specification

**Status:** Draft
**Version:** 0.1.0
**Language:** C11/C17
**Primary Goal:** Cross-platform, GPU-agnostic game engine foundation
**Document Purpose:** Architectural source of truth for documentation, implementation planning, and autonomous task generation.

---

# 1. Executive Summary

The project is a game-engine/runtime foundation written primarily in C, designed around two fundamental abstractions:

1. **Platform Abstraction** — operating-system and platform independent services.
2. **GPU Abstraction** — graphics API and GPU SDK independent rendering infrastructure.

The engine must not expose platform-specific or graphics-API-specific concepts to higher-level engine systems unless explicitly required through a controlled native-handle escape hatch.

The architecture must allow the following implementations to coexist independently:

```text
Platform:
    Windows
    Linux
    macOS
    Future: Android, iOS, WASM, etc.

Graphics:
    Vulkan
    Direct3D 12
    Metal
    Future: OpenGL, WebGPU, etc.
```

The fundamental architectural property is:

```text
Engine
   │
   ├── Platform API
   │       ├── Windows
   │       ├── Linux
   │       └── macOS
   │
   └── GPU API
           ├── Vulkan
           ├── D3D12
           └── Metal
```

Platform and GPU backends must remain independently replaceable.

The engine should ultimately be capable of running the same engine/game code without modification across multiple operating systems and graphics APIs.

---

# 2. Design Goals

## 2.1 Primary Goals

The project MUST:

* be written primarily in C;
* target C11 or later, preferably C17;
* provide a stable internal C API;
* be independent of operating-system APIs at the engine-core level;
* be independent of graphics APIs at the renderer level;
* support dynamically selectable platform and GPU backends;
* isolate platform-specific code;
* isolate GPU-API-specific code;
* provide explicit resource ownership;
* provide explicit memory allocation;
* support multithreaded systems;
* provide deterministic initialization and shutdown;
* provide automated tests for core abstractions;
* be designed so implementation tasks can be delegated to autonomous agents.

---

# 3. Non-Goals for Version 0.1

The initial version MUST NOT attempt to implement a complete commercial game engine.

The following are explicitly out of scope:

* advanced physics;
* networking;
* animation systems;
* skeletal animation;
* sophisticated material graphs;
* visual scripting;
* editor;
* scene editor;
* multiplayer framework;
* advanced audio engine;
* ray tracing abstraction;
* virtual production;
* cinematic tools;
* shader graph;
* asset marketplace;
* console support;
* mobile support.

These systems may be added later.

The initial objective is to establish a robust foundation.

---

# 4. Architectural Principles

## 4.1 Dependency Direction

Dependencies MUST flow toward lower-level abstractions.

Preferred:

```text
Game
  ↓
Engine
  ↓
Renderer
  ↓
GPU API
  ↓
GPU Backend
  ↓
Graphics SDK
```

And:

```text
Engine
  ↓
Platform API
  ↓
Platform Backend
  ↓
Operating System
```

The inverse dependency is prohibited.

For example:

```text
renderer → vulkan.h
```

is prohibited.

Instead:

```text
renderer → gpu.h
gpu_vulkan.c → vulkan.h
```

---

# 5. High-Level Architecture

```text
                         ┌──────────────────┐
                         │       GAME       │
                         └────────┬─────────┘
                                  │
                         ┌────────▼─────────┐
                         │      ENGINE      │
                         └────────┬─────────┘
                                  │
             ┌────────────────────┼────────────────────┐
             │                    │                    │
             ▼                    ▼                    ▼
        ┌─────────┐          ┌──────────┐        ┌──────────┐
        │   ECS   │          │ Renderer │        │  Assets  │
        └─────────┘          └────┬─────┘        └──────────┘
                                  │
                           ┌──────▼───────┐
                           │    GPU API   │
                           └──────┬───────┘
                                  │
                   ┌──────────────┼──────────────┐
                   │              │              │
                Vulkan          D3D12          Metal


                    ┌────────────────────┐
                    │    Platform API    │
                    └─────────┬──────────┘
                              │
                 ┌────────────┼────────────┐
                 │            │            │
              Windows       Linux        macOS
```

---

# 6. Repository Structure

The initial repository SHOULD follow this structure:

```text
engine/
├── CMakeLists.txt
├── LICENSE
├── README.md
│
├── docs/
│   ├── architecture/
│   ├── api/
│   ├── decisions/
│   └── development/
│
├── include/
│   └── engine/
│       ├── engine.h
│       ├── core.h
│       ├── platform.h
│       └── gpu.h
│
├── src/
│   ├── core/
│   ├── platform/
│   │   ├── platform.c
│   │   ├── windows/
│   │   ├── linux/
│   │   └── macos/
│   │
│   ├── gpu/
│   │   ├── gpu.c
│   │   ├── vulkan/
│   │   ├── d3d12/
│   │   └── metal/
│   │
│   ├── renderer/
│   ├── assets/
│   ├── ecs/
│   └── engine/
│
├── tests/
│   ├── unit/
│   ├── integration/
│   └── platform/
│
├── examples/
│   ├── triangle/
│   └── basic_game/
│
├── tools/
│
└── third_party/
```

The exact directory structure may evolve, but dependency boundaries MUST remain intact.

---

# 7. Build System

CMake SHOULD be the initial build system.

The build system MUST support:

```text
Windows
Linux
macOS
```

and expose configuration options for:

```text
ENGINE_BUILD_TESTS
ENGINE_BUILD_EXAMPLES
ENGINE_ENABLE_VULKAN
ENGINE_ENABLE_D3D12
ENGINE_ENABLE_METAL
ENGINE_BUILD_SHARED
ENGINE_BUILD_STATIC
```

Example:

```text
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

The build system MUST NOT require unavailable graphics SDKs for unrelated backends.

For example:

```text
Building Linux + Vulkan
```

must not require:

```text
DirectX SDK
Metal SDK
```

---

# 8. Core Runtime

The core runtime provides infrastructure shared by all engine subsystems.

Initial modules:

```text
core/
├── types
├── assert
├── logging
├── memory
├── allocator
├── strings
├── containers
├── math
├── time
├── threading
└── result/error handling
```

---

# 9. Fundamental Type System

The engine MUST define fixed-width and semantic types.

Example:

```c
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t   i8;
typedef int16_t  i16;
typedef int32_t  i32;
typedef int64_t  i64;

typedef float    f32;
typedef double   f64;

typedef bool     b8;
```

The project SHOULD avoid platform-dependent primitive assumptions.

Pointers MUST NOT be assumed to have a specific size.

---

# 10. Error Handling

The engine SHOULD avoid exceptions and global error state.

Operations SHOULD return explicit results.

Example:

```c
typedef enum EngineResult {
    ENGINE_SUCCESS = 0,
    ENGINE_ERROR_INVALID_ARGUMENT,
    ENGINE_ERROR_OUT_OF_MEMORY,
    ENGINE_ERROR_NOT_SUPPORTED,
    ENGINE_ERROR_INITIALIZATION_FAILED,
    ENGINE_ERROR_IO,
    ENGINE_ERROR_PLATFORM,
    ENGINE_ERROR_GPU,
    ENGINE_ERROR_INTERNAL
} EngineResult;
```

Where useful:

```c
typedef struct {
    EngineResult code;
    const char* message;
} EngineStatus;
```

Errors MUST be actionable and SHOULD contain subsystem context.

---

# 11. Logging

The engine MUST provide a centralized logging system.

Levels:

```text
TRACE
DEBUG
INFO
WARN
ERROR
FATAL
```

The logger SHOULD support:

* timestamps;
* subsystem names;
* thread identifiers;
* source location in debug builds;
* configurable output sinks.

Example:

```c
ENGINE_LOG_INFO("GPU", "Created device");
ENGINE_LOG_ERROR("Vulkan", "Failed to create surface");
```

---

# 12. Memory Architecture

Memory management is a first-class engine subsystem.

The engine MUST NOT assume that all allocations should use `malloc/free` directly.

Define an allocator interface:

```c
typedef struct EngineAllocator EngineAllocator;

typedef void* (*AllocatorAllocFn)(
    EngineAllocator* allocator,
    size_t size,
    size_t alignment
);

typedef void (*AllocatorFreeFn)(
    EngineAllocator* allocator,
    void* memory
);

struct EngineAllocator {
    AllocatorAllocFn alloc;
    AllocatorFreeFn free;
    void* user_data;
};
```

Future allocators:

```text
HeapAllocator
ArenaAllocator
PoolAllocator
StackAllocator
FrameAllocator
ScratchAllocator
GPUUploadAllocator
```

Ownership MUST be documented for every public resource.

---

# 13. Platform Abstraction

The platform API represents operating-system services.

It MUST hide:

* window APIs;
* event systems;
* filesystem APIs;
* threading primitives;
* timing APIs;
* dynamic library APIs;
* native synchronization;
* native handles.

The platform layer MUST NOT expose OS-specific types to the rest of the engine except through explicitly defined opaque/native-handle structures.

---

# 14. Platform Interface

Conceptual interface:

```c
typedef struct Platform Platform;
typedef struct PlatformWindow PlatformWindow;

typedef struct {
    u32 width;
    u32 height;
    const char* title;
    bool resizable;
    bool fullscreen;
} PlatformWindowDesc;
```

Window API:

```c
EngineResult platform_initialize(
    Platform* platform
);

void platform_shutdown(
    Platform* platform
);

EngineResult platform_window_create(
    Platform* platform,
    const PlatformWindowDesc* desc,
    PlatformWindow** out_window
);

void platform_window_destroy(
    PlatformWindow* window
);

void platform_poll_events(
    Platform* platform
);

bool platform_window_should_close(
    PlatformWindow* window
);
```

---

# 15. Platform Services

The Platform API SHOULD eventually provide:

```text
Window
Input
Keyboard
Mouse
Gamepad
Touch
Filesystem
Threads
Mutexes
Condition Variables
Atomics where required
Time
Dynamic Libraries
Environment Variables
Clipboard
Native Handles
```

The initial milestone only needs:

```text
Window
Events
Time
Filesystem
Dynamic Libraries
Threads
```

---

# 16. Platform Backend Contract

Each backend MUST implement the same interface.

Example:

```text
platform/
├── windows/
│   └── platform_windows.c
├── linux/
│   └── platform_linux.c
└── macos/
    └── platform_macos.c
```

The backend MUST NOT leak implementation details outside its module.

For example, code in:

```text
src/renderer/
```

MUST NOT include:

```text
windows.h
X11/Xlib.h
AppKit
Cocoa
```

---

# 17. Native Handle Boundary

Some GPU APIs require native platform objects.

The architecture MUST therefore provide a controlled native-handle abstraction.

Example:

```c
typedef enum NativeWindowType {
    NATIVE_WINDOW_UNKNOWN,
    NATIVE_WINDOW_WIN32,
    NATIVE_WINDOW_X11,
    NATIVE_WINDOW_WAYLAND,
    NATIVE_WINDOW_COCOA
} NativeWindowType;

typedef struct {
    NativeWindowType type;

    union {
        void* pointer;
        u64 integer;
    } value;
} NativeWindowHandle;
```

Higher-level engine systems SHOULD NOT use this type.

Its primary purpose is communication between:

```text
Platform API
       ↓
GPU Backend
```

---

# 18. GPU Abstraction

The GPU abstraction is the most important subsystem after Platform.

Its purpose is to provide a common GPU programming model without coupling the engine to:

```text
Vulkan
Direct3D
Metal
OpenGL
```

The GPU abstraction SHOULD model modern explicit graphics APIs.

The initial target is:

```text
Vulkan
D3D12
Metal
```

---

# 19. GPU Design Philosophy

The GPU abstraction MUST be:

* low-level enough to map efficiently to modern APIs;
* high-level enough to avoid API-specific leakage;
* explicit about resource lifetime;
* explicit about synchronization;
* explicit about command submission;
* explicit about pipeline state;
* explicit about resource usage.

The abstraction MUST NOT simply emulate one API.

In particular, it MUST NOT become:

> "Vulkan with renamed functions."

---

# 20. GPU Object Model

The initial object model:

```text
GpuInstance
GpuAdapter
GpuDevice
GpuQueue
GpuSurface
GpuSwapchain
GpuBuffer
GpuTexture
GpuTextureView
GpuSampler
GpuShader
GpuPipeline
GpuBindGroup / ResourceBinding
GpuCommandPool
GpuCommandBuffer
GpuFence
GpuSemaphore
```

Not all objects need to be public immediately.

---

# 21. Opaque GPU Objects

Public headers SHOULD expose opaque handles.

Example:

```c
typedef struct GpuDevice GpuDevice;
typedef struct GpuBuffer GpuBuffer;
typedef struct GpuTexture GpuTexture;
typedef struct GpuShader GpuShader;
typedef struct GpuPipeline GpuPipeline;
typedef struct GpuCommandBuffer GpuCommandBuffer;
```

Implementation details belong inside backend modules.

---

# 22. GPU Instance

The GPU instance represents the graphics runtime.

Conceptual API:

```c
EngineResult gpu_instance_create(
    const GpuInstanceDesc* desc,
    GpuInstance** out_instance
);

void gpu_instance_destroy(
    GpuInstance* instance
);
```

It SHOULD be responsible for:

* backend initialization;
* capability discovery;
* adapter enumeration;
* validation/debug configuration.

---

# 23. GPU Adapter

Adapters represent physical or logical GPUs.

The API SHOULD expose:

```c
typedef struct {
    char name[256];

    u32 vendor_id;
    u32 device_id;

    GpuAdapterType type;

    u64 dedicated_memory;
    u64 shared_memory;
} GpuAdapterInfo;
```

Adapter types:

```text
DISCRETE
INTEGRATED
VIRTUAL
CPU
UNKNOWN
```

---

# 24. GPU Device

The device owns GPU resources.

Conceptual API:

```c
EngineResult gpu_device_create(
    GpuAdapter* adapter,
    const GpuDeviceDesc* desc,
    GpuDevice** out_device
);

void gpu_device_destroy(
    GpuDevice* device
);
```

---

# 25. GPU Queues

The abstraction SHOULD support queue types:

```text
GRAPHICS
COMPUTE
TRANSFER
```

The backend MAY map these to the appropriate native queues.

The abstraction MUST NOT assume that every GPU exposes independent queues of every type.

Capabilities must be discoverable.

---

# 26. GPU Buffers

Buffers MUST support at minimum:

```text
Vertex
Index
Uniform
Storage
Indirect
Copy Source
Copy Destination
```

Example:

```c
typedef struct {
    u64 size;
    GpuBufferUsage usage;
    GpuMemoryType memory;
} GpuBufferDesc;
```

---

# 27. GPU Textures

Textures MUST support:

```text
1D
2D
3D
Cube
Array
Mipmaps
Multiple formats
```

Initial formats SHOULD include:

```text
RGBA8
BGRA8
RGBA16F
R16F
R32F
Depth24
Depth32F
```

The format list MUST be extensible.

---

# 28. Shaders

The shader abstraction MUST not initially depend on one source language.

Potential pipeline:

```text
Shader Source
     ↓
Shader Compiler
     ↓
Intermediate Representation
     ↓
Backend-specific representation
```

The initial implementation SHOULD choose one practical shader path, while keeping the API extensible.

Potential future support:

```text
SPIR-V
DXIL
MSL
HLSL
GLSL
WGSL
```

The engine MUST distinguish:

```text
Shader source
Shader binary
Shader reflection
Shader module
```

---

# 29. Pipeline

A graphics pipeline represents immutable or mostly immutable rendering state.

It SHOULD include:

```text
Vertex layout
Shader stages
Rasterization state
Depth/stencil state
Blend state
Primitive topology
Render target formats
```

The API MUST avoid exposing native pipeline objects.

---

# 30. Resource Binding

The abstraction MUST support binding GPU resources to shaders.

The first design SHOULD accommodate:

```text
Uniform buffers
Storage buffers
Textures
Texture views
Samplers
```

The exact abstraction may be:

```text
Descriptor Set
Bind Group
Resource Set
```

but terminology should remain API-neutral.

---

# 31. Command Recording

Command buffers SHOULD use explicit recording.

Example:

```c
gpu_cmd_begin(cmd);

gpu_cmd_begin_render_pass(cmd, &render_pass);

gpu_cmd_bind_pipeline(cmd, pipeline);

gpu_cmd_bind_resources(cmd, resources);

gpu_cmd_bind_vertex_buffer(cmd, vertex_buffer);

gpu_cmd_bind_index_buffer(cmd, index_buffer);

gpu_cmd_draw_indexed(
    cmd,
    index_count,
    1,
    0,
    0,
    0
);

gpu_cmd_end_render_pass(cmd);

gpu_cmd_end(cmd);
```

The command abstraction MUST be implementable efficiently on Vulkan, D3D12, and Metal.

---

# 32. Synchronization

Synchronization MUST be an explicit part of the GPU abstraction.

The initial model SHOULD support:

```text
Fence
Semaphore
Resource state transition
Queue synchronization
CPU/GPU synchronization
```

The design MUST account for APIs where synchronization mechanisms differ substantially.

---

# 33. Resource State

Resources SHOULD have explicit usage states.

Example conceptual states:

```text
UNDEFINED
COPY_SOURCE
COPY_DESTINATION
VERTEX_BUFFER
INDEX_BUFFER
UNIFORM_BUFFER
SHADER_RESOURCE
UNORDERED_ACCESS
RENDER_TARGET
DEPTH_STENCIL
PRESENT
```

Backend-specific states MUST remain hidden.

---

# 34. Surface and Swapchain

A surface connects the Platform layer to the GPU layer.

Architecture:

```text
PlatformWindow
      │
      ▼
NativeWindowHandle
      │
      ▼
GpuSurface
      │
      ▼
GpuSwapchain
```

The GPU backend is responsible for converting the native handle into its API-specific surface representation.

---

# 35. Renderer Layer

The renderer sits above the GPU abstraction.

It SHOULD expose concepts such as:

```text
Mesh
Material
Texture
Camera
RenderTarget
RenderPass
Scene
```

The renderer MUST NOT know whether the GPU backend is:

```text
Vulkan
D3D12
Metal
```

---

# 36. Renderer vs GPU API

The distinction MUST remain clear.

GPU API:

```text
GpuBuffer
GpuTexture
GpuPipeline
GpuCommandBuffer
```

Renderer:

```text
Mesh
Material
Model
Camera
Light
RenderObject
```

For example:

```text
Mesh
 ↓
Vertex Buffer
Index Buffer
 ↓
GPU API
```

The renderer creates and manages GPU resources but does not implement the underlying graphics API.

---

# 37. Engine Lifecycle

The engine lifecycle MUST be deterministic.

Preferred lifecycle:

```text
engine_create
    ↓
platform_initialize
    ↓
gpu_initialize
    ↓
renderer_initialize
    ↓
assets_initialize
    ↓
game_initialize
    ↓
main loop
    ↓
game_shutdown
    ↓
assets_shutdown
    ↓
renderer_shutdown
    ↓
gpu_shutdown
    ↓
platform_shutdown
    ↓
engine_destroy
```

Shutdown MUST happen in reverse dependency order.

---

# 38. Main Loop

Initial loop:

```c
while (!engine_should_exit(engine)) {

    platform_poll_events();

    engine_update(engine, delta_time);

    engine_render(engine);

    engine_present(engine);
}
```

The architecture MUST permit future separation of:

```text
Update thread
Render thread
Asset thread
Worker threads
```

without requiring a redesign of the core APIs.

---

# 39. Multithreading

The engine SHOULD use a job-system-oriented architecture eventually.

Initial requirements:

```text
Thread creation
Thread synchronization
Atomic operations
Thread-local storage
```

Future:

```text
JobSystem
WorkerPool
TaskGraph
Fiber support
```

GPU command recording SHOULD eventually support parallel recording where the backend permits it.

---

# 40. Backend Discovery

The engine SHOULD support explicit backend selection.

Example:

```c
typedef enum GpuBackendType {
    GPU_BACKEND_AUTO,
    GPU_BACKEND_VULKAN,
    GPU_BACKEND_D3D12,
    GPU_BACKEND_METAL
} GpuBackendType;
```

Configuration:

```text
AUTO
VULKAN
D3D12
METAL
```

In `AUTO`, the engine selects the best available backend.

---

# 41. Dynamic Backend Loading

The architecture SHOULD allow GPU backends to be dynamically loaded.

Conceptually:

```text
engine
   │
   ├── gpu_vulkan.dll
   ├── gpu_d3d12.dll
   └── gpu_metal.dylib
```

Each backend exports a standardized entry point:

```c
EngineResult gpu_backend_get_api(
    GpuBackendAPI* api
);
```

The exact ABI MUST be defined before dynamic loading is implemented.

---

# 42. Backend Capability System

Backends MUST expose capabilities.

Example:

```c
typedef struct {
    bool geometry_shader;
    bool tessellation;
    bool compute;
    bool indirect_draw;
    bool bindless;
    bool ray_tracing;
    bool mesh_shaders;
} GpuCapabilities;
```

Higher-level systems MUST query capabilities rather than assume availability.

---

# 43. Feature Levels

The engine SHOULD define feature levels.

Example:

```text
GPU_FEATURE_LEVEL_BASELINE
GPU_FEATURE_LEVEL_ENHANCED
GPU_FEATURE_LEVEL_MODERN
```

The exact feature matrix should be defined after Vulkan/D3D12/Metal prototypes exist.

---

# 44. Configuration

Engine configuration SHOULD be data-driven.

Example:

```c
typedef struct {
    const char* application_name;

    u32 window_width;
    u32 window_height;

    bool fullscreen;
    bool vsync;

    GpuBackendType gpu_backend;

    bool validation;
    bool debug;
} EngineConfig;
```

---

# 45. Debug and Validation

Debug builds MUST support additional validation.

GPU backends SHOULD enable native validation when available.

Examples:

```text
Vulkan Validation Layers
D3D12 Debug Layer
Metal validation/debug facilities
```

Native validation code MUST remain inside backend implementations.

---

# 46. Testing Strategy

The project MUST have multiple testing levels.

## Unit tests

Test:

```text
Math
Allocators
Containers
Strings
Handles
Resource lifetime
Configuration
Error handling
```

## Platform tests

Test:

```text
Window creation
Window destruction
Event processing
Time
Filesystem
Threading
```

## GPU tests

Test:

```text
Instance creation
Adapter enumeration
Device creation
Buffer creation
Texture creation
Shader creation
Pipeline creation
Command submission
Presentation
```

## Integration tests

The first major integration test:

```text
Create window
    ↓
Initialize GPU
    ↓
Create swapchain
    ↓
Render triangle
    ↓
Present
    ↓
Shutdown cleanly
```

This test MUST eventually run on every supported platform/backend combination.

---

# 47. First Milestone

The first complete milestone is:

> **Cross-platform triangle renderer using the platform abstraction and GPU abstraction.**

Target matrix:

| OS      | Backend |
| ------- | ------- |
| Windows | Vulkan  |
| Windows | D3D12   |
| Linux   | Vulkan  |
| macOS   | Metal   |

A successful implementation means the same renderer code renders a triangle on all supported combinations.

---

# 48. First Vertical Slice

The recommended first vertical slice is:

```text
Application
   ↓
Engine
   ↓
Platform
   ↓
Window
   ↓
GPU Instance
   ↓
Adapter
   ↓
Device
   ↓
Surface
   ↓
Swapchain
   ↓
Command Buffer
   ↓
Pipeline
   ↓
Vertex Buffer
   ↓
Triangle
   ↓
Present
```

This vertical slice should be completed before implementing large amounts of additional engine functionality.

---

# 49. Initial API Stability Rules

Public API MUST:

* use opaque objects where implementation details are not required;
* avoid exposing SDK-specific types;
* avoid exposing OS-specific types;
* define ownership explicitly;
* define thread-safety explicitly;
* define lifetime explicitly;
* use fixed-width types;
* avoid compiler-specific extensions unless isolated;
* avoid undefined behavior;
* document failure conditions.

---

# 50. Coding Standards

The project SHOULD follow:

```text
C11/C17
4-space indentation
snake_case for functions
PascalCase or explicit convention for types
UPPER_CASE for constants/macros
```

Example:

```c
GpuBuffer* gpu_buffer_create(
    GpuDevice* device,
    const GpuBufferDesc* desc
);
```

Headers MUST contain declarations only unless intentionally header-only.

Implementation-specific structures SHOULD remain in `.c` files.

---

# 51. Dependency Rules

The following dependencies are prohibited:

```text
Renderer → Vulkan
Renderer → D3D12
Renderer → Metal

Core → Platform backend

Core → GPU backend

Game → Platform backend

Game → GPU backend
```

Allowed:

```text
GPU Vulkan backend → Vulkan SDK
GPU D3D12 backend → Windows/D3D12 SDK
GPU Metal backend → Apple SDK

Platform Windows backend → Win32
Platform Linux backend → X11/Wayland/etc.
Platform macOS backend → Cocoa/Metal-related native APIs
```

---

# 52. Third-Party Dependencies

Third-party dependencies MUST be isolated.

They SHOULD live under:

```text
third_party/
```

No third-party library should leak its types into the public engine API unless explicitly approved.

Potential dependencies:

```text
Vulkan SDK
D3D12 SDK
Apple SDK
SPIR-V tooling
Shader compiler
Testing framework
```

The engine should minimize dependencies in its core.

---

# 53. Handles and Resource Identity

GPU resources SHOULD use opaque handles or opaque pointers.

Future versions SHOULD consider generation-based handles:

```c
typedef struct {
    u32 index;
    u32 generation;
} GpuBufferHandle;
```

This can prevent stale-resource access.

The decision between pointer-based and handle-based resources MUST be made before large-scale renderer implementation.

---

# 54. Resource Lifetime

Every resource MUST have a defined owner.

Example:

```text
GpuDevice
 ├── GpuBuffer
 ├── GpuTexture
 ├── GpuPipeline
 └── GpuShader
```

Destroying a parent before its children MUST either:

1. be prohibited;
2. automatically destroy children;
3. defer destruction safely.

The chosen model MUST be documented.

---

# 55. Deferred GPU Destruction

Because GPU resources can remain in use after CPU-side code stops referencing them, the engine SHOULD eventually support deferred destruction.

Concept:

```text
CPU:
    destroy(texture)

GPU:
    texture still referenced by submitted commands
```

The resource is placed in a deferred destruction queue:

```text
Frame N:
    resource marked for destruction

Frame N+K:
    GPU completed work

Frame N+K:
    resource actually destroyed
```

This should be implemented after the initial triangle renderer.

---

# 56. Render Frame Model

The renderer SHOULD eventually follow a frame lifecycle:

```text
begin_frame
    ↓
acquire_swapchain_image
    ↓
record_commands
    ↓
submit
    ↓
present
    ↓
end_frame
```

Multiple frames in flight SHOULD be supported.

Initial target:

```text
2 or 3 frames in flight
```

---

# 57. Future Render Architecture

The initial renderer can be immediate/explicit.

A future renderer SHOULD evolve toward:

```text
Scene
 ↓
Render Graph
 ↓
GPU Commands
```

A Render Graph is explicitly out of scope for the first milestone but MUST remain architecturally possible.

---

# 58. ECS

An ECS subsystem is planned but not part of the first graphics milestone.

Future architecture:

```text
World
 ├── Entities
 ├── Components
 └── Systems
```

Renderer systems consume ECS data without depending on GPU backend implementations.

---

# 59. Asset System

Future asset architecture:

```text
Source Asset
     ↓
Importer
     ↓
Engine Asset
     ↓
Serialized Asset
     ↓
Runtime Asset
     ↓
GPU Resource
```

The asset system MUST distinguish CPU-side assets from GPU resources.

---

# 60. Documentation Architecture

Documentation SHOULD be generated from this specification and split into:

```text
docs/
├── architecture/
│   ├── overview.md
│   ├── platform.md
│   ├── gpu.md
│   ├── renderer.md
│   └── memory.md
│
├── api/
│   ├── core.md
│   ├── platform.md
│   ├── gpu.md
│   └── renderer.md
│
├── development/
│   ├── building.md
│   ├── testing.md
│   ├── backend-development.md
│   └── contributing.md
│
└── decisions/
    ├── ADR-001-language.md
    ├── ADR-002-platform-abstraction.md
    └── ADR-003-gpu-abstraction.md
```

---

# 61. Architecture Decision Records

Significant architectural decisions MUST be recorded as ADRs.

Each ADR SHOULD contain:

```text
Context
Problem
Options considered
Decision
Consequences
Status
```

Examples:

```text
ADR-001: C as implementation language
ADR-002: CMake as build system
ADR-003: Platform abstraction
ADR-004: GPU abstraction
ADR-005: Opaque GPU resources
ADR-006: Dynamic backend loading
ADR-007: Shader representation
ADR-008: Resource lifetime model
```

---

# 62. Agent-Oriented Development Model

The project is explicitly designed to be implemented by AI/software agents.

Every implementation task MUST be independently understandable.

A task SHOULD contain:

```text
ID
Title
Objective
Context
Dependencies
Files to create
Files to modify
Requirements
API contract
Acceptance criteria
Tests
Non-goals
Potential risks
```

Agents MUST NOT be expected to infer architectural decisions from unrelated code.

---

# 63. Task Granularity

Tasks SHOULD be small enough that an agent can complete them without modifying unrelated subsystems.

Good:

```text
PLATFORM-001
Implement platform-independent window descriptor.
```

Good:

```text
GPU-014
Implement Vulkan adapter enumeration.
```

Bad:

```text
Implement the renderer.
```

The latter MUST be decomposed.

---

# 64. Initial Epic Structure

The initial project SHOULD be decomposed into these epics:

```text
E01 — Repository and Build
E02 — Core Types
E03 — Logging and Diagnostics
E04 — Memory and Allocators
E05 — Platform Abstraction
E06 — Windows Backend
E07 — Linux Backend
E08 — macOS Backend
E09 — GPU Abstraction
E10 — Vulkan Backend
E11 — D3D12 Backend
E12 — Metal Backend
E13 — Renderer
E14 — Shader Infrastructure
E15 — Testing Infrastructure
E16 — Example Applications
E17 — Documentation
E18 — CI/CD
```

---

# 65. Initial Task Breakdown

## E01 — Repository and Build

### BUILD-001

Create repository structure.

Acceptance criteria:

* directory structure exists;
* CMake root project builds;
* source and test targets exist;
* no platform-specific code is required for core compilation.

### BUILD-002

Create CMake platform detection.

Acceptance criteria:

* detects Windows;
* detects Linux;
* detects macOS;
* exposes platform compile definitions only to relevant modules.

### BUILD-003

Create test target.

Acceptance criteria:

* tests compile;
* tests execute through CTest;
* CI-compatible exit codes exist.

---

# 66. Core Tasks

### CORE-001 — Fundamental Types

Implement:

```text
u8/u16/u32/u64
i8/i16/i32/i64
f32/f64
```

Acceptance:

* compiles on all target platforms;
* static assertions validate expected sizes.

### CORE-002 — Result System

Implement engine result/error types.

Acceptance:

* all core operations can return explicit failure;
* no global error state is required.

### CORE-003 — Assertions

Implement:

```text
ENGINE_ASSERT
ENGINE_VERIFY
ENGINE_UNREACHABLE
```

with debug/release semantics.

### CORE-004 — Logging

Implement initial logging system.

### CORE-005 — Allocator Interface

Implement allocator abstraction.

### CORE-006 — Default Heap Allocator

Implement `malloc/free` based allocator behind the abstraction.

---

# 67. Platform Tasks

### PLATFORM-001 — Platform Interface

Define:

```text
Platform
PlatformWindow
PlatformWindowDesc
NativeWindowHandle
```

No platform-specific implementation.

### PLATFORM-002 — Platform Lifecycle

Implement initialization/shutdown contracts.

### PLATFORM-003 — Window Lifecycle

Define window creation/destruction semantics.

### PLATFORM-004 — Event System

Define platform-neutral events.

### PLATFORM-005 — Time

Define monotonic engine timer.

### PLATFORM-006 — Filesystem

Define platform-independent file API.

### PLATFORM-007 — Dynamic Libraries

Define runtime library loading API.

### PLATFORM-008 — Threads

Define platform-neutral thread abstraction.

---

# 68. Windows Backend Tasks

### WIN-001

Implement Win32 platform initialization.

### WIN-002

Implement Win32 window creation.

### WIN-003

Implement Win32 event translation.

### WIN-004

Implement Win32 native window handle.

### WIN-005

Implement Win32 filesystem.

### WIN-006

Implement Win32 dynamic library loading.

### WIN-007

Implement Win32 thread primitives.

---

# 69. Linux Backend Tasks

Linux backend architecture MUST first define the windowing strategy.

Potential options:

```text
X11
Wayland
SDL-like abstraction
```

The chosen approach MUST be recorded as an ADR.

Then implement:

```text
LINUX-001 Platform initialization
LINUX-002 Window creation
LINUX-003 Event processing
LINUX-004 Native handles
LINUX-005 Filesystem
LINUX-006 Dynamic libraries
LINUX-007 Threads
```

---

# 70. macOS Backend Tasks

Implement:

```text
MAC-001 Platform initialization
MAC-002 Window creation
MAC-003 Event processing
MAC-004 Native handles
MAC-005 Filesystem
MAC-006 Dynamic libraries
MAC-007 Threads
```

Native Apple APIs MUST remain isolated in the macOS backend.

---

# 71. GPU API Tasks

### GPU-001 — Define GPU Types

Define:

```text
GpuInstance
GpuAdapter
GpuDevice
GpuQueue
GpuSurface
GpuSwapchain
GpuBuffer
GpuTexture
GpuShader
GpuPipeline
GpuCommandBuffer
GpuFence
```

### GPU-002 — Backend Interface

Define `GpuBackendAPI`.

### GPU-003 — Capabilities

Define capability discovery.

### GPU-004 — Adapter Enumeration

Define adapter discovery.

### GPU-005 — Device Creation

Define device lifecycle.

### GPU-006 — Resource Descriptors

Define buffer/texture/shader/pipeline descriptors.

### GPU-007 — Command API

Define command recording.

### GPU-008 — Synchronization API

Define GPU synchronization.

### GPU-009 — Surface API

Define Platform ↔ GPU boundary.

---

# 72. Vulkan Backend Tasks

### VK-001

Create Vulkan backend module.

### VK-002

Create Vulkan instance.

### VK-003

Enumerate physical devices.

### VK-004

Select adapter.

### VK-005

Create logical device.

### VK-006

Create queues.

### VK-007

Create platform surface.

### VK-008

Create swapchain.

### VK-009

Implement buffers.

### VK-010

Implement textures.

### VK-011

Implement shaders.

### VK-012

Implement pipeline.

### VK-013

Implement command buffers.

### VK-014

Implement synchronization.

### VK-015

Implement presentation.

### VK-016

Implement Vulkan validation/debug support.

---

# 73. D3D12 Backend Tasks

Equivalent task sequence:

```text
DX12-001 Backend
DX12-002 Adapter enumeration
DX12-003 Device
DX12-004 Queues
DX12-005 Swapchain
DX12-006 Buffers
DX12-007 Textures
DX12-008 Shaders
DX12-009 Pipeline
DX12-010 Command buffers
DX12-011 Synchronization
DX12-012 Presentation
DX12-013 Debug layer
```

---

# 74. Metal Backend Tasks

Equivalent sequence:

```text
METAL-001 Backend
METAL-002 Adapter/device
METAL-003 Surface
METAL-004 Swapchain/presentation
METAL-005 Buffers
METAL-006 Textures
METAL-007 Shaders
METAL-008 Pipeline
METAL-009 Command buffers
METAL-010 Synchronization
METAL-011 Presentation
METAL-012 Debug support
```

---

# 75. Renderer Tasks

After the GPU abstraction has passed backend validation:

```text
RENDER-001 Renderer lifecycle
RENDER-002 Vertex layout
RENDER-003 Mesh abstraction
RENDER-004 Material abstraction
RENDER-005 Camera
RENDER-006 Render target
RENDER-007 Basic forward renderer
RENDER-008 Frame lifecycle
RENDER-009 Swapchain rendering
RENDER-010 Triangle example
```

---

# 76. Definition of Done

A task is considered complete only when:

* implementation exists;
* API is documented;
* code compiles;
* relevant tests pass;
* no known memory leaks are introduced;
* no undefined behavior is knowingly introduced;
* platform/backend boundaries remain intact;
* acceptance criteria are satisfied;
* changes are limited to the task scope.

---

# 77. Agent Implementation Contract

An implementation agent MUST:

1. Read the relevant architecture documentation.
2. Read all dependencies of the task.
3. Inspect existing interfaces before modifying them.
4. Avoid changing public contracts without justification.
5. Implement only the requested scope.
6. Add tests where applicable.
7. Build the affected targets.
8. Run relevant tests.
9. Report failures explicitly.
10. Document architectural decisions discovered during implementation.

Agents MUST NOT silently redesign the architecture.

---

# 78. Agent Validation Contract

After implementation, an agent SHOULD report:

```text
Task:
Status:

Files changed:

Implementation summary:

Tests added:

Tests executed:

Build result:

Known limitations:

Architectural concerns:

Follow-up tasks:
```

---

# 79. CI Matrix

The eventual CI matrix SHOULD include:

```text
Windows + MSVC
Linux + GCC
Linux + Clang
macOS + Apple Clang
```

Graphics validation:

```text
Windows:
    Vulkan
    D3D12

Linux:
    Vulkan

macOS:
    Metal
```

GPU hardware availability may require separate hardware runners.

---

# 80. Quality Requirements

The engine MUST prioritize:

1. correctness;
2. portability;
3. deterministic behavior;
4. explicit ownership;
5. debuggability;
6. performance;
7. API stability.

Performance optimizations MUST NOT compromise backend portability without documented justification.

---

# 81. Performance Requirements

The architecture SHOULD avoid:

```text
per-draw heap allocations
unnecessary virtual dispatch
unnecessary string processing
backend API calls from high-level systems
global locks in hot paths
```

The GPU abstraction SHOULD allow efficient translation to native command APIs.

The engine MUST NOT introduce an abstraction layer that requires excessive CPU work for every GPU command.

---

# 82. ABI Considerations

If dynamic backend loading is implemented, ABI stability becomes a first-class requirement.

The backend API MUST include:

```text
API version
struct size
feature flags
```

Example:

```c
typedef struct {
    u32 api_version;
    u32 struct_size;
    u64 features;
} GpuBackendInfo;
```

This allows future versions to evolve without immediately breaking older backends.

---

# 83. Versioning

The engine SHOULD use semantic versioning:

```text
MAJOR.MINOR.PATCH
```

Example:

```text
0.1.0
0.2.0
1.0.0
```

Before 1.0, breaking API changes are permitted but MUST be documented.

---

# 84. Security and Robustness

The engine MUST:

* validate public API parameters;
* avoid unchecked buffer operations;
* validate resource sizes;
* detect invalid handles where possible;
* avoid use-after-free;
* avoid double destruction;
* avoid undefined behavior;
* handle allocation failures.

GPU backend code MUST validate native API return codes.

---

# 85. Future Architecture

The architecture should leave room for:

```text
ECS
Job System
Render Graph
Asset Pipeline
Animation
Physics
Audio
Networking
Editor
Scripting
Hot Reload
GPU-driven rendering
Bindless resources
Mesh shaders
Ray tracing
Virtual textures
World streaming
```

These features MUST be implemented above the foundational abstractions whenever possible.

---

# 86. Long-Term Target Architecture

The long-term architecture is:

```text
                         GAME
                           │
                           ▼
                    ┌─────────────┐
                    │    ENGINE   │
                    └──────┬──────┘
                           │
        ┌──────────────────┼───────────────────┐
        │                  │                   │
       ECS              RENDERER              AUDIO
        │                  │                   │
        │            ┌─────▼─────┐             │
        │            │ RENDER    │             │
        │            │ GRAPH     │             │
        │            └─────┬─────┘             │
        │                  │                   │
        └──────────────────┼───────────────────┘
                           │
                     ┌─────▼─────┐
                     │  GPU API  │
                     └─────┬─────┘
                           │
             ┌─────────────┼─────────────┐
             │             │             │
          Vulkan         D3D12         Metal
             │             │             │
             └─────────────┼─────────────┘
                           │
                         GPU


                     PLATFORM API
                           │
             ┌─────────────┼─────────────┐
             │             │             │
          Windows        Linux         macOS
```

---

# 87. Critical Architectural Invariants

The following invariants MUST remain true throughout development.

### Invariant 1

The renderer cannot depend directly on a graphics SDK.

### Invariant 2

The engine core cannot depend directly on an operating system.

### Invariant 3

Platform and GPU backends are independently replaceable.

### Invariant 4

GPU resources have explicit ownership and lifetime.

### Invariant 5

Platform-specific native handles cross the abstraction boundary only through explicitly defined interfaces.

### Invariant 6

Backend-specific capabilities are discoverable rather than assumed.

### Invariant 7

Public APIs do not expose third-party SDK types.

### Invariant 8

Every backend can be tested independently.

### Invariant 9

Adding a new platform should primarily require implementing a Platform backend.

### Invariant 10

Adding a new GPU API should primarily require implementing a GPU backend.

---

# 88. Definition of Architectural Success

The architecture is considered successful when the following is possible:

```text
Same game code
      │
      ▼
Same engine code
      │
      ├───────────────┐
      ▼               ▼
Windows             Linux
      │               │
      ▼               ▼
 Vulkan             Vulkan
      │               │
      ▼               ▼
    GPU             GPU
```

and:

```text
Same renderer
      │
      ├── Vulkan
      ├── D3D12
      └── Metal
```

without modifications to the renderer itself.

The ultimate test is:

> **A new platform or GPU API can be added primarily by implementing its backend rather than modifying the engine architecture.**

---

# 89. Immediate Next Steps

The recommended implementation order is:

```text
1. Repository + CMake
2. Core types
3. Error system
4. Logging
5. Allocator
6. Platform interface
7. One platform backend
8. GPU interface
9. Vulkan backend
10. Triangle
11. Second platform
12. D3D12 backend
13. Metal backend
14. Renderer abstraction
15. Asset system
16. ECS
17. Job system
18. Render graph
```

The project SHOULD resist the temptation to implement large high-level systems before the Platform/GPU architecture has been validated.

---

# 90. First Proof of Architecture

The first meaningful proof of the architecture is not:

> "The engine can render a triangle."

It is:

> "The same renderer implementation can render a triangle through multiple GPU backends and multiple operating-system backends without changing renderer code."

That distinction is fundamental to the project.

The initial engineering objective should therefore be to prove the abstraction boundaries as early as possible.

---

# 91. Recommended Initial Agent Workflow

The project can eventually be operated as a task graph:

```text
SPEC
 │
 ▼
ARCHITECTURE
 │
 ▼
EPICS
 │
 ▼
TASKS
 │
 ▼
AGENT IMPLEMENTATION
 │
 ▼
BUILD
 │
 ▼
TEST
 │
 ▼
CODE REVIEW AGENT
 │
 ▼
ARCHITECTURE VALIDATION
 │
 ▼
MERGE
```

Each task should produce machine-readable metadata that allows the next agent to understand:

```text
what was requested
what was implemented
what changed
what remains
what failed
```

This enables the project to evolve from a manually developed engine into an **AI-orchestrated software factory**.

---

# 92. Initial Project Success Criteria

Version `0.1.0` is complete when:

* [ ] CMake project builds successfully.
* [ ] Core type system exists.
* [ ] Error system exists.
* [ ] Logging exists.
* [ ] Allocator abstraction exists.
* [ ] Platform API exists.
* [ ] At least two platform backends exist.
* [ ] GPU API exists.
* [ ] Vulkan backend exists.
* [ ] At least one additional GPU backend exists.
* [ ] Surface/swapchain abstraction works.
* [ ] Command recording works.
* [ ] Vertex/index buffers work.
* [ ] Shader/pipeline abstraction works.
* [ ] Triangle renders successfully.
* [ ] Renderer does not include GPU SDK headers.
* [ ] Renderer does not include OS headers.
* [ ] Backend-specific types do not leak into public APIs.
* [ ] Unit tests execute successfully.
* [ ] Integration tests execute successfully.
* [ ] Documentation describes all public interfaces.
* [ ] CI builds supported configurations.
* [ ] A new backend can be implemented without architectural changes.

---

# 93. Final Architectural Statement

The project is not primarily defined by its renderer, ECS, physics engine, or asset pipeline.

Its defining characteristic is its **portable systems architecture**.

The fundamental contract is:

```text
                 ENGINE
                   │
        ┌──────────┴──────────┐
        │                     │
   PLATFORM API            GPU API
        │                     │
   ┌────┼────┐          ┌─────┼─────┐
   │    │    │          │     │     │
 Win  Linux Mac       Vulkan DX12  Metal
```

Every subsystem built above these contracts should remain independent from their implementations.

The architecture should optimize for:

```text
Replaceability
Portability
Explicitness
Testability
Performance
Agent-friendly development
```

The first implementation target is therefore not "a complete game engine".

It is:

> **A small, rigorously designed portable runtime capable of opening a native window and rendering through an API-independent GPU interface, with enough architectural discipline that additional platforms and graphics APIs can be implemented as isolated backends.**
