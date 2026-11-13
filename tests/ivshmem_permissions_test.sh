#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if ((EUID != 0)); then printf 'SKIP: root-only permissions fixture (runs in CI containers)\n'; exit; fi
FIXTURE=$(mktemp -d)
trap 'rm -rf -- "$FIXTURE"' EXIT
mkdir -p "$FIXTURE/pci/0000:09:02.0" "$FIXTURE/bin"
# Test an isolated copy of the real helper, replacing only privileged paths.
sed -e "s|ROOT=/sys/bus/pci/devices|ROOT=$FIXTURE/pci|" \
    -e "s|DEST=/usr/local/libexec/lg-ivshmem-permissions|DEST=$FIXTURE/installed|" \
    -e "s|RULE=/etc/udev/rules.d/70-lg-ivshmem.rules|RULE=$FIXTURE/rule|" \
    "$ROOT/setup-ivshmem.sh" > "$FIXTURE/setup.sh"
printf '#!/bin/sh\nexit 0\n' > "$FIXTURE/bin/udevadm"
chmod +x "$FIXTURE/bin/udevadm"
export PATH="$FIXTURE/bin:$PATH"
DEVICE="$FIXTURE/pci/0000:09:02.0"
printf '0x1af4\n' > "$DEVICE/vendor"; printf '0x1110\n' > "$DEVICE/device"
printf '0\n' > "$DEVICE/enable"; : > "$DEVICE/resource2"
bash "$FIXTURE/setup.sh" --user nobody --install
! grep -q 'KERNEL==' "$FIXTURE/rule"
[[ $(stat -c %a "$DEVICE/resource2") == 600 && $(stat -c %u "$DEVICE/resource2") == "$(id -u nobody)" ]]
[[ $(<"$DEVICE/enable") == 1 ]]
# QXL removal reassigns the bus; the installed helper must discover the new BDF.
mv -- "$DEVICE" "$FIXTURE/pci/0000:08:01.0"
DEVICE="$FIXTURE/pci/0000:08:01.0"
chown 0:0 "$DEVICE/resource2"
bash "$FIXTURE/installed" --user nobody
[[ $(stat -c %u "$DEVICE/resource2") == "$(id -u nobody)" ]]
bash "$FIXTURE/setup.sh" --device 0000:08:01.0 --user nobody --install
grep -q 'KERNEL=="0000:08:01.0"' "$FIXTURE/rule"
mkdir "$FIXTURE/pci/0000:08:02.0"
cp "$DEVICE/vendor" "$DEVICE/device" "$FIXTURE/pci/0000:08:02.0/"
if bash "$FIXTURE/setup.sh" --user nobody; then printf 'FAIL: ambiguous devices accepted\n'; exit 1; fi
ln -s /does/not/matter "$DEVICE/driver"
if bash "$FIXTURE/setup.sh" --device 0000:08:01.0 --user nobody; then printf 'FAIL: bound device accepted\n'; exit 1; fi
printf 'PASS: IVSHMEM auto permission rule survives PCI renumbering; explicit pinning, ambiguity and driver refusal\n'
