# Testing an app

This page shows how to test a frame without a window. A test gives the `Ui` a platform it scripts, runs frames, and
checks the app's state and the layout. ClayWind's own tests (`claywind-tests`) work this way.

## Why it needs no window

A frame takes its input from a `Platform` and measures text through a `TextMetrics`. Neither has to be real:

- A test's platform is a struct whose fields the test sets between frames: the pointer, the keys, the clock.
- A test's metrics give every text a fixed size, so a test needs no font and no renderer.

`ui.begin(platform)` starts a frame, the app's frame function declares it, and `ui.endFrame(platform, metrics)` lays
it out. After that the test reads the app's state, and the layout through the `Ui`.

## A complete test

This file tests a counter: a button that adds one. It is a whole test file, with the platform and the metrics it
needs.

```ceesafe
import claywind;

/// A platform the test scripts: it sets the pointer, the keys and the clock between frames.
struct TestPlatform {
    size: Dims;
    pointerState: Pointer;
    clock: f64;
    /// Bit `keyIndex(k)` for each key that went down this frame.
    pressed: u64;
    cursor: Cursor;
}

impl Platform for TestPlatform {
    fn screenSize(borrow this) -> Dims => this.size;
    fn frameTime(borrow this) -> f32 => 1.0 / 60.0;
    fn time(borrow this) -> f64 => this.clock;
    fn pointer(borrow this) -> Pointer => this.pointerState;
    fn keyDown(borrow this, key: Key) -> bool => false;
    fn keyPressed(borrow this, key: Key) -> bool => (this.pressed >> (keyIndex(key) as u64)) & 1 == 1;
    fn keyRepeated(borrow this, key: Key) -> bool => false;
    fn nextChar(borrow mut this) -> u32 => 0;
    fn setCursor(borrow mut this, cursor: Cursor) {
        this.cursor = cursor;
    }
}

/// Every text measures 10 points a byte and 16 tall: a test needs no font.
struct TestMetrics {
    unused: u8;
}

impl TextMetrics for TestMetrics {
    fn measure(borrow this, utf8: Slice<u8>, config: TextConfig) -> Dims => Dims {
        w: round<f32>(utf8.length) * 10.0,
        h: 16.0,
    };
    fn baseline(borrow this, config: TextConfig) -> f32 => -1.0;
}

/// The app under test: its state and its frame.
struct Counter {
    count: u64;
}

fn counterFrame(ui: borrow mut Ui, app: borrow mut Counter) {
    ui.open(Style.column().w(Size.grow()).h(Size.grow()).p(space(4.0)));
    if (Controls.button(mut ui, "add", "Add one")) {
        app.count += 1;
    }
    ui.close();
}

/// One frame; then the input that lasts one frame is taken away and the clock moves on.
fn step(ui: borrow mut Ui, platform: borrow mut TestPlatform, app: borrow mut Counter) -> Result<void, TestFailure> {
    ui.begin(mut platform);
    counterFrame(mut ui, mut app);
    let metrics = TestMetrics { unused: 0 };
    let ok = match (ui.endFrame(mut platform, metrics)) {
        Ok() => true,
        Err(_) => false,
    };
    platform.pointerState.pressed = false;
    platform.pointerState.released = false;
    platform.pressed = 0;
    platform.clock += 1.0 / 60.0;
    return Check.isTrue(ok, "the frame fits");
}

fn platformOf(w: f32, h: f32) -> TestPlatform => TestPlatform {
    size: Dims { w: w, h: h },
    pointerState: Pointer.at(Vec2 { x: -1.0, y: -1.0 }),
    clock: 0.0,
    pressed: 0,
    cursor: Cursor.Default,
};

/// The Ui's memory, from the heap: a megabyte is too much for a test's stack.
fn heap(bytes: usize) -> Vector<u8> {
    var v = Vector<u8>.withCapacity(bytes)!;
    for (i in 0..bytes) {
        v.push(0)!;
    }
    return v;
}

@test
fn aClickOnTheButtonAddsOne() -> Result<void, TestFailure> {
    var storage = heap(1048576);
    let memory = FixedBufferAllocator.over(mut storage.viewMut());
    var ui = Ui.create(memory, UiCapacity.of(128))!;
    var platform = platformOf(800.0, 600.0);
    var app = Counter { count: 0 };

    // The first frame lays the window out, so the button has a box.
    step(mut ui, mut platform, mut app)?;
    let box = ui.boxOf("add")!;
    Check.isTrue(box.w > 0.0 && box.h > 0.0, "the button is laid out")?;

    // The pointer goes to the button's middle, goes down, and comes back up.
    platform.pointerState.position = Vec2 { x: box.x + box.w / 2.0, y: box.y + box.h / 2.0 };
    step(mut ui, mut platform, mut app)?;
    platform.pointerState.pressed = true;
    platform.pointerState.down = true;
    step(mut ui, mut platform, mut app)?;
    Check.equalU64(0, app.count)?; // the press alone does not act
    platform.pointerState.down = false;
    platform.pointerState.released = true;
    step(mut ui, mut platform, mut app)?;
    Check.equalU64(1, app.count)?;

    // A click is a one-frame event.
    step(mut ui, mut platform, mut app)?;
    return Check.equalU64(1, app.count);
}

@test
fn theKeysAndAScreenReaderPressTheButton() -> Result<void, TestFailure> {
    var storage = heap(1048576);
    let memory = FixedBufferAllocator.over(mut storage.viewMut());
    var ui = Ui.create(memory, UiCapacity.of(128))!;
    var platform = platformOf(800.0, 600.0);
    var app = Counter { count: 0 };
    step(mut ui, mut platform, mut app)?;

    // Tab gives the button the focus, and Enter presses it.
    platform.pressed = 1u64 << (keyIndex(Key.Tab) as u64);
    step(mut ui, mut platform, mut app)?;
    step(mut ui, mut platform, mut app)?;
    Check.isTrue(ui.hasFocusNamed("add"), "Tab reaches the button")?;
    platform.pressed = 1u64 << (keyIndex(Key.Enter) as u64);
    step(mut ui, mut platform, mut app)?;
    Check.equalU64(1, app.count)?;

    // The frame's one accessible element is the button, and a reader's press is a click in the next frame.
    Check.equalU64(1, ui.accessCount() as u64)?;
    let node = ui.accessAt(0);
    Check.isTrue(node.role == AccessRole.Button, "a button")?;
    ui.accessAct(node.id, AccessAction.Press);
    step(mut ui, mut platform, mut app)?;
    return Check.equalU64(2, app.count);
}
```

