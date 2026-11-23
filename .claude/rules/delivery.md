# Delivery Rules

How every implementation, test, build, spec and architecture agent works and reports. The orchestrator's brief adds only what is specific to the task.

## Git
- Branch from current `main`: `task/NNN-short-name` (NNN = issue), or `chore/short-name`.
- Commit body ends with `Closes #NNN` (first commit) or `Refs #NNN` (follow-ups), then `Co-Authored-By: Claude <your actual model> <noreply@anthropic.com>`.
- Never push, open a PR, or comment on GitHub. The orchestrator does that.
- Never revert with `git checkout`/`git restore` while you have uncommitted work: back up the file and restore the copy, or use `tools/factory prove`.

## Verify
- Before reporting, self-check every class in `rules/defects.md` that applies to your change. It lists what review keeps finding.
- `tools/factory check`: Debug and Release, warnings as errors, full ctest. Zero warnings is the bar.
- Every new or changed check is proven: inject a real violation, see it fail, restore. Use `tools/factory prove`; it backs up, rebuilds cleanly and confirms the restore.
- Report what you could not verify (another OS, MSVC, GCC, no display) explicitly. Never claim it passed.

## Output budget
- Never dump build logs, full diffs or whole files into context. Pipe through `grep -E 'warning|error'`, `tail`, or read line ranges.
- Read specs by rule ID or section, not end to end, unless the task is a spec review: `tools/factory toc <spec>`, then `tools/factory rule <ID>…`.

## Report (final message, in this order)
1. Branch and commit hash.
2. Files changed, one line each.
3. Test results: Debug and Release counts, warnings.
4. Proofs: each injection and the failure it produced.
5. Not verified, and why.
6. Open questions or deviations from the brief.

## Escalate, don't guess
If the spec, ADR or brief is ambiguous or cannot be met, stop and report it (CLAUDE.md § Escalation Rules). Never redesign silently.
