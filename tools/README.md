# Tooling Layer

Agent-facing interface for repository operations.

Goals:
- minimize tool-output tokens;
- prefer semantic summaries over raw output;
- keep agents independent of RTK/Caveman;
- fall back to standard tools when accelerators are unavailable.

Agents should prefer `tools/factory` for status, diff, build, test, search, inspection, and task lookup.
