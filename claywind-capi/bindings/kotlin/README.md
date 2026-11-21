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

## Building (macOS and Linux)

```sh
cd claywind-capi
ceesafec build              # out/claywind_capi.a
cd bindings/kotlin
./build.sh run              # build/libclaywind_jni.dylib and build/counter.jar, then the example
```

On Linux, build the archive with `ceesafec build --cflag -fPIC`: the JNI library is a shared object, which takes
position-independent code (`CEESAFE-FINDINGS.md` Q3).

`build.sh` compiles `src/main/c/claywind_jni.c` against the JDK's headers and links it with claywind-capi's archive
into `libclaywind_jni.dylib`, then compiles the Kotlin layer and the example into one jar with `kotlinc`. It takes
the JDK from `JAVA_HOME` (else `/usr/libexec/java_home`) and `kotlinc` from `KOTLINC`, the PATH, or Android Studio's
copy. The example runs as:

```sh
java -XstartOnFirstThread --enable-native-access=ALL-UNNAMED -Djava.library.path=build -cp build/counter.jar org.claywind.counter.CounterKt
```

`-XstartOnFirstThread` is required on macOS: AppKit's window lives on the process's first thread, where the JVM
otherwise does not run `main`.

## Android

`./build-android.sh` builds the counter as an APK for arm64-v8a (`build/android/ClayWindCounter.apk`), with the NDK
and the Android SDK (`ANDROID_HOME`, else `~/Library/Android/sdk`):

- claywind-capi's archive for Android, position-independent, and the JNI library over it (`libclaywind_jni.so`);
- the app's activity, `android/CounterActivity.kt`, is a `NativeActivity` whose library is the JNI library. CeeSafe's
  runtime calls that library's `main` on a thread of its own (CeeSafe ADR 0200), which calls the activity's
  `fun claywindMain()`: the app's Kotlin, run there with its own classes. Android's views are not to be touched from
  it; the app ends when it returns;
- the Kotlin (the binding, the counter, the activity) compiled against `android.jar` and turned into dex with Kotlin's
  standard library by `d8`, packed by `aapt2`, aligned and signed with the debug key.

`adb install build/android/ClayWindCounter.apk` installs it. It needs ceesafec 0.86.0 or later.

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
closing the window ended the program. A field started with "Wörld 😀" kept both through an edit. On the Android
emulator (API 37, arm64): taps counted on Add one, text typed with `adb shell input text` reached the Kotlin `String`
and its greeting, a step ticked and Dark switched the theme. Not checked: the exception path, Windows, and a phone.
