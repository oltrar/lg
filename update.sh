#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
git -C "$ROOT" fetch origin
# Git itself protects local edits and rejects divergent history. Never reset.
git -C "$ROOT" switch main
git -C "$ROOT" pull --ff-only origin main
exec bash "$ROOT/install.sh" "$@"
