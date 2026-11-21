#!/bin/sh
# Builds the Swift counter as an iOS app for the simulator (build/ios-simulator/ClayWindCounter.app): claywind-capi's
# archive for the simulator, the ClayWind module and the counter compiled with swiftc against it, bundled and signed
# ad hoc. `./build-ios-simulator.sh run` then installs it on a booted simulator and launches it. ClayWind.run starts
# the application (CeeSafe ADR 0200), so the counter's main.swift is the same as on the Mac. Needs Xcode and ceesafec
# 0.86.0 or later (CEESAFEC). Run from bindings/swift.
set -e
here=$(cd "$(dirname "$0")" && pwd)
capi="$here/../.."
out="$here/build/ios-simulator"
ceesafec=${CEESAFEC:-ceesafec}
target=arm64-apple-ios17.0-simulator
sdk=$(xcrun --sdk iphonesimulator --show-sdk-path)
clang=$(xcrun --sdk iphonesimulator -f clang)
app="$out/ClayWindCounter.app"
rm -rf "$out"
mkdir -p "$out/module" "$app"

(cd "$capi" && "$ceesafec" build --emit lib --target ios --cc "$clang" --cflag -target --cflag "$target" \
    --cflag -isysroot --cflag "$sdk" -o "$out/capi/claywind_capi.a")
cat > "$out/module/module.modulemap" <<EOF
module CClayWind {
    header "$capi/include/claywind.h"
    header "$out/capi/ceesafe/embed.h"
    export *
}
EOF
xcrun --sdk iphonesimulator swiftc -target "$target" -sdk "$sdk" -parse-as-library -emit-module -emit-object \
    -module-name ClayWind -I "$out/module" -emit-module-path "$out/ClayWind.swiftmodule" \
    "$here/Sources/ClayWind/ClayWind.swift" -o "$out/ClayWind.o"
# The main thread's stack is 1 MiB on iOS; 16 MiB, as claywind-pack gives an app.
xcrun --sdk iphonesimulator swiftc -target "$target" -sdk "$sdk" -I "$out" -I "$out/module" \
    "$here/Sources/Counter/main.swift" "$out/ClayWind.o" "$out/capi/claywind_capi.a" \
    -Xlinker -stack_size -Xlinker 0x1000000 -o "$app/ClayWindCounter"
cat > "$app/Info.plist" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleExecutable</key><string>ClayWindCounter</string>
	<key>CFBundleIdentifier</key><string>org.claywind.swiftcounter</string>
	<key>CFBundleName</key><string>ClayWindCounter</string>
	<key>CFBundleDisplayName</key><string>Swift Counter</string>
	<key>CFBundleShortVersionString</key><string>1.0</string>
	<key>CFBundlePackageType</key><string>APPL</string>
	<key>CFBundleVersion</key><string>1</string>
	<key>MinimumOSVersion</key><string>17.0</string>
	<key>UIDeviceFamily</key><array><integer>1</integer><integer>2</integer></array>
	<key>UILaunchScreen</key><dict/>
	<key>CFBundleSupportedPlatforms</key><array><string>iPhoneSimulator</string></array>
</dict>
</plist>
EOF
codesign -s - --force "$app" >/dev/null 2>&1
echo "built $app"

if [ "${1:-}" = "run" ]; then
    xcrun simctl install booted "$app"
    xcrun simctl launch booted org.claywind.swiftcounter
fi
