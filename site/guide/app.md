# State, actions and menus

This page covers the parts of an app around its frame: where state lives, named actions and their key bindings,
menus, file dialogs, background work, several windows, and saving and restoring state.

## State is a struct

ClayWind keeps no copy of your data. The app's state is a struct of your own, and the frame reads it to declare
the window and writes to it when a control reports a change.

```ceesafe
import claywind;

public struct Settings {
    public sound: bool;
    public volume: f32;
    public saves: u32;
}

public fn settingsFrame(ui: borrow mut Ui, app: borrow mut Settings) {
    ui.open(Style.column().p(space(4.0)).gap(space(3.0)));
    let toggled = Controls.checkbox(mut ui, "sound", "Sound", mut app.sound);
    if (app.sound) {
        let moved = Controls.slider(mut ui, "volume", mut app.volume, 0.0, 1.0, 200.0);
    }
    if (Controls.button(mut ui, "save", "Save")) {
        app.saves += 1;
    }
    ui.close();
}
```

A control takes the value it edits as `mut`, changes it in place, and returns whether it changed. There is nothing
to subscribe to and nothing to keep in step: the next frame reads the struct again.

What ClayWind does keep between frames is small and is kept by element id: the focus, scroll offsets, and
transitions under way. [A frame](./frame) explains this.

## When a frame runs

A frame runs after input, while something animates, and when the app asks. Otherwise the window sleeps. State that
changes without input needs to ask:

| Call | What it asks for |
|---|---|
| `ui.requestFrame()` | Another frame after this one |
| `ui.requestFrameAfter(seconds)` | A frame that many seconds from now, unless one comes sooner |

```ceesafe
import claywind;

public struct Clock {
    public shown: f64;
}

public fn clockFrame(ui: borrow mut Ui, app: borrow mut Clock) {
    app.shown = ui.time();
    var text = [0u8; 32];
    var w = TextWriter.create(mut text[..]);
    w.decimal(saturatingTruncate<u64>(app.shown));
    w.text(" s");
    ui.labelBytes(w.written(), Text.of().color(SLATE_100));
    ui.requestFrameAfter(1.0);
}
```

A frame forgets the requests of the frame before, so a frame that still needs a timer asks again.

## Model

`Model<T>` wraps a piece of state. Changing it through `set` or `update` asks the `Ui` for the frame that shows the
change, so code outside a control's result does not need to call `requestFrame` itself.

```ceesafe
import claywind;

public struct Inbox {
    public unread: u32;
}

public fn inboxFrame(ui: borrow mut Ui, inbox: borrow mut Model<Inbox>, arrived: bool) {
    if (arrived) {
        inbox.update(mut ui, (i) => { i.unread += 1; });
    }
    var text = [0u8; 32];
    var w = TextWriter.create(mut text[..]);
    w.decimal(inbox.get().unread as u64);
    w.text(" unread");
    ui.labelBytes(w.written(), Text.of().color(SLATE_100));
}
```

| Call | What it does |
|---|---|
| `Model<T>.of(value)` | A model holding `value` |
| `get()` | The value, to read |
| `set(mut ui, value)` | Replaces the value and asks for a frame |
| `update(mut ui, change)` | Changes the value in place with a function, and asks for a frame |
| `version()` | How many times it changed |

`version()` lets a view skip work: keep the version you last computed from, and redo the work only when it differs.

```ceesafe
import claywind;

public struct Report {
    public rows: Model<u32>;
    /// The version `total` was computed from.
    public seen: u64;
    public total: u64;
}

public fn reportTotal(report: borrow mut Report) -> u64 {
    if (report.seen != report.rows.version()) {
        report.seen = report.rows.version();
        let rows: u32 = report.rows.get();
        report.total = rows as u64 * 12;
    }
    return report.total;
}
```

A model is optional. A plain struct field works the same when the change comes from a control, since input already
runs the next frame.

## Actions and key bindings

An action is a name. A key binding ties a keystroke to an action, and a frame asks whether the action was triggered
this frame.

