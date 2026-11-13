#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
UNIT=looking-glass-host.service
MARKER='# Managed by oltrar/lg setup-startup.sh (v1)'
CONFIG=${XDG_CONFIG_HOME:-"$HOME/.config"}
ACTION=${1:-}
usage() {
  printf 'Usage: bash setup-startup.sh {enable|repair|disable|uninstall|start|restart|stop|status|choose-display}\nRun as your guest desktop user, without sudo.\n'
}
[[ $# == 1 && $ACTION =~ ^(enable|repair|disable|uninstall|start|restart|stop|status|choose-display)$ ]] || { usage; exit 1; }
(( EUID != 0 )) || { printf 'Run without sudo, as the guest desktop user.\n' >&2; exit 1; }
[[ $CONFIG == /* && $ROOT != *$'\n'* && $ROOT != *$'\r'* &&
   $CONFIG != *$'\n'* && $CONFIG != *$'\r'* ]] || {
  printf 'Use an absolute XDG_CONFIG_HOME and paths without line breaks.\n' >&2; exit 1;
}
command -v systemctl >/dev/null || { printf 'A systemd user session is required.\n' >&2; exit 1; }
SERVICE="$CONFIG/systemd/user/$UNIT"
DESKTOP="$CONFIG/autostart/looking-glass-host.desktop"
OPTIONS="$CONFIG/looking-glass-linux-wayland/autostart.env"
STAGING=''
cleanup() {
  if [[ -n $STAGING ]]; then
    rm -f -- "$STAGING/$UNIT"
    rmdir -- "$STAGING" 2>/dev/null || true
  fi
}
trap cleanup EXIT
managed() { [[ -f $1 && ! -L $1 ]] && grep -Fxq -- "$MARKER" "$1"; }
preflight() {
  local file
  for file in "$SERVICE" "$DESKTOP"; do
    if [[ -e $file || -L $file ]] && ! managed "$file"; then
      printf 'Preserving an unmanaged startup file: %s\n' "$file" >&2; exit 1
    fi
  done
}
require_session() {
  [[ ${XDG_SESSION_TYPE:-} == wayland && -n ${WAYLAND_DISPLAY:-} &&
     -n ${DBUS_SESSION_BUS_ADDRESS:-} && -n ${XDG_RUNTIME_DIR:-} ]] || {
    printf 'Run this in a terminal in the guest KDE/GNOME Wayland desktop.\n' >&2; exit 1;
  }
  systemctl --user show-environment >/dev/null
}
stop_managed() {
  # A file can exist while the manager has forgotten/not yet loaded the unit.
  # Reload before stopping and tolerate only the genuine not-found case.
  systemctl --user daemon-reload
  local state
  if ! state=$(systemctl --user show -p LoadState --value "$UNIT"); then
    [[ $state == not-found ]] || return 1
  fi
  if [[ $state != not-found ]]; then systemctl --user stop "$UNIT"; fi
}
# systemd quotes differ from shell and .desktop quotes. No generated command
# uses a shell's eval/-c. Escape specifiers and ExecStart dollar expansion too.
unit_quote() {
  local value=$1
  value=${value//\\/\\\\}; value=${value//\"/\\\"}; value=${value//%/%%}
  printf '"%s"' "$value"
}
exec_quote() { local value=${1//\$/\$\$}; unit_quote "$value"; }
env_file_pattern() {
  # EnvironmentFile is a raw glob pattern. Neither shell quoting nor C-style
  # word quoting applies; escape glob characters and systemd specifiers only.
  local value=$1
  value=${value//\\/\\\\}; value=${value//\*/\\*}; value=${value//\?/\\?}
  value=${value//\[/\\[}; value=${value//\]/\\]}; value=${value//%/%%}
  printf '%s' "$value"
}
desktop_quote() {
  local value=$1
  value=${value//\\/\\\\}; value=${value//\"/\\\"}
  value=${value//\$/\\\$}; value=${value//\`/\\\`}; value=${value//%/%%}
  # Desktop Entry string unescaping happens before Exec argument unquoting.
  value=${value//\\/\\\\}
  printf '"%s"' "$value"
}
write_file() {
  local destination=$1 temporary
  temporary=$(mktemp "${destination}.XXXXXX")
  cat > "$temporary"
  chmod 600 "$temporary"
  mv -f -- "$temporary" "$destination"
}
case $ACTION in
  enable|repair)
    require_session
    [[ -x "$ROOT/_build/host/looking-glass-host" ]] || {
      printf 'Build and test capture first: bash build.sh; bash run-guest.sh\n' >&2; exit 1;
    }
    preflight
    command -v systemd-analyze >/dev/null || { printf 'Missing systemd-analyze.\n' >&2; exit 1; }
    [[ ! -L $OPTIONS && ( ! -e $OPTIONS || -f $OPTIONS ) ]] || {
      printf 'Preserving unexpected options path: %s\n' "$OPTIONS" >&2; exit 1;
    }
    umask 077
    mkdir -p -- "$(dirname -- "$SERVICE")" "$(dirname -- "$DESKTOP")" "$(dirname -- "$OPTIONS")"
    if [[ ! -f $OPTIONS ]]; then
      write_file "$OPTIONS" <<'EOF'
# Startup-only settings, in systemd EnvironmentFile syntax (not a shell script).
# Edit these, then run: bash setup-startup.sh restart
LG_SHM=ivshmem
LG_FPS=60
# Keep yes for automatic startup; use the choose-display command to change once.
LG_CAPTURE_RESTORE=yes
EOF
    fi
    STAGING=$(mktemp -d "$(dirname -- "$SERVICE")/.lg-unit.XXXXXX")
    write_file "$STAGING/$UNIT" <<EOF
$MARKER
[Unit]
Description=Looking Glass Linux guest capture
PartOf=graphical-session.target
After=graphical-session.target

[Service]
Type=exec
# EnvironmentFile takes a single raw path, not an ExecStart-style quoted word.
EnvironmentFile=$(env_file_pattern "$OPTIONS")
ExecStart=/usr/bin/bash $(exec_quote "$ROOT/run-guest.sh")
ExecStartPost=/usr/bin/bash $(exec_quote "$ROOT/check-video.sh")
TimeoutStartSec=180
Restart=no
TimeoutStopSec=10
EOF
    # verify can return success after ignoring malformed directives. Its
    # diagnostics must be empty too, before replacing the installed unit.
    if ! verification=$(systemd-analyze --user verify "$STAGING/$UNIT" 2>&1); then
      printf 'Startup unit validation failed:\n%s\n' "$verification" >&2; exit 1
    fi
    if [[ -n $verification ]]; then
      printf 'Startup unit validation reported warnings; preserving installed unit:\n%s\n' "$verification" >&2
      exit 1
    fi
    mv -f -- "$STAGING/$UNIT" "$SERVICE"
    write_file "$DESKTOP" <<EOF
$MARKER
[Desktop Entry]
Type=Application
Name=Looking Glass guest capture
Comment=Start IVSHMEM capture in the Wayland desktop session
Exec=/usr/bin/bash $(desktop_quote "$ROOT/scripts/start-session.sh")
Terminal=false
OnlyShowIn=KDE;GNOME;
EOF
    systemctl --user daemon-reload
    printf 'Enabled for the next KDE/GNOME Wayland login. Current capture is unchanged.\nSettings: %s\nStop any manual capture before using: bash setup-startup.sh start\n' "$OPTIONS"
    ;;
  disable|uninstall)
    preflight
    if managed "$SERVICE"; then
      stop_managed
      rm -- "$SERVICE"
    fi
    if managed "$DESKTOP"; then rm -- "$DESKTOP"; fi
    systemctl --user daemon-reload
    printf 'Startup disabled; service stopped. Settings and portal permissions kept.\n'
    ;;
  start|restart)
    require_session
    managed "$SERVICE" && managed "$DESKTOP" || {
      printf 'Enable startup first: bash setup-startup.sh enable\n' >&2; exit 1;
    }
    bash "$ROOT/scripts/start-session.sh" "$ACTION"
    ;;
  choose-display)
    require_session
    # Stop only this project's managed unit; the chooser runs in the accessible
    # guest terminal so the user can see errors and use Ctrl+C after verifying.
    if managed "$SERVICE"; then stop_managed; fi
    printf 'Choose the NVIDIA/dummy-plug monitor in the guest sharing dialog.\n'
    printf 'Stop any separate manual capture first. Press Ctrl+C when verified; then restart the startup service.\n'
    exec bash "$ROOT/run-guest.sh" --choose-display
    ;;
  stop)
    if managed "$SERVICE"; then
      stop_managed
    elif [[ ! -e $SERVICE && ! -L $SERVICE ]]; then
      printf 'No managed startup service is installed.\n'
    else
      printf 'Preserving an unmanaged service: %s\n' "$SERVICE" >&2; exit 1
    fi
    ;;
  status)
    if managed "$DESKTOP"; then printf 'Desktop startup: enabled\n'; else printf 'Desktop startup: disabled\n'; fi
    systemctl --user --no-pager status "$UNIT"
    ;;
esac
