#!/bin/sh
# Builds ClayWind for Kotlin on macOS: the JNI library over claywind-capi's archive (build/libclaywind_jni.dylib), the
# Kotlin layer and the example as one jar (build/counter.jar); `./build.sh run` then runs the example. Needs a JDK
# (JAVA_HOME, else /usr/libexec/java_home's) and kotlinc (KOTLINC, else on PATH, else Android Studio's). Run from
# bindings/kotlin, after `ceesafec build` in claywind-capi.
set -e
here=$(cd "$(dirname "$0")" && pwd)
capi="$here/../.."
out="$here/build"
mkdir -p "$out"
java_home=${JAVA_HOME:-$(/usr/libexec/java_home)}
kotlinc=${KOTLINC:-$(command -v kotlinc || echo "/Applications/Android Studio.app/Contents/plugins/Kotlin/kotlinc/bin/kotlinc")}
[ -f "$capi/out/claywind_capi.a" ] || { echo "build.sh: no claywind_capi.a: run ceesafec build in claywind-capi" >&2; exit 1; }

${CC:-cc} -std=c11 -Wall -Wextra -O2 -dynamiclib -o "$out/libclaywind_jni.dylib" \
    -I"$java_home/include" -I"$java_home/include/darwin" -I"$capi/include" -I"$capi/out" \
    "$here/src/main/c/claywind_jni.c" "$capi/out/claywind_capi.a" \
    -framework AppKit -framework QuartzCore -framework CoreText -framework CoreGraphics
"$kotlinc" -nowarn -include-runtime -d "$out/counter.jar" \
    "$here/src/main/kotlin/org/claywind/ClayWind.kt" "$here/examples/Counter.kt" 2>&1 | grep -v '^warning: unable to find kotlin-stdlib' || true
echo "built $out/libclaywind_jni.dylib and $out/counter.jar"

if [ "${1:-}" = "run" ]; then
    exec "$java_home/bin/java" -XstartOnFirstThread --enable-native-access=ALL-UNNAMED \
        -Djava.library.path="$out" -cp "$out/counter.jar" CounterKt
fi
