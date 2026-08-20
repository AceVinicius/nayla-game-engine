# Tooling Rules

Prefer:
- `tools/factory status`
- `tools/factory build {debug|release}`
- `tools/factory test {unit|contract|integration|architecture|all}`
- `tools/factory search <pattern>`
- `tools/factory inspect <path>`

Prefer concise output. Do not dump entire diffs, logs, large files, or generated directories.

The tooling layer may use RTK, Caveman, delta, ripgrep, or plain shell. Agents depend only on the `factory` contract.
