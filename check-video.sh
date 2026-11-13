#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# A real B7 LGMP subscriber. This never reads QXL/SPICE video.
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
[[ -x $ROOT/_build/lg-frame-check ]] || { printf 'Build first: bash build.sh\n' >&2; exit 1; }
printf 'Waiting for a raw Looking Glass frame. Accept the guest monitor-sharing dialog if shown.\n'
exec "$ROOT/_build/lg-frame-check" "${LG_SHM:-ivshmem}" 150
