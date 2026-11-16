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
- **Colour emoji:** bitmap emoji fonts (sbix, as Apple Color Emoji; CBDT, as Noto Color Emoji) and layered ones
  (COLR version 0 with CPAL, as Segoe UI Emoji) draw in their own colours, in Raster and on the GPU (as images). A
  bitmap face is loaded without its bitmaps, which are most of its file: `claywind-fonts` reads the PNG of each
  glyph a frame draws from the strike nearest its size (`SystemFonts.supplyFrame`). Emoji sequences (ZWJ families,
  skin tones, flags, keycaps, VS16) form through the font's GSUB or, for Apple's, its AAT `morx` table. Not yet:
  COLR version 1.
- **System fonts as fallbacks:** `claywind-fonts` (hosted; `SystemFonts.supply`) finds, among the OS's own fonts, a
  face for each character the loaded ones lack, and loads it into the fallback chain. Its catalog (each face's
  coverage, weight and style, read from its cmap and OS/2 tables) is built the first time a character is missing.
- **Unicode text segmentation:** lines break at UAX #14's opportunities (so CJK wraps between ideographs and a
  line breaks after a hyphen), and the caret, Backspace and line clamping work in UAX #29 grapheme clusters. Both
  pass Unicode 16's own test files.
- **Bidirectional text:** each line goes through the Unicode Bidirectional Algorithm (UAX #9, all of it: explicit
  embeddings, overrides and isolates, paired brackets), so Hebrew and other right-to-left text reads right to left
  inside a left-to-right line and as a line of its own, brackets mirrored, and Arabic joins. It passes Unicode 16's BidiTest.txt and
  BidiCharacterTest.txt in full. Not yet: right-aligned right-to-left paragraphs, caret movement and selection in
  visual order.
- **Text editing:** `Controls.textField` keeps a caret and a selection (`TextEdit`): the pointer places the caret and
  a drag selects, the arrows, Home and End move it (Shift extends), Backspace and Delete remove a selection or a
  grapheme cluster, typing and Paste replace the selection, Copy, Cut and Select All act on it, and an input
  method's composition shows underlined at the caret. Hosts pass the platform's shortcuts and clipboard in and out
  (`Ui.editCommand`, `Ui.paste`, `Ui.copiedText`) and drive the input method (`Ui.setComposition`,
  `Ui.wantsTextInput`, `Ui.textInputArea`).
- **Long text:** `indexLines` indexes a document's lines once, and `Controls.textView` shows it a screenful at a time,
  with optional line numbers: only the lines in view become elements, so a 100,000-line document lays out in the
  time a page does. Not yet: wrapping, editing and selection in it.
- **Retina output.** `Raster.setPixelRatio` draws at the display's pixel ratio while layout stays in points.
- **Incremental rendering.** `Damage` works out what changed between frames, and the raster redraws only those
  boxes. The result is byte-identical to a full redraw.
- **Controls:** buttons, checkboxes, text input with a caret, spin input, slider, radio, tabs, dropdown, single- and
  multi-select lists, context menu and tooltip.
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
| `claywind-demo-tasks/` | The demo's background jobs on threads (`claywind-tasks`), for the desktop and GPU hosts: the Controls page's Tasks tab counts primes on one |
| `claywind-hosts/desktop/` | The demo in a native window: AppKit, Win32 or X11, through CeeSafe's `Window`. The Controls page's Live state panel opens in a second window that shares the app's state and the renderer |
| `claywind-hosts/headless/` | The demo rendered to files: screenshots, golden tests and benchmarks |
| `claywind-hosts/browser/` | An HTTP + WebSocket server streaming the demo to a `<canvas>` page |
| `claywind-hosts/terminal/` | A small dashboard drawn in a terminal, with mouse and keyboard |
| `claywind-hosts/gpu/` | The demo drawn by the GPU in a window it draws itself: Metal on macOS, Vulkan on Linux, Direct3D 12 on Windows (Vulkan with `CLAYWIND_GPU=vulkan`, or when Direct3D 12 cannot start). A frame redraws only what `Damage` finds changed (`CLAYWIND_WHOLE=1` redraws it whole). Also a side-by-side check against `Raster` on every tab, and a 5K benchmark |
| `claywind-tests/` | The test suite (`ceesafec test`) |
| `ClayWind-PORT-PLAN.md` | The port's plan and its results, phase by phase |
| `CEESAFE-FINDINGS.md` | What the port found in CeeSafe itself, and how each finding was resolved |

## Getting started

Install the CeeSafe compiler, **ceesafec 0.54.0 or later** (the hosts use its window text input and clipboard, ADR 0141, sleep while nothing changes, ADRs 0142 and 0145, show the system's menus and file dialogs, ADR 0146, take files dropped from the system, ADR 0150, and are GUI programs on Windows, ADR 0151; ClayWind's array sizes are constant expressions, ADR 0143; 0.50.2 links with MinGW's gcc on Windows):

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
ceesafec test            # 72 tests; add --interp to run them in CeeSafe's interpreter
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
slowing as UIKit's scroll views do. CeeSafe's `Window` gives touches from its ADR 0155 (iOS and Android), not yet
released; until then no host passes them on.

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
