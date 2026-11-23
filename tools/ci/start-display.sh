#!/usr/bin/env bash
# Starts Xvfb on :99 and exports DISPLAY to later workflow steps (ADR-012
# Decision 5, PLATFORM-101). Fails if the display does not answer within ~5s.
set -eu
Xvfb :99 -screen 0 1280x720x24 >"$RUNNER_TEMP/xvfb.log" 2>&1 &
echo "DISPLAY=:99" >> "$GITHUB_ENV"
for _ in $(seq 50); do
  DISPLAY=:99 xdpyinfo >/dev/null 2>&1 && exit 0
  sleep 0.1
done
echo "Xvfb did not come up on :99" >&2
exit 1
