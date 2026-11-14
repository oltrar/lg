#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$ROOT/versions.env"
for tool in git cmake ninja pkg-config cc gio desktop-file-validate systemd-analyze wayland-scanner; do
  command -v "$tool" >/dev/null || { printf 'Missing tool: %s\n' "$tool" >&2; exit 1; }
done
pkg-config --exists gio-2.0 gio-unix-2.0 libpipewire-0.3 libspa-0.2 wayland-client wayland-server || {
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
# B7's configure-time checks require all pinned submodules even for host-only builds.
git -C "$SRC" submodule update --init --recursive

bash "$ROOT/scripts/apply-overlay.sh" "$ROOT" "$SRC"

cmake -S "$SRC/host" -B "$ROOT/_build/host" -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DUSE_XCB=OFF -DUSE_PIPEWIRE=ON -DENABLE_BACKTRACE=OFF
cmake --build "$ROOT/_build/host" --parallel
LG_WITH_BACKEND_TEST=1 bash "$ROOT/test.sh"
"${CC:-cc}" -std=gnu11 -D_GNU_SOURCE -Wall -Werror -O2 \
  -ffunction-sections -fdata-sections -Wl,--gc-sections \
  -I "$SRC/common/include" -I "$SRC" -I "$SRC/repos/LGMP/lgmp/include" \
  "$ROOT/tools/frame-check.c" \
  "$ROOT/overlay/common/src/platform/linux/ivshmem.c" \
  "$ROOT/overlay/common/src/platform/linux/ivshmem_pci.c" \
  "$SRC/repos/LGMP/lgmp/src/client.c" "$SRC/repos/LGMP/lgmp/src/status.c" \
  -o "$ROOT/_build/lg-frame-check"
source "$ROOT/scripts/build-info.sh"
lgRecordBuild "$ROOT" "$ROOT/_build/host/looking-glass-host" "$LG_COMMIT"
printf '\nBuilt: %s\nRun from your guest Wayland desktop with: bash run-guest.sh\n' \
  "$ROOT/_build/host/looking-glass-host"
