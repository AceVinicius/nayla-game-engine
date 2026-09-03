#!/bin/bash
# Fail if a forbidden pattern is found under a source directory.
# Usage: forbid-pattern.sh <target_name> <description> <pattern> <source_dir> [exempt_filename]
# If exempt_filename is provided, grep results matching that filename are ignored.

if [ "$#" -lt 4 ] || [ "$#" -gt 5 ]; then
  echo "ERROR: forbid-pattern.sh expects 4 or 5 args (target_name description pattern source_dir [exempt_filename]), got $#" >&2
  exit 2
fi

target_name="$1"
description="$2"
pattern="$3"
source_dir="$4"
exempt_filename="$5"

if [ -z "$pattern" ] || [ ! -d "$source_dir" ]; then
  echo "ERROR: forbid-pattern.sh got an empty pattern or missing source_dir ('$source_dir') for $target_name" >&2
  exit 2
fi

# Search for the pattern
if [ -n "$exempt_filename" ]; then
  # Exempt a specific filename: filter results that include that basename
  # grep -r outputs "path/to/file.c:..." so we check if the line starts with path containing exempt_filename
  grep -r -- "$pattern" "$source_dir" 2>/dev/null | grep -v "/$exempt_filename:" | grep -v "^$exempt_filename:" | grep -q .
  result=$?
  if [ $result -eq 0 ]; then
    echo "ERROR: $description found in $target_name"
    exit 1
  fi
else
  # No exemption: any match is an error
  if grep -rq -- "$pattern" "$source_dir" 2>/dev/null; then
    echo "ERROR: $description found in $target_name"
    exit 1
  fi
fi

exit 0
