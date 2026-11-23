# Platforms and status

This page says where CeeUI runs, what works on each system, which compiler it needs, and what is not there yet.

## Systems

| System | Window | Renderers | Screen reader | Packing |
|---|---|---|---|---|
| macOS | AppKit | `Raster`; Metal | VoiceOver | A `.app`, signed ad hoc |
| Windows | Win32 | `Raster`; Direct3D 12, or Vulkan | NVDA | None yet |
| Linux | Wayland and X11 | `Raster`; Vulkan | Orca | None yet |
| iOS | UIKit | `Raster`; Metal | VoiceOver | A `.app` for the simulator, or signed for a device |
| Android | A `NativeActivity` | `Raster`; Vulkan | TalkBack | An `.apk`, signed with the debug key |
| The web | A canvas in a page | `Raster` | The browser's, through ARIA | A page and a WebAssembly module |

`Raster` is the software renderer. The GPU host (`ceeui-hosts/gpu`) draws with the system's GPU API instead.

Notes on the table:

- **macOS.** Every host builds and runs on macOS.
- **Windows.** The desktop host has run on Windows 11. The GPU host uses Vulkan with `CEEUI_GPU=vulkan`, or when
  Direct3D 12 cannot start.
- **Linux.** The desktop host runs on Wayland and on X11, with a title bar CeeUI draws. It has run on Ubuntu
  under X11.
- **iOS and Android.** The desktop host, built for each, runs the demo on an iPhone and on an Android phone. The iOS
  Simulator's Metal cannot run the GPU host; an iPhone can. [Phones](./phones) has the rest.
- **The web.** It is 64-bit WebAssembly, which Chrome and Firefox run and Safari does not yet. [The web](./web) has
  the rest.
- **Packing.** `ceeui-pack` needs a Mac for the Apple targets and a Unix for all of them. See
  [Packing](./tools#packing).

## The other hosts

| Host | Status |
|---|---|
| The headless host | Renders the demo to files: screenshots, golden tests and benchmarks |
| The browser host | An HTTP and WebSocket server that streams the demo to a canvas. It streams a fixed 1024 × 640 frame |
| The terminal host | Runs its own small dashboard. The full demo is laid out for pixels, not character cells |

The browser host is not the web host: it runs the app on a server and streams its frames. The web host runs the app
in the browser.

## The compiler

CeeUI needs only the CeeSafe compiler, `ceesafec`, and the system's C compiler.

| What | ceesafec |
|---|---|
| The release CeeUI is tested with in CI | 0.94.3 |
| The least for `ceeui-desktop` and the desktop host | 0.93.5 |
| The least for the GPU host | 0.93.0 |
| The least for `ceeui-pack` | 0.84.0 |
| The least for the C API and a new app from the template | 0.78.0 |
| The least for `ceeui-web` | 0.66.0 |

Each package states its least version in its `ceesafe.toml`. Use the release CI tests with unless you have a reason
not to: the screen readers of the table read the demo with CeeSafe 0.94.3.

CI builds and tests on macOS 15, Ubuntu 24.04 and Windows 2025.

## Accessibility

The controls tell a screen reader what they are, and take its presses, focus and steps. Lists, menus, the menu
bar, tabs and the data grid are containers. With CeeSafe 0.94.3 the demo is read and worked by VoiceOver on macOS
and on an iPhone, NVDA on Windows, Orca on Linux and TalkBack on an Android phone. On the web a page keeps the
app's elements for the browser's reader.

[Accessibility](./accessibility) has what each reader gets.

## Not there yet

Accessibility:

- The place of a list's row on iOS, which has none to give.
- Dragging.
- A reader setting a field's text itself.
- Reading by character in a field outside macOS.
- TalkBack's focus rectangle: the window's surface is the app's.
- NVDA's and Orca's table keys do not yet step from a data grid's row into its cells.

The web host:

- Colour emoji.
- The system's own fonts.
- File dialogs.
- Drag and drop from outside.
- A second window.
- Safari, which does not yet run 64-bit WebAssembly.

Packing:

- macOS: no Developer ID, notarization or icon.
- Android: the debug key makes an app that installs but is not a store build.
- Windows is not a host for `ceeui-pack`.

Not ported from the C version of ClayWind:

- The debug view.
- Clay's built-in transitions. CeeUI has its own transition engine.
