#!/usr/bin/env bash
set -euo pipefail

repository_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
target_root=${CODEX_HOME:-"$HOME/.codex"}/skills

mkdir -p "$target_root"
for skill in "$repository_root"/skills/*; do
  test -d "$skill" || continue
  ln -sfn "$skill" "$target_root/$(basename "$skill")"
done

printf 'Installed repository skills in %s\n' "$target_root"
