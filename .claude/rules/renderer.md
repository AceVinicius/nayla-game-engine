---
paths:
  - "src/renderer/**"
  - "tests/contract/test_renderer*"
---

# Renderer Rules

- Renderer uses GPU API, never a concrete GPU SDK.
- No backend-native types.
- No Vulkan/D3D12/Metal branching in renderer code.
- Renderer owns higher-level concepts; GPU API owns GPU primitives.
- Resource lifetime is explicit.
- Switching GPU backends must not require renderer changes.
