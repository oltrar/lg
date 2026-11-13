#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ ${1:-} != --inner ]]; then
  FIXTURE=$(mktemp -d "${TMPDIR:-/tmp}/lg-startup.XXXXXX")
  trap 'rm -rf -- "$FIXTURE"' EXIT
  PROJECT="$FIXTURE/"'project space % $ " \ ` & (test)'
  mkdir -p "$PROJECT/scripts" "$PROJECT/tests" "$PROJECT/_build/host"
  cp "$ROOT/setup-startup.sh" "$ROOT/run-guest.sh" "$PROJECT/"
  cp "$ROOT/scripts/start-session.sh" "$PROJECT/scripts/"
  cp "$ROOT/tests/startup_test.sh" "$PROJECT/tests/"
  if (( EUID == 0 )); then
    if bash "$ROOT/setup-startup.sh" enable >"$FIXTURE/root.log" 2>&1; then
      printf 'FAIL: root startup install was allowed\n' >&2; exit 1
    fi
    grep -q 'Run without sudo' "$FIXTURE/root.log"
    chown -R 65534:65534 -- "$FIXTURE"
    runuser -u nobody -- /usr/bin/bash "$PROJECT/tests/startup_test.sh" --inner "$FIXTURE"
  else
    /usr/bin/bash "$PROJECT/tests/startup_test.sh" --inner "$FIXTURE"
  fi
  exit
fi
FIXTURE=${2:?Missing test fixture}
export HOME="$FIXTURE/home"
export XDG_CONFIG_HOME="$FIXTURE/"'config space % $ " \ ` & (test)'
export XDG_RUNTIME_DIR="$FIXTURE/runtime"
export XDG_SESSION_TYPE=wayland WAYLAND_DISPLAY=wayland-test
export XDG_CURRENT_DESKTOP=KDE DBUS_SESSION_BUS_ADDRESS=unix:path=/test/bus
export LG_TEST_LOG="$FIXTURE/systemctl.log"
unset DISPLAY XAUTHORITY XDG_SESSION_DESKTOP
mkdir -p "$HOME" "$XDG_RUNTIME_DIR" "$FIXTURE/bin"
export PATH="$FIXTURE/bin:$PATH"
cat > "$FIXTURE/bin/systemctl" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$LG_TEST_LOG"
EOF
chmod +x "$FIXTURE/bin/systemctl"
printf '#!/usr/bin/env bash\nexit 0\n' > "$ROOT/_build/host/looking-glass-host"
chmod +x "$ROOT/_build/host/looking-glass-host"
SERVICE="$XDG_CONFIG_HOME/systemd/user/looking-glass-host.service"
DESKTOP="$XDG_CONFIG_HOME/autostart/looking-glass-host.desktop"
OPTIONS="$XDG_CONFIG_HOME/looking-glass-linux-wayland/autostart.env"

# Enable must not launch a second capture, and repeated installs keep settings.
bash "$ROOT/setup-startup.sh" enable
[[ -f $SERVICE && -f $DESKTOP && -f $OPTIONS ]]
! grep -Eq -- 'start looking-glass-host.service|enable ' "$LG_TEST_LOG"
printf 'LG_SHM=ivshmem:0000:09:02.0\nLG_FPS=120\n' > "$OPTIONS"
bash "$ROOT/setup-startup.sh" enable
grep -Fxq 'LG_FPS=120' "$OPTIONS"
[[ $(stat -c %a "$OPTIONS") == 600 ]]
desktop-file-validate "$DESKTOP"
systemd-analyze --user verify "$SERVICE"

# Let GLib really parse/launch the .desktop Exec entry. The project/config paths
# contain spaces, quotes, %, $, backslashes and backticks; no shell expansion.
: > "$LG_TEST_LOG"
gio launch "$DESKTOP"
for ((attempt=0; attempt<100; ++attempt)); do
  if grep -Fxq -- '--user --no-block start looking-glass-host.service' "$LG_TEST_LOG"; then break; fi
  sleep 0.05
done
grep -Fxq -- '--user --no-block start looking-glass-host.service' "$LG_TEST_LOG"
grep -Fxq -- '--user unset-environment XDG_SESSION_DESKTOP DISPLAY XAUTHORITY' "$LG_TEST_LOG"
grep -Fxq -- '--user import-environment WAYLAND_DISPLAY XDG_SESSION_TYPE XDG_CURRENT_DESKTOP XDG_CONFIG_HOME XDG_RUNTIME_DIR DBUS_SESSION_BUS_ADDRESS' "$LG_TEST_LOG"

# GNOME uses the same path. Missing desktop state and X11 must not start capture.
export XDG_CURRENT_DESKTOP=GNOME
bash "$ROOT/setup-startup.sh" restart
grep -Fxq -- '--user --no-block restart looking-glass-host.service' "$LG_TEST_LOG"
: > "$LG_TEST_LOG"
XDG_SESSION_TYPE=x11 bash "$ROOT/scripts/start-session.sh"
[[ ! -s $LG_TEST_LOG ]]
if WAYLAND_DISPLAY= bash "$ROOT/setup-startup.sh" start; then
  printf 'FAIL: startup accepted a missing Wayland display\n' >&2; exit 1
fi
[[ ! -s $LG_TEST_LOG ]]

# Removing startup stops the unit and keeps preferences and portal restore data.
printf 'private portal restore token\n' > "$XDG_CONFIG_HOME/looking-glass-linux-wayland/restore-token"
bash "$ROOT/setup-startup.sh" stop
bash "$ROOT/setup-startup.sh" disable
[[ ! -e $SERVICE && ! -e $DESKTOP && -f $OPTIONS ]]
grep -Fxq -- '--user stop looking-glass-host.service' "$LG_TEST_LOG"
grep -Fxq 'private portal restore token' "$XDG_CONFIG_HOME/looking-glass-linux-wayland/restore-token"
bash "$ROOT/setup-startup.sh" disable

# An unrelated file or a symlink must survive both enable and disable unchanged.
printf 'my startup entry\n' > "$DESKTOP"
for action in enable disable; do
  if bash "$ROOT/setup-startup.sh" "$action"; then
    printf 'FAIL: replaced unmanaged startup entry\n' >&2; exit 1
  fi
  [[ $(<"$DESKTOP") == 'my startup entry' ]]
done
rm -- "$DESKTOP"
ln -s "$OPTIONS" "$DESKTOP"
if bash "$ROOT/setup-startup.sh" enable; then
  printf 'FAIL: replaced startup symlink\n' >&2; exit 1
fi
[[ -L $DESKTOP ]]
printf 'PASS: user startup install/remove, real desktop Exec parsing, systemd unit validation, session environment and settings preservation\n'
