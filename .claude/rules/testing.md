# Testing Rules

Test contracts, not incidental implementation details.

Layers:
- core unit
- API contract
- backend
- integration
- architecture/dependency

Critical checks:
- core has no OS includes;
- renderer has no GPU SDK includes;
- public headers expose no SDK types;
- backend selection is configurable.

Report unavailable platforms/hardware; do not weaken tests.
