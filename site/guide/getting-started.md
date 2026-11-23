# Getting started

This page takes you from nothing to a window of your own: install the compiler, make an app from CeeUI's
template, run it, and change it.

## Install the compiler

CeeUI is written in [CeeSafe](https://ceesafe.github.io/CeeSafe/), and its compiler, `ceesafec`, is all you
install. It uses the system's C compiler (clang or gcc; MSVC or MinGW's gcc on Windows).

```sh
curl -fsSL https://ceesafe.github.io/CeeSafe/install.sh | sh          # macOS and Linux
```

```powershell
irm https://ceesafe.github.io/CeeSafe/install.ps1 | iex               # Windows
```

## Make an app

`ceeui-pack` makes a new app from a template. Build it once from a checkout of CeeUI:

```sh
git clone https://github.com/CeeSafe/CeeUI
cd CeeUI/ceeui-pack && ceesafec build && cd ../..
CeeUI/ceeui-pack/out/ceeui_pack new tasks --ceeui CeeUI
cd tasks
ceesafec build
out/tasks
```

A window opens with a list of tasks: type one and press Enter, tick one off, select one and press Delete. Without
`--ceeui PATH` the app takes CeeUI's packages from GitHub instead of the checkout (`ceesafec fetch` gets them).

The app is two files:

| File | What it is |
|---|---|
| `src/app.cee` | The app: its state, and the frame that lays the window out from it |
| `src/main.cee` | The window: it opens one, sets memory aside, and runs the frames |

## The frame

CeeUI is immediate mode. There are no widget objects to create and keep in step with your data: a function, the
frame, runs whenever something changed and declares the whole window from the app's state. A control is a call, and
what the user did with it is the call's result.

Here is a whole app's frame: a count, and a button that adds to it.

```ceesafe
import ceeui;

public struct Counter {
    public count: u64;
}

public fn counterFrame(ui: borrow mut Ui, app: borrow mut Counter) {
    ui.open(Style.column().w(Size.grow()).h(Size.grow()).p(space(6.0)).gap(space(4.0)).bg(SLATE_950));
    var text = [0u8; 32];
    var w = TextWriter.create(mut text[..]);
    w.text("Count: ");
    w.decimal(app.count);
    ui.labelBytes(w.written(), Text.of().size(24.0).color(SLATE_100));
    if (Controls.button(mut ui, "add", "Add one")) {
        app.count += 1;
    }
    ui.close();
}
```

- `ui.open(style)` begins an element and `ui.close()` ends it; what is declared between them is inside it. This one
  is a column that fills the window.
- `Style` is built Tailwind-fashion: `p` is padding, `gap` the space between children, `bg` the background;
  `space(6.0)` is six steps of Tailwind's spacing scale (24 points).
- `ui.labelBytes` shows text. The number is written into an array on the stack: a frame allocates nothing.
- `Controls.button` draws a button and is true in the frame it was pressed. Its first name, `"add"`, is its id:
  what CeeUI keeps between frames (the focus, a hover's transition) is kept by id.

## Change the template

Open `src/app.cee`. The frame is `appFrame`; its header row holds the title, the count of tasks left and the theme
button. Add a button beside the theme's that removes the finished tasks:

```ceesafe skip
    if (Controls.button(mut ui, "clear", "Clear done")) {
        var i: usize = 0;
        while (i < app.tasks.length) {
            if (app.tasks[i].done) {
                app.tasks.remove(i);
            } else {
                i += 1;
            }
        }
        app.selected = null;
    }
```

Build and run again, or let CeeUI do it each time you save: [hot reload](./tools#hot-reload) rebuilds the app when
a source changes and restarts it where it was.

## The window

`src/main.cee` is the host: the part that owns the window. The template's is short because `ceeui-desktop`'s
`Pane` holds a window's `Ui`, its input and its damage tracking:

```ceesafe skip
    loop {
        if (pane.takeEvents(mut window)) {
            break; // the window was closed
        }
        pane.begin(mut window);
        appFrame(mut pane.ui, mut app, raster);
        if (!pane.end(mut window, raster)) {
            return 1; // the frame does not fit the UI's capacity
        }
        if (!pane.draw(mut raster, mut share, mut window, Color.hex(SLATE_950))) {
            return 1;
        }
        pane.platform.endFrame();
        if (pane.ui.needsFrame()) {
            window.waitFrame();
        } else {
            Window.waitAny(pane.ui.frameDelayMs(IDLE_MS));
        }
    }
```

Each turn takes the window's events, runs the frame, lays it out, draws what changed, and then sleeps until there is
something to do. An idle window uses no CPU. [A frame](./frame) says what each step is, and
[A desktop host](./desktop) goes through the whole file.

## Next

- [A frame](./frame): how a frame runs, and when.
- [Layout](./layout) and [Styles](./styles): rows, columns, sizes and the Tailwind-style builder.
- [Controls](./controls): what there is to put in a window.
- [Packing](./tools#packing): the same app as a macOS, iOS or Android app, or a web page.
