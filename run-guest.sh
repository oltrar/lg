#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
BINARY=${LG_BINARY:-"$ROOT/_build/host/looking-glass-host"}
DEVICE=${LG_SHM:-ivshmem}
FPS=${LG_FPS:-auto}
RESTORE=${LG_CAPTURE_RESTORE:-yes}
CHOOSE_NEXT=${XDG_CONFIG_HOME:-"$HOME/.config"}/looking-glass-linux-wayland/choose-next
if [[ -f $CHOOSE_NEXT && ! -L $CHOOSE_NEXT && -O $CHOOSE_NEXT ]]; then
  RESTORE=no
  rm -- "$CHOOSE_NEXT"
fi
arguments=()
for argument in "$@"; do
  case $argument in
    --choose-display) RESTORE=no ;;
    --help)
      printf 'Usage: bash run-guest.sh [--choose-display] [Looking Glass options...]\n'
      printf 'Choose-display skips the saved monitor once and saves the new selection.\n'
      exit 0
      ;;
    *) arguments+=("$argument") ;;
  esac
done
KDE_OUTPUT=${LG_KDE_OUTPUT:-}
desktop=${XDG_CURRENT_DESKTOP:-}
if [[ ${desktop^^} == *KDE* ]]; then
  if [[ $RESTORE == no && -z $KDE_OUTPUT ]]; then bash "$ROOT/scripts/select-display.sh"; fi
  saved=${XDG_CONFIG_HOME:-"$HOME/.config"}/looking-glass-linux-wayland/kde-output
  if [[ -z $KDE_OUTPUT && -f $saved && ! -L $saved && -O $saved ]]; then
    IFS= read -r KDE_OUTPUT < "$saved"
  fi
fi
[[ -z $KDE_OUTPUT || $KDE_OUTPUT =~ ^[A-Za-z0-9_.:-]{1,255}$ ]] || {
  printf 'Invalid saved KDE connector. Re-select using bash install.sh.\n' >&2; exit 1;
}
source "$ROOT/scripts/capture-report.sh"
if ! lgStartReport "$ROOT"; then
  printf 'Cannot save a capture report; continuing with terminal/journal logging.\n' >&2
fi
[[ ${XDG_SESSION_TYPE:-} == wayland ]] || {
  printf 'Run this in a terminal inside the guest KDE/GNOME Wayland session.\n' >&2; exit 1;
}
[[ -n ${DBUS_SESSION_BUS_ADDRESS:-} ]] || {
  printf 'No desktop session bus; launch from the guest desktop, without sudo.\n' >&2; exit 1;
}
if [[ $FPS == auto ]]; then
  REQUEST_FPS=0
elif [[ $FPS =~ ^[0-9]{1,3}$ ]] && ((10#$FPS >= 1 && 10#$FPS <= 360)); then
  REQUEST_FPS=$((10#$FPS))
else
  printf 'LG_FPS must be auto or an integer from 1 to 360.\n' >&2; exit 1;
fi
[[ $RESTORE == yes || $RESTORE == no ]] || {
  printf 'LG_CAPTURE_RESTORE must be yes or no. Use --choose-display to reselect once.\n' >&2; exit 1;
}
[[ -x "$BINARY" ]] || { printf 'Build first: bash build.sh\n' >&2; exit 1; }
if [[ -f $ROOT/_build/build-info.txt && -z ${LG_BINARY:-} ]]; then
  source "$ROOT/scripts/build-info.sh"
  if [[ $(lgBuildValue "$ROOT" source_sha256) != "$(lgSourceFingerprint "$ROOT")" ||
        $(lgBuildValue "$ROOT" binary_sha256) != "$(sha256sum < "$BINARY" | cut -d ' ' -f 1)" ]]; then
    printf 'WARNING: executable/build record differs from this checkout. Rebuild with bash install.sh; inspect bash version.sh.\n' >&2
  fi
fi
# The application discovers/validates PCI BAR2 for ivshmem[:BDF]. An explicit
# /dev/kvmfrN or file remains supported; no KVMFR device is required by default.
if [[ $DEVICE != ivshmem && $DEVICE != ivshmem:* ]]; then
  [[ -r "$DEVICE" && -w "$DEVICE" ]] || {
    printf 'Shared-memory device/file is missing or inaccessible: %s\n' "$DEVICE" >&2
    exit 1
  }
fi
exec "$BINARY" "app:shmFile=$DEVICE" app:capture=pipewire "pipewire:fps=$REQUEST_FPS" \
  "pipewire:restore=$RESTORE" "pipewire:kdeOutput=$KDE_OUTPUT" "${arguments[@]}"
