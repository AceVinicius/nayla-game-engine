# Documentation Navigation

- `docs/spec/architecture.md` → condensed architecture (points to CLAUDE.md for invariants)
- `docs/spec/specs.md` → full draft technical specification
- `docs/spec/core/` → per-subsystem contracts (types, result, allocator, logging, platform)
- `docs/adr/` → architectural decisions

Agent context loading and tooling: `.claude/CLAUDE.md` and `.claude/rules/tooling.md`.

Load only the documents referenced by the task; read specs by rule ID (`tools/factory toc`, `tools/factory rule`).
