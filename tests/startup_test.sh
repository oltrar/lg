#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ ${1:-} != --inner ]]; then
  FIXTURE=$(mktemp -d "${TMPDIR:-/tmp}/lg-startup.XXXXXX")
  trap 'rm -rf -- "$FIXTURE"' EXIT
  PROJECT="$FIXTURE/"'project space % $ " \ ` & (test)'
  mkdir -p "$PROJECT/scripts" "$PROJECT/tests" "$PROJECT/_build/host"
  cp "$ROOT/setup-startup.sh" "$ROOT/run-guest.sh" "$ROOT/install.sh" "$ROOT/version.sh" "$ROOT/check-video.sh" "$PROJECT/"
  cp "$ROOT/scripts/start-session.sh" "$PROJECT/scripts/"
  cp "$ROOT/scripts/capture-report.sh" "$PROJECT/scripts/"
  cp "$ROOT/scripts/build-info.sh" "$PROJECT/scripts/"
  cp "$ROOT/tests/startup_test.sh" "$PROJECT/tests/"
  cp "$ROOT/tests/environment_file_path_test.c" "$PROJECT/tests/"
  if (( EUID == 0 )); then
    if bash "$ROOT/setup-startup.sh" enable >"$FIXTURE/root.log" 2>&1; then
      printf 'FAIL: root startup install was allowed\n' >&2; exit 1
    fi
    grep -q 'Run without sudo' "$FIXTURE/root.log"
    if bash "$ROOT/install.sh" >"$FIXTURE/install-root.log" 2>&1; then
      printf 'FAIL: root install was allowed\n' >&2; exit 1
    fi
    grep -q 'Run without sudo' "$FIXTURE/install-root.log"
    chown -R 65534:65534 -- "$FIXTURE"
    runuser -u nobody -- /usr/bin/bash "$PROJECT/tests/startup_test.sh" --inner "$FIXTURE"
  else
    /usr/bin/bash "$PROJECT/tests/startup_test.sh" --inner "$FIXTURE"
  fi
  exit
fi
FIXTURE=${2:?Missing test fixture}
export HOME="$FIXTURE/home"
export XDG_CONFIG_HOME="$FIXTURE/"'config space % $ " \ ` & (test) * ? [literal]'
export XDG_RUNTIME_DIR="$FIXTURE/runtime"
export XDG_STATE_HOME="$FIXTURE/state space"
export XDG_SESSION_TYPE=wayland WAYLAND_DISPLAY=wayland-test
export XDG_CURRENT_DESKTOP=KDE DBUS_SESSION_BUS_ADDRESS=unix:path=/test/bus
export LG_TEST_LOG="$FIXTURE/systemctl.log"
export LG_TEST_CAPTURE_LOG="$FIXTURE/capture.log"
unset DISPLAY XAUTHORITY XDG_SESSION_DESKTOP
mkdir -p "$HOME" "$XDG_RUNTIME_DIR" "$FIXTURE/bin"
export PATH="$FIXTURE/bin:$PATH"
cat > "$FIXTURE/bin/systemctl" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$LG_TEST_LOG"
case "$*" in
  *'show -p LoadState --value '*) printf '%s\n' "${LG_TEST_LOAD_STATE:-loaded}"; exit "${LG_TEST_LOAD_RESULT:-0}" ;;
  *'show -p ActiveState --value '*) printf 'inactive\n' ;;
  *reset-failed*) printf 'Unit not loaded\n' >&2; exit 1 ;;
