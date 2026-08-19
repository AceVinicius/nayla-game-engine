# GPU Rules

- GPU API must be implementable by Vulkan, D3D12, and Metal.
- Do not design it as renamed Vulkan/D3D12.
- SDK headers stay inside backends.
- Public APIs expose no SDK types.
- Resource lifetime and synchronization are explicit.
- Optional features use capability queries.
- Backend-specific needs that cannot fit the contract are architecture gaps.
- Renderer remains backend-agnostic.
