#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Run inside the disposable Arch CI container. Export only that container's
# filesystem; no host root mount, VM network or GPU/compositor bypass flags.
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
BUILD="$ROOT/_build/kwin-vm"
timeout 300s qemu-system-x86_64 -machine q35,accel=tcg -cpu max -smp 2 -m 2048M \
  -kernel "$BUILD/vmlinuz" -initrd "$BUILD/initramfs.gz" \
  -append 'console=ttyS0 rdinit=/init panic=-1 quiet' \
  -nodefaults -display none -serial stdio -monitor none -no-reboot -nic none \
  -fsdev local,id=arch,path=/,security_model=none \
  -device virtio-9p-pci,fsdev=arch,mount_tag=archroot | tee "$BUILD/guest.log"
grep -q 'PASS: KWIN_VM_CAPTURE_COMPLETE' "$BUILD/guest.log"
