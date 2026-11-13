#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
BINARY=${LG_BINARY:-"$ROOT/_build/host/looking-glass-host"}
DEVICE=${LG_SHM:-ivshmem}
FPS=${LG_FPS:-60}
[[ ${XDG_SESSION_TYPE:-} == wayland ]] || {
  printf 'Run this in a terminal inside the guest KDE/GNOME Wayland session.\n' >&2; exit 1;
}
[[ -n ${DBUS_SESSION_BUS_ADDRESS:-} ]] || {
  printf 'No desktop session bus; launch from the guest desktop, without sudo.\n' >&2; exit 1;
}
[[ $FPS =~ ^[0-9]{1,3}$ ]] && ((10#$FPS >= 1 && 10#$FPS <= 360)) || {
  printf 'LG_FPS must be an integer from 1 to 360.\n' >&2; exit 1;
}
[[ -x "$BINARY" ]] || { printf 'Build first: bash build.sh\n' >&2; exit 1; }
# The application discovers/validates PCI BAR2 for ivshmem[:BDF]. An explicit
# /dev/kvmfrN or file remains supported; no KVMFR device is required by default.
if [[ $DEVICE != ivshmem && $DEVICE != ivshmem:* ]]; then
  [[ -r "$DEVICE" && -w "$DEVICE" ]] || {
    printf 'Shared-memory device/file is missing or inaccessible: %s\n' "$DEVICE" >&2
    exit 1
  }
fi
exec "$BINARY" "app:shmFile=$DEVICE" app:capture=pipewire "pipewire:fps=$FPS" "$@"
