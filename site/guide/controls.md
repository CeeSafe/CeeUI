# Controls

This page lists CeeUI's controls: what each one is, how to call it, and what the call returns. Lists of many
rows, grids and charts are on [Lists, grids and charts](./lists-grids-charts); text fields with a selection and
editors are on [Editing text](./editing).

## How a control works

A control is a function of `Controls`, called each frame. It draws itself, reads the frame's input, and returns
what the user did.

```ceesafe
import ceeui;

public struct Settings {
    public sound: bool;
    public saves: u32;
}

public fn settingsFrame(ui: borrow mut Ui, app: borrow mut Settings) {
    ui.open(Style.column().gap(space(3.0)).p(space(4.0)));
    Controls.checkbox(mut ui, "sound", "Play sounds", mut app.sound);
    if (Controls.button(mut ui, "save", "Save")) {
        app.saves += 1;
    }
    ui.close();
}
```

Three things hold for every control:

- **The first argument is the `Ui`**, passed as `mut ui`.
- **The second is the control's id**, a name unique in the window. CeeUI keeps the focus, hover transitions
  and the control's place by id. Two controls with one id act as one. A control's parts take ids made from its id.
- **The state lives in the app.** A control keeps no value of its own. The app passes the value as `mut field`,
  and the control changes it there. Most controls return `true` in the frame the value changed.

A control without a label of its own (a text input, a slider, a dropdown) is named by its id to a screen reader.
`ui.accessName` gives it a proper name; see [Accessibility](./accessibility).

```ceesafe frame
var volume: f32 = 0.5;
Controls.slider(mut ui, "vol", mut volume, 0.0, 1.0, 200.0);
ui.accessName("vol", "Volume");
```

The controls draw in the `Ui`'s theme: dark or light, by `ui.setDarkMode`.

| Control | Call | Returns |
|---|---|---|
| Button | `Controls.button` | `true` when clicked |
| Checkbox | `Controls.checkbox` | `true` when toggled |
| Radio group | `Controls.radio` | `true` when the choice changed |
| Tabs | `Controls.tabs` | `true` when another tab was opened |
| Slider | `Controls.slider` | `true` when the value changed |
| Spin input | `Controls.spinInput` | `true` when the value changed |
| Text input | `Controls.textInput`, `Controls.textField` | `true` when the text changed |
| Dropdown | `Controls.dropdown` | `true` when the choice changed |
| Listbox | `Controls.listbox`, `Controls.listboxMulti` | `true` when the selection changed |
| Context menu | `Controls.contextMenu` | The chosen item's index, or `null` |
| Tooltip | `Controls.tooltip` | Nothing |
| Menu bar | `Controls.menuBar` | Nothing; its items trigger actions |

## Button

`Controls.button(mut ui, id, label)` is `true` in the frame the button was clicked. A click is a press and a
release over the button. Tab reaches it, and Enter or Space presses it.

```ceesafe
import ceeui;

public struct Counter {
    public count: u64;
}

public fn counterFrame(ui: borrow mut Ui, app: borrow mut Counter) {
    ui.open(Style.row().gap(space(2.0)));
    if (Controls.button(mut ui, "add", "Add one")) {
        app.count += 1;
    }
    if (Controls.button(mut ui, "reset", "Reset")) {
        app.count = 0;
    }
    ui.close();
}
```

## Checkbox

`Controls.checkbox(mut ui, id, label, mut value)` shows a box and a label, and flips `value` when pressed. It
returns `true` in the frame it flipped. Space or Enter flips it while it has the focus.

```ceesafe
import ceeui;

public struct Notify {
    public email: bool;
    public push: bool;
}

public fn notifyFrame(ui: borrow mut Ui, app: borrow mut Notify) {
    ui.open(Style.column().gap(space(2.0)));
    Controls.checkbox(mut ui, "email", "Email notifications", mut app.email);
    if (Controls.checkbox(mut ui, "push", "Push notifications", mut app.push)) {
        // app.push has its new value here
    }
    ui.close();
}
```

## Radio group

`Controls.radio(mut ui, id, items, mut selected)` shows one option for each item, in a column. `selected` is the
index of the chosen one. It returns `true` when the choice changed.

```ceesafe
import ceeui;

public struct Appearance {
    public theme: usize;
}

public fn appearanceFrame(ui: borrow mut Ui, app: borrow mut Appearance) {
    let themes = ["System", "Light", "Dark"];
    if (Controls.radio(mut ui, "theme", themes[..], mut app.theme) && app.theme != 0) {
        ui.setDarkMode(app.theme == 2);
    }
}
```

The items are a `Slice<StringView>`: an array of strings and `[..]`. Each option is a Tab stop; Space or Enter
selects it.

## Tabs

`Controls.tabs(mut ui, id, items, mut active)` shows a row of tabs. `active` is the index of the open one. It
returns `true` when another tab was opened. The app draws the open tab's content itself.

