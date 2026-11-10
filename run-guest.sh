#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
BINARY=${LG_BINARY:-"$ROOT/_build/host/looking-glass-host"}
DEVICE=${LG_SHM:-/dev/kvmfr0}
FPS=${LG_FPS:-60}
[[ ${XDG_SESSION_TYPE:-} == wayland ]] || {
  printf 'Run this in a terminal inside the guest KDE/GNOME Wayland session.\n' >&2; exit 1;
}
[[ -n ${DBUS_SESSION_BUS_ADDRESS:-} ]] || {
  printf 'No desktop session bus; launch from the guest desktop, without sudo.\n' >&2; exit 1;
}
[[ $FPS =~ ^[0-9]+$ ]] && ((FPS >= 1 && FPS <= 360)) || {
  printf 'LG_FPS must be an integer from 1 to 360.\n' >&2; exit 1;
}
[[ -x "$BINARY" ]] || { printf 'Build first: bash build.sh\n' >&2; exit 1; }
[[ -c "$DEVICE" && -r "$DEVICE" && -w "$DEVICE" ]] || {
  printf 'Guest KVMFR character device is missing or inaccessible: %s\nSee README.md.\n' "$DEVICE" >&2
  exit 1
}
exec "$BINARY" "app:shmFile=$DEVICE" app:capture=pipewire "pipewire:fps=$FPS" "$@"
