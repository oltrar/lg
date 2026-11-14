#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Sourced by run-guest.sh: save a separate private report for every launch, so
# a failed QXL-free boot can be inspected after rebooting with a console again.
lgStartReport() {
  local root=$1 state=${XDG_STATE_HOME:-"$HOME/.local/state"}
  local previous directory reports
  [[ $state == /* ]] || return 1
  command -v tee >/dev/null && command -v timeout >/dev/null || return 1
  reports="$state/looking-glass-linux-wayland/reports"
  previous=$(umask)
  umask 077
  if ! mkdir -p -- "$reports"; then umask "$previous"; return 1; fi
  if ! directory=$(mktemp -d "$reports/$(date -u +%Y%m%dT%H%M%SZ)-XXXXXX"); then
    umask "$previous"; return 1
  fi
  if ! : > "$directory/capture.log"; then umask "$previous"; return 1; fi
  # tee also forwards the normal terminal/journal output. It inherits mode 077.
  exec > >(tee -a -- "$directory/capture.log") 2>&1
  printf 'Capture report: %s\n' "$directory"
  printf 'UTC: %s\nKernel: %s\n' "$(date -u +%FT%TZ)" "$(uname -sr)"
  printf 'Boot ID: '; cat /proc/sys/kernel/random/boot_id 2>/dev/null || true
  printf 'Desktop: %s; session: %s; Wayland display: %s\n' \
    "${XDG_CURRENT_DESKTOP:-unset}" "${XDG_SESSION_TYPE:-unset}" "${WAYLAND_DISPLAY:-unset}"
  printf 'Capture settings: device=%s fps=%s restore=%s\n' "$DEVICE" "$FPS" "$RESTORE"
  printf 'Native KDE connector: %s\n' "${KDE_OUTPUT:-portal}"
  printf 'Guest executable: %s\n' "$BINARY"
  printf 'Checkout commit: %s\n' "$(git -C "$root" rev-parse --verify HEAD 2>/dev/null || printf 'archive')"
  if [[ -f $root/_build/build-info.txt ]]; then
    printf 'Build record:\n'; cat "$root/_build/build-info.txt"
  else
    printf 'Build record missing; rebuild using bash install.sh to identify this executable.\n'
  fi
  {
    printf 'Read-only guest diagnostics for this capture launch\n'
    timeout 8s bash "$root/doctor.sh" || printf '\nGuest diagnostics incomplete or timed out.\n'
    if command -v nvidia-smi >/dev/null; then
      printf '\nNVIDIA model and driver:\n'
      timeout 3s nvidia-smi --query-gpu=name,driver_version --format=csv,noheader || true
    fi
  } > "$directory/diagnostics.log" 2>&1
  umask "$previous"
}
