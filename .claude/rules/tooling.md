# Tooling Rules

Use `tools/factory` for routine operations; it keeps output short:
- `tools/factory check`: configure, build and test Debug and Release, warnings as errors; one summary line per build type, failures only.
- `tools/factory prove <file> <patch-cmd> <test-regex>`: prove a check bites (back up, inject, rebuild, run, restore, verify).
- `tools/factory build {debug|release}`, `tools/factory test {unit|contract|integration|architecture|all}`.
- `tools/factory pr-wait <pr>`: wait for CI and Copilot on a PR; print only failures and review comments.
- `tools/factory status`, `search <pattern>`, `inspect <path>`.

Builds use Ninja when it is installed (exact timestamps). Under the Makefile generator, timestamps have 1-second resolution: an edit and revert within a second can leave stale binaries, so `prove` rebuilds cleanly under Makefiles and touches the restored file so every generator rebuilds it.

Never dump entire diffs, logs, large files, or generated directories.
