#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Called by install.sh; package managers retain their normal confirmation UI.
set -euo pipefail
backend=xdg-desktop-portal-kde
desktop=${XDG_CURRENT_DESKTOP:-}
[[ ${desktop^^} != *GNOME* ]] || backend=xdg-desktop-portal-gnome
if command -v pacman >/dev/null; then
  packages=(base-devel git cmake ninja pkgconf pipewire glib2 dbus systemd wayland
    xdg-desktop-portal "$backend" pciutils desktop-file-utils)
  if ! pacman -Q "${packages[@]}" >/dev/null 2>&1; then
    sudo pacman -S --needed "${packages[@]}"
  fi
else
  # Other distributions retain their usual package-management procedure.
  for tool in git cmake ninja pkg-config cc gio desktop-file-validate systemd-analyze; do
    command -v "$tool" >/dev/null || { printf 'Missing %s; install the build/portal dependencies from README.md.\n' "$tool" >&2; exit 1; }
  done
fi
