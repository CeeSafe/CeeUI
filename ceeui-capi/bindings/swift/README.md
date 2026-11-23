# CeeUI for Swift

A Swift layer over CeeUI's C API (`../../include/ceeui.h`): a window laid out each frame by a Swift closure, in
immediate mode, with `String`s, `inout` state and closures for nesting. `Sources/Counter` is the C example in Swift.

```swift
import CeeUI

var count = 0
var name = "World"
CeeUI.run(title: "Counter", width: 480, height: 360) { f in
    f.heading("Counter")
    f.panel {
        f.label("Pressed \(count) times")
        if f.button("add", "Add one") { count += 1 }
    }
    f.textField("name", name: "Your name", text: &name)
    f.label("Hello, \(name)!")
}
```

## Building

The package links ceeui-capi's archive, so build that first, for the package's macOS version:

```sh
cd ceeui-capi
MACOSX_DEPLOYMENT_TARGET=13.0 ceesafec build   # out/ceeui_capi.a and out/ceesafe/embed.h
cd bindings/swift
swift build
.build/debug/counter
```

Without `MACOSX_DEPLOYMENT_TARGET` the archive is built for the Mac's own macOS version, and the linker warns that it
is newer than the package's.

## iOS

`./build-ios.sh simulator run` builds the counter for the iOS simulator and launches it on the booted one;
`./build-ios.sh device run` builds it for an iPhone or iPad, signs it with the keychain's Apple Development identity
and the newest provisioning profile for `org.ceeui.swiftcounter` or a wildcard one (as ceeui-pack signs), and
installs and launches it on the connected device (`DEVICE` names another; `IDENTITY` and `PROFILE` override the
choice). `CeeUI.run` starts the application (`ceesafe_window_start`, CeeSafe ADR 0200): the first call runs it and
does not return, and the launched application runs `main.swift`'s top-level code again, where `run` goes on. So keep
what comes before `run` to declarations. It needs ceesafec 0.86.0 or later.

## The API

- `CeeUI.run(title:width:height:frame:)` opens a window and runs it until it is closed (`RunResult`). It starts
  CeeSafe's runtime the first time. Call it on the main thread.
- `Frame`: `column(gap:)`, `row(gap:)` and `panel` take a closure for their children and end themselves; `spacer`,
  `label`, `caption`, `heading`; `button` (true in the frame after a press), `checkbox(_:_:isOn:)`,
  `textField(_:name:text:maxBytes:)`; `isDark` and `requestFrame`.
- As in C, a control's result arrives in the frame after the user acted; CeeUI asks for that frame, and for one
  after it, so the closure's changes show at once. A `Frame` is valid only during the closure's call.

## Status

Checked on macOS 27 with real mouse and key events: typed text reached the Swift `String` and its greeting, Add one
counted, and the Dark checkbox switched the theme. On the iOS 26 simulator (iPhone 17 Pro) the counter is laid out
inside the safe area; its input has not been driven there (no Simulator app on the machine checked), so taps and the
soft keyboard are for a device. Not checked on an iPhone yet.
