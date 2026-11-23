# Input: keys, pointer, touch

This page covers what a frame can ask about the user's input: the pointer over an element, keys and typed
characters, the focus, key bindings, scrolling, drag and drop, and touch gestures.

There are no event handlers. A frame asks the `Ui` what happened, and acts on the answer in the same place it
declares the element. The answers are as of this frame's `begin`. See [A frame](./frame) for when frames run.

## Names and ids

Input is asked about by element. An element gets a name with `Style.id("name")`. Some queries take the name, and
others take its hash, a `u32` that `hashId("name")` gives.

```ceesafe frame
ui.open(Style.box().id("card").p(space(4.0)).bg(SLATE_800));
ui.label("A card", Text.of().color(SLATE_100));
ui.close();
let byName = ui.hovered("card");
let byHash = ui.overHash(hashId("card"));
```

For names made at run time (a row's number), build the bytes and hash them with `hashBytes`, then give the element
its id with `Style.idHash(hash)`.

## The pointer on an element

Hit testing uses the last frame's layout, so a query may come before or after the element is declared.

| Query | True when |
|---|---|
| `ui.hovered(name)` | The element is under the pointer |
| `ui.overHash(id)` | The same, by hash |
| `ui.pressed(name)` | It is under the pointer and the primary button is down |
| `ui.clicked(name)` | The button went down and came up over it, this frame |
| `ui.pressedOn(id)` | The primary button went down over it this frame |
| `ui.secondaryPressedOn(id)` | The secondary (right) button went down over it this frame |
| `ui.releasedOutside(id)` | The primary button came up away from it, and it is not capturing a drag |
| `ui.pressedOutside(a, b)` | The primary button went down over neither `a` nor `b` (`b` may be 0) |

```ceesafe
import ceeui;

public struct Likes {
    public count: u32;
}

public fn likesFrame(ui: borrow mut Ui, app: borrow mut Likes) {
    let heart = Style.box().id("heart").px(space(3.0)).py(space(2.0)).bg(SLATE_800).rounded(Radius.Md)
        .cursor(Cursor.Pointer).hover(Style.of().bg(SLATE_700)).active(Style.of().bg(SLATE_600));
    ui.open(heart);
    ui.label("Like", Text.of().color(SLATE_100));
    ui.close();
    if (ui.clicked("heart")) {
        app.count += 1;
    }
}
```

`Style.hover` and `Style.active` change an element's look while it is hovered or pressed, and `Style.cursor` sets
the cursor shape over it (`Cursor.Default`, `Pointer` or `Text`). [Styles](./styles) has the variants.

A screen reader's activation of an element counts as a click and as a press on it.

`ui.pointer()` gives the whole pointer for this frame:

| Field of `Pointer` | What it is |
|---|---|
| `position` | Where it is, in points |
| `down` | The primary button is held |
| `pressed`, `released` | It went down, or came up, this frame |
| `secondaryDown`, `secondaryPressed` | The same for the secondary button |
| `wheel` | The wheel's movement this frame |

`ui.pressCount()` counts presses in quick succession: 2 for a double click, 3 for a triple.

```ceesafe frame
ui.open(Style.box().id("word").p(space(2.0)));
ui.label("Double-click me", Text.of().color(SLATE_100));
ui.close();
if (ui.pressedOn(hashId("word")) && ui.pressCount() == 2) {
    ui.setFocus(hashId("word"));
}
```

## Capturing a drag

A control that follows the pointer after a press (a slider's thumb) captures it. `ui.capture(id)` records the id,
`ui.captured()` gives it back (0 for none), and the control lets go with `ui.capture(0)`.

```ceesafe
import ceeui;

public struct Knob {
    public x: f32;
}

public fn knobFrame(ui: borrow mut Ui, app: borrow mut Knob) {
    let id = hashId("knob");
    if (ui.pressedOn(id)) {
        ui.capture(id);
    }
    if (ui.captured() == id) {
        let p = ui.pointer();
        if (p.down) {
            app.x = p.position.x;
        } else {
            ui.capture(0);
        }
    }
    ui.open(Style.box().id("knob").absolute().left(app.x).top(20.0).w(Size.fixed(24.0)).h(Size.fixed(24.0))
        .bg(BLUE_500).rounded(Radius.Full));
    ui.close();
}
```

The built-in slider, scroll bars and text selection capture in this way. See [Controls](./controls).

## Keys

`Key` names the keys that do not type a character. Letters, digits and punctuation arrive as typed characters.

| Query | True when |
|---|---|
| `ui.keyDown(key)` | The key is held |
| `ui.keyPressed(key)` | It went down this frame |
| `ui.keyRepeated(key)` | The system's key repeat fired this frame |
| `ui.keyHit(key)` | Pressed or repeated this frame |
| `ui.shiftDown()` | Either Shift key is held |
| `ui.wordModifierDown()` | The key that makes an arrow move by words is held: Option on macOS, Control elsewhere |

The keys of `Key`:

| Group | Keys |
|---|---|
| Editing | `Escape`, `Enter`, `Tab`, `Backspace`, `Delete` |
| Moving | `Left`, `Right`, `Up`, `Down`, `Home`, `End`, `PageUp`, `PageDown` |
| Modifiers | `LeftShift`, `RightShift`, `LeftControl`, `RightControl`, `LeftAlt`, `RightAlt` |
| Function keys | `F1` to `F12` |

```ceesafe
import ceeui;

public struct Menu {
    public row: usize;
    public rows: usize;
}

public fn menuKeys(ui: borrow mut Ui, app: borrow mut Menu) {
    if (ui.keyHit(Key.Down) && app.row + 1 < app.rows) {
        app.row += 1;
    }
    if (ui.keyHit(Key.Up) && app.row > 0) {
        app.row -= 1;
    }
}
```

Typed characters are Unicode code points. `ui.typed(c)` says whether `c` was typed this frame, and leaves it for
others to read. `ui.nextChar()` takes the next one, or gives 0 when none is left. Each is taken once.

```ceesafe frame
if (ui.focused() == 0 && (ui.typed('t') || ui.typed('T'))) {
    let dark = ui.isDarkMode();
    ui.setDarkMode(!dark);
}
```

The test `ui.focused() == 0` keeps a key from acting while a text field has the focus. The text controls read the
characters themselves; see [Editing](./editing).

## Focus

One element has the keyboard focus, or none has. The focus is an id.

| Call | What it does |
|---|---|
| `ui.setFocus(id)` | Gives element `id` the focus |
| `ui.clearFocus()` | No element has it |
| `ui.leaveFocus(id)` | `id` gives the focus up at the end of this frame, unless `setFocus` is called before then |
| `ui.focused()` | The focused id, or 0 |
| `ui.hasFocus(id)` | Whether `id` has it |
| `ui.hasFocusNamed(name)` | The same, by name |
| `ui.registerFocusable(id)` | `id` takes part in Tab cycling this frame |
| `ui.activatedByKey(id)` | `id` has the focus and Enter or Space was pressed this frame |

Tab moves the focus to the next element registered this frame, and Shift-Tab to the one before. The order is the
order of registration, and it wraps at both ends. The built-in controls register themselves, and Enter or Space
presses a focused button.

An element of your own joins in with `registerFocusable`. `Style.isFocused(on)` turns the `focus` variant on, so the
element shows that it has the focus:

```ceesafe
import ceeui;

public struct Stars {
    public starred: bool;
}

public fn starFrame(ui: borrow mut Ui, app: borrow mut Stars) {
    let id = hashId("star");
    ui.registerFocusable(id);
    if (ui.pressedOn(id)) {
        ui.setFocus(id);
    }
    if (ui.clicked("star") || ui.activatedByKey(id)) {
        app.starred = !app.starred;
    }
    ui.open(Style.box().id("star").px(space(3.0)).py(space(2.0)).rounded(Radius.Md)
        .bg(if (app.starred) AMBER_500 else SLATE_800)
        .isFocused(ui.hasFocus(id)).focus(Style.of().border(2.0, BLUE_300)));
    ui.label("Star", Text.of().color(SLATE_100));
    ui.close();
}
```

`isFocused` takes any condition. The demo's sidebar uses it for a cursor that the arrow keys move, which is not the
keyboard focus.

## Key bindings and actions

A key binding ties a keystroke to a named action. A frame asks whether the action was triggered.

```ceesafe
import ceeui;

public struct Palette {
    public bound: bool;
    public shown: bool;
}

public fn paletteFrame(ui: borrow mut Ui, app: borrow mut Palette) {
    if (!app.bound) {
        let ok = ui.bind("secondary-shift-p", "palette", "");
        app.bound = true;
    }
    if (ui.action("palette")) {
        app.shown = !app.shown;
    }
}
```

`secondary` is Command on macOS and Control elsewhere. A menu item that names the same action triggers it too.
[State, actions and menus](./app) covers the keystroke syntax, key contexts, `ui.dispatch`, `Ui.bindingFor` and
menus.

## Scrolling

`Style.scroll(axis)` makes an element a scroll container. `ScrollAxis` is `Vertical`, `Horizontal` or `Both`. A
scroll container needs an id, because its offset is kept by id between frames.

```ceesafe frame
ui.open(Style.column().id("log").w(Size.fixed(240.0)).h(Size.fixed(120.0)).gap(space(1.0))
    .scroll(ScrollAxis.Vertical));
for (i in 0usize..40) {
    ui.label("A line of the log", Text.of().color(SLATE_300));
}
ui.close();
```

The user scrolls it in these ways:

| Input | What it does |
|---|---|
| The wheel | Scrolls the deepest scroll container under the pointer that can move |
| A drag with the mouse | Drags the content, and its release carries on with momentum |
| The scroll bar | A thumb is drawn along the right edge while the content is taller; dragging it scrolls |
| A finger's pan | Scrolls along the pan's axis, and its release flings the content |

`Style.noDragScroll()` turns the mouse's drag off for a panel, so a drag in it can select text. The wheel, the
scroll bar and a finger still scroll it.

A frame reads and moves the offset by the container's name:

| Call | What it does |
|---|---|
| `ui.scrollOffset(name)` | The offset as a `Vec2`: 0 at the start, negative as the content moves up or left |
| `ui.scrollBy(name, dx, dy)` | Moves by a delta, clamped to the content |
| `ui.scrollIntoView(name, top, bottom)` | Scrolls as little as shows the content's rows from `top` to `bottom` |
| `ui.scrollToTop(name)` | To the start |
| `ui.scrollToBottom(name)` | To the end |

`top` and `bottom` are in points from the top of the content.

```ceesafe frame
if (ui.keyHit(Key.PageDown)) {
    ui.scrollBy("log", 0.0, -100.0);
}
if (ui.keyPressed(Key.End)) {
    ui.scrollToBottom("log");
}
let scrolledDown = -ui.scrollOffset("log").y;
```

Lists and grids scroll and show only their visible rows by themselves; see
[Lists, grids and charts](./lists-grids-charts).

## Drag and drop

An element becomes a drag source with `ui.dragSource(id, payload)`. The payload is a kind and a value that the app
chooses. A press on the source that moves more than 4 points starts a drag, and its release is not a click.

A drop target asks `ui.dropTarget(id, kind)` each frame. It gives a `DropState`:

| `DropState` | Meaning |
|---|---|
| `Away` | No drag of that kind is over the element |
| `Over(payload)` | A drag of that kind is over it |
| `Dropped(payload)` | One was dropped on it this frame |

```ceesafe
import ceeui;

const CARD: u32 = 1;

public struct Board {
    public done: [bool; 3];
}

public fn boardFrame(ui: borrow mut Ui, app: borrow mut Board) {
    let bin = hashId("done");
    let target = ui.dropTarget(bin, CARD);
    let over = match (target) {
        DropState.Over(_) => true,
        _ => false,
    };
    match (target) {
        DropState.Dropped(p) => {
            if (p.value < 3) {
                app.done[p.value as usize] = true;
            }
        }
        _ => {}
    }
    ui.open(Style.row().gap(space(4.0)));
    ui.open(Style.column().gap(space(2.0)));
    let names = ["card-0", "card-1", "card-2"];
    for (k in 0usize..3) {
        if (app.done[k]) {
            continue;
        }
        let card = hashId(names[k]);
        ui.open(Style.box().idHash(card).px(space(3.0)).py(space(2.0)).bg(SLATE_700).rounded(Radius.Md));
        let dragged = ui.dragSource(card, DragPayload.of(CARD, k as u64));
        ui.label("A card", Text.of().color(if (dragged) SLATE_500 else SLATE_100));
        ui.close();
    }
    ui.close();
    ui.open(Style.box().idHash(bin).w(Size.fixed(160.0)).h(Size.fixed(120.0)).rounded(Radius.Lg)
        .bg(if (over) BLUE_600 else SLATE_800));
    ui.label("Done", Text.of().color(SLATE_100));
    ui.close();
    ui.close();
}
```

`dragSource` is called while the element is open or after it closes. It is true while that element is the one being
dragged, so the frame can dim it.

`ui.dragging()` gives the payload of the drag under way, or null. `ui.dragPosition()` says where it is. Use them to
draw what is dragged under the pointer:

```ceesafe frame
match (ui.dragging()) {
    Some(p) => {
        let at = ui.dragPosition();
        ui.open(Style.box().fixed().passthrough().left(at.x + 8.0).top(at.y + 8.0).z(500).px(space(3.0))
            .py(space(2.0)).rounded(Radius.Md).bg(BLUE_600));
        ui.label("Moving", Text.of().color(SLATE_50));
        ui.close();
    }
    null => {}
}
```

`passthrough()` lets the pointer through the floating element, so the drop target under it is still hit.

### Files from the system

Files dragged from the system over the window are a drag of kind `DRAG_FILES`. Its value is the host's number for
the drop. The frame keeps that number, and the app asks its host for the paths after the frame.

```ceesafe
import ceeui;

public struct Inbox {
    /// The host's number for the last drop, or 0.
    public dropWanted: u64;
}

public fn inboxFrame(ui: borrow mut Ui, app: borrow mut Inbox) {
    let zone = hashId("inbox");
    let target = ui.dropTarget(zone, DRAG_FILES);
    let over = match (target) {
        DropState.Over(_) => true,
        _ => false,
    };
    match (target) {
        DropState.Dropped(p) => { app.dropWanted = p.value; }
        _ => {}
    }
    ui.open(Style.box().idHash(zone).w(Size.grow()).h(Size.fixed(90.0)).center().rounded(Radius.Lg)
        .border(1.0, if (over) EMERALD_500 else SLATE_600));
    ui.label(if (over) "Drop to add them" else "Drop files here", Text.of().color(SLATE_300));
    ui.close();
}
```

A host must pass file drags on for this to work. [A desktop host](./desktop#file-drops) shows how the paths are
read.

## Touch and gestures

On a touch screen the `Ui` recognizes four gestures from the fingers, one at a time:

| Gesture | How it is made |
|---|---|
| Tap | A finger lifted within 10 points of where it came down. Two in quick succession are a double tap |
| Long press | A finger held for half a second within those 10 points |
| Pan | One finger moved past 10 points |
| Pinch | Two fingers. More fingers are ignored |

Each gesture is aimed at the elements under the point where it began. Ask by element name, or ask for the gesture
wherever it began:

| On an element | Anywhere | Gives |
|---|---|---|
| `ui.tapOn(name)` | `ui.tap()` | `Tap?`: `position` and `count` (2 for a double tap) |
| `ui.longPressOn(name)` | `ui.longPress()` | `Vec2?`: where the finger is |
| `ui.panOn(name)` | `ui.pan()` | `Pan?`: `phase`, `start`, `position`, `delta`, `velocity`, and `translation()` |
| `ui.pinchOn(name)` | `ui.pinch()` | `Pinch?`: `phase`, `center`, `startCenter`, `scale`, `scaleDelta` |

```ceesafe
import ceeui;

public struct Photo {
    public zoom: f32;
    public x: f32;
    public y: f32;
}

public fn photoFrame(ui: borrow mut Ui, app: borrow mut Photo) {
    match (ui.tapOn("photo")) {
        Some(t) => {
            if (t.count == 2) {
                app.zoom = 1.0;
            }
        }
        null => {}
    }
    match (ui.pinchOn("photo")) {
        Some(p) => { app.zoom *= p.scaleDelta; }
        null => {}
    }
    match (ui.panOn("photo")) {
        Some(p) => {
            app.x += p.delta.x;
            app.y += p.delta.y;
        }
        null => {}
    }
    ui.open(Style.box().id("photo").w(Size.fixed(240.0)).h(Size.fixed(160.0)).bg(SLATE_800).clip());
    ui.open(Style.box().w(Size.fixed(240.0)).h(Size.fixed(160.0)).bg(SKY_500).translate(app.x, app.y)
        .scale(app.zoom));
    ui.close();
    ui.close();
}
```

A pan and a pinch last several frames. Their `phase` is a `GesturePhase`:

| Phase | Meaning |
|---|---|
| `Began` | The gesture started this frame. A pan's `delta` is then all its movement since the finger came down |
| `Changed` | It moved |
| `Ended` | The fingers lifted. A pan's `velocity` is its speed at the lift, in points a second |
| `Cancelled` | The system took the fingers away, or another gesture took over: undo what it began |

A pan or pinch that begins and ends within one frame gives only its end.

### Touch and the pointer

Controls made for a pointer work by touch without changes:

- The first finger is the pointer while it may still be a tap, so a button shows pressed.
- Its lift is the release that clicks.
- A pan or a pinch cancels the press, so there is no click.
- A long press is a secondary press, which opens a context menu.
- A press that starts a drag keeps the finger as the pointer, so drag sources work by touch.

### Momentum scrolling

A pan scrolls the deepest scroll container under its start that can move along the pan's axis. Its release flings
the content, which slows as a UIKit scroll view does. A finger down stops a fling. An edge of the content stops it
too.

The frames that a fling needs are asked for by the `Ui`: `ui.needsFrame()` stays true while content is moving.

A host must pass the fingers on (`Platform.nextTouch`). The desktop host does on iOS and Android; see
[Phones](./phones).
