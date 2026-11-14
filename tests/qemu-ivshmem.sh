#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Run inside the Debian CI container with qemu-system-x86, busybox-static,
# cpio, gzip and a /boot/vmlinuz-* kernel. Uses TCG: nested KVM is not required.
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
SRC="$ROOT/_upstream/LookingGlass"
BUILD="$ROOT/_build/qemu"
IMAGE="$BUILD/initramfs"
mkdir -p "$IMAGE/bin" "$IMAGE/proc" "$IMAGE/sys" "$IMAGE/dev"
cc -std=gnu11 -D_GNU_SOURCE -Wall -Werror -O1 -static \
  -ffunction-sections -fdata-sections -Wl,--gc-sections \
  -I "$SRC/common/include" -I "$SRC" \
  "$ROOT/tests/qemu_ivshmem_agent.c" "$ROOT/tests/ivshmem_test_debug.c" \
  "$ROOT/overlay/common/src/platform/linux/ivshmem_pci.c" -o "$IMAGE/bin/agent"
cp /bin/busybox "$IMAGE/bin/busybox"
ln -sf busybox "$IMAGE/bin/sh"
if [[ ! -e $IMAGE/dev/console ]]; then
  mknod -m 600 "$IMAGE/dev/console" c 5 1
fi
cat > "$IMAGE/init" <<'INIT'
#!/bin/sh
/bin/busybox --install -s /bin
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
for d in /sys/bus/pci/devices/*; do
  [ "$(cat "$d/vendor")" = 0x1af4 ] || continue
  [ "$(cat "$d/device")" = 0x1110 ] || continue
  [ "$(cat "$d/enable")" != 0 ] || echo 1 > "$d/enable"
  chown 1000:1000 "$d/resource2"
  chmod 600 "$d/resource2"
  if [ -f "$d/resource2_wc" ]; then
    chown 1000:1000 "$d/resource2_wc"
    chmod 600 "$d/resource2_wc"
  fi
done
if [ -d /sys/module/kvmfr ]; then
  echo 'FAIL: KVMFR unexpectedly loaded'
else
  /bin/agent || echo "FAIL: raw IVSHMEM agent exit $?"
fi
poweroff -f
INIT
chmod 755 "$IMAGE/init"
(
  cd "$IMAGE"
  find . -print0 | cpio --null -o --format=newc 2>/dev/null | gzip -1 > "$BUILD/initramfs.gz"
)
SHM="$BUILD/ivshmem.raw"
truncate -s 64M "$SHM"
printf 'LG-HOST-HELLO\0' | dd of="$SHM" conv=notrunc status=none
KERNEL=$(find /boot -maxdepth 1 -name 'vmlinuz-*' -type f | sort | tail -n 1)
[[ -n $KERNEL ]] || { printf 'No test kernel found.\n' >&2; exit 1; }
timeout 120s qemu-system-x86_64 -machine q35,accel=tcg -cpu max -m 256M \
  -kernel "$KERNEL" -initrd "$BUILD/initramfs.gz" \
  -append 'console=ttyS0 rdinit=/init panic=-1 quiet' \
  -nodefaults -display none -serial stdio -monitor none -no-reboot \
  -object "memory-backend-file,id=lgmem,mem-path=$SHM,size=67108864,share=on" \
  -device ivshmem-plain,memdev=lgmem | tee "$BUILD/guest.log"
grep -q 'PASS: unprivileged guest mapped 64 MiB IVSHMEM BAR2 without KVMFR' "$BUILD/guest.log"
[[ $(tail -c 16 "$SHM" | head -c 11) == LG-GUEST-OK ]] || {
  printf 'Guest writes were not visible in the physical host backing file.\n' >&2; exit 1;
}
printf 'PASS: QEMU guest-to-host IVSHMEM coherence, 64 MiB, no KVMFR\n'
