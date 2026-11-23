#!/bin/sh
# Builds examples/counter.c against ceeui-capi: the library first (ceesafec build), then the C program linked with
# it. Run from ceeui-capi, on macOS, Linux, or Windows in Git's bash (with clang, as ceesafec uses there).
set -e
ceesafec build --emit lib
case "$(uname -s)" in
Darwin)
    cc=${CC:-cc}
    $cc -std=c11 -Wall -Wextra -Iinclude -Iout -c examples/counter.c -o out/counter.o
    $cc out/counter.o out/ceeui_capi.a -o out/counter \
        -framework AppKit -framework QuartzCore -framework CoreText -framework CoreGraphics
    ;;
MINGW* | MSYS* | CYGWIN*)
    # The runtime's objects name the Windows libraries they need (#pragma comment(lib)), as clang-cl's linker reads.
    cc=${CC:-clang}
    $cc -std=c11 -Wall -Wextra -Iinclude -Iout -c examples/counter.c -o out/counter.o
    $cc out/counter.o out/ceeui_capi.a -o out/counter.exe
    ;;
*)
    # The window runtime opens X11 and Wayland with dlopen.
    cc=${CC:-cc}
    $cc -std=c11 -Wall -Wextra -Iinclude -Iout -c examples/counter.c -o out/counter.o
    $cc out/counter.o out/ceeui_capi.a -o out/counter -lm -lpthread -ldl
    ;;
esac
echo "built out/counter"
