# A frame

This page covers how a frame runs: its four steps, how elements are named, what CeeUI keeps between frames,
when a frame runs, and the memory a `Ui` works in.

## The four steps

CeeUI is immediate mode. Every frame:

1. the host gives `Ui.begin` a `Platform`, which supplies the input and the clock;
2. the app declares its elements and controls;
3. `endFrame` lays the frame out;
4. a `Backend` draws it.

Here are the four steps as one function. A host calls it in a loop.

```ceesafe
import ceeui;

public fn runFrame<M: TextMetrics, B: Backend>(
    ui: borrow mut Ui,
    platform: borrow mut dyn Platform,
    metrics: borrow M,
    backend: borrow mut B,
) -> bool {
    ui.begin(mut platform);                                  // 1: input, the clock, the screen's size
    ui.open(Style.column().w(Size.grow()).h(Size.grow()).p(space(4.0)).bg(SLATE_950));
    ui.label("Hello", Text.of().size(20.0).color(SLATE_100)); // 2: the app declares the window
    ui.close();
    match (ui.endFrame(mut platform, metrics)) {             // 3: layout
        Ok() => {}
        Err(_) => { return false; }                          // the frame does not fit the Ui's capacity
    }
    ui.renderInto(mut backend);                              // 4: drawing
    return true;
}
```

| Step | Call | What it does |
|---|---|---|
| 1 | `ui.begin(mut platform)` | Takes the pointer, keys, typed characters and touches. Finds what the pointer is over in the last frame's layout. Moves the scroll containers. Opens the root element, which is the size of the screen. |
| 2 | `ui.open`, `ui.close`, `ui.label`, `Controls.*` | The app declares the window from its state. |
| 3 | `ui.endFrame(mut platform, metrics)` | Lays the elements out and makes the frame's draw commands. Sets the cursor. Moves the focus on Tab. |
| 4 | `ui.renderInto(mut backend)` | Sends the commands to a backend, in paint order. |

A `Platform` is an interface the host implements: the screen's size, the pointer, the keys, the clock.
`ceeui-desktop` has `DesktopPlatform` over a window, and `ceeui-demo` has `ScriptPlatform`, which plays
scripted input for tests.

`TextMetrics` measures text for layout, and a `Backend` draws commands. `Raster`, the software renderer, is both,
so a host passes the same `raster` to `endFrame` and to `renderInto`.

The app's part is step 2. [Getting started](./getting-started) shows a frame function, and the rest of this guide
is about what goes in one.

## The pointer is a frame behind

`begin` tests the pointer against the layout of the frame before, because this frame's layout does not exist
yet. So a hover or a click is seen one frame after the layout it belongs to.

```ceesafe frame
ui.open(Style.box().id("card").p(space(4.0)).bg(SLATE_800).hover(Style.of().bg(SLATE_700)));
ui.label("A card", Text.of().color(SLATE_100));
ui.close();
if (ui.clicked("card")) {
    ui.requestFrame();
}
```

`ui.hovered(name)`, `ui.pressed(name)` and `ui.clicked(name)` ask about an element by its id. An element is
clicked when the pointer went down over it and came back up over it. After a frame that saw input, `needsFrame`
is true, so the host runs the frame that draws the hover without the app asking.

## Ids

An element's id is a name: `Style.id("card")`. The name is hashed when the style is built (`hashId`, FNV-1a), and
only the `u32` is kept.

```ceesafe frame
ui.open(Style.row().id("toolbar").w(Size.grow()).h(Size.fixed(40.0)).bg(SLATE_900));
ui.close();
let toolbar: u32 = hashId("toolbar");
match (ui.boxOfHash(toolbar)) {
    Some(box) => {
        if (box.w < 400.0) {
            ui.label("A narrow toolbar", Text.of().color(SLATE_400));
        }
    }
    null => {}
}
```

An element without an id is anonymous. Its id is derived from its parent's id and its place among the parent's
children, so it changes when an earlier sibling comes or goes.

Controls take a name as their first argument after `ui`:

```ceesafe frame
if (Controls.button(mut ui, "save", "Save")) {
    ui.requestFrame();
}
```

The name is the control's id. A frame keeps no widget objects, so the id is how CeeUI knows this frame's
button is the same button as last frame's: the one that has the focus, the one that was pressed, the one whose
hover is fading. Two controls in one window need two names.

For ids made at run time, such as one for each row of a list, hash the bytes yourself and give the style the hash:

```ceesafe frame
for (i in 0u64..3) {
    var name = [0u8; 32];
    var w = TextWriter.create(mut name[..]);
    w.text("row-");
    w.decimal(i);
    ui.open(Style.row().idHash(hashBytes(w.written())).w(Size.grow()).h(Size.fixed(28.0)));
    ui.close();
}
```

`hashBytes` never gives 0, which means anonymous.

## What is kept between frames

The app's state is the app's. CeeUI keeps only what a frame cannot rebuild, each piece by element id:

