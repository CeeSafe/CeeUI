# ClayWind for Kotlin

Kotlin over ClayWind's C API (`../../include/claywind.h`), through JNI: a window laid out each frame by a function
with a `Frame` receiver, in immediate mode. `examples/Counter.kt` is the C example in Kotlin.

```kotlin
var count = 0
var name = "World"
ClayWind.run("Counter", 480, 360) {
    heading("Counter")
    panel {
        label("Pressed $count times")
        if (button("add", "Add one")) count++
    }
    name = textField("name", "Your name", name)
    label("Hello, $name!")
}
```

## Building (macOS)

```sh
cd claywind-capi
ceesafec build              # out/claywind_capi.a
cd bindings/kotlin
./build.sh run              # build/libclaywind_jni.dylib and build/counter.jar, then the example
```

`build.sh` compiles `src/main/c/claywind_jni.c` against the JDK's headers and links it with claywind-capi's archive
into `libclaywind_jni.dylib`, then compiles the Kotlin layer and the example into one jar with `kotlinc`. It takes
the JDK from `JAVA_HOME` (else `/usr/libexec/java_home`) and `kotlinc` from `KOTLINC`, the PATH, or Android Studio's
copy. The example runs as:

```sh
java -XstartOnFirstThread --enable-native-access=ALL-UNNAMED -Djava.library.path=build -cp build/counter.jar CounterKt
```

`-XstartOnFirstThread` is required on macOS: AppKit's window lives on the process's first thread, where the JVM
otherwise does not run `main`.

## The API

- `ClayWind.run(title, width, height) { ... }` opens a window and runs it until it is closed (`RunResult`). It starts
  CeeSafe's runtime the first time.
- `Frame`: `column(gap)`, `row(gap)` and `panel` take a block for their children and end themselves; `spacer`,
  `label`, `caption`, `heading`; `button` (true in the frame after a press); `checkbox(id, label, checked)` and
  `textField(id, name, text, maxBytes)` return their value, the user's change included; `isDark`, `requestFrame`.
- As in C, a control's result arrives in the frame after the user acted; ClayWind asks for that frame, and for one
  after it, so the block's changes show at once. A `Frame` is valid only during the block's call.
- Text crosses JNI as UTF-8 byte arrays, not as JNI strings (modified UTF-8, which differs past U+FFFF).
- An exception thrown by the block is printed; the window lays out nothing more, and `run` returns
  `RunResult.Threw` when it is closed.

## Status

Checked on macOS 27 with JDK 25 and Android Studio's kotlinc, with real mouse and key events: typed text reached the
Kotlin `String` and its greeting, Add one counted, a step's checkbox ticked, the Dark checkbox switched the theme, and
closing the window ended the program. A field started with "Wörld 😀" kept both through an edit. Not checked: the
exception path, Linux and Windows, and Android, where a ClayWind app is a CeeSafe program packed by claywind-pack
rather than a Kotlin activity calling this library.