```ceesafe
import ceeui;

public struct Pages {
    public tab: usize;
}

public fn pagesFrame(ui: borrow mut Ui, app: borrow mut Pages) {
    let titles = ["General", "Network", "About"];
    ui.open(Style.column().w(Size.grow()).gap(space(3.0)));
    Controls.tabs(mut ui, "pages", titles[..], mut app.tab);
    match (app.tab) {
        0 => { ui.label("General settings", Text.of()); }
        1 => { ui.label("Network settings", Text.of()); }
        _ => { ui.label("About this app", Text.of()); }
    }
    ui.close();
}
```

The open tab is the row's Tab stop. With the focus on it, Left and Right open the tab beside it.

## Slider

`Controls.slider(mut ui, id, mut value, min, max, width)` shows a track `width` points wide. A press on the
track captures the pointer, and the value follows it until release. It returns `true` when the value changed.

```ceesafe
import ceeui;

public struct Audio {
    public volume: f32;
}

public fn audioFrame(ui: borrow mut Ui, app: borrow mut Audio) {
    ui.open(Style.row().gap(space(3.0)).alignXY(AxisAlign.Start, AxisAlign.Center));
    Controls.slider(mut ui, "volume", mut app.volume, 0.0, 1.0, 200.0);
    var text = [0u8; 16];
    var w = TextWriter.create(mut text[..]);
    Format.fixed(mut w, app.volume * 100.0, 0);
    w.byte('%');
    ui.labelBytes(w.written(), Text.of().mono());
    ui.close();
}
```

The value is an `f32` between `min` and `max`.

## Spin input

`Controls.spinInput(mut ui, id, mut value, min, max, step)` shows a whole number between a minus and a plus
button. The buttons, and Up and Down while it has the focus, change the value by `step` within `min` and `max`.
It returns `true` when the value changed.

```ceesafe
import ceeui;

public struct Order {
    public quantity: i32;
}

public fn orderFrame(ui: borrow mut Ui, app: borrow mut Order) {
    Controls.spinInput(mut ui, "quantity", mut app.quantity, 0, 99, 1);
}
```

## Text input

A text control edits UTF-8 bytes in a buffer the app owns: an array, and how many of its bytes are text. Text
that does not fit the buffer is not taken. There are two:

| Call | What it is |
|---|---|
| `Controls.textInput` | Edits at the end only: typing appends, Backspace removes the last character |
| `Controls.textField` | A caret and a selection, the clipboard, and input methods |

Both return `true` when the text changed, and both give up the focus on Enter.

```ceesafe
import ceeui;

public struct Note {
    public text: [u8; 64];
    public length: usize;
}

public fn noteFrame(ui: borrow mut Ui, app: borrow mut Note) {
    ui.open(Style.column().w(Size.fixed(280.0)));
    Controls.textInput(mut ui, "note", mut app.text[..], mut app.length);
    ui.close();
}
```

A text control is as wide as its container, so put it in an element of the width you want.

`Controls.textField` also takes the text metrics, which it measures with to place the caret, and a `TextEdit`:
the caret and the selection. A frame gets the metrics from its host and passes them on.

```ceesafe
import ceeui;

public struct Profile {
    public name: [u8; 64];
    public nameLength: usize;
    public nameEdit: TextEdit;
}

public fn profileFrame<M: TextMetrics>(ui: borrow mut Ui, app: borrow mut Profile, metrics: borrow M) {
    ui.open(Style.column().w(Size.fixed(280.0)));
    Controls.textField(mut ui, metrics, "name", mut app.name[..], mut app.nameLength, mut app.nameEdit);
    ui.accessName("name", "Name");
    ui.close();
}
```

[Editing text](./editing) describes `textField`, the password field and the multi-line editors.

## Dropdown

`Controls.dropdown(mut ui, id, items, mut selected, mut open)` shows the chosen item. A press opens a list
below it, above everything else. A press on an item chooses it and closes the list; a press outside closes it.
It returns `true` when the choice changed.

```ceesafe
import ceeui;

public struct Ticket {
    public priority: usize;
    public priorityOpen: bool;
}

public fn ticketFrame(ui: borrow mut Ui, app: borrow mut Ticket) {
    let priorities = ["Low", "Medium", "High"];
    ui.open(Style.column().w(Size.fixed(180.0)));
    Controls.dropdown(mut ui, "priority", priorities[..], mut app.priority, mut app.priorityOpen);
    ui.close();
}
```

The app keeps two things: the index of the chosen item, and whether the list is open. The dropdown is as wide as
its container.

## Listbox

`Controls.listbox(mut ui, id, items, mut selected, height)` is a scrolling list with one selected row.
`selected` is that row's index and `height` the list's height. It returns `true` when the selection changed.

