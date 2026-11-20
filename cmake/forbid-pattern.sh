#!/bin/bash
# Fail if a forbidden pattern is found under a source directory.
# Usage: forbid-pattern.sh <target_name> <description> <pattern> <source_dir> [exempt_basename_regex]
#   pattern                extended regex (grep -E), matched case-insensitively
#   exempt_basename_regex  optional bash ERE matched against each file's basename;
#                          a matching file is skipped. "" or omitted means no exemption.
# Exits 1 and lists each offending line when a non-exempt file matches.

if [ "$#" -lt 4 ] || [ "$#" -gt 5 ]; then
  echo "ERROR: forbid-pattern.sh expects 4 or 5 args (target_name description pattern source_dir [exempt_basename_regex]), got $#" >&2
  exit 2
fi

target_name="$1"
description="$2"
pattern="$3"
source_dir="$4"
exempt_re="$5"

if [ -z "$pattern" ] || [ ! -d "$source_dir" ]; then
  echo "ERROR: forbid-pattern.sh got an empty pattern or missing source_dir ('$source_dir') for $target_name" >&2
  exit 2
fi

violations=0
while IFS= read -r file; do
  base="${file##*/}"
  if [ -n "$exempt_re" ] && [[ $base =~ $exempt_re ]]; then
    continue
  fi
  if [ "$violations" -eq 0 ]; then
    echo "ERROR: $description found in $target_name"
  fi
  grep -HniE -- "$pattern" "$file"
  violations=$((violations + 1))
done < <(grep -rliE -- "$pattern" "$source_dir" 2>/dev/null)

[ "$violations" -eq 0 ]
