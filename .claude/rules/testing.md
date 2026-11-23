---
paths:
  - "tests/**"
---

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

## Writing tests

- Use `tests/check.h`: `CHECK`, `CHECK_OK`, `EXPECT` (exact result code), `TestCase` table + `RUN_TESTS`. Never copy `printf`/`return 1` blocks.
- No `assert()`: Release defines `NDEBUG` and compiles it away, so the test passes vacuously.
- Architecture checks are rows in `tests/architecture/test_core_invariants.c`, not new blocks.
- A check must be proven: inject a real violation, confirm it fails, revert. A check that cannot fail is worse than none.
- A test must show its subject ran (e.g. assert the injected allocator was used), so it cannot pass vacuously.
- Run `tools/factory test all` and `ctest --test-dir cmake-build-release`: `factory` runs debug only.
