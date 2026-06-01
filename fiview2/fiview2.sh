#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# fiview2 launch wrapper
#
# Workaround: NVIDIA driver/library version mismatch on this machine.
# Forces Mesa software renderer (softpipe) as fallback.
# Remove these exports once the NVIDIA driver is updated.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN="${SCRIPT_DIR}/../build/bin/fiview2"

if [[ ! -x "$BIN" ]]; then
    echo "fiview2: binary not found: $BIN"
    echo "Run: cmake --build build --target fiview2"
    exit 1
fi

# Try native GPU first; fall back to Mesa software renderer
if "$BIN" "$@" 2>/dev/null; then
    exit 0
fi

echo "fiview2: GPU context failed, trying Mesa software renderer..."
LIBGL_ALWAYS_SOFTWARE=1 \
MESA_LOADER_DRIVER_OVERRIDE=softpipe \
__GLX_VENDOR_LIBRARY_NAME=mesa \
"$BIN" "$@"
