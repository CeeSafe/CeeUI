# What ClayWind is

This page says what ClayWind is, what it runs on, what is in it, and how its repository is laid out.

## A UI toolkit in CeeSafe

ClayWind is an immediate-mode UI toolkit with Tailwind-flavoured styles. It is written in
[CeeSafe](https://ceesafe.github.io/CeeSafe/), a memory-safe systems language that compiles to C.

An app is a function, the frame. It runs whenever something changed and declares the whole window from the app's
state. There are no widget objects to create and keep in step with your data.

```ceesafe
import claywind;

public struct Hello {
    public greeted: bool;
}

public fn helloFrame(ui: borrow mut Ui, app: borrow mut Hello) {
    ui.open(Style.column().w(Size.grow()).h(Size.grow()).center().gap(space(4.0)).bg(SLATE_950));
    ui.label(if (app.greeted) "Hello." else "Nobody greeted yet.", Text.of().size(20.0).color(SLATE_100));
    if (Controls.button(mut ui, "greet", "Greet")) {
        app.greeted = true;
    }
    ui.close();
}
```

The parts under that function are ClayWind's own:

| Part | What it is |
|---|---|
| Layout engine | A port of [Clay](https://github.com/nicbarker/clay) 0.14's layout core: rows and columns, grow, fit, fixed and percent sizes, wrapping, floating elements, scrolling |
| Style builder | `Style`, built Tailwind-fashion: `Style.row().p(space(4.0)).bg(SLATE_900).rounded(Radius.Lg)` |
| Software renderer | `Raster`: anti-aliased paths, rounded rectangles, borders, gradients, images, shadows, filters, blend modes |
| GPU backends | The GPU host draws the same frames with Metal, Vulkan or Direct3D 12 |
| Fonts | TrueType and OpenType (CFF) fonts and collections, read and rendered by the toolkit |
| Text shaping | OpenType GSUB and GPOS, Arabic joining, Indic scripts, bidirectional lines, colour emoji, line breaking |
| Controls | Buttons, text fields, lists, a data grid, charts, menus and more |

Nothing is installed but the CeeSafe compiler, `ceesafec`. [Getting started](./getting-started) takes you from
there to a window of your own.

## What it runs on

One app runs on each of these:

| System | How it is drawn |
|---|---|
| macOS | A native window (AppKit); the software renderer, or Metal in the GPU host |
| Windows | A native window (Win32); the software renderer, or Direct3D 12 or Vulkan in the GPU host |
| Linux | A window on Wayland or X11; the software renderer, or Vulkan in the GPU host |
| iOS | An app packed by `claywind-pack`; Metal in the GPU host |
| Android | An `.apk` packed by `claywind-pack`; Vulkan in the GPU host |
| The web | A WebAssembly module that draws onto a canvas with the software renderer |

There are three more hosts for the demo: a headless one that writes frames to files, one that streams frames to a
browser page, and one that draws a small dashboard in a terminal.

[A desktop host](./desktop), [Phones](./phones) and [The web](./web) describe each kind of host.
[Status](./status) says what has been run where, and what is missing.

## What is in it

- [A frame](./frame): how a frame runs, when it runs, and what is kept between frames.
- [Layout](./layout): rows, columns, sizes, alignment, wrapping, floating elements and scrolling.
- [Styles](./styles): the style builder, colours, variants, responsive styles, themes and transitions.
- [Text](./text): labels, fonts, rich text and text shaping.
- [Controls](./controls): buttons, checkboxes, sliders, radio buttons, tabs, tooltips and menus.
- [Lists, grids and charts](./lists-grids-charts): the virtual list, the data grid and charts.
- [Editing](./editing): text fields and text editing.
- [Input](./input): the pointer, the keys, touch gestures, and drag and drop.
- [Drawing](./drawing): vector paths.
- [An app](./app): an app's state, and background work.
- [A desktop host](./desktop), [Phones](./phones) and [The web](./web): the hosts.
- [Accessibility](./accessibility): what a screen reader is told.
- [Bindings](./bindings): ClayWind from C.
- [Tools](./tools): packing, hot reload and the layout inspector.
- [Testing](./testing): the test suite.

## The packages

The repository is a set of CeeSafe packages. An app depends on `claywind`, and on the hosted packages it needs.

| Path | What it is |
|---|---|
| `claywind/` | The library: layout, the renderer, fonts, images, controls, `Ui`, `Style`, transitions and damage tracking |
| `claywind-fonts/` | System font discovery for hosts: fallback fonts for the characters a frame's fonts lack |
| `claywind-desktop/` | What a desktop app needs around ClayWind: `DesktopPlatform` over CeeSafe's `Window`, the system's fonts, `Pane`, the system's menus, file drops, and hot reload's side in the app |
| `claywind-tasks/` | Background work for a UI: a `Task` runs jobs on threads, and their results wake the window |
| `claywind-web/` | The web host: a ClayWind app as a WebAssembly module in a browser |
| `claywind-capi/` | ClayWind for C: a C header, and bindings built on it |
| `claywind-demo/` | The demo app, and `ScriptPlatform`, a scripted input source for tests |
| `claywind-demo-tasks/` | What the demo's hosts share: its saved state and its background jobs |
| `claywind-dev/` | Hot reload: rebuilds an app when its sources change and restarts it where it was |
| `claywind-pack/` | Packing: an app as a macOS `.app`, an iOS `.app`, an Android `.apk` or a web page; and `new`, a new app from its template |
| `claywind-hosts/desktop/` | The demo in a native window |
| `claywind-hosts/headless/` | The demo rendered to files: screenshots, golden tests and benchmarks |
| `claywind-hosts/browser/` | An HTTP and WebSocket server that streams the demo to a `<canvas>` page |
| `claywind-hosts/terminal/` | A small dashboard drawn in a terminal, with mouse and keyboard |
| `claywind-hosts/gpu/` | The demo drawn by the GPU: Metal, Vulkan or Direct3D 12 |
| `claywind-hosts/web/` | The demo on the web host |
| `claywind-tests/` | The test suite (`ceesafec test`) |

`claywind` itself is not hosted: it reads no files and opens no windows. A host gives it input through a
`Platform` and takes its drawing through a `Backend`. [A frame](./frame) shows both.

## Credits

ClayWind stands on [Clay](https://github.com/nicbarker/clay) by Nic Barker. The layout model, the element and
sizing rules and the layout algorithms are Clay's. The layout engine here (`claywind/src/layout_*.cee`) is a port
of them, altered for CeeSafe. Clay is under the zlib licence; its notice is in the repository's
`THIRD_PARTY_NOTICES.md`.

The software renderer is written from scratch to match the output of [ThorVG](https://github.com/thorvg/thorvg),
and contains none of its code. The Tailwind-style API began as
[TomasBorquez/renderer.h](https://github.com/TomasBorquez/renderer.h).

ClayWind is under the MIT licence.
