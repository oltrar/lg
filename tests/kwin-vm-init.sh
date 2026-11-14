#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# PID 1 inside the disposable GPU-less compositor test VM only.
set -uo pipefail
mount -t tmpfs -o mode=1777 tmpfs /tmp
mount -t tmpfs tmpfs /run
chmod 666 /dev/udmabuf
cd /project
if bash tests/kwin-native.sh; then
  printf 'PASS: KWIN_VM_CAPTURE_COMPLETE\n'
else
  printf 'FAIL: KWin capture VM\n'
fi
/usr/bin/poweroff -f -f