```ceesafe
import claywind;

public struct Editor {
    public bound: bool;
    public palette: bool;
    public saves: u32;
}

public fn editorFrame(ui: borrow mut Ui, app: borrow mut Editor) {
    if (!app.bound) {
        let a = ui.bind("secondary-shift-p", "palette", "");
        let b = ui.bind("secondary-s", "save", "");
        app.bound = true;
    }
    if (ui.action("palette")) {
        app.palette = !app.palette;
    }
    if (ui.action("save")) {
        app.saves += 1;
    }
}
```

Bind once, on the first frame. `ui.bind(keystroke, action, context)` is false when the keystroke does not parse or
the bindings are full. A later binding of the same keystroke in the same context replaces the earlier one.

`ui.action(name)` is true whatever triggered the action: its keystroke, a menu item, or `ui.dispatch`. Code that
handles an action is written once.

### Keystrokes

A keystroke is modifiers and a key, joined with `-`.

| Part | Meaning |
|---|---|
| `secondary` | Command on macOS, Control elsewhere |
| `cmd` or `command` | Command |
| `ctrl` or `control` | Control |
| `alt` or `option` | Alt, or Option on macOS |
| `shift` | Shift |
| A character | The key that types it without modifiers: `s`, `1`, `/` |
| A named key | `escape`, `enter`, `tab`, `backspace`, `delete`, `left`, `right`, `up`, `down`, `home`, `end`, `pageup`, `pagedown`, `f1` to `f12` |

Examples: `"secondary-shift-p"`, `"ctrl-alt-left"`, `"f5"`, `"escape"`.

The host tells the `Ui` which key `secondary` is with `ui.setSecondaryIsCommand`; see
[A desktop host](./desktop).

### Built-in actions

These are bound by default, and a focused text field or editor acts on them:

| Action | Default binding |
|---|---|
| `copy` | `secondary-c` |
| `cut` | `secondary-x` |
| `paste` | `secondary-v` |
| `select-all` | `secondary-a` |
| `undo` | `secondary-z` |
| `redo` | `secondary-shift-z`, and `ctrl-y` |
| `delete` | None |

One more default, `secondary-alt-i`, is bound to `toggle-inspector`.

An app may bind them to other keys. A menu item or a `dispatch` that names one of them works on the focused field
too. [Editing](./editing) covers the text controls.

### Key contexts

A binding with a context applies only while the focused element is inside an element that names that context.
`ui.keyContext(name)` names the element that was opened last.

```ceesafe
import claywind;

public struct Workspace {
    public bound: bool;
    public runs: u32;
    public refreshes: u32;
}

public fn workspaceFrame(ui: borrow mut Ui, app: borrow mut Workspace) {
    if (!app.bound) {
        let a = ui.bind("f5", "refresh", "");
        let b = ui.bind("f5", "run", "console");
        app.bound = true;
    }
    ui.open(Style.column().id("console").p(space(3.0)));
    ui.keyContext("console");
    if (Controls.button(mut ui, "clear", "Clear")) {
        app.runs = 0;
    }
    ui.close();
    if (ui.action("run")) {
        app.runs += 1;
    }
    if (ui.action("refresh")) {
        app.refreshes += 1;
    }
}
```

With the focus on the Clear button, F5 triggers `run`. Elsewhere it triggers `refresh`. The innermost context
around the focused element wins, then the bindings made everywhere. Contexts are read from the frame before, so a
binding follows the focus one frame later.

### Triggering an action from code

`ui.dispatch(name)` triggers an action on the next frame, as its key binding would, and asks for that frame. Use it
for a toolbar button or a command palette that runs the same action as a shortcut.

```ceesafe frame
if (Controls.button(mut ui, "copyButton", "Copy")) {
    ui.dispatch("copy");
}
```

Up to 8 actions can be dispatched in a frame.

### Showing a shortcut

`ui.bindingFor(name)` gives the keystroke bound to an action everywhere, or null. Bindings in a key context are
not returned. The result has `secondary` resolved to Command or Control for this platform.

