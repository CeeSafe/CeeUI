# Clay — ClayWind in pure CeeSafe

ClayWind is a Tailwind-flavoured immediate-mode UI toolkit: layout, rendering, text, controls and transitions. Its C
version is built on [Clay](https://github.com/nicbarker/clay), [ThorVG](https://github.com/thorvg/thorvg) and SDL3.
This repository is ClayWind ported to [CeeSafe](https://ceesafe.github.io/CeeSafe/), a memory-safe systems language
that compiles to C. It has **no C code and no dependencies**: no `extern "C"` in the port, and nothing to install but
the compiler.

The same demo runs in a native window on macOS, Windows and Linux, headless (for tests and screenshots), in a
browser, and in a terminal.

**Credits.** ClayWind stands on [Clay](https://github.com/nicbarker/clay) by Nic Barker. The layout model, the
element and sizing rules and the layout algorithms are Clay's, and the layout engine here (`claywind/src/layout_*.cee`)
is a port of them, altered for CeeSafe. Clay is under the zlib licence ([THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).

![The ClayWind demo's dashboard, drawn by the CeeSafe renderer](docs/dashboard.png)

![The effects tab: rounded clipping, gradients and blend modes](docs/effects.png)

## What is in it

- **A layout engine.** A port of Clay 0.14's layout core, as ClayWind forked it: flex rows and columns, grow, fit,
  fixed and percent sizing, wrapping, gaps and margins, aspect ratios, floating and absolute elements, scrolling,
  line clamping, and per-element transforms. It is checked against the C original with a differential fuzzer.
- **A software renderer** that matches ThorVG's output: a render fuzzer against ThorVG finds 490 of 500 random
  frames identical, and every difference is a known, documented case.
  - Anti-aliased paths, rounded rectangles, borders and rings, linear and radial gradients, and images (PNG and BMP
    decoding included).
  - Shadows, blur, grayscale and brightness filters, opacity, and all CSS blend modes.
- **TrueType and OpenType (CFF) fonts:** glyphs read and rendered by the toolkit itself, from single fonts and
  collections (.ttc), CID-keyed CJK fonts such as Hiragino and Noto Sans CJK included. Text
  is shaped into glyphs that measuring and drawing share, and a character the run's font lacks comes from a
  fallback chain (`Raster.setFallbacks`), so CJK, Hangul, Greek and Cyrillic draw in a Latin run.
- **OpenType shaping:** each font run goes through the font's own GSUB and GPOS lookups (all substitution
  types, and single, pair, cursive, mark-to-base, mark-to-ligature, mark-to-mark and contextual positioning) for
  the run's script, with GDEF glyph classes and lookup flags: ligatures, contextual alternates, GPOS kerning, and
  combining marks placed on their base by the font's anchors. Arabic letters take their joining forms (isol, fina,
  medi, init by Unicode's joining types, ZWJ and ZWNJ honoured) in HarfBuzz's feature stages, and cursive
  attachment hangs Nastaliq's letters off one another.
- **Indic scripts:** Devanagari, Bengali, Gurmukhi, Gujarati, Oriya, Tamil, Telugu, Kannada and Malayalam go
  through an Indic shaper ported from HarfBuzz's: syllables, reordering (pre-base matras, reph), half, below and
  post-base forms, conjuncts, and decomposition of two-part vowels. Across 8,080 randomized and real words in ten
  fonts (Noto Sans Malayalam among them), every glyph and position matches HarfBuzz's (`claywind-tests`' shaping test, with cases from
  `tools/shaping/hb_cases.py`).
- **Colour emoji:** bitmap emoji fonts (sbix, as Apple Color Emoji; CBDT, as Noto Color Emoji) and COLR ones
  draw in their own colours, in Raster and on the GPU (as images). COLR version 1 (Windows 11's Segoe UI Emoji)
  draws its paint graph: linear, two-circle and sweep gradients, transforms, glyph clips, layers, and composites by
  every Porter-Duff operator and blend mode, at the font's default instance; version 0's layers draw too. A
  bitmap face is loaded without its bitmaps, which are most of its file: `claywind-fonts` reads the PNG of each
  glyph a frame draws from the strike nearest its size (`SystemFonts.supplyFrame`). Emoji sequences (ZWJ families,
  skin tones, flags, keycaps, VS16) form through the font's GSUB or, for Apple's, its AAT `morx` table.
- **System fonts as fallbacks:** `claywind-fonts` (hosted; `SystemFonts.supply`) finds, among the OS's own fonts, a
  face for each character the loaded ones lack, and loads it into the fallback chain. Its catalog (each face's
  coverage, weight and style, read from its cmap and OS/2 tables) is built the first time a character is missing.
- **Unicode text segmentation:** lines break at UAX #14's opportunities (so CJK wraps between ideographs and a
  line breaks after a hyphen), the caret, Backspace and line clamping work in UAX #29 grapheme clusters, and words
  are UAX #29's (`wordBoundary`, `wordEnd`, `wordStart`, `isWordLike`). All three pass Unicode 16's own test files.
- **Bidirectional text:** each line goes through the Unicode Bidirectional Algorithm (UAX #9, all of it: explicit
  embeddings, overrides and isolates, paired brackets), so Hebrew and other right-to-left text reads right to left
  inside a left-to-right line and as a line of its own, brackets mirrored, and Arabic joins. It passes Unicode 16's BidiTest.txt and
  BidiCharacterTest.txt in full. A text field's arrows move the caret as the text is drawn (in right-to-left text,
  Right goes towards its start), and a selection is highlighted where its characters are, so one across both
  directions is several boxes (`TextMetrics.spans`). A paragraph whose first strong character is right to left sits at
  the right (`TextAlign.Start`, the default), in a text field too.
- **Text editing:** `Controls.textField` keeps a caret and a selection (`TextEdit`): the pointer places the caret and
  a drag selects (a double click a word, a triple click everything), the arrows, Home and End move it (Shift
  extends; with Option on macOS or Control elsewhere, the arrows move by words), Backspace and Delete remove a selection or a
  grapheme cluster, typing and Paste replace the selection, Copy, Cut and Select All act on it, and an input
  method's composition shows underlined at the caret. Hosts pass the platform's shortcuts and clipboard in and out
  (`Ui.editCommand`, `Ui.paste`, `Ui.copiedText`) and drive the input method (`Ui.setComposition`,
  `Ui.wantsTextInput`, `Ui.textInputArea`).
- **Long text:** `indexLines` indexes a document's lines once, and `Controls.textView` shows it a screenful at a time,
  with optional line numbers: only the lines in view become elements, so a 100,000-line document lays out in the
  time a page does. `Controls.selectableTextView` is the same view with a caret and a selection: the pointer selects
  (a double click a word, a triple click a line), the arrows, Up, Down, Home, End and the page keys move the caret
  across lines, Shift extends, Copy and Select All act on it, and the view scrolls to the caret. `Controls.textEditor`
  edits a `TextDocument` (its text, line index, rows per line and undo history, all in caller-owned memory): typing,
  Enter, Backspace and Delete (joining lines at their edges), Cut and Paste across lines, Undo and Redo (secondary-Z,
  secondary-Shift-Z, Control-Y) a word of typing at a time; an edit with no room is refused. Lines wrap at the view's
  width at UAX #14's opportunities (Up, Down and the page keys then move by rows); only the lines in view are laid out
  and measured, the rest counted as one row until they are. An input method's composition shows underlined at the
  caret.
- **Retina output.** `Raster.setPixelRatio` draws at the display's pixel ratio while layout stays in points.
- **Incremental rendering.** `Damage` works out what changed between frames, and the raster redraws only those
  boxes. The result is byte-identical to a full redraw.
- **Controls:** buttons, checkboxes, text input with a caret, spin input, slider, radio, tabs, dropdown, single- and
  multi-select lists, context menu and tooltip.
- **Virtual list:** `Controls.virtualList` scrolls any number of rows of one height, of which only those in view
  become elements (a million rows cost a screenful). The program draws each row's content between
  `Controls.virtualRow` and `ui.close()`; a press selects a row, and Up, Down, the page keys, Home and End move the
  selection while the list has the focus, scrolling just enough to show it.
- **Data grid:** `Controls.dataGrid` is a virtual list cut into columns under a header that stays at the top and
  follows the rows sideways. Pressing a title sorts by its column (`GridSort`; the grid says when, and the program
  sorts its rows), and dragging a title's edge resizes its column (the program keeps the widths). Cells
  (`Controls.gridCell`) hold anything and clip what does not fit. The demo's Data page shows 10,000 orders.
- **Rich text:** `Ui.richLabel` lays out one paragraph in several styles (`TextStyle`: a run of bytes in a `Text`
  look, its font, size, colour and spacing): the runs wrap together as one paragraph, measured a run at a time, and
  share a baseline on each line; in a right-to-left paragraph they run from the right. `LayoutTree.richText` is the
  same for a layout used directly.
- **Charts:** `Controls.chart` draws line, area and bar charts of one or more series as vector paths, over a grid
  on round values (1, 2 or 5 times a power of ten, taking in 0). The pointer over the plot highlights the nearest
  label and shows its values; a screen reader hears the chart's name and size.
- **Vector paths.** `PathBuilder` makes a path of lines and quadratic and cubic curves; `Ui.path` draws it in an
  element's box, filled (nonzero or even-odd) or stroked with round joins, optionally from a view box.
- **Also:** focus cycling with Tab, vector icons, eased transitions, and dark and light themes.

## Repository layout

| Path | What it is |
|---|---|
| `claywind/` | The library: `layout_*`, `raster_*`, `font`, `image`, `controls`, `ui`, `style`, `transition`, `damage` and the rest, under `src/` |
| `claywind-fonts/` | System font discovery for hosts (it reads files, as `claywind` cannot): fallback fonts for the characters a frame's fonts lack |
| `claywind-desktop/` | What a desktop app needs around ClayWind (hosted): `DesktopPlatform` over CeeSafe's `Window`, the system's fonts, `Pane` (a window's Ui, Damage and input; panes share one Raster), the system's menus from a `MenuModel`, file drops, and hot reload's `--hot` side. The desktop and GPU hosts use it, as an app would |
| `claywind-tasks/` | Background work for a UI (hosted): a `Task` runs jobs on threads of their own, whose results wake the window (`Window.waitEvent`) and reach the frame through `poll`; a new job cancels the one before |
| `claywind-demo/` | The demo app (the C version's `main.c`) and `ScriptPlatform`, a scripted input source for tests |
| `claywind-dev/` | Hot reload: rebuilds an app when its sources change and restarts it where it was (`--hot`) |
| `claywind-pack/` | Packaging: an app as a macOS `.app`, an iOS `.app` (simulator, or signed for a device) or an Android `.apk`, from its package directory |
| `claywind-demo-tasks/` | What both hosts share for the demo: its saved state (`hotSave`, `hotRestore`: hot reload's, and a phone's when the app leaves the screen) and its background jobs on threads (`claywind-tasks`): the Controls page's Tasks tab counts primes on one |
| `claywind-hosts/desktop/` | The demo in a native window: AppKit, Win32 or X11, through CeeSafe's `Window`. The Controls page's Live state panel opens in a second window that shares the app's state and the renderer |
| `claywind-hosts/headless/` | The demo rendered to files: screenshots, golden tests and benchmarks |
| `claywind-hosts/browser/` | An HTTP + WebSocket server streaming the demo to a `<canvas>` page |
| `claywind-hosts/terminal/` | A small dashboard drawn in a terminal, with mouse and keyboard |
| `claywind-hosts/gpu/` | The demo drawn by the GPU in a window it draws itself: Metal on macOS and iOS, Vulkan on Linux and Android, Direct3D 12 on Windows (Vulkan with `CLAYWIND_GPU=vulkan`, or when Direct3D 12 cannot start). A frame redraws only what `Damage` finds changed (`CLAYWIND_WHOLE=1` redraws it whole). Also a side-by-side check against `Raster` on every tab, and a 5K benchmark |
| `claywind-tests/` | The test suite (`ceesafec test`) |
| `ClayWind-PORT-PLAN.md` | The port's plan and its results, phase by phase |
| `CEESAFE-FINDINGS.md` | What the port found in CeeSafe itself, and how each finding was resolved |

## Getting started

Install the CeeSafe compiler, **ceesafec 0.66.0 or later** (the hosts use its window text input and clipboard, ADR 0141, sleep while nothing changes, ADRs 0142 and 0145, show the system's menus and file dialogs, ADR 0146, take files dropped from the system, ADR 0150, are GUI programs on Windows, ADR 0151, and take a touch screen's
fingers, ADR 0155, a phone's safe area, ADR 0156, its soft keyboard, ADR 0157, its file dialogs, ADR 0158, an app's life, ADR 0159, and tell a screen reader what a window holds, ADR 0164; ClayWind's array sizes are constant expressions, ADR 0143; 0.50.2 links with MinGW's gcc on Windows):

```sh
curl -fsSL https://ceesafe.github.io/CeeSafe/install.sh | sh          # macOS and Linux
```

```powershell
irm https://ceesafe.github.io/CeeSafe/install.ps1 | iex               # Windows
```

`ceesafec` uses the system's C compiler: clang or gcc, or MSVC on Windows.

### The desktop app

```sh
cd claywind-hosts/desktop
ceesafec build
./out/desktop
```

- **Fonts.** It uses Arial, Arial Bold and Courier New. On Linux it uses the Microsoft core fonts, or falls back to
  DejaVu; `CLAYWIND_FONT_DIR=<dir>` points it elsewhere.
- **Light mode.** Press **T** to toggle it, or start the app with `CLAYWIND_LIGHT=1`.
- **The window** resizes freely. On a Retina display it draws one pixel per pixel.

### The other hosts

```sh
# Headless: write one frame (.bmp or raw RGBA)
cd claywind-hosts/headless && ceesafec build
CLAYWIND_SCREENSHOT=dashboard.bmp ./out/headless

# Browser: then open http://127.0.0.1:8080/
cd claywind-hosts/browser && ceesafec build && ./out/browser 8080

# Terminal: mouse, wheel and arrow keys; q quits
cd claywind-hosts/terminal && ceesafec build && ./out/terminal
```

The headless host also takes these, among others (see the top of its `src/main.cee`):

| Variable | What it does |
|---|---|
| `CLAYWIND_SCREENSHOT_TAB` | Which tab to show |
| `CLAYWIND_SHOT_EVENTS` | Scripted clicks and keys |
| `CLAYWIND_PIXEL_RATIO` | The pixel ratio to draw at |
| `CLAYWIND_INCREMENTAL` | Draw every frame through `Damage` |
| `CLAYWIND_BENCH` | Time the frames instead of writing one |

### Tests

```sh
cd claywind-tests
ceesafec test            # 131 tests; add --interp to run them in CeeSafe's interpreter
```

- **In this repository:** the test suite.
- **In the C ClayWind repository** (private, branch `ceesafe-port-tools`): the tools that compare the port with the
  C version.
  - **Layout:** a differential fuzzer against `clay.h`.
  - **Pixels:** a render fuzzer against ThorVG.
  - **The demo:** a scene-by-scene comparison.

## Using the library

ClayWind is immediate mode. Every frame:
1. the host gives `Ui.begin` a `Platform`, which supplies the input and the clock;
2. the app declares its elements and controls, styled Tailwind-fashion;
3. `endFrame` lays the frame out;
4. a `Backend` draws it. The built-in `Raster` is the software renderer; the host then shows its pixels.

A frame runs only when something changed: `Ui.needsFrame` and `Ui.frameDelayMs` tell the host whether to run the
next frame at the display's pace or to sleep on `Window.waitEvent` until input, a caret blink, a timer the app asked
for (`Ui.requestFrameAfter`), or a background job's result (`claywind-tasks`) wakes it. An idle window uses no CPU.

Several windows: each has its own `Ui` and `Damage`, and they can share one `Raster`, its fonts and caches, which
keeps each window's last frame in a `RasterFrame` while it draws another's, so every window is still redrawn only
where it changed. The host sleeps on `Window.waitAny` until any of them needs a frame.

Menus are made of actions: an app describes them once to a `MenuBuilder` (menus, items that name actions,
separators), and a host shows them as the system's menus (a macOS menu bar, a Windows window's menu; CeeSafe's
`Window.setMenu`), each item's shortcut its action's key binding (`Ui.bindingFor`), a chosen item reaching the frame
as its action. Where the system has no menu bar (X11), `Controls.menuBar` draws the same menus in the window. The
demo's File menu opens and saves files through the system's dialogs (`FileDialog`).

Drag and drop: an element becomes a drag source with a payload the app chooses (`Ui.dragSource`), and a drop
target learns whether a drag of the kind it accepts is over it or was dropped on it (`Ui.dropTarget`); a drag's
release is no click. The demo's Controls page has a board whose cards are dragged between columns. Files dragged
from the system come the same way, as a drag of kind `DRAG_FILES` (the host passes CeeSafe's `Window.dragOver` and
`nextDrop` on through `Platform.fileDrag`), and the board's place for them lists what was dropped.

Touch and gestures: a platform with a touch screen gives every finger (`Platform.nextTouch`), and the `Ui` recognizes
taps (and double taps), long presses, pans and pinches, each aimed at the elements under where it began
(`Ui.tapOn`, `longPressOn`, `panOn`, `pinchOn`; `tap`, `longPress`, `pan`, `pinch` wherever it began). Controls made
for a pointer keep working: the first finger presses, its lift clicks, a pan or pinch cancels the press, and a long
press is a secondary press. A pan scrolls the scroll container under it along its axis, and its release flings it,
slowing as UIKit's scroll views do. The desktop and GPU hosts turn on CeeSafe's touches (`Window.setTouches`,
ADR 0155: iOS and Android) and pass them on; the desktop host built for Android runs the demo by touch.

A phone's screen: a platform says how far the system covers the screen from each edge (`Platform.safeArea`: the
status bar, a sensor housing, the home indicator, rounded corners; none on a desktop). The frame is still the whole
screen: `Style.safe(ui.safeArea())` adds the insets to an element's padding, so its background reaches the edges and
its content stays clear (`Insets.sides()` and `ends()` for a bar that reaches an edge itself). `Ui.widthClass` and
`heightClass` are `Compact` or `Regular` (a phone held upright is compact in width, on its side in height), beside
the Tailwind breakpoints; a rotation is a new screen size. The pixel ratio is the window's scale, as on the desktop.

Responsive styles, mobile first as Tailwind's: the base style is a phone's, and `sm`, `md`, `lg`, `xl` and `xxl`
(640, 768, 1024, 1280 and 1536 points of the window's width, Tailwind's `sm:` to `2xl:`) each hold from their width
up, the wider over the narrower, before the hover and focus variants. `flexRow()` and `flexCol()` change a style's
direction, so a column of a phone becomes a row of a tablet:
`Style.column().gap(space(2.0)).md(Style.of().flexRow().gap(space(4.0)))`. What is built only from a width up
(Tailwind's `hidden md:block`) is an `if`: `if (ui.atLeast(Breakpoint.Md)) { sidebar(ui); }`; `ui.breakpoint()`
is the widest one reached (`Base` below `sm`), and `ui.containerWidth(id)` an element's own width, for `@container`.
The desktop and GPU hosts pass the window's safe area (`Window.safeArea`, CeeSafe ADR 0156: iOS and Android); the
desktop host built for Android draws the demo below the status bar and above the gesture bar, upright and turned.

Leaving a field: a click or a tap anywhere else leaves a text field, which on a phone puts the keyboard away. It
leaves when the tap ends, so what was tapped is still where it was (the keyboard's going moves the page), and at
the frame's end, so a button that focuses the field again (a chat's Send) keeps the keyboard without a blink
(`Ui.leaveFocus`). The keyboard's Return key says `Done` in a field unless the app reports another face
(`ui.reportReturnKey(ReturnKey.Send)`; CeeSafe ADR 0161): the key is Enter whichever it shows.

A phone's keyboard: a text field's focus is what shows the soft keyboard (the hosts' `setTextInput`), and typed text,
the input method's composition, Backspace and Enter reach the field as on the desktop. `Platform.keyboardInset` says
how much of the screen's bottom the keyboard covers; `ui.safeAreaWithKeyboard()` is the safe area with its bottom
raised to the keyboard, so a page padded by it (`Style.safe`) ends above the keyboard while one shows. A focused
field may say what it takes (`ui.reportTextKind(TextKind.Email)`: text, number, decimal, email, URL or password),
which chooses the keyboard. The desktop and GPU hosts pass the keyboard's inset and a field's kind (`Window.keyboardInset`
and `setTextInputKind`, CeeSafe ADR 0157: iOS and Android). The desktop host, built for each, runs the demo on an
Android phone and on an iPhone, where it finds the system's Helvetica Neue and Courier New: a tap on a text field
shows the keyboard and its keys type into the field. Open shows the system's document picker and gives a copy of
the picked file; Save writes the notes to a file of the app's own and exports it through the picker
(`FileDialog.exportFile`, CeeSafe ADR 0158), since a phone gives no path to write to.

A phone's app life: when the demo leaves the screen (`WindowAppEvent.Background`, CeeSafe ADR 0159) the host saves
the same snapshot hot reload uses into the app's own folder (`Directory.appData`), and it starts from that snapshot,
so an app the system ended comes back on its page with its text. Back on the screen it lays out and draws again;
at a memory warning it lets go of the inspector's cached frame.

The GPU on a phone (roadmap P4 step 7): the GPU host draws with Metal on an iPhone (a `CAMetalLayer` over the
`UIView` that `WindowHandle.UIKit` gives) and with Vulkan on Android (a surface on `WindowHandle.Android`'s
`ANativeWindow`). Android gives the app another window each time it is back on the screen, so the host attaches
again when the handle changes, and presents nothing while there is none; the swapchain is the window's own size,
unrotated, so a phone held sideways is drawn upright. It saves and restores its state as the desktop host does.
The iOS Simulator's Metal cannot run it (no reading of the render target in a fragment shader); an iPhone can. On an
iPhone it needs ceesafec 0.62.1 (findings O11).

Packaging (roadmap P4 step 8): `claywind-pack`, run in an app's package directory, makes what a system installs,
under `out/pack`:

```
cd claywind-hosts/gpu
../../claywind-pack/out/claywind_pack macos         --name "ClayWind GPU" --id com.example.gpu   # NAME.app, ad hoc
../../claywind-pack/out/claywind_pack ios-simulator --name "ClayWind GPU" --id com.example.gpu   # xcrun simctl install
../../claywind-pack/out/claywind_pack ios           --name "ClayWind GPU" --id com.example.gpu   # xcrun devicectl device install app
../../claywind-pack/out/claywind_pack android       --name "ClayWind GPU" --id com.example.gpu   # NAME.apk: adb install
```

It runs `ceesafec build --windowed` for the target, writes the `Info.plist` or the `AndroidManifest.xml` (a
`NativeActivity` kept through a rotation and the keyboard, `android:configChanges`; every orientation on iOS) and
runs the system's tools. The name, identifier and version default to the package's (`com.example.<name>`).
- **iOS**: linked with a 16 MiB main thread stack (`-Wl,-stack_size`), where iOS gives 1 MiB, an eighth of a
  Mac's, with UIKit on it under the program.
- **iOS device**: signed with the first "Apple Development" identity in the keychain (`--identity`) and the newest
  provisioning profile on the Mac that fits the identifier and has not expired, an exact one before a wildcard
  (`--profile`); Xcode makes both the first time it builds an app for the device. It does not make a profile.
- **Android**: the NDK, build-tools and platform are the newest under `ANDROID_HOME` (`ANDROID_NDK_HOME`);
  `--abi arm64-v8a,x86_64` builds a library for each; signed with `~/.android/debug.keystore` (`--keystore`,
  `--keystore-pass`), so it installs but is not a store build.
- **macOS**: signed ad hoc, for this Mac; no Developer ID, notarization or icon yet.
- **`--network WHY`**, for an app that uses the network: Android's `INTERNET` permission, and on iOS the
  sentence shown when the app first reaches a device on the local network (`NSLocalNetworkUsageDescription`).
It needs a Mac for the Apple targets and a Unix for all (it runs `mkdir`, `cp`, `rm`, `zip`): Windows is not a host
for it yet.

Hot reload: `claywind-dev`, run in an app's package directory, builds the app, runs it with `--hot out/hot`, and
watches the sources of the package and of every package it depends on by path. When a source changes it builds
again; a build that fails prints its errors and leaves the app running, and one that succeeds asks the app to save
its state (`SnapshotWriter`; the demo's `DemoApp.save`) and end, then starts the new build from that state. So a
code change shows in the app where it was:

```sh
cd claywind-hosts/desktop && ../../claywind-dev/out/claywind_dev
```

The demo (`claywind-demo/src/demo.cee`) uses most of the API. `claywind-hosts/desktop/src/main.cee` is a complete
host loop in about 250 lines: window events, layout, incremental rendering, presenting only the changed boxes, and
sleeping while nothing changes.

## Status

- **Every host** builds and runs on macOS. The desktop host has also run on Windows 11 and on Ubuntu under X11.
- **The browser host** streams a fixed 1024 × 640 frame.
- **The terminal host** runs its own small dashboard. The full demo is laid out for pixels, not character cells.
- **Accessibility:** the controls tell a screen reader what they are (`Ui.access` and `claywind/src/access.cee`:
  role, name, value, state, a text field's caret), and take its presses, focus and steps. With CeeSafe 0.66.0 (ADR
  0164) VoiceOver reads and works the demo on macOS and iOS, TalkBack on Android, and NVDA on Windows; Linux gives
  a reader nothing yet. Not there: containers and tables (a list's rows are buttons); dragging; a reader setting a
  field's text itself; reading by character in a field outside macOS; TalkBack's focus rectangle (the window's
  surface is the app's). A control of your own is one line after its `open`:
  `ui.access(id, AccessRole.Button, label)`; a field with no label of its own is named after it:
  `ui.accessName("server", "Server address")`.
- **The web host** (`claywind-web`, with the demo on it in `claywind-hosts/web`): a ClayWind app as a WebAssembly
  module in a browser, drawn by the software renderer onto a canvas at the screen's pixel ratio. It redraws and
  shows only what changed and sleeps while nothing does; it has the pointer, the keyboard, touches, the pointer's
  shape, and copy, cut and paste with the system's clipboard. `claywind-pack web --font FILE...` packs an app
  (`sh build.sh` in `claywind-hosts/web` does it for the demo): it needs a clang with the `wasm64` target and
  `wasm-ld`, and the result is served by its own `serve.py`, or any server that sends the two cross-origin isolation
  headers. It is 64-bit WebAssembly, which Chrome and Firefox run and Safari does not yet. For a screen reader the
  page keeps the app's elements (role, name, state, place) as unseen elements of its own over the canvas; a reader's
  press, focus and slider step go to the app, and the app's focus is the page's. A text field's element is a text
  input holding the field's text, caret and selection: a reader reads the field there, by character as the caret
  moves; a phone shows its keyboard for it (of the field's kind, with its Return key), and an input method composes
  in it. Fallback fonts (`claywind-pack web --fallback FILE...`) are fetched only when text has a character the
  app's fonts lack. Not there: colour emoji, the system's own fonts, lists and menus as containers for a reader,
  file dialogs, drag and drop from outside, a second window.
- **Linux:** the desktop host needs X11; Wayland is not supported yet.
- **Not ported from the C version:** the debug view and Clay's built-in transitions. ClayWind has its own transition
  engine.

## Licence and credits

- **This port:** MIT ([LICENSE](LICENSE)); every source file is marked `SPDX-License-Identifier: MIT`. Clay's notice
  is also in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
- **Clay** (layout): the layout engine (`claywind/src/layout_*.cee`) is a port, altered for CeeSafe, of the ClayWind
  fork of [Clay](https://github.com/nicbarker/clay) 0.14, by Nic Barker. Clay is under the zlib/libpng licence:

  > Copyright (c) 2024 Nic Barker
  >
  > This software is provided 'as-is', without any express or implied warranty. In no event will the authors be
  > held liable for any damages arising from the use of this software.
  >
  > Permission is granted to anyone to use this software for any purpose, including commercial applications, and
  > to alter it and redistribute it freely, subject to the following restrictions:
  >
  > 1. The origin of this software must not be misrepresented; you must not claim that you wrote the original
  >    software. If you use this software in a product, an acknowledgment in the product documentation would be
  >    appreciated but is not required.
  > 2. Altered source versions must be plainly marked as such, and must not be misrepresented as being the
  >    original software.
  > 3. This notice may not be removed or altered from any source distribution.

- **ThorVG:** the renderer is written from scratch to match [ThorVG](https://github.com/thorvg/thorvg)'s output, and
  contains none of its code.
- **renderer.h:** ClayWind's Tailwind-style API began as
  [TomasBorquez/renderer.h](https://github.com/TomasBorquez/renderer.h).
