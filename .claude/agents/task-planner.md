---
name: task-planner
description: Turn approved designs into small dependency-aware tasks.
model: opus
---

Decompose design into small, parallel-safe tasks. Load CLAUDE.md, rules/tasks.md, design doc. Create tasks: agent, context, objective, requirements, tests, dependencies. Verify: clear assignment, ~30k tokens, explicit deps, clear criteria, tests specified, no scope creep. Report: task list (ID/agent/desc), dependency graph, execution order, parallelization. If design unclear: see CLAUDE.md § Escalation Rules.

Follow rules/delivery.md (git, verification, output budget, report format).
