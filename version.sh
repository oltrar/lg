#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$ROOT/scripts/build-info.sh"
printf 'Project directory: %s\n' "$ROOT"
printf 'Checkout branch: %s\n' "$(git -C "$ROOT" symbolic-ref --short -q HEAD 2>/dev/null || printf 'archive-or-detached')"
printf 'Checkout commit: %s\n' "$(git -C "$ROOT" rev-parse --verify HEAD 2>/dev/null || printf 'archive')"
BINARY=${LG_BINARY:-"$ROOT/_build/host/looking-glass-host"}
printf 'Guest executable: %s\n' "$BINARY"
[[ -x $BINARY ]] || printf 'Executable missing or not executable: run bash install.sh.\n'
if [[ -f $ROOT/_build/build-info.txt ]]; then
  printf '\nLast successful build:\n'
  cat "$ROOT/_build/build-info.txt"
  if [[ $(lgBuildValue "$ROOT" source_sha256) != "$(lgSourceFingerprint "$ROOT")" ]]; then
    printf 'SOURCE MISMATCH: rebuild this checkout with bash install.sh.\n'
  fi
  if [[ -f $BINARY && $(lgBuildValue "$ROOT" binary_sha256) != "$(sha256sum < "$BINARY" | cut -d ' ' -f 1)" ]]; then
    printf 'BINARY MISMATCH: this executable differs from the recorded build.\n'
  fi
else
  printf 'Build record missing: rebuild with bash install.sh to identify the executable.\n'
fi
SERVICE="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user/looking-glass-host.service"
printf '\nInstalled service: %s\n' "$SERVICE"
if [[ -f $SERVICE ]]; then
  sed -n '/^EnvironmentFile=/p; /^ExecStart=/p' "$SERVICE"
else
  printf 'No service file at this path.\n'
fi
if command -v systemctl >/dev/null; then
  pid=$(timeout 2s systemctl --user show looking-glass-host.service --property=MainPID --value 2>/dev/null || true)
  if [[ $pid =~ ^[0-9]+$ ]] && ((pid > 0)); then
    printf 'Service main PID: %s\nProcess executable: ' "$pid"
    readlink "/proc/$pid/exe" 2>/dev/null || printf 'unavailable\n'
  fi
fi
