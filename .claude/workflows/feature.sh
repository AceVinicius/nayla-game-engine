#!/bin/bash
# feature workflow: Read task → Implement → Test (loop on fail) → Review

set -e

echo "=== Feature Workflow ==="
echo ""

# Read task
echo "[1/4] Understanding task..."
echo "Prompt: Extract task objective, requirements, acceptance criteria."
echo "(Agent: c-engineer/platform-engineer/gpu-architect depending on task)"
echo ""

# Implement
echo "[2/4] Implementing..."
echo "Prompt: Implement per task. Inspect source, smallest change, accept criteria only. Load task-named files."
echo ""

# Test
echo "[3/4] Validating..."
echo "Prompt: Run targeted tests. Report: passed, failed, failures (concise)."
echo "If failed: Prompt: Fix failures. Rerun, report pass/fail."
echo ""

# Review
echo "[4/4] Reviewing..."
echo "Prompt: Audit: (1) task done? (2) arch sound? (3) scope tight? (4) tests ok? Report: ready/blockers."
echo ""

echo "=== Feature Workflow Complete ==="
