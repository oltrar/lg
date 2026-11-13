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
[[ $connector =~ ^[A-Za-z0-9_.:-]{1,255}$ ]] || { printf 'Unsupported connector name.\n' >&2; exit 1; }
umask 077
mkdir -p -- "$CONFIG"
[[ ! -L $CONFIG/kde-output && ( ! -e $CONFIG/kde-output || -O $CONFIG/kde-output ) ]] || exit 1
temporary=$(mktemp "$CONFIG/kde-output.XXXXXX")
printf '%s\n' "$connector" > "$temporary"
mv -f -- "$temporary" "$CONFIG/kde-output"
if [[ -f $CONFIG/choose-next && ! -L $CONFIG/choose-next && -O $CONFIG/choose-next ]]; then rm -- "$CONFIG/choose-next"; fi
printf 'Saved %s. KDE startup will use this connector without a portal chooser.\n' "$connector"
