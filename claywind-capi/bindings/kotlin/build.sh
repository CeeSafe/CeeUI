#!/bin/sh
# Builds ClayWind for Kotlin on macOS or Linux: the JNI library over claywind-capi's archive (build/libclaywind_jni,
# .dylib or .so), the Kotlin layer and the example as one jar (build/counter.jar); `./build.sh run` then runs the
# example. Needs a JDK (JAVA_HOME, else macOS's /usr/libexec/java_home) and kotlinc (KOTLINC, else on PATH, else
# Android Studio's on macOS). Run after `ceesafec build` in claywind-capi.
set -e
here=$(cd "$(dirname "$0")" && pwd)
capi="$here/../.."
out="$here/build"
mkdir -p "$out"
case "$(uname -s)" in
Darwin)
    java_home=${JAVA_HOME:-$(/usr/libexec/java_home)}
    platform=darwin
    library="$out/libclaywind_jni.dylib"
    link="-dynamiclib -framework AppKit -framework QuartzCore -framework CoreText -framework CoreGraphics"
    first_thread=-XstartOnFirstThread
    ;;
*)
    java_home=${JAVA_HOME:?set JAVA_HOME to a JDK}
    platform=linux
    library="$out/libclaywind_jni.so"
    # The window runtime opens X11 and Wayland with dlopen.
    link="-shared -fPIC -lm -lpthread -ldl"
    first_thread=
    ;;
esac
kotlinc=${KOTLINC:-$(command -v kotlinc || echo "/Applications/Android Studio.app/Contents/plugins/Kotlin/kotlinc/bin/kotlinc")}
[ -f "$capi/out/claywind_capi.a" ] || { echo "build.sh: no claywind_capi.a: run ceesafec build in claywind-capi" >&2; exit 1; }

# shellcheck disable=SC2086 # $link is several flags
${CC:-cc} -std=c11 -Wall -Wextra -O2 -o "$library" \
    -I"$java_home/include" -I"$java_home/include/$platform" -I"$capi/include" -I"$capi/out" \
    "$here/src/main/c/claywind_jni.c" "$capi/out/claywind_capi.a" $link
rm -f "$out/counter.jar"
"$kotlinc" -nowarn -include-runtime -d "$out/counter.jar" \
    "$here/src/main/kotlin/org/claywind/ClayWind.kt" "$here/examples/Counter.kt" 2>&1 | grep -v '^warning: unable to find kotlin-stdlib' || true
[ -f "$out/counter.jar" ] || { echo "build.sh: kotlinc made no jar (above)" >&2; exit 1; }
echo "built $library and $out/counter.jar"

if [ "${1:-}" = "run" ]; then
    # shellcheck disable=SC2086 # empty off macOS
    exec "$java_home/bin/java" $first_thread --enable-native-access=ALL-UNNAMED \
        -Djava.library.path="$out" -cp "$out/counter.jar" org.claywind.counter.CounterKt
fi
