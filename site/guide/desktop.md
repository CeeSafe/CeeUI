# A desktop host

This page explains the host of a desktop app: the file that opens the window, sets memory aside, and runs the
frames. It goes through the `src/main.cee` that `claywind-pack new` makes, step by step, then covers the manifest,
Linux, the title bar, menus, file drops, the theme and the environment variables.

## What a host does

The `claywind` package has no window, reads no files and allocates nothing by itself. A host supplies those:

| The host gives | To |
|---|---|
| Memory, once, at startup | The `Ui`, the `Damage` and the `Raster` |
| Fonts, as bytes | The `Raster` |
| Input and a clock, each frame | The `Ui`, through a `Platform` |
| A place for the pixels | The window |

On a desktop the `claywind-desktop` package does most of this over CeeSafe's `Window`. Its `Pane` holds one
window's `Ui`, `Damage` and input. The same host also runs on iOS and Android; see [Phones](./phones).

The host's source needs three packages:

```ceesafe skip
import claywind;
import claywind_desktop;
import claywind_fonts;
```

## The manifest

`ceesafe.toml` names the package and its dependencies. This is what `claywind-pack new tasks --claywind Clay`
writes:

```toml
[package]
name = "tasks"
version = "0.1.0"
kind = "executable"
windowed = true # a GUI program: no console window on Windows
ceesafec = ">=0.78.0"

[dependencies]
claywind = { path = "Clay/claywind" }
claywind_fonts = { path = "Clay/claywind-fonts" }
claywind_desktop = { path = "Clay/claywind-desktop" }
```

| Key | Meaning |
|---|---|
| `kind = "executable"` | A program with a `main` |
| `windowed = true` | A GUI program: Windows opens no console window for it |
| `claywind` | The toolkit: layout, styles, controls, text, the software renderer |
| `claywind_desktop` | `Pane`, `DesktopPlatform`, the system's menus, file drops, the title bar, hot reload's side |
| `claywind_fonts` | `SystemFonts`: fallback fonts from the system for characters the loaded fonts lack |

Without `--claywind PATH` the dependencies come from GitHub, and `ceesafec fetch` gets them:

```toml
[dependencies]
claywind = { git = "https://github.com/CeeSafe/Clay.git", branch = "main", dir = "claywind" }
claywind_fonts = { git = "https://github.com/CeeSafe/Clay.git", branch = "main", dir = "claywind-fonts" }
claywind_desktop = { git = "https://github.com/CeeSafe/Clay.git", branch = "main", dir = "claywind-desktop" }
```

## The window

`main` creates the app's state, then opens a window with a title and a size in points. In the template, `__NAME__`
stands for the app's name.

```ceesafe skip
fn main() -> i32 {
    var app = App.create();
    var window = match (Window.open("__NAME__", 720, 560)) {
        Ok(w) => w,
        Err(e) => {
            Console.writeError("__NAME__: cannot open a window: ");
            Console.writeErrorLine(windowError(e));
            return 1;
        }
    };
```

`Window` is CeeSafe's, from its standard library. It is AppKit on macOS, Win32 on Windows, and Wayland or X11 on
Linux. `windowError` is `claywind-desktop`'s: it turns the reason a window did not open into words.

## Memory

All of ClayWind's memory is set aside here, once. A frame allocates nothing.

```ceesafe skip
    // The UI's and the renderer's memory, set aside once: a frame allocates nothing.
    var paneStorage = zeros(33554432);
    var rasterStorage = zeros(301989888);
    let paneMemory = FixedBufferAllocator.over(mut paneStorage.viewMut());
    let rasterMemory = FixedBufferAllocator.over(mut rasterStorage.viewMut());
```

`zeros(count)` gives that many zero bytes. The pane gets 32 MiB and the raster 288 MiB. Each
`FixedBufferAllocator` hands that memory out to what is created from it.

