#!/bin/sh
# SPDX-License-Identifier: MIT
# Builds the web host into out/web: the module (web.wasm), the page, and the three fonts the demo draws with.
#   sh build.sh            needs ceesafec, a clang with the wasm64 target and wasm-ld
#   CLANG=... WASM_LD=... FONT_DIR=... override what is found
set -e
cd "$(dirname "$0")"
CLANG=${CLANG:-$( [ -x /opt/homebrew/opt/llvm/bin/clang ] && echo /opt/homebrew/opt/llvm/bin/clang || echo clang )}
WASM_LD=${WASM_LD:-wasm-ld}
FONT_DIR=${FONT_DIR:-/System/Library/Fonts/Supplemental}
OUT=out/web
rm -rf out/c "$OUT"
mkdir -p "$OUT/fonts"
ceesafec build --emit c -o out/c
objects=""
for source in out/c/*.c out/c/c/*.c; do
    object="${source%.c}.o"
    "$CLANG" --target=wasm64 -O2 -ffreestanding -nostdlib -mbulk-memory -Iout/c -c "$source" -o "$object"
    objects="$objects $object"
done
# What the program imports (web_wait and the rest, extern "C" in main.cee) is the worker's: left undefined, imported.
"$WASM_LD" -m wasm64 --no-entry --import-undefined --export=run --export=web_take -o "$OUT/web.wasm" $objects
cp page/index.html page/web.js page/worker.js page/serve.py "$OUT/"
# The demo's faces: a regular, a bold and a monospaced one. They are the system's, copied for this machine only.
cp "$FONT_DIR/Arial.ttf" "$OUT/fonts/regular.ttf"
cp "$FONT_DIR/Arial Bold.ttf" "$OUT/fonts/bold.ttf"
cp "$FONT_DIR/Courier New.ttf" "$OUT/fonts/mono.ttf"
echo "built $OUT ($(wc -c < "$OUT/web.wasm" | tr -d ' ') bytes of WebAssembly): python3 $OUT/serve.py, then http://localhost:8017/"
