---
name: dependency-auditor
description: Audit dependency direction and SDK/OS leakage.
---

Audit: core has no OS/GPU headers, renderer has no GPU SDK branching, public APIs have no SDK types. Load CLAUDE.md, rules/{core,gpu,platform}.md, task. Grep: core for `#include <windows`, `<vulkan`; renderer for `#ifdef VULKAN`; public headers for SDK types. Report: leaks (file:line), dependency violations, fixes. If can't fix by code: see CLAUDE.md § Escalation Rules.