The raster's share is large because it is sized for the largest frame, 2880 by 1800 pixels, with its layers and
masks, and for about 96 MB of fonts, since a CJK fallback font is tens of megabytes. A host that also runs on a phone sizes it for the window instead, with `rasterCapacityFor(window)` and
`capacity.bytes()`, as the demo's host does.

## The pane and its capacities

```ceesafe skip
    var pane = match (Pane.create(paneMemory, 1, 4096, 16384, MAX_PIXELS_WIDTH * MAX_PIXELS_HEIGHT, window.size())) {
        Ok(p) => p,
        Err(_) => {
            Console.writeErrorLine("__NAME__: not enough memory for the UI");
            return 1;
        }
    };
```

`Pane.create` takes the memory and five more arguments:

| Argument | Here | Meaning |
|---|---|---|
| `id` | `1` | The pane's number. Each window has its own, from 1 |
| `elements` | `4096` | The most elements a frame may declare |
| `commands` | `16384` | The most render commands a frame may produce, for the `Damage` |
| `pixels` | 2880 × 1800 | The largest frame the pane keeps a copy of, when several windows share a raster |
| `size` | `window.size()` | The window's size in points |

The capacities are limits, not costs per frame. A frame that declares more elements than fit is not laid out, and
`pane.end` reports it. Raise `elements` if that happens.

A pane has three public parts that the host uses:

| Field | What it is |
|---|---|
| `pane.ui` | The `Ui` the frame function gets |
| `pane.damage` | The `Damage` that finds what changed between frames |
| `pane.platform` | The `DesktopPlatform`: this frame's input, as the `Ui` reads it |

## The raster

```ceesafe skip
    var raster = match (Raster.create(rasterMemory, desktopRasterCapacity())) {
        Ok(r) => r,
        Err(_) => {
            Console.writeErrorLine("__NAME__: not enough memory for the renderer");
            return 1;
        }
    };
```

The `Raster` is the software renderer. It also holds the fonts and measures text, which is why the frame function
and `pane.end` take it. `desktopRasterCapacity()` is a `RasterCapacity` for frames up to `MAX_PIXELS_WIDTH` by
`MAX_PIXELS_HEIGHT`, with room for the system's fallback fonts. [Drawing](./drawing) covers the raster's images
and the pixel ratio.

## Fonts

```ceesafe skip
    if (!loadSystemFonts(mut raster)) {
        Console.writeErrorLine("__NAME__: no fonts found; set CLAYWIND_FONT_DIR");
        return 1;
    }
    // Fonts for what the loaded ones lack (other scripts, emoji), found among the system's when a frame needs them.
    var systemFonts = SystemFonts.create();
```

`loadSystemFonts` loads three faces into the raster: a regular one as `FONT_18`, a bold one as `FONT_BOLD`, and a
monospaced one as `FONT_MONO`. It takes them from the first of these places that has them:

| Place | Faces |
|---|---|
| `CLAYWIND_FONT_DIR`, when set | `Arial.ttf`, `Arial Bold.ttf`, `Courier New.ttf` |
| macOS | Arial, Arial Bold, Courier New |
| Windows | Arial, Arial Bold, Courier New |
| Linux with the Microsoft core fonts | Arial, Arial Bold, Courier New |
| Linux with DejaVu | DejaVu Sans, Sans Bold, Sans Mono |
| Android | Droid Sans, Sans Bold, Sans Mono |
| Fedora and others | Liberation Sans, Sans Bold, Mono |
| iOS | Helvetica Neue, its bold, Courier New |

When `CLAYWIND_FONT_DIR` is set, only that directory is tried.

To use fonts of your own, read the files and call `raster.loadFont(id, bytes)` instead. [Text](./text) covers fonts
and font ids.

`SystemFonts` supplies fallbacks. It finds, among the system's fonts, faces for the characters that the loaded
fonts lack: other scripts, and emoji. It scans the system's font folders the first time a character is missing,
so text the loaded fonts cover never causes a scan.

## Before the loop

