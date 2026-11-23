#!/bin/sh
# Builds the Kotlin counter as an Android app (build/android/CeeUICounter.apk) for arm64-v8a: ceeui-capi's
# archive for Android, the JNI library over it as the NativeActivity's library (libceeui_jni.so, whose `main` calls
# the activity's ceeuiMain(): CeeSafe ADR 0200), the Kotlin compiled to dex, packed and signed with the debug key.
# Needs ceesafec 0.86.0 or later (CEESAFEC), the Android SDK with an NDK (ANDROID_HOME, else ~/Library/Android/sdk), a
# JDK and kotlinc (KOTLINC, else on PATH, else Android Studio's). Run from bindings/kotlin.
set -e
here=$(cd "$(dirname "$0")" && pwd)
capi="$here/../.."
out="$here/build/android"
ceesafec=${CEESAFEC:-ceesafec}
sdk=${ANDROID_HOME:-$HOME/Library/Android/sdk}
newest() { ls "$1" | sort -V | tail -1; }
ndk="$sdk/ndk/$(newest "$sdk/ndk")"
tools="$sdk/build-tools/$(newest "$sdk/build-tools")"
jar="$sdk/platforms/$(newest "$sdk/platforms")/android.jar"
bin="$ndk/toolchains/llvm/prebuilt/$(newest "$ndk/toolchains/llvm/prebuilt")/bin"
kotlinc=${KOTLINC:-$(command -v kotlinc || echo "/Applications/Android Studio.app/Contents/plugins/Kotlin/kotlinc/bin/kotlinc")}
stdlib="$(dirname "$kotlinc")/../lib/kotlin-stdlib.jar"
cc="$bin/aarch64-linux-android25-clang" # API level 25: CeeSafe's floor (its ADR 0188)
rm -rf "$out"
mkdir -p "$out/lib/arm64-v8a" "$out/classes" "$out/dex"

# The C API for Android, position-independent: it goes into a shared library.
(cd "$capi" && "$ceesafec" build --emit lib --target android --arch arm64 --cc "$cc" --cflag -fPIC \
    -o "$out/capi/ceeui_capi.a")
"$cc" -std=c11 -Wall -Wextra -O2 -shared -fPIC -Wl,--no-undefined -o "$out/lib/arm64-v8a/libceeui_jni.so" \
    -I"$capi/include" -I"$out/capi" "$here/src/main/c/ceeui_jni.c" "$out/capi/ceeui_capi.a" -landroid -llog -lm -ldl

# The Kotlin: the binding, the counter, the activity; then dex, with Kotlin's standard library.
"$kotlinc" -nowarn -cp "$jar" -d "$out/classes.jar" "$here/src/main/kotlin/org/ceeui/CeeUI.kt" \
    "$here/examples/Counter.kt" "$here/android/CounterActivity.kt" 2>&1 | grep -v '^warning: unable to find kotlin-stdlib' || true
[ -f "$out/classes.jar" ] || { echo "build-android.sh: kotlinc made no jar (above)" >&2; exit 1; }
"$tools/d8" --min-api 25 --lib "$jar" --output "$out/dex" "$out/classes.jar" "$stdlib"

# The APK: the manifest, the dex and the library, aligned and signed with the debug key.
"$tools/aapt2" link -o "$out/base.apk" -I "$jar" --manifest "$here/android/AndroidManifest.xml"
(cd "$out/dex" && zip -q "$out/base.apk" classes.dex)
(cd "$out" && zip -q -r base.apk lib)
"$tools/zipalign" -f -p 4 "$out/base.apk" "$out/aligned.apk"
# The debug key, made as Android Studio makes it when there is none (a fresh CI runner).
keystore="$HOME/.android/debug.keystore"
if [ ! -f "$keystore" ]; then
    mkdir -p "$HOME/.android"
    keytool -genkeypair -keystore "$keystore" -storepass android -keypass android -alias androiddebugkey \
        -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Android Debug,O=Android,C=US" >/dev/null
fi
"$tools/apksigner" sign --ks "$keystore" --ks-pass pass:android \
    --out "$out/CeeUICounter.apk" "$out/aligned.apk"
echo "built $out/CeeUICounter.apk"
