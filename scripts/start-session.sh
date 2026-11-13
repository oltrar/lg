#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# XDG autostart supplies the logged-in desktop's environment. Import a scoped
# list before starting the user unit; never import the entire shell environment.
set -euo pipefail
ACTION=${1:-start}
[[ $ACTION == start || $ACTION == restart ]] || exit 1
(( EUID != 0 )) || { printf 'Guest capture must run as the desktop user.\n' >&2; exit 1; }
if [[ ${XDG_SESSION_TYPE:-} != wayland ]]; then
  printf 'Looking Glass startup skipped: this is not a Wayland session.\n'
  exit 0
fi
[[ -n ${WAYLAND_DISPLAY:-} && -n ${DBUS_SESSION_BUS_ADDRESS:-} &&
   -n ${XDG_RUNTIME_DIR:-} ]] || {
  printf 'Looking Glass startup requires the active Wayland desktop and session bus.\n' >&2; exit 1;
}
present=()
absent=()
for variable in WAYLAND_DISPLAY XDG_SESSION_TYPE XDG_CURRENT_DESKTOP \
                XDG_SESSION_DESKTOP XDG_CONFIG_HOME XDG_RUNTIME_DIR \
                DBUS_SESSION_BUS_ADDRESS DISPLAY XAUTHORITY; do
  if [[ -v $variable ]]; then present+=("$variable"); else absent+=("$variable"); fi
done
# Clear optional values from a previous desktop login, rather than reusing them.
if ((${#absent[@]})); then systemctl --user unset-environment "${absent[@]}"; fi
systemctl --user import-environment "${present[@]}"
systemctl --user reset-failed looking-glass-host.service
# Do not block desktop startup while a portal chooser is waiting for the user.
exec systemctl --user --no-block "$ACTION" looking-glass-host.service
