#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$ROOT/versions.env"
for tool in git cmake ninja pkg-config cc; do
  command -v "$tool" >/dev/null || { printf 'Missing tool: %s\n' "$tool" >&2; exit 1; }
done
pkg-config --exists gio-2.0 gio-unix-2.0 libpipewire-0.3 libspa-0.2 || {
  printf 'Install glib2 and pipewire development files; see README.md\n' >&2; exit 1;
}
SRC="$ROOT/_upstream/LookingGlass"
mkdir -p "$ROOT/_upstream"
if [[ ! -d "$SRC/.git" ]]; then
  git -c core.autocrlf=false clone --depth 1 --branch "$LG_TAG" \
    https://github.com/gnif/LookingGlass.git "$SRC"
fi
[[ $(git -C "$SRC" rev-parse HEAD) == "$LG_COMMIT" ]] || {
  printf 'Unexpected upstream revision; use the pinned B7 source.\n' >&2; exit 1;
}
git -C "$SRC" submodule update --init --recursive repos/LGMP

# Preserve edits that are neither the pinned upstream nor this overlay.
while IFS= read -r -d '' overlay; do
  relative=${overlay#"$ROOT/overlay/"}
  destination="$SRC/$relative"
  if [[ -e "$destination" ]] && ! cmp -s "$overlay" "$destination"; then
    expected=$(git -C "$SRC" rev-parse --verify "HEAD:$relative" 2>/dev/null || true)
    actual=$(git -C "$SRC" hash-object --no-filters "$destination")
    [[ -n "$expected" && "$actual" == "$expected" ]] || {
      printf 'Preserving modified file: %s\nUse a separate project copy for a clean build.\n' "$destination" >&2
      exit 1
    }
  fi
  mkdir -p -- "$(dirname -- "$destination")"
  cp -- "$overlay" "$destination"
done < <(find "$ROOT/overlay" -type f -print0)

LG_WITH_BACKEND_TEST=1 bash "$ROOT/test.sh"
cmake -S "$SRC/host" -B "$ROOT/_build/host" -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DUSE_XCB=OFF -DUSE_PIPEWIRE=ON -DENABLE_BACKTRACE=OFF
cmake --build "$ROOT/_build/host" --parallel
printf '\nBuilt: %s\nRun from your guest Wayland desktop with: bash run-guest.sh\n' \
  "$ROOT/_build/host/looking-glass-host"
