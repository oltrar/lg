#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Build, configure permissions, select a monitor, and verify actual raw video.
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
[[ $# == 0 || ( $# == 1 && $1 == --skip-device-setup ) ]] || {
  printf 'Usage: bash install.sh [--skip-device-setup]\n' >&2; exit 1;
}
((EUID != 0)) || { printf 'Run without sudo in the guest Wayland desktop.\n' >&2; exit 1; }
[[ ${XDG_SESSION_TYPE:-} == wayland && -n ${WAYLAND_DISPLAY:-} &&
   -n ${DBUS_SESSION_BUS_ADDRESS:-} && -n ${XDG_RUNTIME_DIR:-} ]] || {
  printf 'Run inside the logged-in guest KDE/GNOME Wayland desktop.\n' >&2; exit 1;
}
printf 'Installing checkout: %s\nStop any separately launched manual capture first.\n' "$ROOT"
bash "$ROOT/scripts/ensure-deps.sh"
bash "$ROOT/setup-startup.sh" stop
bash "$ROOT/build.sh"
if [[ ${1:-} != --skip-device-setup ]]; then
  # Migrate the older BDF-pinned rule too, so removing QXL cannot invalidate
  # permissions for the sole auto-discovered IVSHMEM device at the next boot.
  sudo bash "$ROOT/setup-ivshmem.sh" --user "$(id -un)" --install
fi
bash "$ROOT/setup-startup.sh" repair
CONFIG=${XDG_CONFIG_HOME:-"$HOME/.config"}/looking-glass-linux-wayland
desktop=${XDG_CURRENT_DESKTOP:-}
if [[ ${desktop^^} == *KDE* ]]; then
  bash "$ROOT/scripts/select-display.sh"
else
  (umask 077; : > "$CONFIG/choose-next")
  printf '\nChoose the NVIDIA/dummy-plug monitor and allow restoring this selection in the sharing dialog.\n'
fi
bash "$ROOT/setup-startup.sh" start
if ! systemctl --user start looking-glass-host.service; then
  printf '\nSetup could not verify raw video. Recent service output:\n' >&2
  journalctl --user -u looking-glass-host.service -n 60 --no-pager >&2 || true
  exit 1
fi
bash "$ROOT/version.sh"
printf '\nSetup complete: raw video verified and login startup enabled. Open your B7 client on the physical host.\n'
