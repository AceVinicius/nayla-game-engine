#!/bin/bash
# Fail if a forbidden pattern is found under a source directory.
# Usage: forbid-pattern.sh <target_name> <description> <pattern> <source_dir>
set -e

if [ "$#" -ne 4 ]; then
  echo "ERROR: forbid-pattern.sh expects 4 args (target_name description pattern source_dir), got $#" >&2
  exit 2
fi

target_name="$1"
description="$2"
pattern="$3"
source_dir="$4"

if [ -z "$pattern" ] || [ ! -d "$source_dir" ]; then
  echo "ERROR: forbid-pattern.sh got an empty pattern or missing source_dir ('$source_dir') for $target_name" >&2
  exit 2
fi

if grep -rq -- "$pattern" "$source_dir" 2>/dev/null; then
  echo "ERROR: $description found in $target_name"
  exit 1
fi

exit 0