The sections below take its parts one at a time.

## The platform

`Platform` is the interface a host implements. A test implements it over plain fields. Nine methods have no
default and must be written: `screenSize`, `frameTime`, `time`, `pointer`, `keyDown`, `keyPressed`, `keyRepeated`,
`nextChar` and `setCursor`. The others have defaults that say "none": the safe area, the keyboard's inset,
keystrokes, a chosen menu action, a file drag and touches.

ClayWind's tests use a fuller one, `FakePlatform` in `claywind-tests/tests/ui_test.cee`. It also scripts typed
characters, keystrokes with modifiers, touches, the safe area, the keyboard's inset and files dragged from the
system. Copy it when a test needs those.

## Frames

A test runs one frame at a time through a function like `step`. What it does after the frame matters:

- `pointerState.pressed` and `released`, and the pressed keys, are true for one frame. `step` clears them so the
  next frame does not see them again.
- The clock moves on by a frame, so what depends on the time, such as a caret's blink, advances.
- A frame that does not fit the `Ui`'s capacity fails the test.

Run a frame before you ask where anything is: the first frame lays the window out.

## Pressing by position

`ui.boxOf(name)` is the box an element with that id had in the last frame, or null. A built-in control's id is the
name you gave it, and its parts are named after it: the data grid's test finds a column's title as
`"orders~title1"`.

