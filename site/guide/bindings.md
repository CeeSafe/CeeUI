# From C, Swift, Kotlin and Rust

This page covers ClayWind's C API, `claywind-capi`, and the three bindings over it: Swift, Kotlin and Rust. Each
section shows the same example, a counter, and how to build it.

## The C API

`claywind-capi` builds to a static library, `out/claywind_capi.a`, with one header, `include/claywind.h`. The
library holds CeeSafe's runtime, so a C program links nothing else of CeeSafe's.

The model is immediate mode, as it is for an app written in CeeSafe:

1. The program calls `claywind_run` with a frame function. ClayWind opens a window.
2. For each frame ClayWind calls the function with a `claywind_frame`. The function lays out the whole window from
   the program's state.
3. ClayWind lays the frame out, draws it and waits for input.

`claywind_run` returns when the window is closed (`CLAYWIND_CLOSED`) or cannot go on.

What the header offers:

| Calls | What they do |
|---|---|
| `claywind_run` | Opens a window of a width and height in points, and runs it with a frame function |
| `claywind_column`, `claywind_row`, `claywind_panel`, `claywind_end` | Layout. Each start is closed by `claywind_end`. A column and a row take the gap between their children, in points. A panel is a column with padding on a raised, rounded background |
| `claywind_spacer` | A spacer between a row's or a column's children |
| `claywind_label`, `claywind_caption`, `claywind_heading` | Text |
| `claywind_button` | A button: true in the frame after it was pressed |
| `claywind_checkbox` | A checkbox over a `bool` of the program's: true in the frame after it was flipped |
| `claywind_text_field` | A text field over a UTF-8 buffer of the program's: true when the user changed the text |
| `claywind_is_dark`, `claywind_set_dark` | The theme |
| `claywind_request_frame` | Asks for another frame |
| `claywind_version` | The library's version, to compare with `CLAYWIND_VERSION` |

Rules that hold for all of them:

- Text is UTF-8, given as a pointer and a length. The `_s` macros (`claywind_label_s` and the rest) take a
  NUL-terminated string.
- ClayWind copies what it is given. Nothing passed in needs to outlive the call.
- A control's id must be unique in the frame, and stays the same from frame to frame.
- A `claywind_frame` is valid only during the frame function's call, on the thread `claywind_run` runs on.
- What a frame leaves open is closed for it.

### A control's result arrives in the next frame

The frame function's calls are recorded, and ClayWind replays them into its `Ui` when the function returns. So a
control's result arrives in the frame after the one the user acted in. A button is true in the frame after it was
pressed. A checkbox's new state and a text field's new text arrive the same way.

ClayWind asks for that frame at once, and for one more after it. What the program changed in answer then shows
even where the frame lays it out before the control, such as a count above its button. The delay is about one
screen refresh.

### Text fields

A text field's text stays in the program's buffer: `claywind_text_field` takes the buffer, its capacity and the
text's length. ClayWind keeps the caret and the selection. When the user changes the text, the next call writes it
into the buffer, cut at a character to fit, and returns true. No NUL is written. The program may change the buffer
itself: the field then shows the program's text.

### The example

`claywind-capi/examples/counter.c` uses all of the API:

