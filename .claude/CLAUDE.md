# Portable Game Engine

C17/C23 portable game engine with hard architectural boundaries between core, renderer, and platform/GPU backends.

## Core Invariants

1. **Core**: no OS or GPU SDK headers.
2. **Renderer**: no GPU SDK branching (Vulkan/D3D12/Metal).
3. **Public APIs**: only engine-defined types, never SDK types.
4. **Backend details**: opaque to core and renderer; isolation enforced.
5. **Ownership/lifetime**: explicit; no hidden global state.
6. **Capabilities**: queried, never assumed; graceful degradation.
7. **Architecture changes**: require ADRs; no implementation hacks.

## Architecture

**Two hard boundaries:**

```
Core (C code, no OS/GPU deps) ↔ Platform API ↔ OS Backends (Windows/Mac/Linux)
                           ↔ GPU API ↔ Graphics Backends (Vulkan/D3D12/Metal)
Renderer (GPU API only, no SDK types)
```

All subsystems are portable; switching backends MUST NOT require core/renderer changes.

## Agents & Specialization

**Architecture layer** (`architect`, `gpu-architect`, `spec-writer`, `task-planner`)
- Design decisions, cross-subsystem impacts, contract specification, task decomposition

**Implementation layer** (`c-engineer`, `platform-engineer`, `gpu-backend-engineer`, `renderer-engineer`, `build-engineer`)
- Code, builds, backends; follow contracts without redesigning

**Validation layer** (`test-engineer`, `dependency-auditor`, `performance-engineer`, `reviewer`)
- Tests, dependency direction, performance, final audit

**→ Always use the most specialized agent for the task.**

## Context Loading

Load only:
1. This file
2. Assigned agent definition
3. Assigned task
4. Rules explicitly referenced by task/agent
5. Required specs/source files

**Do NOT load unrelated docs** — narrow context is a feature.

## Escalation Rules

Raise immediately when:
- **Architecture ambiguity** (2+ valid designs) → `architect`
- **GPU abstraction gap** (can't fit contract) → `gpu-architect`
- **Specification ambiguity** (contract unclear) → `spec-writer`
- **Task decomposition unclear** (how to slice) → `task-planner`

## Workflow Stages

```
Specification (define) → Architecture (design) → Tasks (decompose) 
  → Implementation (build) → Tests (validate) → Audit (check) → Review (approve)
```

Independent tasks run in parallel (one agent per task, each in its own worktree); dependent ones run in order.

## Effort & Tokens

- **Models**: implementers (`*-engineer`, `test-engineer`) use Sonnet. Design, specs and review (`architect`, `gpu-architect`, `spec-writer`, `task-planner`, `reviewer`) use Opus. Mechanical audits (`dependency-auditor`) use Haiku. First-pass correctness costs less than fix rounds.
- **Per-task budget**: ~100k tokens for an implementation task, ~40k for a review. Past that, stop and report what is left; usually the task needs splitting.
- **Delivery**: every agent follows `rules/delivery.md` (git, verification, output budget, report format). Briefs add only what is task-specific.
- **Large files**: read by rule ID, section or line range; never load full diffs, logs or whole specs.

## Git Workflow

- `main` is protected: always green, changes only through PRs.
- One branch per task: `task/NNN-short-name` (NNN = GitHub issue number). Chores: `chore/...`.
- Create the issue first (`task-planner`, issue template `task`); the PR body says `Closes #N`.
- Before opening a PR: `tools/factory check` (Debug + Release, zero warnings) and a `reviewer` pass.
- CI must pass. Squash-merge; the branch is deleted on merge.

## Source of Truth

- Spec/architecture → `docs/`
- Work definition → GitHub issues
- Current behavior → source + tests
- Decisions → ADRs