esac
EOF
chmod +x "$FIXTURE/bin/systemctl"
cat > "$ROOT/_build/host/looking-glass-host" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$@" > "$LG_TEST_CAPTURE_LOG"
printf 'TEST: capture process started\n'
EOF
printf '#!/usr/bin/env bash\nprintf "TEST: guest GPU diagnostics\\n"\n' > "$ROOT/doctor.sh"
chmod +x "$ROOT/_build/host/looking-glass-host"
cat > "$ROOT/setup-ivshmem.sh" <<'EOF'
#!/usr/bin/env bash
printf 'TEST: device permissions %s\n' "$*" >> "$LG_TEST_LOG"
EOF
cat > "$FIXTURE/bin/sudo" <<'EOF'
#!/usr/bin/env bash
exec "$@"
EOF
chmod +x "$FIXTURE/bin/sudo"
printf '#!/bin/sh\nexit 0\n' > "$ROOT/scripts/ensure-deps.sh"
mkdir -p "$ROOT/overlay" "$ROOT/tools"
printf 'source version one\n' > "$ROOT/overlay/test.c"
printf 'frame verifier source\n' > "$ROOT/tools/test.c"
printf 'LG_COMMIT=test-upstream\n' > "$ROOT/versions.env"
printf '_build/\n' > "$ROOT/.gitignore"
cat > "$ROOT/build.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$ROOT/scripts/build-info.sh"
printf 'TEST: build invoked\n' >> "$LG_TEST_LOG"
lgRecordBuild "$ROOT" "$ROOT/_build/host/looking-glass-host" test-upstream
EOF
git -C "$ROOT" init -q --initial-branch=main
git -C "$ROOT" add .
git -C "$ROOT" -c user.name=Test -c user.email=test@example.invalid commit -qm fixture
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
verification=$(systemd-analyze --user verify "$SERVICE" 2>&1)
[[ -z $verification ]]
pattern=$(sed -n 's/^EnvironmentFile=//p' "$SERVICE")
[[ $pattern == /* ]]
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror "$ROOT/tests/environment_file_path_test.c" -o "$FIXTURE/envfile-path-test"
# The generator escapes all percent signs; systemd's %% expansion is literal %.
"$FIXTURE/envfile-path-test" "${pattern//%%/%}" "$OPTIONS"

# Reproduce the actual user failure: verify may return zero while discarding
# the quoted EnvironmentFile path. Repair must remove those literal quotes.
sed 's|^EnvironmentFile=.*|EnvironmentFile="/tmp/lg-old-options.env"|' "$SERVICE" > "$FIXTURE/broken.service"
broken=$(systemd-analyze --user verify "$FIXTURE/broken.service" 2>&1 || true)
[[ $broken == *'EnvironmentFile= path is not absolute'* ]]
cp "$FIXTURE/broken.service" "$SERVICE"
bash "$ROOT/setup-startup.sh" repair
pattern=$(sed -n 's/^EnvironmentFile=//p' "$SERVICE")
"$FIXTURE/envfile-path-test" "${pattern//%%/%}" "$OPTIONS"
grep -Fxq 'LG_FPS=120' "$OPTIONS"

# A warning with exit code zero must block installation and keep the old unit.
# A stale managed file + an absent manager unit must not abort installation.
: > "$LG_TEST_LOG"
LG_TEST_LOAD_STATE=not-found LG_TEST_LOAD_RESULT=1 bash "$ROOT/setup-startup.sh" stop
! grep -Fxq -- '--user stop looking-glass-host.service' "$LG_TEST_LOG"
# A real stop error must remain an error (do not blindly ignore failures).
mkdir -p "$FIXTURE/stop-error"
printf '#!/usr/bin/env bash\nif [[ $* == *" stop "* ]]; then exit 1; fi\nprintf "loaded\\n"\n' > "$FIXTURE/stop-error/systemctl"
chmod +x "$FIXTURE/stop-error/systemctl"
if PATH="$FIXTURE/stop-error:$PATH" bash "$ROOT/setup-startup.sh" stop; then
  printf 'FAIL: real service-stop failure was swallowed\n' >&2; exit 1
fi

# A warning with exit code zero must block installation and keep the old unit.
mkdir -p "$FIXTURE/warning-tool"
printf '#!/usr/bin/env bash\nprintf "test parser warning\\n" >&2\nexit 0\n' > "$FIXTURE/warning-tool/systemd-analyze"
chmod +x "$FIXTURE/warning-tool/systemd-analyze"
cp "$SERVICE" "$FIXTURE/good.service"
if PATH="$FIXTURE/warning-tool:$PATH" bash "$ROOT/setup-startup.sh" repair; then
  printf 'FAIL: zero-exit parser warning was ignored\n' >&2; exit 1
fi
cmp "$SERVICE" "$FIXTURE/good.service"

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
grep -Fxq -- '--user import-environment WAYLAND_DISPLAY XDG_SESSION_TYPE XDG_CURRENT_DESKTOP XDG_CONFIG_HOME XDG_STATE_HOME XDG_RUNTIME_DIR DBUS_SESSION_BUS_ADDRESS' "$LG_TEST_LOG"

# GNOME uses the same path. Missing desktop state and X11 must not start capture.
export XDG_CURRENT_DESKTOP=GNOME
bash "$ROOT/setup-startup.sh" restart
grep -Fxq -- '--user --no-block restart looking-glass-host.service' "$LG_TEST_LOG"
# The selection command stops the managed service, opens a fresh manual chooser
# once, and leaves saved startup preferences unchanged. Ordinary runs restore.
cp "$OPTIONS" "$FIXTURE/options-before"
bash "$ROOT/setup-startup.sh" choose-display
grep -Fxq -- '--user stop looking-glass-host.service' "$LG_TEST_LOG"
grep -Fxq 'pipewire:restore=no' "$LG_TEST_CAPTURE_LOG"
! grep -Fxq -- '--choose-display' "$LG_TEST_CAPTURE_LOG"
cmp "$OPTIONS" "$FIXTURE/options-before"
bash "$ROOT/run-guest.sh"
grep -Fxq 'pipewire:restore=yes' "$LG_TEST_CAPTURE_LOG"
LG_CAPTURE_RESTORE=no bash "$ROOT/run-guest.sh" 'app:license=no'
grep -Fxq 'pipewire:restore=no' "$LG_TEST_CAPTURE_LOG"
grep -Fxq 'app:license=no' "$LG_TEST_CAPTURE_LOG"
LG_CAPTURE_RESTORE=yes bash "$ROOT/run-guest.sh" --choose-display
grep -Fxq 'pipewire:restore=no' "$LG_TEST_CAPTURE_LOG"
REPORTS="$XDG_STATE_HOME/looking-glass-linux-wayland/reports"
[[ -d $REPORTS ]]
for ((attempt=0; attempt<100; ++attempt)); do
  if grep -l 'TEST: capture process started' "$REPORTS"/*/capture.log >/dev/null; then break; fi
  sleep 0.05
done
grep -l 'TEST: capture process started' "$REPORTS"/*/capture.log >/dev/null
grep -l 'TEST: guest GPU diagnostics' "$REPORTS"/*/diagnostics.log >/dev/null
for report in "$REPORTS"/*; do
  [[ $(stat -c %a "$report") == 700 ]]
  [[ $(stat -c %a "$report/capture.log") == 600 ]]
  [[ $(stat -c %a "$report/diagnostics.log") == 600 ]]
done
before=$(find "$REPORTS" -mindepth 1 -maxdepth 1 -type d | wc -l)
if LG_CAPTURE_RESTORE=invalid bash "$ROOT/run-guest.sh"; then
  printf 'FAIL: invalid capture restore setting was accepted\n' >&2; exit 1
fi
after=$(find "$REPORTS" -mindepth 1 -maxdepth 1 -type d | wc -l)
(( after > before ))
for ((attempt=0; attempt<100; ++attempt)); do
  if grep -l 'LG_CAPTURE_RESTORE must be yes or no' "$REPORTS"/*/capture.log >/dev/null; then break; fi
  sleep 0.05
done
grep -l 'LG_CAPTURE_RESTORE must be yes or no' "$REPORTS"/*/capture.log >/dev/null
: > "$LG_TEST_LOG"
XDG_SESSION_TYPE=x11 bash "$ROOT/scripts/start-session.sh"
[[ ! -s $LG_TEST_LOG ]]

# The one-command install uses this checkout, records its build and repairs
# the service. Version output distinguishes source/binary changes afterward.
bash "$ROOT/install.sh" > "$FIXTURE/install.log"
grep -Fxq 'TEST: build invoked' "$LG_TEST_LOG"
grep -q 'TEST: device permissions --user .* --install' "$LG_TEST_LOG"
[[ -f $XDG_CONFIG_HOME/looking-glass-linux-wayland/choose-next ]]
bash "$ROOT/run-guest.sh"
grep -Fxq 'pipewire:restore=no' "$LG_TEST_CAPTURE_LOG"
[[ ! -e $XDG_CONFIG_HOME/looking-glass-linux-wayland/choose-next ]]
bash "$ROOT/run-guest.sh"
grep -Fxq 'pipewire:restore=yes' "$LG_TEST_CAPTURE_LOG"
[[ -f $ROOT/_build/build-info.txt ]]
grep -Fxq "project_commit=$(git -C "$ROOT" rev-parse HEAD)" "$ROOT/_build/build-info.txt"
grep -Fxq 'project_branch=main' "$ROOT/_build/build-info.txt"
source "$ROOT/scripts/build-info.sh"
[[ $(lgBuildValue "$ROOT" binary_sha256) =~ ^[[:xdigit:]]{64}$ ]]
bash "$ROOT/version.sh" > "$FIXTURE/version.log"
! grep -q 'MISMATCH' "$FIXTURE/version.log"
printf 'source version two\n' > "$ROOT/overlay/test.c"
bash "$ROOT/version.sh" > "$FIXTURE/version.log"
grep -q 'SOURCE MISMATCH' "$FIXTURE/version.log"
printf '# changed executable\n' >> "$ROOT/_build/host/looking-glass-host"
bash "$ROOT/version.sh" > "$FIXTURE/version.log"
grep -q 'BINARY MISMATCH' "$FIXTURE/version.log"
: > "$LG_TEST_LOG"
if WAYLAND_DISPLAY= bash "$ROOT/setup-startup.sh" start; then
  printf 'FAIL: startup accepted a missing Wayland display\n' >&2; exit 1
fi
[[ ! -s $LG_TEST_LOG ]]

# Removing startup stops the unit and keeps preferences and portal restore data.
printf 'private portal restore token\n' > "$XDG_CONFIG_HOME/looking-glass-linux-wayland/restore-token"
bash "$ROOT/setup-startup.sh" stop
bash "$ROOT/setup-startup.sh" uninstall
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
printf 'PASS: missing-unit install/first start, real stop errors, device setup, fresh choice once, unit validation, build identity and private reports\n'