```ceesafe frame
match (ui.bindingFor("save")) {
    Some(b) => {
        var text = [0u8; 32];
        var w = TextWriter.create(mut text[..]);
        if (b.modifiers.command) {
            w.text("Cmd+");
        }
        if (b.modifiers.control) {
            w.text("Ctrl+");
        }
        if (b.modifiers.shift) {
            w.text("Shift+");
        }
        var utf8 = [0u8; 4];
        let n = encodeUtf8(b.character, mut utf8);
        w.bytes(utf8[0..n]);
        ui.labelBytes(w.written(), Text.of().color(SLATE_400));
    }
    null => {}
}
```

A `KeyBinding` has `key` (a `Key`, or null), `character` (lowercase, when there is no named key) and `modifiers`
(`shift`, `control`, `alt`, `command`). `ui.usesCommand()` says whether `secondary` is Command.

## Menus

Menus are made of actions. An app describes them once, to a `MenuBuilder`: `menu` starts a menu, and the items and
separators after it belong to that menu.

```ceesafe
import claywind;

public fn appMenu<B: MenuBuilder>(m: borrow mut B) {
    m.menu("File");
    m.item("Open…", "open-file");
    m.item("Save", "save");
    m.separator();
    m.item("Quit", "quit");
    m.menu("Edit");
    m.item("Cut", "cut");
    m.item("Copy", "copy");
    m.item("Paste", "paste");
}
```

An item names an action. Choosing it triggers the action, so `ui.action("save")` is true for the menu and for the
shortcut alike. The item shows the action's key binding as its shortcut; there is no second place to declare it.

The function is generic so the same description can feed any builder. `MenuModel` is the builder ClayWind
provides: it keeps the description, for a host to turn into the system's menus or for ClayWind to draw.

```ceesafe
import claywind;

public fn describe<B: MenuBuilder>(m: borrow mut B) {
    m.menu("View");
    m.item("Zoom In", "zoom-in");
    m.item("Zoom Out", "zoom-out");
}

public fn countItems() -> usize {
    var model = MenuModel.create();
    describe(mut model);
    var items: usize = 0;
    for (k in 0..model.partCount()) {
        let isItem = match (model.partKind(k)) {
            MenuPartKind.Item => true,
            _ => false,
        };
        if (isItem) {
            items += 1;
        }
    }
    return items;
}
```

A `MenuModel` holds up to 96 parts and 2,048 bytes of titles. What does not fit is left out.

### The system's menus

On macOS the menus go in the menu bar, and on Windows in the window's menu. `claywind-desktop` builds them from the
model, and passes the chosen items back as actions. This is the demo's desktop host:

```ceesafe skip
    var menuModel = MenuModel.create();
    demoMenu(mut menuModel);
    var menuSet = false;
```

After the first frame, which binds the actions' keys, it gives the menus to the window:

```ceesafe skip
        if (!menuSet) {
            menuSet = true;
            let shown = match (systemMenu(menuModel, main.ui)) {
                Ok(menu) => window.setMenu(menu).isOk(),
                Err(_) => false,
            };
            if (!shown) {
                app.setDrawsMenuBar(true); // no menu bar from the system: the demo draws its own
                main.ui.requestFrame();
            }
        }
```

Each turn of the loop, before the frame, it takes what was chosen:

```ceesafe skip
        takeMenuChoices(mut window, menuModel, mut main.platform);
```

`systemMenu` reads each item's shortcut with `ui.bindingForHash`, which is why the menus are set after the bindings
exist.

### A menu bar ClayWind draws

Where the system has no menu bar (X11, for one), `window.setMenu` fails and the app draws the same menus in its
window with `Controls.menuBar`.

