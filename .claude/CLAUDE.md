# Portable Game Engine

C17/C23 portable game engine.

## Architecture

Two hard boundaries:
- Platform API → OS backends
- GPU API → graphics API backends

Core and renderer MUST NOT depend on concrete OS or GPU SDKs.

## Invariants

1. Core: no OS dependencies.
2. Renderer: no GPU SDK dependencies.
3. Public APIs: no third-party SDK types.
4. Backend details stay behind interfaces/opaque types.
5. Ownership/lifetime is explicit.
6. Capabilities are queried, not assumed.
7. Architecture changes require an ADR.
8. Do not solve architectural problems with implementation hacks.

## Agents

Architecture: `architect`, `gpu-architect`, `spec-writer`, `task-planner`
Implementation: `c-engineer`, `platform-engineer`, `gpu-backend-engineer`, `renderer-engineer`, `build-engineer`
Validation: `test-engineer`, `dependency-auditor`, `performance-engineer`, `reviewer`

Use the most specialized agent.

## Context Rule

Load only:
1. this file;
2. assigned agent;
3. task;
4. rules explicitly referenced by task/agent;
5. required specifications/files.

Do NOT load unrelated repository documentation.

## Source of Truth

- Architecture/specification → `docs/`
- Work definition → `tasks/`
- Current behavior → source/tests
- Architectural decisions → ADRs

Prefer references to copied context.

## Escalation

- **Architecture ambiguity** → `architect`
- **GPU abstraction ambiguity** → `gpu-architect`
- **Specification ambiguity** → `spec-writer`
- **Task decomposition ambiguity** → `task-planner`

## Workflow

Specification → Architecture → Task → Implementation → Tests → Audit → Review

## Token & Cost Discipline

- Default agent: Haiku (fast). Escalate to Opus for synthesis/review.
- Per-task budget: ~30k tokens. Escalate if exceeded.
- Workflows: Use `pipeline()` by default; `parallel()` only when all results are needed together.
- Prefer `agent(schema:)` for structured output instead of prose summaries.
- Read files in chunks for large files; avoid loading entire diffs or logs.
