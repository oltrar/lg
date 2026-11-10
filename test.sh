#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
mkdir -p "$ROOT/_build/tests"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -O1 -g \
  "$ROOT/tests/frame_layout_test.c" -o "$ROOT/_build/tests/frame_layout_test"
"$ROOT/_build/tests/frame_layout_test"
if [[ ${LG_WITH_BACKEND_TEST:-0} == 1 ]]; then
  SRC="$ROOT/_upstream/LookingGlass"
  "${CC:-cc}" -std=gnu11 -D_GNU_SOURCE -Wall -Werror -O1 \
    -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -I "$SRC/host/include" -I "$SRC/common/include" \
    -I "$SRC/host/platform/Linux/capture/pipewire/src" -I "$SRC" \
    $(pkg-config --cflags libpipewire-0.3) \
    "$ROOT/tests/backend_frame_test.c" -o "$ROOT/_build/tests/backend_frame_test"
  "$ROOT/_build/tests/backend_frame_test"
fi
