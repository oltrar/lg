#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Guest-only, scoped permissions for ONE IVSHMEM BAR2. Never runs capture as root.
set -euo pipefail
ADDRESS=''
TARGET_USER=${SUDO_USER:-}
PERSIST=0
AUTO=0
while (($#)); do
  case "$1" in
    --device) ADDRESS=${2:?Missing PCI address}; shift 2 ;;
    --user) TARGET_USER=${2:?Missing user}; shift 2 ;;
    --install) PERSIST=1; shift ;;
    -h|--help)
      printf 'Usage: sudo bash setup-ivshmem.sh --user USER [--device DOMAIN:BUS:SLOT.FUNCTION] [--install]\n'
      exit 0 ;;
    *) printf 'Unknown argument: %s\n' "$1" >&2; exit 1 ;;
  esac
done
[[ $EUID == 0 ]] || { printf 'Run this guest setup script with sudo.\n' >&2; exit 1; }
[[ $TARGET_USER =~ ^[a-zA-Z_][a-zA-Z0-9_.-]*\$?$ && $TARGET_USER != root ]] || {
  printf 'Specify the non-root guest desktop account with --user USER.\n' >&2; exit 1;
}
UID_NUMBER=$(id -u "$TARGET_USER")
GID_NUMBER=$(id -g "$TARGET_USER")
((UID_NUMBER > 0)) || { printf 'The desktop account must not have UID 0.\n' >&2; exit 1; }
ROOT=/sys/bus/pci/devices
if [[ -z $ADDRESS ]]; then
  AUTO=1
  devices=()
  for path in "$ROOT"/*; do
    [[ -f $path/vendor && -f $path/device ]] || continue
    [[ $(<"$path/vendor") == 0x1af4 && $(<"$path/device") == 0x1110 ]] || continue
    devices+=("${path##*/}")
  done
  ((${#devices[@]} == 1)) || {
    printf 'Found %s IVSHMEM devices; specify the desired one with --device BDF.\n' "${#devices[@]}" >&2
    exit 1
  }
  ADDRESS=${devices[0]}
fi
[[ $ADDRESS =~ ^[[:xdigit:]]{4}:[[:xdigit:]]{2}:[01][[:xdigit:]]\.[0-7]$ ]] || {
  printf 'Invalid PCI address.\n' >&2; exit 1;
}
DEVICE="$ROOT/$ADDRESS"
[[ $(<"$DEVICE/vendor") == 0x1af4 && $(<"$DEVICE/device") == 0x1110 ]] || {
  printf 'Refusing a device that is not IVSHMEM (1af4:1110).\n' >&2; exit 1;
}
[[ ! -L $DEVICE/driver ]] || {
  printf 'A driver is bound to %s. Use its /dev/kvmfrN explicitly, or unbind ONLY this IVSHMEM device first.\n' "$ADDRESS" >&2
  exit 1
}
[[ -e $DEVICE/resource2 ]] || { printf 'IVSHMEM BAR2 is missing.\n' >&2; exit 1; }
if [[ $(<"$DEVICE/enable") == 0 ]]; then
  printf '1\n' > "$DEVICE/enable"
fi
# sysfs has no POSIX ACL support. Scope ownership to this device's memory BAR.
chown "$UID_NUMBER:$GID_NUMBER" "$DEVICE/resource2"
chmod 0600 "$DEVICE/resource2"
printf 'Guest %s can map %s/resource2; no KVMFR module is required.\n' "$TARGET_USER" "$DEVICE"

if ((PERSIST)); then
  DEST=/usr/local/libexec/lg-ivshmem-permissions
  RULE=/etc/udev/rules.d/70-lg-ivshmem.rules
  # Install a root-owned helper, rather than running a user-writable project
  # script as root from udev. Auto mode re-discovers the unique IVSHMEM device
  # after topology changes (e.g. removing QXL). Explicit --device stays pinned.
  install -Dm755 -- "${BASH_SOURCE[0]}" "$DEST"
  if ((AUTO)); then
    printf 'ACTION=="add", SUBSYSTEM=="pci", ATTR{vendor}=="0x1af4", ATTR{device}=="0x1110", RUN+="%s --user %s"\n' \
      "$DEST" "$TARGET_USER" > "$RULE"
  else
    printf 'ACTION=="add", SUBSYSTEM=="pci", KERNEL=="%s", ATTR{vendor}=="0x1af4", ATTR{device}=="0x1110", RUN+="%s --device %%k --user %s"\n' \
      "$ADDRESS" "$DEST" "$TARGET_USER" > "$RULE"
  fi
  chmod 0644 "$RULE"
  udevadm control --reload-rules
  printf 'Installed persistent permissions (automatic unique-device discovery: %s).\n' "$AUTO"
fi
