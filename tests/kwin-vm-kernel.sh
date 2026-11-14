#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Build a tiny boot image using a stock Debian kernel. The current Arch root
# filesystem is supplied separately over 9p; no nested KVM or hardware GPU.
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
BUILD="$ROOT/_build/kwin-vm"
IMAGE="$BUILD/initramfs"
KERNEL=$(find /boot -maxdepth 1 -name 'vmlinuz-*' -type f | sort -V | tail -n 1)
[[ -n $KERNEL ]]
VERSION=${KERNEL##*/vmlinuz-}
mkdir -p "$IMAGE/bin" "$IMAGE/proc" "$IMAGE/sys" "$IMAGE/dev" "$IMAGE/newroot" "$IMAGE/lib/modules/$VERSION"
cp /bin/busybox "$IMAGE/bin/busybox"
ln -sf busybox "$IMAGE/bin/sh"
for name in virtio_pci 9p 9pnet_virtio udmabuf; do
  modprobe --show-depends -S "$VERSION" "$name" | while read -r action module rest; do
    [[ $action == insmod ]] || continue
    mkdir -p "$IMAGE$(dirname -- "$module")"
    cp "$module" "$IMAGE$module"
  done
done
cp /lib/modules/"$VERSION"/modules.{dep,alias,softdep,builtin,builtin.modinfo} "$IMAGE/lib/modules/$VERSION/"
cat > "$IMAGE/init" <<'INIT'
#!/bin/sh
/bin/busybox --install -s /bin
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
modprobe virtio_pci || poweroff -f
modprobe 9pnet_virtio || poweroff -f
modprobe 9p || poweroff -f
modprobe udmabuf || poweroff -f
mount -t 9p -o trans=virtio,version=9p2000.L,msize=262144 archroot /newroot || poweroff -f
mount --move /proc /newroot/proc
mount --move /sys /newroot/sys
mount --move /dev /newroot/dev
exec switch_root /newroot /usr/bin/bash /project/tests/kwin-vm-init.sh
INIT
chmod 755 "$IMAGE/init"
(
  cd "$IMAGE"
  find . -print0 | cpio --null -o --format=newc 2>/dev/null | gzip -1 > "$BUILD/initramfs.gz"
)
cp "$KERNEL" "$BUILD/vmlinuz"
