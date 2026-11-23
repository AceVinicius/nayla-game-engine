# Tooling Rules

Use `tools/factory` for routine operations; it keeps output short:
- `tools/factory check`: configure, build and test Debug and Release, warnings as errors; one summary line per build type, failures only.
- `tools/factory check --ref <branch>`: the same gate on a temporary worktree of another branch or commit (reviewers); the worktree is removed afterwards.
- `tools/factory toc <spec.md>`: headings with line numbers and the rule IDs each section defines.
- `tools/factory rule <ID>...`: print just those rule definitions (`**<ID>` paragraphs in `docs/`) with file:line. Use instead of reading a whole spec.
- `tools/factory prove <file> <patch-cmd> <test-regex>`: prove a check bites (back up, inject, rebuild, run, restore, verify).
- `tools/factory build {debug|release}`, `tools/factory test {unit|contract|integration|architecture|all}`.
- `tools/factory pr-wait <pr>`: wait for CI and Copilot on a PR; print only failures and review comments.
- `tools/factory issue <n>`: issue title, state and body (no comments). Use instead of `gh issue view`.
- `tools/factory pr-comments <pr>`: unresolved, non-outdated review threads with path:line and comment id.
- `tools/factory status`: build directories, generator, branch.

Build failures print diagnostics only (compiler messages and build-time check `ERROR:` blocks, repo-relative paths), never compiler command lines. To search code use Grep; to read a file use Read with a line range.

Builds use Ninja when it is installed (exact timestamps). Under the Makefile generator, timestamps have 1-second resolution: an edit and revert within a second can leave stale binaries, so `prove` rebuilds cleanly under Makefiles and touches the restored file so every generator rebuilds it.

Never dump entire diffs, logs, large files, or generated directories.
