#!/usr/bin/env bash
set -euo pipefail

readonly maximum_lines=400
readonly source_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)

status=0
check_file()
{
  local line_count
  line_count=$(wc -l < "$1")
  if ((line_count > maximum_lines)); then
    printf 'error: %s has %s lines (maximum: %s)\n' "$1" "$line_count" "$maximum_lines" >&2
    status=1
  fi
}

while IFS= read -r -d '' path; do
  check_file "$path"
done < <(find "$source_root/src/sources" -type f \
  \( -name '*.inc' -o -name '*.cpp' -o -name '*.hpp' \) -print0)

while IFS= read -r -d '' path; do
  printf 'error: source implementation must be grouped in a subdirectory: %s\n' "$path" >&2
  status=1
done < <(find "$source_root/src/sources" -maxdepth 1 -type f \
  \( -name '*.inc' -o -name '*.cpp' -o -name '*.hpp' \) -print0)

if ((status == 0)); then
  printf 'Source modules are grouped and at or below %s lines.\n' "$maximum_lines"
fi

exit "$status"
