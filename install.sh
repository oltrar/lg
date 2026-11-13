#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Build and register THIS checkout. Dependency/IVSHMEM setup stays explicit.
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
[[ $# == 0 ]] || { printf 'Usage: bash install.sh\n' >&2; exit 1; }
((EUID != 0)) || { printf 'Run without sudo in the guest Wayland desktop.\n' >&2; exit 1; }
[[ ${XDG_SESSION_TYPE:-} == wayland && -n ${WAYLAND_DISPLAY:-} &&
   -n ${DBUS_SESSION_BUS_ADDRESS:-} && -n ${XDG_RUNTIME_DIR:-} ]] || {
  printf 'Run inside the logged-in guest KDE/GNOME Wayland desktop.\n' >&2; exit 1;
}
printf 'Installing checkout: %s\nStop any separately launched manual capture first.\n' "$ROOT"
bash "$ROOT/setup-startup.sh" stop
bash "$ROOT/build.sh"
bash "$ROOT/setup-startup.sh" repair
bash "$ROOT/setup-startup.sh" start
bash "$ROOT/version.sh"
printf '\nCapture startup was requested. Use bash setup-startup.sh status or the saved capture report to verify video.\n'
