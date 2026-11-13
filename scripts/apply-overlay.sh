#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Update only upstream originals or files known to be managed by this project.
set -euo pipefail
ROOT=${1:?Missing project root}
SRC=${2:?Missing upstream source root}
STATE="$ROOT/_build/applied-overlays"
mkdir -p "$ROOT/_build"
MANIFEST=$(mktemp "$ROOT/_build/applied-overlays.XXXXXX")
trap 'rm -f -- "$MANIFEST"' EXIT

known_overlay() {
  local actual=$1 relative=$2 revision expected
  if [[ -f $STATE ]] && grep -Fxq -- "$actual $relative" "$STATE"; then
    return 0
  fi
  # Migration from older builds that did not record managed hashes: only
  # accept an exact overlay version in THIS project's checked-out history.
  [[ -e $ROOT/.git ]] || return 1
  while IFS= read -r revision; do
    expected=$(git -C "$ROOT" rev-parse --verify "$revision:overlay/$relative" 2>/dev/null || true)
    [[ $actual != "$expected" ]] || return 0
  done < <(git -C "$ROOT" log --format=%H -- "overlay/$relative")
  return 1
}

sources=()
destinations=()
while IFS= read -r -d '' overlay; do
  relative=${overlay#"$ROOT/overlay/"}
  destination="$SRC/$relative"
  if [[ -e $destination ]] && ! cmp -s "$overlay" "$destination"; then
    expected=$(git -C "$SRC" rev-parse --verify "HEAD:$relative" 2>/dev/null || true)
    actual=$(git -C "$SRC" hash-object --no-filters "$destination")
    if [[ -z $expected || $actual != "$expected" ]] && ! known_overlay "$actual" "$relative"; then
      printf 'Preserving modified file: %s\nUse a separate project copy for a clean build, or preserve your edits before updating.\n' "$destination" >&2
      exit 1
    fi
  fi
  sources+=("$overlay")
  destinations+=("$destination")
  printf '%s %s\n' "$(git -C "$SRC" hash-object --no-filters "$overlay")" "$relative" >> "$MANIFEST"
done < <(find "$ROOT/overlay" -type f -print0)

# Preflight every file before modifying any source.
for index in "${!sources[@]}"; do
  mkdir -p -- "$(dirname -- "${destinations[index]}")"
  cp -- "${sources[index]}" "${destinations[index]}"
done
mv -f -- "$MANIFEST" "$STATE"
