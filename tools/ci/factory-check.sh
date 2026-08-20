#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
chmod +x tools/factory
tools/factory status
echo "FACTORY TOOLING CHECK: PASS"
