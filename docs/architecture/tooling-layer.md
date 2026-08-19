# Tooling Layer Architecture

```text
Agent
  ↓
tools/factory
  ↓
git / cmake / ctest / rg / shell
```

Purpose:
1. reduce tool-output tokens;
2. provide stable agent-facing commands;
3. hide optional RTK/Caveman integration;
4. keep local and CI workflows deterministic.

Agents depend on `factory`, not on a specific optimizer.

Future extensions:
- JSON output;
- token/line budgets;
- changed-file summaries;
- compiler-error extraction;
- test-failure extraction;
- task-state operations;
- automatic context manifests.
