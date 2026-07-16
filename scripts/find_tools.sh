#!/usr/bin/env bash
set -euo pipefail

find_tool() {
  local name="$1"
  if command -v "$name" >/dev/null 2>&1; then command -v "$name"; return; fi
  local candidate
  for candidate in "$HOME"/Library/Android/sdk/cmake/*/bin/"$name"; do
    [[ -x "$candidate" ]] && { echo "$candidate"; return; }
  done
  return 1
}
