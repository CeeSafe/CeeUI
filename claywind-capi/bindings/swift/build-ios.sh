#!/bin/sh
# Builds the Swift counter as an iOS app: `./build-ios.sh simulator [run]` for the simulator (signed ad hoc; run
# installs it on the booted simulator and launches it), `./build-ios.sh device [run]` for an iPhone or iPad (signed
# with the keychain's Apple Development identity and the newest fitting provisioning profile, as claywind-pack signs;
# run installs it on the connected device with devicectl and launches it). The app is build/ios-<which>/
# ClayWindCounter.app: claywind-capi's archive for the target, the ClayWind module and the counter compiled with swiftc.
# ClayWind.run starts the application (CeeSafe ADR 0200), so main.swift is the same as on the Mac. Needs Xcode and
# ceesafec 0.86.0 or later (CEESAFEC). IDENTITY and PROFILE choose another identity or profile. Run from
# bindings/swift.
set -e
which=${1:-simulator}
here=$(cd "$(dirname "$0")" && pwd)
capi="$here/../.."
out="$here/build/ios-$which"
ceesafec=${CEESAFEC:-ceesafec}
id=org.claywind.swiftcounter
case "$which" in
simulator) sdkname=iphonesimulator target=arm64-apple-ios17.0-simulator platform=iPhoneSimulator ;;
device) sdkname=iphoneos target=arm64-apple-ios17.0 platform=iPhoneOS ;;
*) echo "usage: build-ios.sh simulator|device [run]" >&2; exit 2 ;;
esac
sdk=$(xcrun --sdk $sdkname --show-sdk-path)
clang=$(xcrun --sdk $sdkname -f clang)
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
xcrun --sdk $sdkname swiftc -target "$target" -sdk "$sdk" -parse-as-library -emit-module -emit-object \
    -module-name ClayWind -I "$out/module" -emit-module-path "$out/ClayWind.swiftmodule" \
    "$here/Sources/ClayWind/ClayWind.swift" -o "$out/ClayWind.o"
# The main thread's stack is 1 MiB on iOS; 16 MiB, as claywind-pack gives an app.
xcrun --sdk $sdkname swiftc -target "$target" -sdk "$sdk" -I "$out" -I "$out/module" \
    "$here/Sources/Counter/main.swift" "$out/ClayWind.o" "$out/capi/claywind_capi.a" \
    -Xlinker -stack_size -Xlinker 0x1000000 -o "$app/ClayWindCounter"
cat > "$app/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleExecutable</key><string>ClayWindCounter</string>
	<key>CFBundleIdentifier</key><string>$id</string>
	<key>CFBundleName</key><string>ClayWindCounter</string>
	<key>CFBundleDisplayName</key><string>Swift Counter</string>
	<key>CFBundleShortVersionString</key><string>1.0</string>
	<key>CFBundlePackageType</key><string>APPL</string>
	<key>CFBundleVersion</key><string>1</string>
	<key>MinimumOSVersion</key><string>17.0</string>
	<key>UIDeviceFamily</key><array><integer>1</integer><integer>2</integer></array>
	<key>UILaunchScreen</key><dict/>
	<key>CFBundleSupportedPlatforms</key><array><string>$platform</string></array>
</dict>
</plist>
EOF

if [ "$which" = simulator ]; then
    codesign -s - --force "$app" >/dev/null 2>&1
else
    # The newest unexpired profile for this app's id, else a wildcard one (claywind-pack's findProfile).
    profile=${PROFILE:-}
    if [ -z "$profile" ]; then
        now=$(date -u +%Y-%m-%dT%H:%M:%SZ)
        best="" bestExact=0 bestExpiry=""
        for p in "$HOME/Library/Developer/Xcode/UserData/Provisioning Profiles"/*.mobileprovision \
                 "$HOME/Library/MobileDevice/Provisioning Profiles"/*.mobileprovision; do
            [ -f "$p" ] || continue
            plist=$(security cms -D -i "$p" 2>/dev/null)
            appid=$(printf '%s' "$plist" | plutil -extract Entitlements.application-identifier raw - 2>/dev/null || true)
            expiry=$(printf '%s' "$plist" | plutil -extract ExpirationDate raw - 2>/dev/null || true)
            pattern=${appid#*.}
            exact=0
            [ "$pattern" = "$id" ] && exact=1
            [ $exact = 1 ] || [ "$pattern" = "*" ] || continue
            [ "$expiry" \> "$now" ] || continue
            if [ -z "$best" ] || [ $exact -gt $bestExact ] || { [ $exact = $bestExact ] && [ "$expiry" \> "$bestExpiry" ]; }; then
                best=$p bestExact=$exact bestExpiry=$expiry
            fi
        done
        profile=$best
    fi
    [ -n "$profile" ] || { echo "build-ios.sh: no provisioning profile fits $id: build any app for the device in Xcode once, or set PROFILE" >&2; exit 1; }
    appid=$(security cms -D -i "$profile" | plutil -extract Entitlements.application-identifier raw -)
    team=${appid%%.*}
    identity=${IDENTITY:-$(security find-identity -v -p codesigning | grep '"Apple Development:' | head -1 | awk '{print $2}')}
    [ -n "$identity" ] || { echo "build-ios.sh: no \"Apple Development\" identity in the keychain; set IDENTITY" >&2; exit 1; }
    cp "$profile" "$app/embedded.mobileprovision"
    cat > "$out/entitlements.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0">
<dict>
	<key>application-identifier</key><string>$team.$id</string>
	<key>com.apple.developer.team-identifier</key><string>$team</string>
	<key>get-task-allow</key><true/>
	<key>keychain-access-groups</key><array><string>$team.$id</string></array>
</dict>
</plist>
EOF
    codesign -f -s "$identity" --entitlements "$out/entitlements.plist" "$app"
fi
echo "built $app"

if [ "${2:-}" = "run" ]; then
    if [ "$which" = simulator ]; then
        xcrun simctl install booted "$app"
        xcrun simctl launch booted "$id"
    else
        device=${DEVICE:-$(xcrun devicectl list devices 2>/dev/null | awk '/physical/ && /iPhone|iPad/ && (/ connected / || / available /) {for (k = 1; k <= NF; k++) if ($k ~ /^[0-9A-F]+-[0-9A-F]+$/) {print $k; exit}}')}
        [ -n "$device" ] || { echo "build-ios.sh: no connected iPhone or iPad (devicectl list devices); set DEVICE" >&2; exit 1; }
        xcrun devicectl device install app --device "$device" "$app"
        xcrun devicectl device process launch --device "$device" "$id"
    fi
fi
