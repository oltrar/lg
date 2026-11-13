#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
mkdir -p "$ROOT/_build/tests"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -O1 -g \
  "$ROOT/tests/frame_layout_test.c" -o "$ROOT/_build/tests/frame_layout_test"
"$ROOT/_build/tests/frame_layout_test"
bash "$ROOT/tests/overlay_upgrade_test.sh"
bash "$ROOT/tests/ivshmem_permissions_test.sh"
"${CC:-cc}" -std=gnu11 -D_GNU_SOURCE -Wall -Werror -O1 -g \
  "$ROOT/tests/ivshmem_pci_test.c" \
  "$ROOT/overlay/common/src/platform/linux/ivshmem_pci.c" \
  -o "$ROOT/_build/tests/ivshmem_pci_test"
"$ROOT/_build/tests/ivshmem_pci_test"
if [[ ${LG_WITH_BACKEND_TEST:-0} == 1 ]]; then
  SRC="$ROOT/_upstream/LookingGlass"
  "${CC:-cc}" -std=gnu11 -D_GNU_SOURCE -Wall -Werror -O1 \
    $(pkg-config --cflags libspa-0.2) \
    "$ROOT/tests/format_negotiation_test.c" -o "$ROOT/_build/tests/format_negotiation_test"
  "$ROOT/_build/tests/format_negotiation_test"
  "${CC:-cc}" -std=gnu11 -D_GNU_SOURCE -Wall -Werror -O1 \
    -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -I "$SRC/host/include" -I "$SRC/common/include" \
    -I "$SRC/host/platform/Linux/capture/pipewire/src" -I "$SRC" \
    $(pkg-config --cflags libpipewire-0.3) \
    "$ROOT/tests/backend_frame_test.c" "$ROOT/tests/ivshmem_test_debug.c" \
    -o "$ROOT/_build/tests/backend_frame_test"
  "$ROOT/_build/tests/backend_frame_test"
  "${CC:-cc}" -std=gnu11 -D_GNU_SOURCE -Wall -Werror -O1 -msse4.1 \
    -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -I "$SRC/common/include" \
    "$ROOT/tests/frame_alignment_test.c" "$ROOT/tests/ivshmem_test_debug.c" \
    "$SRC/common/src/framebuffer.c" "$SRC/common/src/cpuinfo.c" \
    -o "$ROOT/_build/tests/frame_alignment_test"
  "$ROOT/_build/tests/frame_alignment_test"
  "${CC:-cc}" -std=gnu11 -D_GNU_SOURCE -Wall -Werror -O1 \
    -I "$SRC/common/include" -I "$SRC/host/platform/Linux/capture/pipewire/src" \
    $(pkg-config --cflags gio-2.0 gio-unix-2.0) \
    "$ROOT/tests/portal_dbus_test.c" "$ROOT/tests/ivshmem_test_debug.c" \
    "$ROOT/overlay/host/platform/Linux/capture/pipewire/src/portal.c" \
    $(pkg-config --libs gio-2.0 gio-unix-2.0) \
    -o "$ROOT/_build/tests/portal_dbus_test"
  timeout 30s "$ROOT/_build/tests/portal_dbus_test"
  "${CC:-cc}" -std=gnu11 -D_GNU_SOURCE -Wall -Werror -O1 \
    -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -I "$SRC/common/include" -I "$SRC" \
    "$ROOT/tests/ivshmem_transport_test.c" "$ROOT/tests/ivshmem_test_debug.c" \
    "$ROOT/overlay/common/src/platform/linux/ivshmem_pci.c" \
    -o "$ROOT/_build/tests/ivshmem_transport_test"
  "$ROOT/_build/tests/ivshmem_transport_test"
  bash "$ROOT/tests/startup_test.sh"
fi
