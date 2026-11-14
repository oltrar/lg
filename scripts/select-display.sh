#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Explicit setup-time choice, saved by connector rather than desktop position.
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
CONFIG=${XDG_CONFIG_HOME:-"$HOME/.config"}/looking-glass-linux-wayland
((EUID != 0)) || { printf 'Run as the guest desktop user, without sudo.\n' >&2; exit 1; }
[[ -t 0 ]] || { printf 'Choose a display in a guest terminal: bash install.sh\n' >&2; exit 1; }
listing=$("$ROOT/_build/host/lg-kde-source") || exit 1
[[ -n $listing ]] || { printf 'No active named KDE outputs.\n' >&2; exit 1; }
mapfile -t outputs <<< "$listing"
printf '\nChoose the NVIDIA/dummy-plug connector for unattended capture:\n'
for i in "${!outputs[@]}"; do printf '  %d) %s\n' "$((i+1))" "${outputs[i]//$'\t'/  }"; done
while :; do
  read -r -p 'Display number: ' choice
  if [[ $choice =~ ^[1-9][0-9]{0,2}$ ]] && ((choice <= ${#outputs[@]})); then break; fi
  printf 'Enter a displayed number.\n' >&2
done
connector=${outputs[choice-1]%%$'\t'*}
IFS=$'\t' read -r _ _ refresh _ <<< "${outputs[choice-1]}"
rate=${refresh%%.*}; rate=${rate%% *}
if [[ $rate =~ ^[0-9]+$ ]] && ((rate < 50)); then
  printf 'This output is running at %s. For smoother video/gaming select a 60 Hz or faster mode in KDE Display Configuration.\n' "$refresh"
fi
[[ $connector =~ ^[A-Za-z0-9_.:-]{1,255}$ ]] || { printf 'Unsupported connector name.\n' >&2; exit 1; }
umask 077
mkdir -p -- "$CONFIG"
[[ ! -L $CONFIG/kde-output && ( ! -e $CONFIG/kde-output || -O $CONFIG/kde-output ) ]] || exit 1
temporary=$(mktemp "$CONFIG/kde-output.XXXXXX")
printf '%s\n' "$connector" > "$temporary"
mv -f -- "$temporary" "$CONFIG/kde-output"
OPTIONS="$CONFIG/autostart.env"
if [[ -f $OPTIONS && ! -L $OPTIONS && -O $OPTIONS ]] && grep -Fxq 'LG_FPS=60' "$OPTIONS"; then
  # Upgrade the previous generated 60-fps default for high-refresh outputs.
  # Explicit nondefault caps (30/120/144/etc.) are preserved.
  temporary=$(mktemp "$OPTIONS.XXXXXX")
  sed 's/^LG_FPS=60$/LG_FPS=auto/' "$OPTIONS" > "$temporary"
  mv -f -- "$temporary" "$OPTIONS"
  printf 'Upgraded the previous 60-fps setting to match the selected output refresh automatically.\n'
fi
if [[ -f $CONFIG/choose-next && ! -L $CONFIG/choose-next && -O $CONFIG/choose-next ]]; then rm -- "$CONFIG/choose-next"; fi
printf 'Saved %s. KDE startup will use this connector without a portal chooser.\n' "$connector"
