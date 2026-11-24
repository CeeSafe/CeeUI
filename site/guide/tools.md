# Packing, hot reload, the inspector

This page covers the tools around an app: `ceeui-pack`, which makes a new app and packs one for a system;
`ceeui-dev`, which reloads an app when its source changes; the layout inspector; the headless host, which writes
frames to files; and the benchmark.

## Packing

`ceeui-pack` is a program of the repository. Build it once:

```sh
cd ceeui-pack && ceesafec build      # out/ceeui_pack
```

### A new app

```sh
ceeui-pack/out/ceeui_pack new tasks
cd tasks && ceesafec fetch && ceesafec build && out/tasks
```

`new NAME` makes the directory NAME, an app that builds and runs as it is: a window with a list of tasks to add
to, tick off and delete. Its state and frame are in `src/app.cee`, its window and memory in `src/main.cee`. The app
takes CeeUI's packages from GitHub. With `--ceeui PATH` it takes them from the checkout at PATH.
[Getting started](./getting-started) goes through the app.

### Packing an app

Run `ceeui-pack` in the app's package directory with a target. It writes under `out/pack`:

```sh
cd ceeui-hosts/gpu
../../ceeui-pack/out/ceeui_pack macos         --name "CeeUI GPU" --id com.example.gpu
../../ceeui-pack/out/ceeui_pack ios-simulator --name "CeeUI GPU" --id com.example.gpu
../../ceeui-pack/out/ceeui_pack ios           --name "CeeUI GPU" --id com.example.gpu
../../ceeui-pack/out/ceeui_pack android       --name "CeeUI GPU" --id com.example.gpu
```

| Target | What it makes | Install it with |
|---|---|---|
| `macos` | `out/pack/NAME.app`, signed ad hoc, for this Mac only | Open it |
| `ios-simulator` | `out/pack/ios-simulator/NAME.app` | `xcrun simctl install` |
| `ios` | `out/pack/ios/NAME.app`, signed for a device | `xcrun devicectl device install app` |
| `android` | `out/pack/NAME.apk`, a `NativeActivity` app signed with the debug key | `adb install` |
| `web` | `out/pack/web`, a page and a WebAssembly module | Serve it: see [The web](./web) |

For the first four it runs `ceesafec build --windowed` for the target, writes the `Info.plist` or the
`AndroidManifest.xml`, and runs the system's own tools.

