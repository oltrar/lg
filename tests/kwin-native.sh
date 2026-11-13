#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Real KWin software/virtual compositor: per-app protocol authorization,
# native monitor selection, actual PipeWire frames and a fresh compositor boot.
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ ${1:-} != --inner ]]; then exec dbus-run-session -- bash "$0" --inner; fi
FIXTURE=$(mktemp -d)
cleanup() {
  for process in "${HOST_PID:-}" "${KWIN_PID:-}" "${PW_PID:-}"; do
    [[ -z $process ]] || kill "$process" 2>/dev/null || true
  done
  for process in "${HOST_PID:-}" "${KWIN_PID:-}" "${PW_PID:-}"; do
    [[ -z $process ]] || wait "$process" 2>/dev/null || true
  done
  rm -rf -- "$FIXTURE"
}
trap cleanup EXIT
export XDG_RUNTIME_DIR="$FIXTURE/runtime" PIPEWIRE_RUNTIME_DIR="$FIXTURE/runtime"
export XDG_CONFIG_HOME="$FIXTURE/config" XDG_DATA_HOME="$FIXTURE/data" XDG_CACHE_HOME="$FIXTURE/cache"
export WAYLAND_DISPLAY=lg-test XDG_SESSION_TYPE=wayland XDG_CURRENT_DESKTOP=KDE
export LIBGL_ALWAYS_SOFTWARE=1 KWIN_COMPOSE=O2
unset KWIN_WAYLAND_NO_PERMISSION_CHECKS
mkdir -p "$XDG_RUNTIME_DIR" "$XDG_CONFIG_HOME" "$XDG_DATA_HOME/applications" "$XDG_CACHE_HOME"
chmod 700 "$XDG_RUNTIME_DIR"
for program in looking-glass-host lg-kde-source; do
  cat > "$XDG_DATA_HOME/applications/lg-test-$program.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Looking Glass native test
Exec=$ROOT/_build/host/$program
NoDisplay=true
X-KDE-Wayland-Interfaces=zkde_screencast_unstable_v1
EOF
done
kbuildsycoca5 --noincremental
pipewire > "$FIXTURE/pipewire.log" 2>&1 & PW_PID=$!
for ((n=0;n<100;n++)); do [[ ! -S $XDG_RUNTIME_DIR/pipewire-0 ]] || break; sleep .05; done
connector=''
for boot in 1 2; do
  kwin_wayland --virtual --no-lockscreen --width 320 --height 240 --socket lg-test \
    > "$FIXTURE/kwin.log" 2>&1 & KWIN_PID=$!
  ready=0
  for ((n=0;n<100;n++)); do
    if [[ -S $XDG_RUNTIME_DIR/lg-test ]] && \
       "$ROOT/_build/host/lg-kde-source" > "$FIXTURE/outputs" 2> "$FIXTURE/list.log"; then ready=1; break; fi
    sleep .1
  done
  if (( ! ready )); then cat "$FIXTURE/kwin.log" "$FIXTURE/list.log"; exit 1; fi
  if [[ -z $connector ]]; then IFS=$'\t' read -r connector rest < "$FIXTURE/outputs"; fi
  truncate -s 0 "$FIXTURE/shm"; truncate -s 64M "$FIXTURE/shm"
  "$ROOT/_build/host/looking-glass-host" "app:shmFile=$FIXTURE/shm" app:capture=pipewire \
    "pipewire:kdeOutput=$connector" > "$FIXTURE/host.log" 2>&1 & HOST_PID=$!
  if ! "$ROOT/_build/lg-frame-check" "$FIXTURE/shm" 30; then
    cat "$FIXTURE/host.log" "$FIXTURE/kwin.log" "$FIXTURE/pipewire.log"; exit 1
  fi
  kill -INT "$HOST_PID"
  code=0; wait "$HOST_PID" || code=$?
  [[ $code == 16 ]] || { cat "$FIXTURE/host.log"; exit 1; }
  HOST_PID=''
  kill "$KWIN_PID"; wait "$KWIN_PID" || true; KWIN_PID=''
  printf 'PASS: real KWin native capture and B7 frame after compositor boot %s (permissions enabled)\n' "$boot"
done