```ceesafe skip
    var share = RasterShare.create();
    pane.ui.setDarkMode(true);
    pane.ui.setSecondaryIsCommand(match (window.nativeHandle()) {
        WindowHandle.AppKit(_, _) => true,
        _ => false,
    });
```

- `RasterShare` records which pane's frame the raster holds. With one window it needs no more attention.
- `setDarkMode(true)` starts the app in the dark theme.
- `setSecondaryIsCommand` says which key `secondary` means in key bindings: Command on macOS (an AppKit window),
  Control elsewhere. See [State, actions and menus](./app#actions-and-key-bindings).

## The loop

Each turn of the loop is one frame. This is the whole of it:

```ceesafe skip
    loop {
        if (pane.takeEvents(mut window)) {
            break; // the window was closed
        }
        pane.begin(mut window);
        appFrame(mut pane.ui, mut app, raster);
        if (!pane.end(mut window, raster)) {
            Console.writeErrorLine("__NAME__: the frame does not fit the UI's capacity");
            return 1;
        }
        if (systemFonts.supplyFrame(mut raster, pane.ui) > 0) {
            // The same commands draw other glyphs now; the next frame lays them out with the fonts' measures.
            pane.damage.invalidate();
            pane.ui.requestFrame();
        }
        if (!pane.draw(
            mut raster,
            mut share,
            mut window,
            Color.hex(if (pane.ui.isDarkMode()) SLATE_950 else SLATE_50),
        )) {
            Console.writeErrorLine("__NAME__: the window refused a frame");
            return 1;
        }
        pane.platform.endFrame();
        if (pane.ui.needsFrame()) {
            window.waitFrame();
        } else {
            Window.waitAny(pane.ui.frameDelayMs(IDLE_MS));
        }
    }
    return 0;
}
```

| Step | What it does |
|---|---|
| `pane.takeEvents(mut window)` | Takes the window's events into the platform: pointer, keys, typed text, touches, the safe area. True when the window asks to close |
| `pane.begin(mut window)` | Starts the frame: measures the time since the last one, and gives the `Ui` the input |
| `appFrame(mut pane.ui, mut app, raster)` | Your frame: declares the window from the app's state |
| `pane.end(mut window, raster)` | Lays the frame out, with text measured by the raster. Sets the cursor, and tells the window where the text input is and what a screen reader may read. False when the frame does not fit the capacity |
| `systemFonts.supplyFrame(mut raster, pane.ui)` | Loads fallback fonts for characters in this frame that no loaded font has. It gives how many faces it loaded |
| `pane.draw(mut raster, mut share, mut window, clear)` | Draws what changed into the raster over the `clear` color, and shows those boxes in the window. False when the window refused the frame |
| `pane.platform.endFrame()` | Clears the input that lasts one frame: presses, releases, typed characters |
| `window.waitFrame()` or `Window.waitAny(ms)` | Waits for the next frame |

### After new fonts

When `supplyFrame` loads a face, the frame just laid out was measured without it. Two calls put that right:

- `pane.damage.invalidate()` makes the next frame draw whole, because the same commands now draw other glyphs.
- `pane.ui.requestFrame()` runs the next frame at once, so the text is laid out with the new font's measures.

### Drawing

`pane.draw` draws at the window's pixel ratio, so a Retina display gets one pixel per pixel. A window too large for
`MAX_PIXELS_WIDTH` by `MAX_PIXELS_HEIGHT` at its ratio is drawn at a lower one, which the window scales up.

Only what changed is redrawn, and the window is given only those boxes. When nothing changed, nothing is drawn or
shown. The clear color is what shows where no element has a background; the template uses the page's color.

### Waiting

```ceesafe skip
        if (pane.ui.needsFrame()) {
            window.waitFrame();
        } else {
            Window.waitAny(pane.ui.frameDelayMs(IDLE_MS));
        }
```

| State | Wait |
|---|---|
| `needsFrame()` is true: input arrived, something animates, or the app asked | `window.waitFrame()`: the next frame at the display's pace |
| Otherwise | `Window.waitAny(ms)`: sleep until a window has an event or is woken, or `ms` pass |

`frameDelayMs(idle)` gives the time to the next thing the frame itself needs: a caret's blink, or a timer the frame
asked for with `ui.requestFrameAfter`. With neither it gives `idle`. `IDLE_MS` is the longest wait there is, so an
idle window uses no CPU until an event arrives. A background job's result wakes it; see
[State, actions and menus](./app#background-work).

## Linux: Wayland and X11

The desktop host runs on Wayland and on X11, through CeeSafe's `Window`. The host's code is the same for both, and
the same as on macOS and Windows. One thing can differ on Linux:

| Part | What happens |
|---|---|
| Menus | Where the system gives no menu bar (X11), the app draws its menus in the window with `Controls.menuBar` |

The title bar ClayWind draws, the next section, is the same on every desktop system.

## The title bar

A window opened with `WindowStyle.ProgramTitleBar` has no title bar of the system's, where the system allows that.
ClayWind then draws one: the title, and minimize, maximize and close buttons, in the theme. The demo's host opens
its window this way:

```ceesafe skip
    var window = match (Window.openStyled(
        "ClayWind — CeeSafe",
        startWidth,
        startHeight,
        WindowSurface.Frames,
        WindowStyle.ProgramTitleBar,
    )) {
```

`WindowBar`, in `claywind-desktop`, draws the bar after the app's frame and acts on its buttons after the layout:

```ceesafe skip
        main.begin(mut window);
        demoFrameWith(mut main.ui, mut app, raster, mut jobs);
        let barPress = WindowBar.draw(mut main.ui, window, "ClayWind — CeeSafe");
        if (!main.end(mut window, raster)) {
            Console.writeErrorLine("desktop: the frame does not fit the UI capacity");
            return 1;
        }
        if (WindowBar.act(mut window, main.ui, barPress)) {
            return 0; // its close button
        }
```

| Call | What it does |
|---|---|
| `WindowBar.draw(mut ui, window, title)` | Draws the bar when the window has the program's title bar. Otherwise it draws nothing |
| `WindowBar.act(mut window, ui, press)` | Tells the window where the bar and its buttons are, minimizes or maximizes on a press, and is true when close was pressed |

Both calls do nothing on a window with the system's title bar, so a host can make them on every system. The window
is told where the bar is so that the system moves the window by it and opens the window's menu there. Where the
system keeps buttons of its own in the bar, as macOS does, the bar leaves them clear and draws none.

The bar is 36 points high (`TITLE_BAR_HEIGHT`) and is fixed over the content. The `Pane` adds its height to the
top of the safe area, so a frame that keeps to the safe area stays below it:

```ceesafe frame
ui.open(Style.column().id("page").w(Size.grow()).h(Size.grow()).bg(SLATE_950).safe(ui.safeArea()));
ui.label("Below the title bar", Text.of().color(SLATE_100));
ui.close();
```

The template's frame does this already. The same call keeps the content clear of a phone's status bar.

`WindowBar` uses `TitleBar`, which is in `claywind` and knows nothing of windows. A host of another kind can draw
the bar itself:

```ceesafe frame
let press = TitleBar.draw(mut ui, "My app", false);
if (press.close) {
    ui.requestFrame();
}
```

`TitleBar.draw` gives a `TitleBarPress` with `minimize`, `maximize` and `close`. `TitleBar.layout(ui)` says where
the bar and its buttons were laid out.

## The system's menus

An app describes its menus once, in a `MenuModel`. `systemMenu` turns the model into the system's menus, and
`takeMenuChoices` passes chosen items to the next frame as actions.

Before the loop:

```ceesafe skip
    var menuModel = MenuModel.create();
    demoMenu(mut menuModel);
    var menuSet = false;
```

In the loop, before the frame:

```ceesafe skip
        takeMenuChoices(mut window, menuModel, mut main.platform);
```

After the first frame has drawn:

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

The menus are set after the first frame because that frame binds the actions' keys, and each item's shortcut is
its action's key binding. When `setMenu` fails, the app draws its own bar with `Controls.menuBar`.
[State, actions and menus](./app#menus) covers describing menus and handling their actions.

## File drops

`Drops`, in `claywind-desktop`, passes files dragged from the system on to the frame, and keeps the paths of the
last drop.

Before the loop:

```ceesafe skip
    var drops = Drops.create();
```

In the loop, before the frame:

```ceesafe skip
        drops.poll(mut window, mut main.platform);
```

The frame sees a drag of kind `DRAG_FILES`, and a drop target takes it; see
[Input](./input#files-from-the-system). The drop's payload value is a number. After the frame, the host asks
`Drops` for that drop's paths and hands them to the app:

```ceesafe skip
        match (app.takeFileDrop()) {
            Some(n) => {
                match (drops.pathsOf(n)) {
                    Some(paths) => {
                        app.filesDropped(truncate<u32>(paths.length));
                        for (p in paths.view()) {
                            app.fileDropped(p.view().bytes());
                        }
                        main.ui.requestFrame();
                    }
                    null => {}
                }
            }
            null => {}
        }
```

`app.takeFileDrop`, `filesDropped` and `fileDropped` are the demo app's own functions. `drops.pathsOf(number)` is
null when `number` is not the last drop.

## Light and dark mode

The theme is a flag on the `Ui`. The host sets it at startup, and the app may change it in a frame.

| Call | What it does |
|---|---|
| `ui.setDarkMode(on)` | Sets the theme |
| `ui.isDarkMode()` | Reads it |
| `ui.theme(dark, light)` | One of two colors, by the theme |
| `ui.tone(dark)` | A dark theme's color as it is, or, in the light theme, a slate turned over: 50 and 950, 100 and 900, and so on |

```ceesafe frame
let dark = ui.isDarkMode();
ui.open(Style.row().p(space(4.0)).gap(space(3.0)).bg(ui.theme(SLATE_950, SLATE_50)));
ui.label("Theme", Text.of().color(ui.tone(SLATE_100)));
if (Controls.button(mut ui, "theme", if (dark) "Light" else "Dark")) {
    ui.setDarkMode(!dark);
}
ui.close();
```

The built-in controls draw in the theme by themselves. The host follows it for the clear color:

```ceesafe skip
            Color.hex(if (pane.ui.isDarkMode()) SLATE_950 else SLATE_50),
```

The template starts dark. The demo's host starts dark unless `CLAYWIND_LIGHT` is set:

```ceesafe skip
    let dark = match (resumed) {
        Some(r) => r.dark,
        null => Environment.variable("CLAYWIND_LIGHT") == null,
    };
```

## Environment variables

| Variable | Read by | Effect |
|---|---|---|
| `CLAYWIND_FONT_DIR=DIR` | `loadSystemFonts`, so the template's app and the demo | The directory that holds `Arial.ttf`, `Arial Bold.ttf` and `Courier New.ttf` |
| `CLAYWIND_FONT_PATH=DIR` | `SystemFonts` | One more folder to search for fallback fonts |
| `CLAYWIND_LIGHT=1` | The demo's host | Starts in light mode |
| `CLAYWIND_DRAWN_MENUS=1` | The demo's host | Draws the menu bar in the window on every system |

```sh
CLAYWIND_LIGHT=1 CLAYWIND_FONT_DIR=/usr/share/fonts/truetype/dejavu ./out/desktop
```

## More in the demo's host

`claywind-hosts/desktop/src/main.cee` is a fuller host. Beyond the template's it has:

- a second window that shares the app's state and the raster: [Several windows](./app#several-windows);
- the system's file dialogs: [File dialogs](./app#file-dialogs);
- background jobs that wake the window: [Background work](./app#background-work);
- hot reload, and a phone's saved state: [Saving and restoring state](./app#saving-and-restoring-state).