| Kept | Used for |
|---|---|
| The last frame's layout | Hit-testing the pointer; `ui.boxOf(name)`, `ui.containerWidth(name)` |
| The focused element | Keyboard input, Tab and Shift-Tab |
| The captured element | A drag that began on a control and goes on outside it |
| Scroll positions and momentum | Scroll containers ([Layout](./layout#scrolling)) |
| Transitions' shown values | Easing a colour, an opacity or a transform ([Styles](./styles#transitions)) |
| Pressed elements and the press count | Clicks, double and triple clicks |
| Dark mode | `ui.isDarkMode()`, `ui.theme` |

A scroll container that a frame does not declare loses its position at that frame's end.

`ui.boxOf` and `ui.containerWidth` give the element's place and size in the last frame. They are empty (`null`,
and 0) before its first layout.

```ceesafe frame
let wide = ui.containerWidth("panel") >= 480.0;
ui.open(Style.column().id("panel").w(Size.grow()).gap(space(2.0)));
ui.label(if (wide) "Wide panel" else "Narrow panel", Text.of().color(SLATE_100));
ui.close();
```

## When a frame runs

A frame runs only when something changed. After a frame, the host asks the `Ui` what to do next:

| Call | Answer |
|---|---|
| `ui.needsFrame()` | `true` when the next frame must run at once |
| `ui.frameDelay()` | Seconds the host may wait for an event, or `null` when nothing changes until one arrives |
| `ui.frameDelayMs(idle)` | The same in whole milliseconds, or `idle` when nothing changes until an event arrives |

`needsFrame` is true when input arrived in this frame, a transition is moving, a scroll has momentum or is being
dragged, or the app asked for a frame.

The end of a host's loop is this:

```ceesafe skip
        if (pane.ui.needsFrame()) {
            window.waitFrame();                             // the next frame, at the display's pace
        } else {
            Window.waitAny(pane.ui.frameDelayMs(IDLE_MS));  // sleep until an event, or the delay's end
        }
```

An idle window uses no CPU. The delay is not `idle` in two cases: a focused text field's caret blinks every half
second, and the app asked for a later frame.

The app asks for frames with two calls:

```ceesafe frame
// Something the frame cannot see changed (a timer fired, a job finished): run another frame after this one.
ui.requestFrame();

// The frame shows a clock: run a frame a second from now, unless one comes sooner.
ui.requestFrameAfter(1.0);
```

A frame forgets the requests of the frame before. A frame that still needs a later frame asks again:

```ceesafe
import ceeui;

public struct Banner {
    public shownUntil: f64;
}

public fn bannerFrame(ui: borrow mut Ui, banner: borrow Banner) {
    let left = banner.shownUntil - ui.time();
    if (left > 0.0) {
        ui.open(Style.row().w(Size.grow()).p(space(3.0)).bg(EMERALD_700));
        ui.label("Saved", Text.of().color(WHITE));
        ui.close();
        ui.requestFrameAfter(left); // the frame that takes the banner away
    }
}
```

`ui.time()` is the platform's clock in seconds, and `ui.frameTime()` the seconds since the frame before.

State held in a `Model` asks for the frame itself when it changes; [An app](./app) covers it.

## Capacity

A `Ui` holds a fixed number of elements, set when it is created.

```ceesafe
import ceeui;

public fn smallUi<region r>(memory: borrow(r) FixedBufferAllocator) -> Result<Ui<r>, AllocError> {
    var capacity = UiCapacity.of(2048); // room for 2,048 elements
    capacity.scrollContainers = 16;
    return Ui.create(memory, capacity);
}
```

`UiCapacity.of(elements)` gives room for that many elements with typical text. Its fields can be changed before
`Ui.create`:

| Field | What it bounds | `UiCapacity.of(n)` |
|---|---|---|
| `layout.elements` | Elements in a frame, text leaves included | `n` |
| `layout.textBytes` | Bytes of all the frame's text | `n * 32` |
| `layout.words` | Words of wrapped text | `n * 16` |
| `layout.lines` | Lines of text | `n * 4` |
| `layout.commands` | Draw commands | `n * 6` |
| `layout.styles` | Style runs of rich text | `n` |
| `animations` | Elements with a transition | 512 |
| `scrollContainers` | Scroll containers | 64 |
| `textBytes` | Bytes of clipboard text, and of an input method's composition | 16,384 |

A frame that does not fit is not drawn in part. `endFrame` returns `Err`, and the frame has no commands. A host
keeps the last frame it drew, or stops:

```ceesafe skip
        match (ui.endFrame(mut platform, raster)) {
            Ok() => {}
            Err(_) => {
                Console.writeErrorLine("headless: the frame does not fit the UI capacity");
                return 1;
            }
        }
```

Long lists do not need a large capacity: a virtual list declares only the rows on screen
([Lists, grids and charts](./lists-grids-charts)).

## Memory

A `Ui` takes all its memory once, from a `FixedBufferAllocator` over bytes the host sets aside. It borrows that
allocator for its whole life.

```ceesafe
import ceeui;

public fn run() -> i32 {
    var storage = Vector<u8>.create();
    for (i in 0..8388608) {
        storage.push(0)!;
    }
    let memory = FixedBufferAllocator.over(mut storage.viewMut());
    var ui = match (Ui.create(memory, UiCapacity.of(2048))) {
        Ok(u) => u,
        Err(_) => { return 1; } // the bytes are too few for this capacity
    };
    ui.setDarkMode(true);
    // ... the loop of frames ...
    return 0;
}
```

`Ui.create` fails with an `AllocError` when the bytes are too few for the capacity. After it, a frame allocates
nothing: `begin`, `open`, `close`, the controls and `endFrame` work in the memory the `Ui` already has. A frame's
own text goes into arrays on the stack, as the count in [Getting started](./getting-started#the-frame) does.

The software renderer is made the same way, from an allocator of its own (`Raster.create`).
[A desktop host](./desktop) goes through a whole host, where `Pane` holds a window's `Ui` and its memory.
