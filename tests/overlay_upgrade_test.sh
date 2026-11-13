#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mkdir -p "$ROOT/_build/tests"
FIXTURE=$(mktemp -d "$ROOT/_build/tests/overlay-upgrade.XXXXXX")
PROJECT="$FIXTURE/project"
SRC="$FIXTURE/upstream"
mkdir -p "$PROJECT/overlay" "$SRC"
git -C "$SRC" init -q
printf 'upstream\n' > "$SRC/managed.c"
git -C "$SRC" add managed.c
git -C "$SRC" -c user.name=Test -c user.email=test@example.invalid commit -qm upstream
git -C "$PROJECT" init -q
printf '_build/\n' > "$PROJECT/.gitignore"
printf 'version one\n' > "$PROJECT/overlay/managed.c"
git -C "$PROJECT" add .
git -C "$PROJECT" -c user.name=Test -c user.email=test@example.invalid commit -qm v1
bash "$ROOT/scripts/apply-overlay.sh" "$PROJECT" "$SRC"
cmp "$PROJECT/overlay/managed.c" "$SRC/managed.c"
printf 'version two\n' > "$PROJECT/overlay/managed.c"
git -C "$PROJECT" add .
git -C "$PROJECT" -c user.name=Test -c user.email=test@example.invalid commit -qm v2
bash "$ROOT/scripts/apply-overlay.sh" "$PROJECT" "$SRC"
cmp "$PROJECT/overlay/managed.c" "$SRC/managed.c"

# Migrate a pre-manifest build by recognizing an exact prior overlay in Git.
rm -- "$PROJECT/_build/applied-overlays"
printf 'version one\n' > "$SRC/managed.c"
bash "$ROOT/scripts/apply-overlay.sh" "$PROJECT" "$SRC"
cmp "$PROJECT/overlay/managed.c" "$SRC/managed.c"

# A local edit must be preserved and the update must fail before copying.
printf 'my local edits\n' > "$SRC/managed.c"
if bash "$ROOT/scripts/apply-overlay.sh" "$PROJECT" "$SRC"; then
  printf 'FAIL: unexpected overwrite of a local source edit\n' >&2
  exit 1
fi
[[ $(<"$SRC/managed.c") == 'my local edits' ]]
printf 'PASS: overlay first apply, managed upgrade, history migration and local-edit preservation\n'
