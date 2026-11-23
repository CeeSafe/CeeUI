# Accessibility

This page says what a ClayWind app tells a screen reader, how a control of your own reports itself, how lists and
tables are reported, what a reader's actions do, and what each system's reader gets.

## What a screen reader is told

ClayWind draws its own pixels, so a screen reader cannot see the window's controls. Each frame, the `Ui` builds a
list of the frame's elements in reading order. The host gives the list to the system. Each element has:

| Part | What it is |
|---|---|
| A role | What the element is: an `AccessRole` |
| A name | Its label, as UTF-8 text |
| A value | A field's text or a dropdown's choice; a slider's range and where it is |
| A state | Flags: checked, selected, focused, disabled, expanded, secure |
| A place | Its box in the window, from the frame's layout |
| A caret | A text field's caret and the other end of its selection |

The roles:

| `AccessRole` | What it is |
|---|---|
| `Text` | Text that is read and nothing else |
| `Button` | A button |
| `Checkbox` | A checkbox |
| `Radio` | One of a set of which one is chosen |
| `TextField` | A text field |
| `Slider` | A slider or a spin field |
| `Tab` | A tab |
| `ListItem` | A row of a list |
| `MenuItem` | An item of a menu |
| `Dropdown` | A button that opens a list of choices and shows the chosen one |
| `Image` | An image |
| `Group`, `List`, `Menu`, `MenuBar`, `TabList`, `Table`, `Row`, `Cell` | Containers: see [Containers](#containers) |
| `ColumnHeader` | The title of a table's column |

The state flags:

| Flag | The element |
|---|---|
| `ACCESS_CHECKED` | is checked (a checkbox, a radio) |
| `ACCESS_SELECTED` | is selected (a row, a tab) |
| `ACCESS_FOCUSED` | has the keyboard focus |
| `ACCESS_DISABLED` | cannot be used |
| `ACCESS_EXPANDED` | is open (a dropdown, a menu's title) |
| `ACCESS_SECURE` | hides its text as it is typed |

## The built-in controls

The controls report themselves. A button is a `Button` named by its label, a checkbox a `Checkbox` that says whether
it is checked, a text field a `TextField` with its text and caret. Lists, menus, the menu bar, tabs and the data
grid are containers. A chart is an `Image`: a reader hears its name and size.

A text label outside a control is an element of its own, with the role `Text`. A label inside a control is the
control's and is not reported apart.

A control with no label of its own, such as a text field, a slider or a dropdown, is called by its id. Name it
with `ui.accessName` after the control:

```ceesafe
import claywind;

public struct Settings {
    public server: [u8; 64];
    public serverLength: usize;
    public volume: f32;
}

public fn settingsFrame(ui: borrow mut Ui, app: borrow mut Settings) {
    ui.open(Style.column().p(space(4.0)).gap(space(3.0)));
    Controls.textInput(mut ui, "server", mut app.server[..], mut app.serverLength);
    ui.accessName("server", "Server address");
    Controls.slider(mut ui, "volume", mut app.volume, 0.0, 1.0, 200.0);
    ui.accessName("volume", "Volume");
    ui.close();
}
```

## A control of your own

A control you build from elements reports itself with one call after its `open`:
`ui.access(id, role, label)`. The id is the element's own, so the element needs one. Text inside the element is
then the control's.

`ui.access` returns the node's index. The setters take it:

| Call | What it sets |
|---|---|
| `ui.accessFlag(node, flag, on)` | A state flag, on or off |
| `ui.accessValue(node, bytes)` | The value as text |
| `ui.accessRange(node, min, max, now)` | A slider's range and where it is |
| `ui.accessCaret(node, caret, anchor)` | A text field's caret and its selection's other end, as byte offsets into the value. Call it after `accessValue` |
| `ui.accessName(name, label)` | The label of the element with the id `name` |

This is a toggle that a pointer, the keyboard and a screen reader can all work:

```ceesafe
import claywind;

public fn starToggle(ui: borrow mut Ui, starred: borrow mut bool) {
    let id = hashId("star");
    ui.registerFocusable(id); // Tab reaches it
    ui.open(Style.box().idHash(id).px(space(3.0)).py(space(2.0)).bg(SLATE_700).cursor(Cursor.Pointer));
    if (ui.pressedOn(id) || ui.activatedByKey(id)) {
        starred = !starred;
    }
    let node = ui.access(id, AccessRole.Checkbox, "Starred".bytes());
    ui.accessFlag(node, ACCESS_CHECKED, starred);
    ui.label("Star", Text.of().color(SLATE_100));
    ui.close();
}
```

Change the state before you report it, so the frame says the new state.

## Containers

A container holds other elements. What is reported between a container's `open` and its `close` is its child, and a
reader says where the child is: "3 of 40".

| Role | What it holds |
|---|---|
| `Group` | Elements that belong together under a name: a toolbar, a form's section |
| `List` | A list's rows (`ListItem`s) |
| `Menu` | An open menu's items (`MenuItem`s) |
| `MenuBar` | A menu bar's menus |
| `TabList` | A row of `Tab`s |
| `Table` | A table's `Row`s |
| `Row` | A table row's `Cell`s and `ColumnHeader`s |
| `Cell` | What is drawn in the cell |

A `ColumnHeader` is not a container: it holds its text, as a control does. Containers nest up to 16 deep.

This is a list of your own:

```ceesafe frame
let names = ["Alpha", "Beta", "Gamma"];
ui.open(Style.column().id("servers").gap(space(1.0)));
let _ = ui.access(hashId("servers"), AccessRole.List, "Servers".bytes());
for (i in 0..names.length) {
    ui.open(Style.box().id(names[i]).p(space(2.0)));
    let row = ui.access(hashId(names[i]), AccessRole.ListItem, names[i].bytes());
    ui.accessFlag(row, ACCESS_SELECTED, i == 0);
    ui.label(names[i], Text.of());
    ui.close();
}
ui.close();
```

The rows are counted, so a reader says "Beta, 2 of 3".

### Virtual lists

A virtual list draws only the rows in view, so counting them gives the wrong numbers. Say the numbers yourself:

| Call | What it says |
|---|---|
| `ui.accessSize(node, rows, columns)` | A container has `rows` items or rows and `columns` columns, the ones not drawn too |
| `ui.accessPlace(node, row, column)` | An element's place in its container, from 1: an item's `row`, a cell's `row` and `column` |

A 0 leaves a number to be counted. `Controls.virtualList` and `Controls.dataGrid` make these calls for their rows
and cells: see [Lists, grids and charts](./lists-grids-charts).

```ceesafe frame
ui.open(Style.column().id("log"));
let list = ui.access(hashId("log"), AccessRole.List, "Log".bytes());
ui.accessSize(list, 100000, 0);
ui.open(Style.box().id("log-row"));
let row = ui.access(hashId("log-row"), AccessRole.ListItem, "Started".bytes());
ui.accessPlace(row, 41, 0);
ui.label("Started", Text.of());
ui.close();
ui.close();
```

`ui.accessContainer()` is the id of the container open around the element being built, or 0 for none. A control
that is part of a container can name itself after it, as a grid's cell does after its row.

## A reader's actions

A screen reader may ask four things of an element (`AccessAction`). The host passes each on with `ui.accessAct`,
and the next frame's controls see it.

| `AccessAction` | What the frame sees |
|---|---|
| `Press` | A press of the element: `ui.pressedOn(id)` is true, and so are `ui.clicked` and `ui.clickedHash` |
| `Focus` | The element has the keyboard focus |
| `Increment` | `ui.accessStep(id)` is 1 more: one step up of a slider or a spin field |
| `Decrement` | `ui.accessStep(id)` is 1 less |

A control of your own that tests `ui.pressedOn` or `ui.clicked` takes a reader's press with no more code. One with a
range adds `ui.accessStep(id)` steps to its value:

```ceesafe
import claywind;

public fn levelMeter(ui: borrow mut Ui, level: borrow mut f32) {
    let id = hashId("level");
    let steps = ui.accessStep(id);
    if (steps > 0 && level < 10.0) {
        level += 1.0;
    }
    if (steps < 0 && level > 0.0) {
        level -= 1.0;
    }
    ui.open(Style.box().idHash(id).w(Size.fixed(200.0)).h(Size.fixed(16.0)).bg(SLATE_700));
    let node = ui.access(id, AccessRole.Slider, "Level".bytes());
    ui.accessRange(node, 0.0, 10.0, level);
    ui.close();
}
```

An action is input: the frame after it runs at once, so the list the reader is given says what the action did.

## Keyboard access

A screen reader's user works an app with the keys, and so do many others.

- Tab and Shift-Tab move the focus through the frame's controls. A control of your own joins them with
  `ui.registerFocusable(id)`.
- Enter or Space presses the focused button, checkbox or radio (`ui.activatedByKey(id)`).
- Up and Down move a list's selection. In a virtual list and a data grid the page keys, Home and End move it too.
- Left and Right open the tab before or after. The open tab is the Tab stop.
- The keys reach menus and the menu bar.

[Input](./input) has the keys and the focus in full.

## What each system's reader gets

The host turns the list into what the system asks for. The demo is read and worked by all five readers of this
table, with CeeSafe 0.94.3.

| System | Reader | Through | Known limits |
|---|---|---|---|
| macOS | VoiceOver | NSAccessibility | Only those common to every system, below |
| iOS | VoiceOver | UIAccessibility | A list's row has no place ("3 of 40"): iOS has none to give. A grid's row is read whole |
| Windows | NVDA | UI Automation | NVDA's table keys do not yet step from a grid's row into its cells |
| Linux | Orca | AT-SPI | Orca's table keys do not yet step from a grid's row into its cells |
| Android | TalkBack | Android's accessibility nodes | No focus rectangle: the window's surface is the app's. A grid's row is read whole |
| The web | The browser's | ARIA elements over the canvas | See [The web](./web#screen-readers) |

On every system:

- A reader hears a container and the place in it: "list, 6 items", "tab, 2 of 5", a table of 10,001 rows and 4
  columns with its column headers.
- A data grid's rows hold a cell for each column. UI Automation, AT-SPI, AppKit and a web page have each cell at its
  row and column under the column's title. A reader's focus is the row, which says its name.
- Dragging is not there, and a reader cannot set a field's text itself.
- Reading by character in a field works on macOS, and on the web, where a field is a text input of the page. It is
  not there on the other systems.