```ceesafe
import claywind;

public fn notesMenu<B: MenuBuilder>(m: borrow mut B) {
    m.menu("File");
    m.item("New Note", "new-note");
    m.menu("Edit");
    m.item("Copy", "copy");
    m.item("Paste", "paste");
}

public struct Notes {
    public menus: MenuModel;
    /// Which menu is open; 0 for none. Controls.menuBar keeps it here between frames.
    public menuOpen: usize;
    public count: u32;
}

public fn notesFrame(ui: borrow mut Ui, app: borrow mut Notes) {
    if (app.menus.isEmpty()) {
        notesMenu(mut app.menus);
        let bound = ui.bind("secondary-n", "new-note", "");
    }
    ui.open(Style.column().id("window").w(Size.grow()).h(Size.grow()).bg(SLATE_950));
    Controls.menuBar(mut ui, "menubar", app.menus, mut app.menuOpen);
    if (ui.action("new-note")) {
        app.count += 1;
    }
    ui.close();
}
```

A press on a title opens its menu. Each item shows its action's key binding. Choosing one dispatches its action for
the next frame. A press outside, or Escape, closes the menu.

### Context menus

`Controls.contextMenu` shows a menu at a point while its `open` flag is true. The app opens it, usually on a
secondary press, and gets the chosen item's index.

```ceesafe
import claywind;

public struct Files {
    public menuOpen: bool;
    public menuX: f32;
    public menuY: f32;
    public renames: u32;
    public deletes: u32;
}

public fn filesFrame(ui: borrow mut Ui, app: borrow mut Files) {
    ui.open(Style.box().id("file").p(space(3.0)).bg(SLATE_800).rounded(Radius.Md));
    ui.label("report.txt", Text.of().color(SLATE_100));
    ui.close();
    if (ui.secondaryPressedOn(hashId("file"))) {
        let p = ui.pointer();
        app.menuOpen = true;
        app.menuX = p.position.x;
        app.menuY = p.position.y;
    }
    let items = ["Rename", "Delete"];
    match (Controls.contextMenu(mut ui, "fileMenu", items[..], mut app.menuOpen, app.menuX, app.menuY)) {
        Some(i) => {
            if (i == 0) {
                app.renames += 1;
            } else {
                app.deletes += 1;
            }
        }
        null => {}
    }
}
```

Declare the menu last in the frame, so it floats over everything else. A choice, or a press outside, sets `open`
to false. On a touch screen a long press is a secondary press, so the same code opens the menu there.

| Control | What it adds |
|---|---|
| `Controls.contextMenuWith` | A slice of `bool` saying which items are enabled |
| `Controls.editMenu` | Cut, Copy, Paste, Delete and Select All for a text field or editor; a choice dispatches the built-in action |

## File dialogs

A file dialog blocks until the user answers, so it is not shown inside a frame. The frame records that a file is
wanted, and the host shows the dialog after the frame.

In the frame, the demo sets a field when its File menu's action is triggered:

```ceesafe skip
    if (app.opensFiles && ui.action("open-file")) {
        app.fileWanted = FileRequest.Open;
    }
```

After the frame, the host takes the request and answers it:

```ceesafe skip
        match (app.takeFileRequest()) {
            Some(request) => {
                answerFileRequest(request, mut app, mut window);
                main.ui.requestFrame(); // the next frame shows how it went
            }
            null => {}
        }
```

`FileDialog` is CeeSafe's, from its standard library. The demo's `answerFileRequest` opens a file with it like
this:

```ceesafe skip
            match (FileDialog.openFile(mut window, "Open", "")) {
                Ok(Some(path)) => {
                    match (File.open(path.view())) {
                        Ok(f) => {
                            match (f.length()) {
                                Ok(n) => { app.fileOpened(path.view().bytes(), n); }
                                Err(_) => { app.fileNotDone("Could not read the file"); }
                            }
                        }
                        Err(_) => { app.fileNotDone("Could not open the file"); }
                    }
                }
                Ok(null) => { app.fileNotDone("Open cancelled"); }
                Err(_) => { app.fileNotDone("No file dialog on this system"); }
            }
```

| Call the demo uses | Result |
|---|---|
| `FileDialog.openFile(mut window, "Open", "")` | A path, null when cancelled, or an error where there is no dialog |
| `FileDialog.saveFile(mut window, "Save As", "notes.txt", "txt")` | The same, for a file to write |
| `FileDialog.exportFile(mut window, "Save As", draft.view(), "notes.txt")` | Whether a file the app wrote was saved where the user chose |

