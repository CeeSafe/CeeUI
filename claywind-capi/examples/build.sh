#!/bin/sh
# Builds examples/counter.c against claywind-capi: the library first (ceesafec build), then the C program linked with
# it. Run from claywind-capi. macOS and Linux.
set -e
ceesafec build --emit lib
cc=${CC:-cc}
$cc -std=c11 -Wall -Wextra -Iinclude -Iout -c examples/counter.c -o out/counter.o
case "$(uname -s)" in
Darwin)
    $cc out/counter.o out/claywind_capi.a -o out/counter \
        -framework AppKit -framework QuartzCore -framework CoreText -framework CoreGraphics
    ;;
*)
    $cc out/counter.o out/claywind_capi.a -o out/counter -lm -lpthread -ldl
    ;;
esac
echo "built out/counter"