```ceesafe
import ceeui;

public struct Files {
    public current: usize;
}

public fn filesFrame(ui: borrow mut Ui, app: borrow mut Files) {
    let names = ["README.md", "main.cee", "app.cee", "ceesafe.toml"];
    if (Controls.listbox(mut ui, "files", names[..], mut app.current, Size.fixed(150.0))) {
        // app.current is the row that was chosen
    }
}
```

The list is a Tab stop. With the focus, Up and Down choose the row.

A listbox builds every item each frame. For hundreds of rows or more, or rows that hold more than a label, use a
[virtual list](./lists-grids-charts).

### Multiple selection

`Controls.listboxMulti` takes a `bool` for each item in place of one index. A press on a row flips that row's
`bool`. It returns `true` when one flipped.

```ceesafe
import ceeui;

public struct Stage {
    public staged: [bool; 4];
}

public fn stageFrame(ui: borrow mut Ui, app: borrow mut Stage) {
    let names = ["README.md", "main.cee", "app.cee", "ceesafe.toml"];
    Controls.listboxMulti(mut ui, "stage", names[..], mut app.staged[..], Size.fixed(150.0));
}
```

Each row is a Tab stop; Space or Enter flips it.

## Context menu

`Controls.contextMenu(mut ui, id, items, mut open, x, y)` shows a menu at a place in the window while `open` is
true. It returns the index of the item chosen this frame, or `null`. Choosing an item, or pressing outside the
menu, sets `open` to false.

The app opens the menu, usually on a secondary (right) press, and keeps where it was opened.

```ceesafe
import ceeui;

public struct Canvas {
    public menuOpen: bool;
    public menuX: f32;
    public menuY: f32;
    public removed: u32;
}

public fn canvasFrame(ui: borrow mut Ui, app: borrow mut Canvas) {
    let p = ui.pointer();
    if (p.secondaryPressed) {
        app.menuOpen = true;
        app.menuX = p.position.x;
        app.menuY = p.position.y;
    }
    ui.open(Style.column().id("canvas").w(Size.grow()).h(Size.grow()));
    ui.label("Right-click anywhere", Text.of());
    let items = ["Rename", "Duplicate", "Remove"];
    match (Controls.contextMenu(mut ui, "canvasMenu", items[..], mut app.menuOpen, app.menuX, app.menuY)) {
        Some(choice) => {
            if (choice == 2) {
                app.removed += 1;
            }
        }
        null => {}
    }
    ui.close();
}
```

Declare the menu last in the frame: it floats above what is declared before it. To open a menu over one element
only, test `ui.secondaryPressedOn(hashId("canvas"))`.

`Controls.contextMenuWith` takes one more argument after the items, a `Slice<bool>`: whether each item is
enabled. A disabled item is dimmed and cannot be chosen.

```ceesafe
import ceeui;

public struct Clip {
    public menuOpen: bool;
    public menuX: f32;
    public menuY: f32;
    public hasSelection: bool;
}

public fn clipMenu(ui: borrow mut Ui, app: borrow mut Clip) -> usize? {
    let items = ["Cut", "Copy", "Paste"];
    let enabled = [app.hasSelection, app.hasSelection, true];
    return Controls.contextMenuWith(mut ui, "clip", items[..], enabled[..], mut app.menuOpen, app.menuX, app.menuY);
}
```

For text, `Controls.editMenu` is this menu ready made; see [Editing text](./editing#the-edit-menu).

## Tooltip

`Controls.tooltip(mut ui, targetId, text)` shows a tip beside the pointer while the element with id `targetId`
is under it. It returns nothing.

```ceesafe frame
ui.open(Style.row().gap(space(2.0)));
if (Controls.button(mut ui, "sync", "Sync")) {
    // start syncing
}
ui.close();
Controls.tooltip(mut ui, "sync", "Fetch changes from the server");
```

The target is any element with an id, a control or an element of your own (`Style.box().id("badge")`). Declare
tooltips last in the frame, so that they float above the rest.

## Menu bar

`Controls.menuBar(mut ui, id, model, mut open)` draws a menu bar from a `MenuModel`: a row of menu titles, each
opening a list of items. `open` says which menu is open, 0 for none, and the app keeps it between frames.

```ceesafe
import ceeui;

public struct Shell {
    public menus: MenuModel;
    public menuOpen: usize;
}

public fn shellFrame(ui: borrow mut Ui, app: borrow mut Shell) {
    ui.open(Style.column().w(Size.grow()).h(Size.grow()));
    Controls.menuBar(mut ui, "menubar", app.menus, mut app.menuOpen);
    ui.label("The window's content", Text.of());
    ui.close();
}
```

A menu's item does not return a value. Each item names an action, and choosing it triggers that action on the
next frame, as its key binding would. The frame asks `ui.action("name")` whether it was triggered.

A host shows the same `MenuModel` as the system's menus: a macOS menu bar, a Windows window's menu.
`Controls.menuBar` is for where the system has no menu bar, such as X11. [The app](./app) describes menus,
actions and key bindings.