```c
/* SPDX-License-Identifier: MIT
 * A ClayWind window from C: a counter, a theme switch, a greeting typed into a text field and a short list, laid out
 * each frame from the program's state.
 * Build it with examples/build.sh. */
#include <stdio.h>

#include "ceesafe/embed.h"
#include "claywind.h"

struct state {
    int count;
    bool dark;
    bool done[3];
    uint8_t name[128];
    size_t name_length;
};

static const char *const tasks[3] = {"Build claywind-capi", "Link it from C", "Call it from Swift"};

static void frame(claywind_frame *f, void *user) {
    struct state *s = user;
    claywind_set_dark(f, s->dark);

    claywind_row(f, 12);
    claywind_heading_s(f, "ClayWind from C");
    claywind_spacer(f);
    claywind_checkbox_s(f, "dark", "Dark", &s->dark);
    claywind_end(f);
    claywind_caption_s(f, "Each frame is laid out by a C function; ClayWind draws it.");

    claywind_panel(f);
    char text[64];
    snprintf(text, sizeof text, "Pressed %d time%s", s->count, s->count == 1 ? "" : "s");
    claywind_label_s(f, text);
    claywind_row(f, 8);
    if (claywind_button_s(f, "add", "Add one")) {
        s->count++;
    }
    if (claywind_button_s(f, "reset", "Reset")) {
        s->count = 0;
    }
    claywind_end(f);
    claywind_end(f);

    claywind_panel(f);
    claywind_caption_s(f, "Your name");
    claywind_text_field_s(f, "name", "Your name", s->name, sizeof s->name, &s->name_length);
    if (s->name_length > 0) {
        snprintf(text, sizeof text, "Hello, %.*s!", (int)s->name_length, (const char *)s->name);
        claywind_label_s(f, text);
    }
    claywind_end(f);

    claywind_panel(f);
    claywind_label_s(f, "Steps");
    for (int k = 0; k < 3; k++) {
        char id[16];
        snprintf(id, sizeof id, "task-%d", k);
        claywind_checkbox_s(f, id, tasks[k], &s->done[k]);
    }
    claywind_end(f);
}

int main(int argc, char **argv) {
    ceesafe_config config = {0};
    config.size = sizeof config;
    config.flags = CEESAFE_INIT_ENVIRONMENT;
    config.argc = argc;
    config.argv = (const char *const *)argv;
    if (ceesafe_init(&config) != 0) {
        fprintf(stderr, "counter: CeeSafe's runtime did not start\n");
        return 1;
    }
    struct state s = {0, true, {true, false, false}, "World", 5};
    int32_t result = claywind_run_s("ClayWind from C", 520, 520, frame, &s);
    if (result != CLAYWIND_CLOSED) {
        fprintf(stderr, "counter: claywind_run stopped (%d)\n", result);
    }
    ceesafe_shutdown();
    return result == CLAYWIND_CLOSED ? 0 : 1;
}
```

### Building and linking

```sh
cd claywind-capi
./examples/build.sh   # ceesafec build --emit lib, then the example compiled and linked
./out/counter
```

`ceesafec build --emit lib` writes `out/claywind_capi.a` and `out/ceesafe/embed.h`. A program of your own:

1. includes `claywind.h` and `ceesafe/embed.h` (`-Iinclude -Iout`);
2. calls `ceesafe_init` once before `claywind_run`, with a config whose `size` is `sizeof(ceesafe_config)`, on the
   thread that will run the window. On macOS that is the main thread;
3. links `out/claywind_capi.a`. On macOS it also links `-framework AppKit -framework QuartzCore -framework CoreText
   -framework CoreGraphics`.

| `claywind_run` returns | When |
|---|---|
| `CLAYWIND_CLOSED` (0) | The window was closed |
| `CLAYWIND_NO_WINDOW` (1) | No window could be opened, or there was no frame function |
| `CLAYWIND_NO_MEMORY` (2) | Not enough memory for the UI or the renderer |
| `CLAYWIND_NO_FONTS` (3) | No system fonts were found. Set `CLAYWIND_FONT_DIR` |
| `CLAYWIND_FRAME_TOO_LARGE` (4) | A frame had more elements than the UI holds |
| `CLAYWIND_WINDOW_REFUSED` (5) | The window refused a frame |

The example is checked on macOS. `examples/build.sh` has a link line for Linux, which has not been run.

## Swift

`claywind-capi/bindings/swift` is a Swift package over the header. A closure lays out each frame; text is `String`,
state is `inout`, and `column`, `row` and `panel` take a closure for their children and end themselves.

`Sources/Counter/main.swift` is the C example in Swift:

```swift
// SPDX-License-Identifier: MIT
// The C example (examples/counter.c) in Swift: a counter, a theme switch, a greeting typed into a text field and a
// short list, laid out each frame from the program's state.

import ClayWind

var count = 0
var dark = true
var name = "World"
var done = [true, false, false]
let tasks = ["Build claywind-capi", "Link it from C", "Call it from Swift"]

let result = ClayWind.run(title: "ClayWind from Swift", width: 520, height: 520) { f in
    f.isDark = dark
    f.row(gap: 12) {
        f.heading("ClayWind from Swift")
        f.spacer()
        f.checkbox("dark", "Dark", isOn: &dark)
    }
    f.caption("Each frame is laid out by a Swift closure; ClayWind draws it.")
    f.panel {
        f.label("Pressed \(count) time\(count == 1 ? "" : "s")")
        f.row {
            if f.button("add", "Add one") { count += 1 }
            if f.button("reset", "Reset") { count = 0 }
        }
    }
    f.panel {
        f.caption("Your name")
        f.textField("name", name: "Your name", text: &name)
        if !name.isEmpty { f.label("Hello, \(name)!") }
    }
    f.panel {
        f.label("Steps")
        for k in tasks.indices {
            f.checkbox("task-\(k)", tasks[k], isOn: &done[k])
        }
    }
}
if result != .closed {
    print("counter: ClayWind stopped (\(result))")
}
```