A click is three steps, as on a real pointer:

1. Put the pointer over the element and run a frame.
2. Set `pressed` and `down`, and run a frame.
3. Clear `down`, set `released`, and run a frame. The button reports the click in this frame.

A press that is dragged off the button before it comes up is no click.

## Keys and typed text

The example's platform keeps the keys that went down this frame as bits, one for each `Key` by `keyIndex`:

```ceesafe skip
    platform.pressed = 1u64 << (keyIndex(Key.Tab) as u64);
    step(mut ui, mut platform, mut app)?;
```

Typed characters are a queue that `nextChar` reads, a code point at a time. `FakePlatform` keeps them in an array
that a helper fills:

```ceesafe skip
fn typeChars(platform: borrow mut FakePlatform, text: StringView) {
    let b = text.bytes();
    for (i in 0..b.length) {
        platform.typed[i] = b[i] as u32;
    }
    platform.typedCount = b.length;
}
```

## Accessibility

After a frame, the `Ui` has the list a screen reader is given. A test reads it, and asks what a reader would ask.

| Call | What it gives |
|---|---|
| `ui.accessCount()` | How many accessible elements the frame has |
| `ui.accessAt(index)` | Element `index`, in reading order: its `id`, `role`, `flags`, range, place and `parent` |
| `ui.accessLabel(node)` | Its name, as bytes |
| `ui.accessValueOf(node)` | Its value, as bytes |
| `ui.accessBox(node)` | Its box in the window, or null |
| `node.has(ACCESS_CHECKED)` | Whether it has a state flag |
| `ui.accessAct(id, action)` | Asks an action of an element; the next frame's controls see it |

These lines are from `claywind-tests/tests/access_test.cee`. `nodeNamed` is the test's own helper, which finds a
node by its label:

```ceesafe skip
    ui.accessAct(nodeNamed(ui, "Save")?.id, AccessAction.Press);
    ui.accessAct(nodeNamed(ui, "I agree")?.id, AccessAction.Press);
    accessStep(mut ui, mut platform, mut app)?;
    Check.isTrue(app.saved == 1, "the button is clicked")?;
    Check.isTrue(ui.needsFrame(), "an action is input: the next frame runs at once, and says what it did")?;
    Check.isTrue(app.agree, "the checkbox is checked")?;
    Check.isTrue(nodeNamed(ui, "I agree")?.has(ACCESS_CHECKED), "and says so in the same frame")?;
```

[Accessibility](./accessibility) says what each part of a node means.

## Check assertions

A test is a function marked `@test` that returns `Result<void, TestFailure>`. `Check` is CeeSafe's, and each of its
assertions returns that result, so `?` ends the test at the first failure.

| Assertion | It passes when |
|---|---|
| `Check.isTrue(condition, what)` | `condition` is true; `what` is printed when it is not |
| `Check.isFalse(condition, what)` | `condition` is false |
| `Check.equalU64(expected, actual)` | The two numbers are equal |
| `Check.equalBytes(expected, actual)` | The two byte slices are equal |
| `Check.fail(what)` | Never: it fails the test |

## Running the tests

A package names its test directories in `ceesafe.toml`. This is `claywind-tests`' own:

```toml
[package]
name = "claywind_tests"
version = "0.1.0"
kind = "executable"
tests = ["tests"]
```

`ceesafec test` builds the test files and runs every `@test` function:

```sh
cd claywind-tests
ceesafec test            # add --interp to run them in CeeSafe's interpreter
```

The test files of a package are compiled together. `FakePlatform` is declared in `ui_test.cee` and used by the
other test files, and helpers such as `heap` and `near32` are shared the same way.

## Tests of the whole app

Two more ways to test, both in the repository:

- `claywind-demo`'s `ScriptPlatform` reads input from a script of clicks, moves, keys and typed text, a frame at a
  time. `claywind-tests/tests/demo_test.cee` drives the demo with it.
- [The headless host](./tools#the-headless-host) writes a frame of the demo to a file, with the same scripted
  input.
