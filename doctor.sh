#!/usr/bin/env bash
set -uo pipefail
printf 'Run inside the Linux guest. No configuration is changed.\n'
printf '\nSession: %s / %s\n' "${XDG_CURRENT_DESKTOP:-unset}" "${XDG_SESSION_TYPE:-unset}"
printf 'Kernel: %s\n' "$(uname -r)"
printf '\nGPU and IVSHMEM PCI devices:\n'
command -v lspci >/dev/null && lspci -nnk | sed -n '/VGA\|3D controller\|Display controller\|Inter-VM/,+3p'
printf '\nKVMFR devices:\n'
find /dev -maxdepth 1 -name 'kvmfr*' -exec ls -l {} \;
printf '\nKVMFR static allocation parameter (should be empty/zero in the guest):\n'
cat /sys/module/kvmfr/parameters/static_size_mb 2>/dev/null || true
printf '\nDesktop portal and PipeWire services:\n'
systemctl --user --no-pager status pipewire.service xdg-desktop-portal.service 2>/dev/null || true
printf '\nPortal capture properties:\n'
command -v gdbus >/dev/null && gdbus introspect --session \
  --dest org.freedesktop.portal.Desktop --object-path /org/freedesktop/portal/desktop \
  2>/dev/null | sed -n '/interface org.freedesktop.portal.ScreenCast/,/^  };/p'
printf '\nRunning-kernel headers:\n'
ls -ld "/usr/lib/modules/$(uname -r)/build" 2>/dev/null || true