The package links `claywind-capi`'s archive, so build that first, for the package's macOS version:

```sh
cd claywind-capi
MACOSX_DEPLOYMENT_TARGET=13.0 ceesafec build   # out/claywind_capi.a and out/ceesafe/embed.h
cd bindings/swift
swift build
.build/debug/counter
```

`ClayWind.run` starts CeeSafe's runtime the first time. Call it on the main thread.

### On iOS

`build-ios.sh` builds the counter as an iOS app. It needs Xcode and ceesafec 0.86.0 or later.

```sh
cd claywind-capi/bindings/swift
./build-ios.sh simulator run    # builds it, installs it on the booted simulator and launches it
./build-ios.sh device run       # builds and signs it, installs it on the connected device and launches it
```

For a device it signs with the keychain's Apple Development identity and the newest fitting provisioning profile,
as `claywind-pack` does. `IDENTITY` and `PROFILE` choose others, and `DEVICE` names another device.

On iOS the first call of `ClayWind.run` starts the application and does not return. The launched application runs
`main.swift`'s top-level code again, where `run` goes on. So keep what comes before `run` to declarations.

The counter is checked on the iOS simulator, where it is laid out inside the safe area. It is not checked on an
iPhone yet.

## Kotlin

`claywind-capi/bindings/kotlin` is Kotlin over the header, through JNI. A function with a `Frame` receiver lays out
each frame. `checkbox` and `textField` return their value, the user's change included.

`examples/Counter.kt` is the C example in Kotlin:

```kotlin
// SPDX-License-Identifier: MIT
// The C example (examples/counter.c) in Kotlin: a counter, a theme switch, a greeting typed into a text field and a
// short list, laid out each frame from the program's state. build.sh builds and runs it.

package org.claywind.counter

import org.claywind.ClayWind
import org.claywind.RunResult
import kotlin.system.exitProcess

fun main() {
    val result = counter()
    if (result != RunResult.Closed) {
        System.err.println("counter: ClayWind stopped ($result)")
        exitProcess(1)
    }
}

/** The counter's window, until it is closed: the desktop's `main` runs it, and on Android the activity (android/). */
fun counter(): RunResult {
    var count = 0
    var dark = true
    var name = "World"
    val done = booleanArrayOf(true, false, false)
    val tasks = listOf("Build claywind-capi", "Link it from C", "Call it from Kotlin")

    return ClayWind.run("ClayWind from Kotlin", 520, 520) {
        isDark = dark
        row(gap = 12f) {
            heading("ClayWind from Kotlin")
            spacer()
            dark = checkbox("dark", "Dark", dark)
        }
        caption("Each frame is laid out by a Kotlin function; ClayWind draws it.")
        panel {
            label("Pressed $count time${if (count == 1) "" else "s"}")
            row {
                if (button("add", "Add one")) count++
                if (button("reset", "Reset")) count = 0
            }
        }
        panel {
            caption("Your name")
            name = textField("name", "Your name", name, maxBytes = 128)
            if (name.isNotEmpty()) label("Hello, $name!")
        }
        panel {
            label("Steps")
            tasks.forEachIndexed { k, task -> done[k] = checkbox("task-$k", task, done[k]) }
        }
    }
}
```

On macOS and Linux:

```sh
cd claywind-capi
ceesafec build              # out/claywind_capi.a
cd bindings/kotlin
./build.sh run              # build/libclaywind_jni.dylib and build/counter.jar, then the example
```

`build.sh` compiles the JNI library against the JDK's headers and links it with `claywind-capi`'s archive. It then
compiles the Kotlin layer and the example into one jar with `kotlinc`. It takes the JDK from `JAVA_HOME` and
`kotlinc` from `KOTLINC`, the PATH, or Android Studio's copy.

