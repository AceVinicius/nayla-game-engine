# Platform Rules

- OS headers stay inside platform backends.
- Common Platform API contains behavior, not one OS's implementation.
- Backends share the same contract.
- Native handles cross boundaries only through explicit interfaces.
- Platform-specific behavior must not leak into core or renderer.