On a phone `saveFile` is `Err(WindowError.Unsupported)`, because the system gives no path to write to. The demo then
writes the file to a temporary folder and calls `exportFile`. See [Phones](./phones).

## Background work

Work that takes long runs on a thread of its own, so frames keep running. The `claywind-tasks` package has `Task<T>`
for this. A job sends values of type `T` to the UI thread. Each one wakes the window, and the frame takes them with
`poll`.

```toml
[dependencies]
claywind_tasks = { path = "../claywind-tasks" }
```

Create a `Task` with the window's waker. The first argument is how many values a job may send ahead of the UI:

```ceesafe skip
import claywind_tasks;

var primes = Task<PrimeCount>.create(8, window.waker())!;
```

A job is a boxed function that takes a `TaskSender`. This is the demo's, which counts primes and reports every
100,000 numbers:

```ceesafe skip
fn countJob(below: u64) -> Result<Box<dyn mut fn(borrow TaskSender<PrimeCount>) + Send>, AllocError> {
    return Ok(Box.create(move (out: borrow TaskSender<PrimeCount>) => {
        var found: u64 = 0;
        var at: u64 = 0;
        while (at < below) {
            let to = (at + STEP).min(below);
            found += primesIn(at, to);
            at = to;
            if (!out.send(PrimeCount { below: below, checked: at, found: found })) {
                return; // cancelled
            }
        }
    })?);
}
```

In the frame, take every value that has arrived:

```ceesafe skip
    loop {
        match (primes.poll(mut ui)) {
            Some(c) => { app.primes = c; }
            null => { break; }
        }
    }
```

| Call | What it does |
|---|---|
| `Task<T>.create(capacity, waker)` | A task whose jobs can send `capacity` values ahead of the UI |
| `start(job)` | Runs `job` on a new thread, and cancels the job before it |
| `cancel()` | Cancels the running job: it sends nothing more, and what it sent is dropped |
| `running()` | Whether the latest job is still running |
| `poll(mut ui)` | The next value the latest job sent, or null. A value asks for another frame |
| `out.send(value)` | In a job: sends a value and wakes the window. False once the job was cancelled |
| `out.cancelled()` | In a job: whether to stop |

A job is not stopped from outside. It must check `send`'s result or `cancelled()` often and return. Dropping the
`Task` cancels its jobs and waits for them, so no thread outlives it.

The UI sees only the latest job's values. Starting a search again as the user types needs no bookkeeping: the
earlier job's results never arrive.

## Several windows

Each window has its own `Ui` and its own `Damage`. The windows can share one `Raster`, with its fonts and caches.
The raster keeps each window's last frame in a `RasterFrame` while it draws another's, so every window is still
redrawn only where it changed.

With `claywind-desktop`, a `Pane` is one window's `Ui`, `Damage`, input and kept frame. The demo's host makes two,
each with its own id and memory:

```ceesafe skip
    var main = match (Pane.create(mainMemory, 1, 4096, 16384, full, window.size())) {
        Ok(p) => p,
        Err(_) => {
            Console.writeErrorLine("desktop: not enough memory for the UI");
            return 1;
        }
    };
    let small = WindowSize { width: INSPECTOR_WIDTH, height: INSPECTOR_HEIGHT };
    var inspector = match (Pane.create(inspectorMemory, 2, 512, 2048, full / 2, small)) {
        Ok(p) => p,
        Err(_) => {
            Console.writeErrorLine("desktop: not enough memory for the inspector");
            return 1;
        }
    };
```

Both windows show the same app state, so the host runs a frame function for each, one after the other, with the
same `app`. A `RasterShare` tells the panes how many windows draw with the raster (`share.setWindows(2)`). The host
then sleeps until either window needs a frame:

```ceesafe skip
        if (main.ui.needsFrame() || inspectorBusy) {
            window.waitFrame();
        } else {
            var delay = main.ui.frameDelayMs(IDLE_MS);
            if (second != null) {
                delay = delay.min(inspector.ui.frameDelayMs(IDLE_MS));
            }
            Window.waitAny(delay);
        }
```

The full code is in `claywind-hosts/desktop/src/main.cee`. [A desktop host](./desktop) explains a `Pane`.

## Saving and restoring state

A snapshot is the app's state as bytes. `SnapshotWriter` writes values into a buffer in an order, and
`SnapshotReader` reads them back in the same order.

```ceesafe
import claywind;

/// Change this when what `save` writes changes.
const STATE_VERSION: u32 = 1;

public struct Draft {
    public page: u32;
    public wrap: bool;
    public title: [u8; 64];
    public titleLength: usize;
}

public fn save(app: borrow Draft, buffer: SliceMut<u8>) -> usize {
    var w = SnapshotWriter.over(mut buffer, STATE_VERSION);
    w.u32(app.page);
    w.bool(app.wrap);
    w.bytes(app.title[0..app.titleLength]);
    return if (w.fits()) w.written().length else 0;
}

public fn restore(app: borrow mut Draft, saved: Slice<u8>) -> bool {
    var r = SnapshotReader.over(saved, STATE_VERSION);
    let page = r.u32();
    let wrap = r.bool();
    var title = [0u8; 64];
    let titleLength = r.bytesInto(mut title[..]);
    if (!r.intact()) {
        return false;
    }
    app.page = page;
    app.wrap = wrap;
    app.title = title;
    app.titleLength = titleLength;
    return true;
}
```

Both have `u8`, `bool`, `u32`, `u64`, `f32` and `f64`. The writer has `bytes`, and the reader `bytesInto`, which
gives how many bytes it kept.

| Check | Meaning |
|---|---|
| `w.fits()` | Everything written fitted the buffer |
| `r.intact()` | The header was this app's version, and no read ran past the end |

A reader that is not intact gives 0, false and empty for every value. Read into locals, check `intact()`, and only
then change the app, so a stale snapshot is never half applied. Clamp what you read to the ranges the app allows:
an index saved by an older build may be past the end now.

### Hot reload

Snapshots are what keeps an app where it was across a hot reload. `claywind-dev` starts the app with `--hot DIR`,
and `claywind-desktop` has the app's side:

| Function | What it does |
|---|---|
| `hotDirectory()` | The directory `--hot` named, or null |
| `hotReadState(dir)` | The bytes the previous build saved, or empty |
| `hotWriteState(dir, state)` | Saves bytes for the next build |
| `HotWatch.start(dir, waker)` | Watches for a new build on a thread, and wakes the window |
| `watch.reloadAsked()` | Whether a new build is waiting for this one to save and end |

The demo wraps the snapshot and these calls in two functions of its own, `hotSave` and `hotRestore`, in
`claywind-demo-tasks/src/hot.cee`. This is its `hotSave`:

```ceesafe skip
public fn hotSave(dir: StringView, size: WindowSize, dark: bool, app: borrow DemoApp) -> bool {
    var buffer = [0u8; 4096];
    var w = SnapshotWriter.over(mut buffer[..], HOT_STATE_VERSION);
    w.u32(size.width);
    w.u32(size.height);
    w.bool(dark);
    app.save(mut w);
    if (!w.fits()) {
        return false;
    }
    return hotWriteState(dir, w.written());
}
```

It saves the window's size and the theme before the app's own state. The host calls it when a new build asks to
take over, then ends:

```ceesafe skip
        if (reload) {
            match (hotDir) {
                Some(d) => {
                    if (!hotSave(d, window.size(), main.ui.isDarkMode(), app)) {
                        Console.writeErrorLine("desktop: could not save the state for the new build");
                    }
                }
                null => {}
            }
            return 0;
        }
```

The same snapshot serves a phone, where the system may end an app that is off the screen: the demo saves when it
leaves the screen and starts from what it saved. [Tools](./tools#hot-reload) covers `claywind-dev`, and
[Phones](./phones) the app's life on a phone.
