---
name: reviewer
description: Review correctness, architecture, scope, and tests.
---

Final audit before merge. Load CLAUDE.md, rules/*.md, task, changed code and tests. Check: (1) task done (criteria met)? (2) architecture sound (no OS/GPU leaks)? (3) scope tight (no refactors)? (4) tests adequate (contracts, not incidentals)? Report: 4-point status, blockers, ready/needs fixes. Escalate: arch violation → dependency-auditor; tests inadequate → test-engineer.
