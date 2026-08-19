# Tooling Rules

Prefer:
- `tools/factory status`
- `tools/factory diff`
- `tools/factory build <preset>`
- `tools/factory test <preset>`
- `tools/factory search <pattern>`
- `tools/factory inspect <path>`
- `tools/factory task <id>`

Prefer concise output. Do not dump entire diffs, logs, large files, or generated directories.

The tooling layer may use RTK, Caveman, delta, ripgrep, or plain shell. Agents depend only on the `factory` contract.
