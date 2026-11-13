#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
SRC="$ROOT/_upstream/LookingGlass"
FIXTURE=$(mktemp -d)
cleanup() {
  if [[ -n ${PW_PID:-} ]]; then kill "$PW_PID" 2>/dev/null || true; wait "$PW_PID" 2>/dev/null || true; fi
  rm -rf -- "$FIXTURE"
}
trap cleanup EXIT
export XDG_RUNTIME_DIR="$FIXTURE" PIPEWIRE_RUNTIME_DIR="$FIXTURE"
export XDG_CONFIG_HOME="$FIXTURE/config" XDG_CURRENT_DESKTOP=KDE
mkdir -p "$XDG_CONFIG_HOME"
cc -std=gnu11 -D_GNU_SOURCE -Wall -Werror -O1 \
  -I "$ROOT/_build/host/platform/Linux/capture/pipewire" \
  -I "$SRC/host/platform/Linux/capture/pipewire/src" \
  $(pkg-config --cflags gio-2.0 gio-unix-2.0 libpipewire-0.3 wayland-server) \
  "$ROOT/tests/pipewire_lgmp_test.c" \
  "$ROOT/_build/host/platform/Linux/capture/pipewire/zkde-screencast-protocol.c" \
  $(pkg-config --libs gio-2.0 gio-unix-2.0 libpipewire-0.3 wayland-server) \
  -o "$ROOT/_build/tests/pipewire_lgmp_test"
pipewire > "$FIXTURE/daemon.log" 2>&1 &
PW_PID=$!
for ((i=0; i<100; ++i)); do [[ ! -S $FIXTURE/pipewire-0 ]] || break; sleep .05; done
[[ -S $FIXTURE/pipewire-0 ]] || { cat "$FIXTURE/daemon.log"; exit 1; }
truncate -s 64M "$FIXTURE/shm"
if ! timeout 60s "$ROOT/_build/tests/pipewire_lgmp_test" \
    "$ROOT/_build/host/looking-glass-host" "$ROOT/_build/lg-frame-check" "$FIXTURE/shm" "$FIXTURE"; then
  cat "$FIXTURE/daemon.log"; exit 1
fi
truncate -s 0 "$FIXTURE/shm"; truncate -s 64M "$FIXTURE/shm"
if ! timeout 60s "$ROOT/_build/tests/pipewire_lgmp_test" \
    "$ROOT/_build/host/looking-glass-host" "$ROOT/_build/lg-frame-check" "$FIXTURE/shm" "$FIXTURE" --kde; then
  cat "$FIXTURE/daemon.log"; exit 1
fi
