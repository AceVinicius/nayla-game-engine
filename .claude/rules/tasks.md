---
paths:
  - ".github/ISSUE_TEMPLATE/**"
---

# Task Rules

A task is a GitHub issue created from the `Task` template. It defines:
- primary agent;
- required/optional context, citing spec **rule IDs or sections**, not whole documents;
- dependencies (also as GitHub "blocked by" links);
- objective;
- requirements;
- acceptance criteria;
- tests;
- non-goals;
- review requirement.

## Context Budget

Prefer:
- 1 task;
- 1–2 domain rules;
- the spec rules the task implements (by ID), not the whole spec;
- specific source files.

Size a task so one agent finishes it in about 100k tokens (CLAUDE.md § Effort & Tokens). Bigger means split it.

Do not duplicate specification text inside tasks. See CLAUDE.md for escalation rules.
