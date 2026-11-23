#!/bin/sh
# SPDX-License-Identifier: MIT
# Packs the demo for a browser into out/pack/web with ceeui-pack's `web` target, which it builds first.
#   sh build.sh            needs ceesafec, a clang with the wasm64 target and wasm-ld
#   FONT_DIR=...           where the demo's three faces are found
set -e
cd "$(dirname "$0")"
FONT_DIR=${FONT_DIR:-/System/Library/Fonts/Supplemental}
(cd ../../ceeui-pack && ceesafec build)
# The demo's faces: a regular, a bold and a monospaced one, and a fallback with most scripts, fetched only when text
# needs it. They are the system's, copied for this machine only.
../../ceeui-pack/out/ceeui_pack web --name "CeeUI" \
    --font "$FONT_DIR/Arial.ttf" --font "$FONT_DIR/Arial Bold.ttf" --font "$FONT_DIR/Courier New.ttf" \
    --fallback "$FONT_DIR/Arial Unicode.ttf"
