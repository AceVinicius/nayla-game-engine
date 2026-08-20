#!/bin/bash
# bugfix workflow: Analyze → Repro → Root Cause → Fix + Regression → Validate

set -e

echo "=== Bugfix Workflow ==="
echo ""

# Analysis
echo "[1/5] Analyzing..."
echo "Prompt: Extract: broken, subsystem, observed vs expected."
echo ""

# Repro
echo "[2/5] Reproducing..."
echo "Prompt: Write smallest test. Run, confirm fails, report test + failure."
echo ""

# Root Cause
echo "[3/5] Finding root cause..."
echo "Prompt: Trace: Load rules/subsystem. Find root (not symptom). Report: file:line, cause, why."
echo ""

# Fix
echo "[4/5] Fixing..."
echo "Prompt: Fix root cause. Minimal (no refactor). Apply, verify repro passes."
echo "Then: Write regression test catching bug, add to suite."
echo ""

# Validate
echo "[5/5] Validating..."
echo "Prompt: Run tests. Report: passed, failed, failures (concise)."
echo ""

echo "=== Bugfix Workflow Complete ==="
