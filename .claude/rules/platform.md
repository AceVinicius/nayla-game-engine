# Platform Rules

- OS headers stay inside platform backends, plus one surface file per native window type in each GPU backend (`src/gpu/<backend>/surface_<type>.{c,m}`, ADR-010).
- Common Platform API contains behavior, not one OS's implementation.
- Backends share the same contract.
- Native handles cross boundaries only through explicit interfaces.
- Platform-specific behavior must not leak into core or renderer.
