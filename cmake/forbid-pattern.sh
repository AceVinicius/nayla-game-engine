#!/bin/bash
# Fail if a forbidden pattern is found under a source directory.
# Usage: forbid-pattern.sh <target_name> <description> <pattern> <source_dir> [exempt_basename_regex] [icase]
#   pattern                extended regex (grep -E)
#   exempt_basename_regex  optional bash ERE matched against each file's basename;
#                          a matching file is skipped. "" means no exemption.
#   icase                  "1" matches case-insensitively; anything else is case-sensitive.
# Exit: 0 no match, 1 a match in a non-exempt file, 2 usage or grep error.
# A grep error (invalid regex, unreadable source) is exit 2, never a pass.

if [ "$#" -lt 4 ] || [ "$#" -gt 6 ]; then
  echo "ERROR: forbid-pattern.sh expects 4 to 6 args (target_name description pattern source_dir [exempt_basename_regex] [icase]), got $#" >&2
  exit 2
fi

target_name="$1"
description="$2"
pattern="$3"
source_dir="$4"
exempt_re="${5:-}"
icase="${6:-}"

if [ -z "$pattern" ] || [ ! -d "$source_dir" ]; then
  echo "ERROR: forbid-pattern.sh got an empty pattern or missing source_dir ('$source_dir') for $target_name" >&2
  exit 2
fi

list_flags=-rlE
line_flags=-HnE
if [ "$icase" = 1 ]; then
  list_flags=-rliE
  line_flags=-HniE
fi

matches=$(grep $list_flags -- "$pattern" "$source_dir")
status=$?
if [ "$status" -gt 1 ]; then
  echo "ERROR: grep failed (exit $status) while checking '$description' for $target_name: invalid pattern or unreadable source" >&2
  exit 2
fi

violations=0
while IFS= read -r file; do
  [ -z "$file" ] && continue
  base="${file##*/}"
  if [ -n "$exempt_re" ] && [[ $base =~ $exempt_re ]]; then
    continue
  fi
  if [ "$violations" -eq 0 ]; then
    echo "ERROR: $description found in $target_name"
  fi
  grep $line_flags -- "$pattern" "$file"
  violations=$((violations + 1))
done <<< "$matches"

[ "$violations" -eq 0 ]
