# ClayWind for Swift

A Swift layer over ClayWind's C API (`../../include/claywind.h`): a window laid out each frame by a Swift closure, in
immediate mode, with `String`s, `inout` state and closures for nesting. `Sources/Counter` is the C example in Swift.

```swift
import ClayWind

var count = 0
var name = "World"
ClayWind.run(title: "Counter", width: 480, height: 360) { f in
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

The package links claywind-capi's archive, so build that first, for the package's macOS version:

```sh
cd claywind-capi
MACOSX_DEPLOYMENT_TARGET=13.0 ceesafec build   # out/claywind_capi.a and out/ceesafe/embed.h
cd bindings/swift
swift build
.build/debug/counter
```

Without `MACOSX_DEPLOYMENT_TARGET` the archive is built for the Mac's own macOS version, and the linker warns that it
is newer than the package's.

## The API

- `ClayWind.run(title:width:height:frame:)` opens a window and runs it until it is closed (`RunResult`). It starts
  CeeSafe's runtime the first time. Call it on the main thread.
- `Frame`: `column(gap:)`, `row(gap:)` and `panel` take a closure for their children and end themselves; `spacer`,
  `label`, `caption`, `heading`; `button` (true in the frame after a press), `checkbox(_:_:isOn:)`,
  `textField(_:name:text:maxBytes:)`; `isDark` and `requestFrame`.
- As in C, a control's result arrives in the frame after the user acted; ClayWind asks for that frame, and for one
  after it, so the closure's changes show at once. A `Frame` is valid only during the closure's call.

## Status

Checked on macOS 27 with real mouse and key events: typed text reached the Swift `String` and its greeting, Add one
counted, and the Dark checkbox switched the theme. Linux and Windows are not set up; Kotlin and Rust come next.
