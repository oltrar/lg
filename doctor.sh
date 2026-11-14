#!/usr/bin/env bash
set -uo pipefail
printf 'Run inside the Linux guest. No configuration is changed.\n'
printf '\nSession: %s / %s\n' "${XDG_CURRENT_DESKTOP:-unset}" "${XDG_SESSION_TYPE:-unset}"
printf 'Kernel: %s\n' "$(uname -r)"
printf '\nGPU and IVSHMEM PCI devices:\n'
command -v lspci >/dev/null && lspci -nnk | sed -n '/VGA\|3D controller\|Display controller\|Inter-VM/,+3p'
printf '\nDRM outputs (match the connector to its PCI device/driver):\n'
for connector in /sys/class/drm/card*-*; do
  [[ -f $connector/status ]] || continue
  name=${connector##*/}
  card=${name%%-*}
  printf '\n%s\n' "$name"
  printf 'GPU device: '; readlink -f "/sys/class/drm/$card/device" 2>/dev/null || true
  printf 'driver: '; readlink -f "/sys/class/drm/$card/device/driver" 2>/dev/null || true
  printf 'status: '; cat "$connector/status"
  printf 'enabled: '; cat "$connector/enabled" 2>/dev/null || true
  printf 'modes: '; head -n 5 "$connector/modes" 2>/dev/null | tr '\n' ' '; printf '\n'
done
printf '\nNVIDIA DRM modesetting parameter (when readable):\n'
cat /sys/module/nvidia_drm/parameters/modeset 2>/dev/null || true
if [[ ${XDG_CURRENT_DESKTOP:-} == *KDE* ]] && command -v kscreen-doctor >/dev/null; then
  printf '\nKDE output configuration:\n'
  timeout 10s kscreen-doctor -o 2>&1 || true
fi
printf '\nKVMFR devices:\n'
find /dev -maxdepth 1 -name 'kvmfr*' -exec ls -l {} \;
printf '\nKVMFR static allocation parameter (should be empty/zero in the guest):\n'
cat /sys/module/kvmfr/parameters/static_size_mb 2>/dev/null || true
printf '\nPreferred raw IVSHMEM devices (no KVMFR required):\n'
for device in /sys/bus/pci/devices/*; do
  [[ -f $device/vendor && -f $device/device ]] || continue
  [[ $(<"$device/vendor") == 0x1af4 && $(<"$device/device") == 0x1110 ]] || continue
  printf '\n%s\n' "${device##*/}"
  printf 'enabled: '; cat "$device/enable"
  printf 'driver: '; readlink "$device/driver" 2>/dev/null || printf 'none\n'
  ls -l "$device/resource2" 2>/dev/null || true
  ls -l "$device/resource2_wc" 2>/dev/null || true
  printf 'BAR2 resource: '; sed -n '3p' "$device/resource"
done
printf '\nGuest kernel lockdown status:\n'
cat /sys/kernel/security/lockdown 2>/dev/null || true
printf '\nDesktop portal and PipeWire services:\n'
systemctl --user --no-pager status pipewire.service xdg-desktop-portal.service 2>/dev/null || true
printf '\nPortal capture properties:\n'
command -v gdbus >/dev/null && gdbus introspect --session \
  --dest org.freedesktop.portal.Desktop --object-path /org/freedesktop/portal/desktop \
  2>/dev/null | sed -n '/interface org.freedesktop.portal.ScreenCast/,/^  };/p'
printf '\nRunning-kernel headers (only needed for optional KVMFR):\n'
ls -ld "/usr/lib/modules/$(uname -r)/build" 2>/dev/null || true