| Option | What it sets | Default |
|---|---|---|
| `--name NAME` | The app's name on the system | The package's name |
| `--id ID` | The bundle identifier or Android package | `com.example.<name>` |
| `--version VERSION` | The app's version | The package's version |
| `--abi LIST` | Android: the ABIs to build a library for, comma separated | `arm64-v8a` |
| `--identity NAME` | iOS device: the signing identity | The first "Apple Development" identity in the keychain |
| `--profile FILE` | iOS device: the provisioning profile | The newest one on the Mac that fits the identifier and has not expired, an exact one before a wildcard |
| `--keystore FILE`, `--keystore-pass PASS` | Android: the keystore to sign with | `~/.android/debug.keystore` |
| `--network WHY` | The app uses the network: Android's `INTERNET` permission, and on iOS the sentence shown when the app first reaches a device on the local network | No network |
| `--bluetooth WHY` | The app prints to Bluetooth printers: Android's Bluetooth permissions, and on iOS and macOS the sentence shown when the app first uses Bluetooth | No Bluetooth |
| `--notifications` | The app shows notifications (CeeSafe's `Notifications`): Android's `POST_NOTIFICATIONS`, which Android 13 and later ask the user for | No notifications |
| `--firebase FILE` | Android: the app's `google-services.json`; the APK is built with Gradle (a generated project in `out/pack/android-gradle`, a wrapper pinned to Gradle 9.6.0) with Firebase Messaging and a messaging service that shows a push while the app is not running (CeeSafe's `Push.register` then gives an FCM token, ADR 0249) | No Firebase: no Gradle |
| `--extension DIR` | iOS: the package in DIR built as the app's notification service extension (CeeSafe's `NotificationExtension`), `PlugIns/<name>Notifications.appex`, identifier `<id>.NotificationService`, signed with its own profile | No extension |
| `--group ID` | iOS: an App Group the app and its extension share, in both entitlements (CeeSafe's `AppGroup`, `Secrets.readIn`) | No group |
| `--push` | The app receives push notifications (CeeSafe's `Push`): on an iOS device it is signed with the profile's `aps-environment`; the profile must be an explicit App ID's with Push Notifications | No push |
| `--font FILE`, `--fallback FILE` | `web`: the app's fonts and its fallback fonts | None |

What to know of each system:

- **iOS.** The app is linked with a 16 MiB main thread stack, where iOS gives 1 MiB. It allows every orientation.
- **An iOS device.** Xcode makes the identity and the profile the first time it builds an app for the device.
  `ceeui-pack` does not make a profile.
- **Android.** From Android 7.1 (API level 25). The NDK, build-tools and platform are the newest under
  `ANDROID_HOME`. `--abi arm64-v8a,x86_64` builds a library for each; the 32-bit `armeabi-v7a` and `x86` need
  ceesafec 0.84.0 or later. The activity is kept through a rotation and the keyboard. The debug key makes an app
  that installs but is not a store build.
- **macOS.** Signed ad hoc, for this Mac. There is no Developer ID, notarization or icon yet.

`ceeui-pack` needs a Mac for the Apple targets and a Unix for all of them. Windows is not a host for it yet.

[Phones](./phones) says what changes in an app on iOS and Android.

## Hot reload

`ceeui-dev`, run in an app's package directory, builds the app, runs it, and watches its sources:

```sh
cd ceeui-dev && ceesafec build && cd ..
cd ceeui-hosts/desktop && ../../ceeui-dev/out/ceeui_dev
```

It watches the package's `src` and the sources of every package it depends on by path. When a source changes it
builds again.

- A build that fails prints its errors, and the app keeps running.
- A build that succeeds asks the app to save its state and end, then starts the new build from that state.

So a code change shows in the app where it was: on the same page, with the same text.

The app has a part in this. `ceeui-dev` starts it with `--hot out/hot`. The app reads the state its previous
build saved, watches for the request to reload, and then writes its state and ends. `ceeui-desktop` has that
part (`HotWatch`), and the state is written with `SnapshotWriter`:

```ceesafe
import ceeui;

public struct Notes {
    public count: u64;
    public dark: bool;
}

/// Writes the app's state into `buffer`; the bytes written.
public fn saveNotes(app: borrow Notes, buffer: SliceMut<u8>) -> usize {
    var w = SnapshotWriter.over(buffer, 1);
    w.u64(app.count);
    w.bool(app.dark);
    return w.written().length;
}
```

The second argument of `SnapshotWriter.over` is the app's version of its state. A snapshot of another version reads
as defaults.

## The inspector

The layout inspector is in every app. Secondary-Alt-I turns it on and off: Command-Option-I on macOS, Control-Alt-I
elsewhere. `ui.setInspecting(true)` does the same from code.

While it is on:

- The element under the pointer is outlined, with its padding.
- A card beside it gives the element's id, box, sizing, padding and gap, background, corners and children. For a
  text element it gives the size and the words.

It reads the last frame's layout, so it never changes what the app lays out. It costs nothing while off, and it
lets the pointer through to the app.

```ceesafe frame
ui.setInspecting(true);
ui.open(Style.column().id("page").p(space(4.0)));
ui.label("Point at me", Text.of());
ui.close();
```

## The headless host

`ceeui-hosts/headless` runs the demo with a scripted platform and the software renderer, and writes frames to
files. It has no window. The repository uses it for screenshots, golden tests and benchmarks.

```sh
cd ceeui-hosts/headless && ceesafec build
CEEUI_SCREENSHOT=dashboard.bmp ./out/headless
```

The frame is 1024 by 640 points. Environment variables say what to do:

| Variable | What it does |
|---|---|
| `CEEUI_SCREENSHOT=PATH` | The file to write. `.bmp` is a 32-bit BMP; anything else is raw RGBA bytes |
| `CEEUI_SCREENSHOT_TAB=N` | The tab of the demo to open |
| `CEEUI_SHOT_EVENTS=...` | Scripted input: see below |
| `CEEUI_SHOT_FRAME=N` | The frame to write (default 28) |
| `CEEUI_LIGHT=1` | Start in light mode |
| `CEEUI_INSPECT=1` | The layout inspector on |
| `CEEUI_FONT_DIR=DIR` | Where `Arial.ttf`, `Arial Bold.ttf` and `Courier New.ttf` are (default `/System/Library/Fonts/Supplemental`) |
| `CEEUI_PIXEL_RATIO=N` | Draw N device pixels per point. 2 is a Retina frame, twice the size each way |
| `CEEUI_INCREMENTAL=1` | Redraw every frame, but only what changed. The result must not differ |
| `CEEUI_BENCH=N` | Run N frames and report the time per frame instead |

### Scripted events

`CEEUI_SHOT_EVENTS` is a list of events, each on a frame, separated by semicolons. `ceeui-demo`'s
`ScriptPlatform` reads it.

| Event | What it does |
|---|---|
| `FRAME:click:X,Y` | Presses the button at (X, Y) on FRAME and releases it on the next frame |
| `FRAME:right:X,Y` | A secondary press at (X, Y) |
| `FRAME:move:X,Y` | Moves the pointer to (X, Y) |
| `FRAME:key:NAME` | Presses a key: `tab`, `enter`, `up`, `down`, `left`, `right`, `backspace`, or one letter |
| `FRAME:type:TEXT` | Types TEXT |

Every frame is 1/60 of a second, so a script gives the same frames each time it runs.

```sh
CEEUI_SHOT_EVENTS="5:click:300,200;8:type:hello" CEEUI_SCREENSHOT=typed.bmp ./out/headless
```

[Testing an app](./testing) shows how to drive a `Ui` of your own without a window.

## The benchmark

`tools/bench.py` measures the demo on this machine and checks each number against its budget for this system in
`bench/budgets.json`. It measures through the headless host and the desktop host's binary.

```sh
python3 tools/bench.py
python3 tools/bench.py --json numbers.json --markdown numbers.md --skip-build
```

| Number | What it is |
|---|---|
| `build_s` | A clean build of the headless host, in seconds |
| `headless_mb` | The headless host's binary, stripped, in MB |
| `desktop_mb` | The desktop host's binary, stripped, in MB |
| `frame_ms_dashboard_1x`, `frame_ms_dashboard_2x`, `frame_ms_effects_1x` | A frame of a demo tab laid out and drawn whole, in ms: the median of three runs of 200 frames |
| `first_frame_ms` | The process started, fonts loaded, the first frame laid out, drawn and written, in ms: the median of five starts |
| `peak_mb` | The most memory the headless host held while running the dashboard at 2x, in MB (not on Windows) |
| `idle_redraws` | Frames that redrew anything, of 60, on the controls tab with no input. It is 1, the first frame, when nothing moves |
| `incremental_differs` | Tabs whose frame drawn through `Damage` is not the frame drawn whole, byte for byte. It is 0 |

A budget is the most a number may be. A number over its budget fails the run with exit code 1. A system with no
budgets only records its numbers. `bench/budgets.json` has budgets for `macos`, `linux` and `windows`; CI runs the
benchmark on all three.

| Option | What it does |
|---|---|
| `--budgets FILE` | The budgets to check against (default `bench/budgets.json`) |
| `--json OUT` | Write the numbers to OUT |
| `--markdown OUT` | Append a table of the numbers to OUT |
| `--skip-build` | Use the hosts as built; `build_s` is not measured |
| `--fonts DIR` | Only put the three fonts in DIR, for the other hosts' checks |

The fonts are Arial, Arial Bold and Courier New: the system's on macOS and Windows, and Liberation Sans and Mono on
Linux, which have the same metrics.

Lower a budget when the code gets faster. Raise one only with the reason in the commit.