On macOS the JVM must run `main` on the process's first thread, where AppKit's window lives:

```sh
java -XstartOnFirstThread --enable-native-access=ALL-UNNAMED -Djava.library.path=build -cp build/counter.jar org.claywind.counter.CounterKt
```

Two more things to know:

- Text crosses JNI as UTF-8 byte arrays, not as JNI strings.
- An exception thrown by the block is printed. The window lays out nothing more, and `run` returns
  `RunResult.Threw` when it is closed.

### On Android

`build-android.sh` builds the counter as an APK for arm64-v8a. It needs ceesafec 0.86.0 or later, the Android SDK
with an NDK (`ANDROID_HOME`, else `~/Library/Android/sdk`), a JDK and `kotlinc`.

```sh
cd claywind-capi/bindings/kotlin
./build-android.sh
adb install build/android/ClayWindCounter.apk
```

The app's activity, `android/CounterActivity.kt`, is a `NativeActivity` whose library is the JNI library. CeeSafe's
runtime calls that library's `main` on a thread of its own, which calls the activity's `claywindMain()`. The app's
Kotlin runs there. Android's views are not to be touched from it, and the app ends when it returns.

The counter is checked on the Android emulator. It is not checked on a phone yet.

## Rust

`claywind-capi/bindings/rust` is a crate over the header. A closure lays out each frame; text is `&str`, state is
`&mut`, and `column`, `row` and `panel` take a closure for their children and end themselves. `claywind::sys` has
the raw declarations.

`examples/counter.rs` is the C example in Rust:

```rust
// SPDX-License-Identifier: MIT
// The C example (examples/counter.c) in Rust: a counter, a theme switch, a greeting typed into a text field and a
// short list, laid out each frame from the program's state. `cargo run --example counter`.

fn main() {
    let mut count = 0;
    let mut dark = true;
    let mut name = String::from("World");
    let mut done = [true, false, false];
    let tasks = ["Build claywind-capi", "Link it from C", "Call it from Rust"];

    let result = claywind::run("ClayWind from Rust", 520, 520, |f| {
        f.set_dark(dark);
        f.row(12.0, |f| {
            f.heading("ClayWind from Rust");
            f.spacer();
            f.checkbox("dark", "Dark", &mut dark);
        });
        f.caption("Each frame is laid out by a Rust closure; ClayWind draws it.");
        f.panel(|f| {
            f.label(&format!("Pressed {count} time{}", if count == 1 { "" } else { "s" }));
            f.row(8.0, |f| {
                if f.button("add", "Add one") {
                    count += 1;
                }
                if f.button("reset", "Reset") {
                    count = 0;
                }
            });
        });
        f.panel(|f| {
            f.caption("Your name");
            f.text_field("name", "Your name", &mut name, 128);
            if !name.is_empty() {
                f.label(&format!("Hello, {name}!"));
            }
        });
        f.panel(|f| {
            f.label("Steps");
            for (k, task) in tasks.iter().enumerate() {
                f.checkbox(&format!("task-{k}"), task, &mut done[k]);
            }
        });
    });
    if result != claywind::RunResult::Closed {
        eprintln!("counter: ClayWind stopped ({result:?})");
        std::process::exit(1);
    }
}
```

The crate links `claywind-capi`'s archive, so build that first:

```sh
cd claywind-capi
ceesafec build                 # out/claywind_capi.a
cd bindings/rust
cargo run --example counter
```

`build.rs` passes the archive to the linker by its path. `CLAYWIND_CAPI_ARCHIVE` names another archive. On macOS it
links AppKit, QuartzCore, CoreText and CoreGraphics.

A panic in the closure does not unwind into C. It is caught, later frames draw nothing, and `run` returns
`RunResult::Panicked` once the window is closed. Call `run` on the main thread.

## Where the bindings run

| Binding | Checked on | Not checked yet |
|---|---|---|
| C | macOS | Linux, Windows |
| Swift | macOS; the iOS simulator (layout only) | An iPhone |
| Kotlin | macOS; the Android emulator | Windows, a phone |
| Rust | macOS | Linux, Windows |

CI builds the C example and the Rust binding, the Swift binding on macOS, and the Kotlin binding on macOS and Linux.
