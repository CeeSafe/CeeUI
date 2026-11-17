# CeeSafe language findings from the ClayWind port

Collected while porting ClayWind (Clay layout, a ThorVG-equivalent rasterizer, TrueType fonts, controls, BMP/PNG
decoding, three hosts) to pure CeeSafe, 2026-11-03, with ceesafec 0.10.1–0.10.3. Per-phase detail is in
`ClayWind-PORT-PLAN.md` §9–§16. Ranked by how much code each one bent.

## A. Worth raising upstream

### A1. `dyn` and generics do not compose (highest cost)
- A `dyn I` value does not satisfy a generic bound `T: I`.
- A type with region parameters, or one that holds a loan, cannot be `dyn` (N0268). The font engine borrows its pool,
  so it cannot be `dyn TextMetrics`.
- **Suspected bug:** a `borrow mut T` parameter passed to a `borrow mut dyn I` parameter is *moved*, not reborrowed
  (N0250 on the second use), although §13.3 says every use of a `borrow mut` reborrows.
- Cost in the port: `endLayout`, `endFrame`, `renderInto`, `textInputEx` became generic free functions; the
  controls test wraps its platform in a struct (`Rig`) to lend a field; `ScriptPlatform` copies its 4 KB script.

### A2. Methods cannot declare type parameters; no inherent `impl S {}` blocks
- The public API is lopsided: `Controls.button(...)` is a method, `textInputEx(...)` a free function only because it
  is generic over the metrics.

### A3. Arrays and slices cannot hold references
- `Slice<StringView>` and `[StringView; N]` are rejected ("array and slice elements cannot be references in v0").
- Cost: the controls take item lists as one `"Low|Medium|High"` view (a public-API wart). With no multi-line string
  literals, the browser page is written as 30 calls.

### A4. Suspected bugs and misleading diagnostics
- A float literal in a nested `if` expression defaults to `f64` even when the result is annotated `f32`.
- `var k = len;` with `len: borrow mut usize` reports N0246 "a reference variable must be declared with 'let'". The
  intent is a copy of a `Copy` value; it should copy, or the message should say how (`var k: usize = len + 0` works).
- A call through `dyn` is "may sleep" unless the interface method is `@effects()`, so async code rejects
  `ui.begin(platform)`. The fix is right; the diagnostic does not point to it.

## B. Ergonomics (frequent, small)
- `for (i in 0..16)` is `i32` even when `i` is only used as an index (many `0usize..16`).
- Missing: `min`/`max`/`abs` on `i32`/`u32`; `TextWriter.unsigned`; struct constants; `const` `StringView`; constant
  arrays of floats (stored as bit patterns).
- Blocks are not values in `match` arms or `if` expressions.
- `void` cannot be a type argument.
- Tests cannot live in a freestanding package; test files see the package's items, so helper names clash.
- The interpreter is slow: the suite takes about 18 minutes interpreted, seconds compiled.

## C. By design (leave alone)
No shadowing, no `is`, shifts only on unsigned values, `Vector` hosted-only, `box` reserved, `Console` blocking
(`AsyncConsole` in async code).

## D. Port refactors that follow upstream fixes
- A1 fixed: `endLayout`/`endFrame`/`renderInto`/`textInputEx` back to methods; drop `Rig` and the script copy.
- A3 fixed: `'|'`-separated item lists become real arrays (public API; do it before the API settles).
- A2 fixed: `textInputEx` moves into `Controls`.

## E. Status in ~/dev/CeeSafe (2026-11-03, uncommitted working tree)

Every item was reproduced against `out/ceesafec` 0.10.3 (repros in the session scratchpad, `repro/`).

| Item | Status | Where |
|---|---|---|
| A1 `borrow mut` moved into `dyn` (bug) | **Fixed.** A `borrow mut` local or field is reborrowed, then coerced. The field case was N0251 before. | `src/check/expr.cee` `coerce`; ADR 0063 §2.3; corpus `run-pass/dyn-reborrow`, `check-fail/dyn-reborrow` |
| A1 `dyn I` as a `T: I` bound | **Proposed**, not implemented | ADR 0064 §3.1 |
| A1 loans in `dyn` (OD-10a) | **Proposed**, an existing open decision | ADR 0064 §3.2 |
| A2 generic methods, inherent `impl` | **Proposed** | ADR 0064 §1 |
| A3 arrays and slices of views | **Proposed**, following the Vector rule of §25.4 | ADR 0064 §2 |
| A4 `f32` literal in a nested `if` | **Not reproduced** in isolation; dropped | — |
| A4 `var k = len` (N0246) | **Improved.** The message says how to copy, and `ceesafec fix` inserts `: T`. | `src/check/body.cee`; ADR 0063 §2.4 |
| A4 `dyn` "may sleep" explanation | **Improved.** It names the interface method and the `@effects()` fix. | `src/nir/effect_check.cee` `explainOp`; ADR 0063 §2.4 |
| B `0..16` used as an index | **Changed rule.** A range of literals is `usize` when the body indexes with its variable. | `src/check/range_index.cee`; spec §16.4; ADR 0063 §2.1; corpus `for-range-index` (run-pass and check-fail) |
| B `min`/`max` on integers | **Added** (built-ins; interpreter, C backend, `rt.h`) | ADR 0063 §2.2; corpus `run-pass/int-min-max` |
| B `abs` on integers | **Open** (it overflows at MIN) | ADR 0063 §5, question 2 |
| B `TextWriter.unsigned` | **Already exists** as `TextWriter.decimal(u64)`; my miss | — |
| B constants: structs, views, float arrays | Partly covered by ADR 0064 §2 (constant tables of views) | — |
| B blocks as values, `void` type argument | **Left alone** (§15 G5 by design) | ADR 0064 §4 |
| B interpreter speed | **Noted** for a benchmark item | ADR 0064 §4 |

**Verification:**
- Full corpus: 580 tests pass, 0 failures.
- New tests: they fail on 0.10.3 and pass with the change, compiled and interpreted.
- Fixed point: the changed compiler rebuilds itself to byte-identical C.
- Downstream: ClayWind (72 tests) and CosmoStack check.
- Contract suite: result in ROADMAP §10 once it finishes.

## F. Accepted and implemented (2026-11-04, uncommitted in ~/dev/CeeSafe)

The owner accepted ADR 0063 and ADR 0064 ("Both"), with `abs` as "Both". Everything proposed is implemented:

| Item | Now |
|---|---|
| A1 `dyn I` as a `T: I` bound | `dyn I` implements `I`, behind references only (by value: N0222 or N0330) |
| A1 loans in `dyn` (OD-10a) | a type with region parameters coerces to `borrow dyn I`; not to `Box<dyn I>` |
| A2 generic methods, inherent `impl` | `fn m<T>(…)` in structs, `x.m<T>(…)`, `impl S { … }` and `impl<T> S<T> { … }` |
| A3 arrays and slices of views | `Slice<StringView>`, `[StringView; N]`, `const LEVELS: [StringView; 3]` |
| B `abs` | `abs()` panics at the minimum; `unsignedAbs()` never panics |

Every soundness attack tried was rejected (ADR 0064 §5.2–§5.4). Full corpus: 596 tests, 0 failures. The fixed point
and the contract suite pass.

**§D refactors in ClayWind:** possible now, but the code would then need this unreleased compiler
(`~/dev/CeeSafe/out/ceesafec`), not the installed 0.10.3. They wait for a release, or for an explicit go-ahead.

## G. Released in ceesafec 0.11.0; §D done (2026-11-04)

ADRs 0063–0065 shipped as v0.11.0 (CeeSafe `main` 5204206, 45dde8d). ClayWind now requires 0.11.0.

**§D refactors, done:**
- `endLayout` and `renderInto` (was `renderTree`) are methods of `LayoutTree`, and `endFrame` and `renderInto` are
  methods of `Ui`, in `impl<region r> … { }` blocks.
- `textInputEx` is `Controls.textInputEx`.
- Item lists are `Slice<StringView>`; `itemCount`/`itemAt` are gone. The demo's lists are `const [StringView; N]`
  tables.
- `Rig` is gone from the controls test, since a `borrow mut` platform is now reborrowed into `dyn`.
- `ScriptPlatform<region r>` borrows its script instead of copying 4 KB of it.
- `var k: usize = len + 0` became `var k: usize = len`.
- The browser page is one `const PAGE: [StringView; 30]` table, not 30 calls.

**Verified:** all seven packages check, and 72 tests pass compiled. The 14 headless demo scenes are byte-identical
to before. The browser page is byte-identical, and `test_client.py` passes.

**New, small findings:**
- **`static` tables of views are rejected** (N0337: `[StringView; N]` is not `Share`). `static NAME: StringView`
  is accepted through a special case for a bare view (`src/check/statics.cee:184`), which does not extend to arrays
  of views. Static string data is safe to share, so the special case should cover arrays of views too.
- **A `const` table must be bound to a local to slice it or take `.length`** (N0247, N0216). This is documented
  (§10, "bound to a local to be sliced"), but it adds a line at every call that passes a table as a slice. A
  `static` table would avoid it, which makes the first finding the one worth fixing.

## H. Fixed in ~/dev/CeeSafe (2026-11-04)

- **Static tables of views (ADR 0066).** `static T: [StringView; N]` is accepted, so a table can be sliced where it
  is named. `static mut` tables stay rejected.
- **A soundness hole found while testing it (ADR 0067).** 0.11.0 compiled `unsafe { LAST = s.view(); }` into a
  `static mut LAST: StringView`, which dangles once `s` is gone. A write to a static now may carry no loan (N0272).
- **`static` in signatures (ADR 0068).** Writing `static` on a parameter is N0273. A result that names only `static`
  is enforced: the function may return only static data, and callers may keep the result past its arguments or store
  it in a static.
- **Released as ceesafec 0.12.0** (CeeSafe 8ba88b6, 046d24f, 7bed6e9, c23764e). ClayWind's tables are `static`
  now, and the `let x = TABLE;` lines are gone.

## I. Gaps that block a GPUI-class ClayWind (2026-11-14, ceesafec 0.45.1)

Found while planning ClayWind as a competitor to GPUI (the "ClayWind vs GPUI Roadmap" doc). These are missing
features, not bugs. ClayWind keeps no C of its own, so each one has to land in CeeSafe's std and hosted runtime
first. They are ranked by the roadmap phase they block.

| # | Gap | Evidence | Blocks |
|---|---|---|---|
| I1 | **No GPU surface.** `Window` only presents CPU RGBA8 buffers (`present`, `presentChanged`). It cannot give out a Metal layer, a Vulkan surface, a D3D12 swap chain or the native window handle. | `lib/std/window.cee:2`, `:187`, `:200`; no Metal, Vulkan or Direct3D code under `lib/` or `runtime/` | P1 GPU renderer |
| I2 | **No IME composition.** Typed text arrives one code point at a time (`WindowEvent.Text(u32)`). There is no preedit or marked-text event and no way to place the candidate window, so Chinese and Japanese input cannot work. | `lib/std/window.cee:128`; no `ime`, `composition` or `preedit` in `window.cee` or `ceesafe_rt_window.c` | P2 Text |
| I3 | **One window only.** The std window is "one window that shows premultiplied RGBA8 frames". | `lib/std/window.cee:2` | P3 App runtime |
| I4 | **No clipboard, menus, drag and drop or file dialogs** in `Window` or the hosted runtime. | `lib/std/window.cee`, the whole API (`open`, `nextEvent`, `present`, `presentChanged`, `size`, `scale`, `setCursor`, `setTitle`, `waitFrame`) | P3 App runtime |
| I5 | **No system font access.** Nothing in std finds installed fonts or their files. ClayWind's hosts hard-code font directories per OS (`claywind-hosts/desktop/src/main.cee`). | no font API under `lib/`; the only font hit in `runtime/` is X11's cursor font | P2 Text (fallback chains) |
| I6 | **No mobile or web target found.** I found no iOS, Android or wasm target in the docs or spec. The Rust-port blog says Windows and WebAssembly were not tried. | `docs/blog/porting-rust-to-ceesafe.md:151` | P4 Mobile, P5 web |
| I7 | **No accessibility bridge** (NSAccessibility, UI Automation, AT-SPI and the mobile equivalents). | no accessibility code in `lib/` or `runtime/` | P5 Platform completeness |
| I8 | **No Wayland.** The Linux window is X11 only. | `lib/std/window.cee:2` (ADR 0069) | P5 Platform completeness |

**Already there (no gap):** a C ABI for embedding CeeSafe in a host program (ADR 0088: `ceesafe_exports.h`,
`--emit obj`, `--emit rust`). The roadmap's Swift, Kotlin and Rust bindings can build on it. There are also
threads with `Arc` and `Mutex` (`lib/std/sync.cee`) and async I/O (`lib/std/async.cee`, ADR 0031) for P3's runtime.

**Decided (2026-11-14): the faster way.** CeeSafe does not grow a GPU API. ClayWind is allowed a small `extern "C"`
GPU layer (Metal, then Vulkan and Direct3D 12), and this ends its "no C" rule. So I1 shrinks to what that layer needs
from `Window`:
- **Native handles:** the `NSView` on macOS (one that can host a `CAMetalLayer`), the `HWND` on Windows, and the X11
  `Display*` and `Window` on Linux. Later, the `UIView` on iOS and the `ANativeWindow` on Android.
- **A GPU-owned mode:** a way to open a window that the GPU layer draws into, so that `present` and `presentChanged`
  do not fight it for the surface.
- **Resize and scale events that arrive before the next frame,** so the swap chain can be resized in step.

The other gaps (I2–I8) still need CeeSafe's runtime.

**I1 done: ADR 0137, released as ceesafec 0.46.0** (CeeSafe `main` cbf54947, tag `v0.46.0`):
- `Window.openWith(title, width, height, WindowSurface.External)` opens a window the runtime never paints. `present`
  and `presentChanged` on it are `WindowError.Unsupported`.
- `window.nativeHandle()` is a `WindowHandle`: `AppKit(nswindow, nsview)`, `Win32(hwnd, hinstance)`,
  `Xlib(display, window)` or `Scripted`. The handles are `u64`s, which only `unsafe` code can turn back into pointers.
- `WindowEvent.Resized` also arrives when only the scale changes, so a swap chain can be resized before the next
  frame.
- **Checked on macOS:** a Metal spike set a `CAMetalLayer` on the `NSView` and cleared it for 180 frames at scale 2.
  The X11 path compiles under gcc 12. The Win32 path is compiled only by the release workflow so far.
- **Still open:** live resizing on macOS. While the user drags the window's edge, AppKit's own loop holds the
  program's loop, so no frames are drawn (ADR 0137 §4).

## J. Found while building the GPU backends (2026-11-14, ceesafec 0.46.0)

- **J1. A manifest cannot name a C file per system.** `[target.linux]`, `[target.macos]` and `[target.windows]` take
  only `links`, `lib-dirs` and `include-dirs` (spec/tooling.md §4.1, ADR 0047). `claywind-hosts/gpu` needs `metal.c`
  on macOS, `vulkan.c` on Linux and Windows, and `d3d12.c` on Windows only. It lists all three in `c`, and each file
  wraps itself in `#if` so it compiles to nothing on the others. That works, but every file has to know which systems it is for, and a file meant for
  one system still has to compile on all of them. **Proposed:** let a `[target.OS]` table take `c` too, added after
  `[package]`'s, as its other fields are.
- **J2 (fixed in ceesafec 0.50.2). With GCC on Windows (MinGW), a program that uses the runtime's sockets or windows
  does not link.** Fixed at the source: ceesafec passes `-lws2_32 -lgdi32 -lbcrypt` to gcc on Windows (tooling.md
  §8.3), the runtime builds without gcc's warnings (the first two below), and the release workflow builds and runs a
  link check with MinGW's gcc (it printed "linked: Winsock, GDI and BCrypt" on the 0.50.2 release's win-x64 runner).
  Met again with 0.50.0 building the desktop host on the x64 laptop; with 0.50.2 the desktop and GPU hosts build there
  with WinLibs gcc 16 and no workaround, and `claywind-hosts/gpu`'s `[target.windows] links` is gone. The report as
  made: The
  hosted runtime names its system libraries with `#pragma comment(lib, …)` (`ceesafe_rt_io.c`, `ceesafe_rt_async.c`,
  `ceesafe_rt_window.c`). MSVC and clang-cl honour that, but GCC ignores it ("ignoring '#pragma comment'"). With
  WinLibs GCC 16.2 on Windows 11 x64 and ceesafec 0.46.0, `ws2_32`, `gdi32` and `bcrypt` were missing:
  `undefined reference to __imp_WSASocketW`, `StretchDIBits`, `BCryptGenRandom` and others. `claywind-hosts/gpu`
  works around it with `[target.windows] links = ["ws2_32", "gdi32", "user32", "bcrypt"]`. **Proposed:** the driver
  passes `-lws2_32 -lgdi32 -luser32 -lbcrypt` itself when the compiler is GCC or MinGW clang on Windows. The release
  workflow builds Windows with `cl` only, so a GCC smoke build there would catch this.
  - The same build warns only in CeeSafe's C: `ceesafe_rt_async.c` converts an object pointer to a function pointer
    four times (`-Wpedantic`; `memcpy`, as `ceesafe_rt_window.c` does, avoids it); `ceesafe_rt_io.c:3655` has an
    unused parameter `s`; and the generated `main.c` has `'t_9' may be used uninitialized` (GCC 16,
    `-Wmaybe-uninitialized`). The first two are fixed in 0.50.2; the third is a false positive (a whole-struct
    copy of a `Result` literal whose other union member is unset; no value is read uninitialized) and stays.


## K. Found while building text shaping and bidi (2026-11-15, ceesafec 0.48.0)

- **K1 (fixed in ceesafec 0.50.0, ADR 0143). An array length cannot be a constant expression.** An array length
  may now be any `usize` constant expression (`[0u32; 256 * 8]`), not one using a const parameter (ADR 0083 stays).
  ClayWind's sizes are their real expressions now (`BidiBuffer`'s `[u32; BIDI_CAPACITY * BIDI_SCRATCH]`, the lookup
  cache's `CACHED_LOOKUPS * 2` digests). The report as made: `[0u32; 256 * 8]` is refused: "an array length must be
  an integer literal, a 'usize' constant or a const parameter" (N0259). So a buffer sized as "8 u32s for each of
  1,024 characters" has to be written `[0u32; 8192]`, and the two numbers can drift apart. The same goes for
  `[u8; BIDI_CAPACITY]`-style fields whose size should follow another constant times a factor. **Proposed:** accept
  any constant expression of type `usize` (arithmetic on literals and `usize` constants) as an array length.
- **K2 (a misleading message, not a gap; fixed in ceesafec 0.50.0, ADR 0144). `Slice<u8>`
  seemed to have no `trim`.** Byte slices have had `trim()`, `trimStart()` and `trimEnd()` since ADR 0071 (lib/core's
  `ByteSearch`); N0200 listed only the built-in slice methods, so `trimmed()` looked unsupported. 0.50.0 makes
  N0200 list the byte and text methods and suggest close names ("did you mean 'trim' or 'trimEnd'?"). ClayWind's
  bidi test now uses `trim()`. The report as made: Parsing Unicode's test files (fields like ` 2 ` between semicolons) needs one;
  the slice methods are `copyFrom`, `fill`, `swap`, `sortBy`, `forEach`, `any`, `all`, `partitionPoint`, `splitAt`
  and `splitAtMut`, so the tests skip spaces by hand. **Proposed:** `trimmed()` (ASCII whitespace) on `Slice<u8>`.
- **K3 (fixed in ceesafec 0.49.1). An `impl` in a file that sorts before its type's file makes the type private to
  importers.** Fixed at the source: `declarePackageNames` no longer takes an inherent impl as a declaration of its
  type's name (CeeSafe branch `fix/claywind-k3`, package test 38 `impl-order`); ClayWind's files are `state.cee` and
  `actions.cee` again. The report as made: In package
  `claywind`, `public struct Ui<region r>` is declared in `src/ui.cee`. Adding `src/state.cee` containing only

  ```
  impl<region r> Ui<r> {
      public fn requestFrame(borrow mut this) { this.requested = true; }
  }
  ```

  makes every other package's `import claywind.Ui;` fail with `error[N0312]: 'claywind.Ui' is not public: only its
  own package can use it` (ceesafec 0.49.0). The same impl in `src/zz_state.cee` or `src/ui_edit.cee`, which sort
  after `ui.cee`, is fine. So visibility seems to be decided from the first declaration-like item for the name in
  file order, and an `impl` block (which has no `public`) taken as it. ClayWind worked around it by naming the file
  `ui_state.cee`, until the fix. **Proposed:** an `impl` never decides its type's visibility; a test with the impl's file sorting
  before the type's.

## L. Found while adding a second window (roadmap P3 step 4, 2026-11-15, ceesafec 0.50.2)

- **L1 (fixed in ceesafec 0.51.0, ADR 0145). A program cannot sleep until any of its windows needs it.** `Window.waitEvent` (ADR 0142) returns for its own
  window's events and wakes only, so with two windows open the input of the one not waited on stays queued until the
  other's timeout. Fixed at the source: ADR 0145, `Window.waitAny(timeoutMs)`, which returns when any open
  window has an event or a wake (the owner chose it over widening `waitEvent` or polling in turn). ClayWind's desktop
  host waits on it with the inspector window open: two idle windows use no CPU but the dashboard's clock.
- **L2 (fixed in ceesafec 0.51.0, ADR 0145). On macOS, one window's poll swallows another's input.** `cee_native_pump` passes every AppKit event to
  `cee_mac_translate(w, …)` for the window being polled, which queues only the events whose `NSWindow` is that
  window's and lets AppKit dispatch the rest: a click in window B, taken from AppKit's queue while window A's
  `nextEvent` or wait pumps, never reaches B's queue. Fixed in the same ADR: each event goes to the window it is
  for. Also met: macOS keeps at most 16 windows open (`CEE_MAC_WINDOWS`; the 17th `open` is `Resources`), which
  type-system.md §26.13 did not say; it does since 0.52.0 (ADR 0146).

## M. Found while writing claywind-dev, the hot-reload runner (roadmap P3 step 7, 2026-11-15, ceesafec 0.52.0)

- **M1 (fixed in ceesafec 0.54.0). A `move` closure cannot keep a view of its capture in a local.** The cause was wider: any `borrow mut` parameter's placeholder loans, a closure's `env` among them, were taken as write targets through a shared view (type-system.md §25.7 rule 2 now says only mutable borrows are followed back); claywind-dev passes its app's arguments as written. The report as made: In a `move` closure's body (a
  `Thread.spawn` literal or a boxed closure), `views.push(text.view())`, where `text` is a captured `String` and
  `views` a `Vector<StringView>` local to the body, is N0272: "this stores a value borrowed from 'env.text' into what
  'env' refers to, which outlives this call". Nothing goes into the environment: the vector is the body's own and dies
  before the body returns. The same lines in a plain function check, and so do they after `let owned = text;` in the
  body. Seven-line reproduction:
  ```
  fn main() -> i32 {
      let text = String.from("hello")!;
      let t = Thread.spawn(move () => {
          var views = Vector<StringView>.create();
          let pushed = views.push(text.view());
      })!;
      t.join();
      return 0;
  }
  ```
  ClayWind's claywind-dev passes its app's arguments to `Process.run` this way, from a thread.
- **M2 (fixed in ceesafec 0.54.0, ADR 0151). A CeeSafe program with a window opens a terminal window too on Windows.** `windowed = true` (or `--windowed`) links a GUI program, `main` still its entry; started from a terminal it writes there, and without one its output goes to `NUL`. ClayWind's desktop and GPU hosts set it. The report as made: Every executable `ceesafec build`
  makes on Windows is a console program (the console subsystem; `main` its entry), so Windows gives it a console
  window whenever it is not started from one: ClayWind's desktop host, double-clicked or started from Explorer or a
  scheduled task, shows a terminal beneath its window (the owner saw it on the x64 laptop). There is no manifest
  field or option for a GUI program (`-mwindows`, `/SUBSYSTEM:WINDOWS`). **Proposed:** a manifest field for a
  windowed executable, which keeps `main` as the entry, and its output in the terminal that started it when there is
  one (`AttachConsole(ATTACH_PARENT_PROCESS)`).
- **M3 (fixed in ceesafec 0.54.0). An unknown type in a struct field is reported again at a wrong place.** The cause: an impl's comparison built the interface's signature in the impl's file, so the interface's types were looked up there (the stray error carried an offset in the interface's file); it also let an impl in another package return a type of its own with the interface's type's name. The report as made: A field whose type is not imported
  (`files: Drag?;`, `Drag` from another package, not named by the file's `import lib.X;` lines) gives the right
  N0204 at the field, and a second N0204 for the same name at an unrelated place: inside a method's signature or a
  struct literal elsewhere in the file (ClayWind's desktop host: at `y: saturatingTruncate<u32>(…)` in another
  function). Reproduction: a library with `public struct Drag`, `public struct Point` and `public interface Source { fn
  drag(borrow this) -> Drag? => null; }`; an app that imports `lib.Source` and `lib.Point` only, with `struct Thing {
  a: u32; files: Drag?; fn create() -> Thing => Thing { a: 1, files: null }; }` and `impl Source for Thing { fn
  drag(borrow this) -> Drag? => this.files; }`: three N0204, one of them at `create`'s `-> Thing` (13:17).
- **M4 (fixed in ceesafec 0.55.0, ADR 0152). A git dependency must be the package at the repository's root.** tooling.md §4: "the dependency is the
  package at the checkout's root". ClayWind is one repository of several packages in subdirectories (`claywind/`,
  `claywind-fonts/`, `claywind-tasks/`, …), as most multi-package projects are, so an outside app (P3's gate, a chat
  client in its own repository, "built on ClayWind's released packages only") cannot name `claywind` at a tag: only
  path dependencies reach a subdirectory. **Proposed:** a git dependency may name a directory in the checkout
  (`{ git = …, tag = …, dir = "claywind" }`), and a path dependency inside a checkout is the same package as that
  checkout's directory named directly, so a package and its dependents in one repository resolve once.
- **M5. On macOS a window program can crash when a file drag ends, after a menu choice (ceesafec 0.54.0).** ClayWind's
  desktop host, after an item of its menu bar was chosen (System Events' `click menu item`) and a file was dropped on
  its window by a dragging source that exits when the drag ends, crashed in AppKit's drag teardown
  (`RemotePortInvalidationCallback` → `CoreDragTerminate` → `CoreDragEndDragging` → `objc_msgSend` on a dead object;
  heap corruption detected by the allocator without Guard Malloc). Under Guard Malloc it crashes in about one run of
  three; the drop alone, a second window, or a drop followed by a second window do not; a plain Cocoa window taking
  the same drag never does. Being investigated in CeeSafe's macOS runtime (ADRs 0146, 0150).

## N. Found while writing the chat client (roadmap P3's gate, 2026-11-15, ceesafec 0.54.0 and ADR 0152's branch)

- **N1 (fixed in ceesafec 0.55.0, ADR 0153: `Clock.localOffset(at)`). lib/std cannot tell the local time zone.** A chat client shows each message's time ("22:50"), but
  `Clock.now()` is UTC and §26.11 says "calendar dates, time zones and formatting are not part of the library" (ADR
  0055), so the client prints UTC times (`claywind-chat`'s `clock`): three hours off here (UTC+3). An app cannot work
  it out itself without reading the time zone database or calling C. **Proposed:** `Clock.localOffset(at: UnixTime) ->
  i32`, the seconds east of UTC that the system's time zone gives at `at` (daylight saving included): `localtime_r`'s
  `tm_gmtoff` on POSIX systems, `GetTimeZoneInformationForYear` and `SystemTimeToTzSpecificLocalTime` on Windows, 0 in
  the interpreter (for reproducible runs) and where the system has no zone. Dates and formatting can stay out.
- **N2 (fixed in ceesafec 0.55.0). On Windows, git dependencies cannot be
  fetched.** ceesafec ran `env GIT_TERMINAL_PROMPT=0 git …` (lib/std cannot set a child's environment, §26.9), and
  Windows has no `env`: every git dependency failed with `'"env"' is not recognized` (seen with 0.54.0 and ADR 0152's
  branch on the Windows 11 x64 laptop). Fixed: on Windows the variable is set by the batch file ceesafec already runs
  tools through (`set "GIT_TERMINAL_PROMPT=0"`), and a `%` in an argument there is doubled, so a percent-encoded URL is
  not read as a variable.
- **N3 (fixed in ceesafec 0.55.0). On Windows, a git dependency's checkout never matches `ceesafe.lock`.** Git for
  Windows converts checkouts to CRLF by default (`core.autocrlf=true` in its system config), so the files' hash differs
  from the lock's, written where they were LF: N0325, "its files changed". Fixed: ceesafec runs git with `-c
  core.autocrlf=false -c core.eol=lf`.
- **N4 (fixed in ceesafec 0.55.0). On Windows, a program whose first socket is `AsyncTcpStream.connect` cannot
  connect.** The connect creates its socket (`WSASocketW`) without starting Winsock, which only the blocking sockets,
  the listeners and the resolver did, so the chat client (which never listens) failed with "Input/output error"
  (WSANOTINITIALISED, unmapped). A server, which listens first, never met it. Fixed: the connect starts Winsock first.
  Checked on the laptop: connect-and-write, connect then `Async.blocking`, and connect-write-read on a thread of their
  own, all failing before and passing after; and the chat client live.

## O. Found while taking ClayWind to phones (roadmap P4, 2026-11-16, ceesafec 0.57.0 to 0.61.0)

- **O1 (fixed in ceesafec 0.58.0). `console_input_check.py` failed about one run in four.** The checker opened its
  FIFO's writing end, without blocking, before the interpreted program had opened the reading end (ENXIO). Met while
  running the contract suite for ADR 0156. Fixed in the tool: it tries again until the program has opened it.
- **O2 (fixed in ceesafec 0.60.0). Seven corpus HIR dumps changed whenever lib/std gained a function.** `--dump-hir`
  printed a closure with its function's id among every function, the libraries' included, so the dumps had to be
  regenerated for ADRs 0156, 0157 and 0158, for numbers alone. Fixed in the compiler: the number counts the program's
  own functions.
- **O3 (fixed in ceesafec 0.59.0 and 0.60.0). Phones had no text input and no file dialogs.** A text
  field could not be typed into (ADR 0157: the soft keyboard and the input method), and the demo's Open and Save did
  nothing, which the owner found on his iPhone and Android phone (ADR 0158: open as copies, `FileDialog.exportFile`).
- **O4 (fixed in ceesafec 0.60.0). `Directory.temporary()` failed on Android.** It looked in `TMPDIR`, which Android
  does not set for an app, then in `/tmp`, which Android does not have. Met when the demo's Save needed a file of its
  own to export. Fixed in the runtime: an app's temporary directory there is its cache folder.
- **O5 (fixed in ceesafec 0.60.0). A temporary directory's name began "ceesa", not "ceesafe-".** The runtime copied
  five bytes of the eight, from the first commit of the runtime. Seen in a listing of `/tmp`. Fixed.
- **O6 (open). A release's macOS job sometimes never starts.** For 0.58.0 and 0.59.0 one macOS job each (a seed
  check, then the x64 build) was cancelled after fifteen minutes with no step run, and the release was not published
  until the job was run again. Nothing in the tree is at fault as far as the logs show; a rerun of the failed jobs
  has worked both times; 0.60.0's jobs all started.
- **O7 (fixed in ceesafec 0.62.0). A phone app could not tell that it left the screen.** The system may end an app
  in the background without a word, so the demo lost its page and its text. ADR 0159: `Window.nextAppEvent()`
  (`Background`, `Foreground`, `MemoryWarning`) and `Directory.appData`. Tried by the owner on both phones.
- **O8 (fixed in ceesafec 0.62.0). `sendFile`'s copy pushed a byte at a time.** Where there is no sendfile (the
  interpreter always), lib/std filled each 64 KiB piece with 65,536 calls of `push`, so one corpus program took 24 s
  interpreted and timed out on a loaded machine. Met in the contract suite for ADR 0159. One `resize` now: 0.5 s.
- **O9 (fixed in part in 0.64.0; the rest is a redesign, shelved). The interpreter is slow at plain work.**
  `run-pass/ordered-map.cee` took 31 s interpreted (44 s with 0.33.0, so no regression) and could pass the corpus's
  60 s on a loaded machine. 37% of a profile was allocation: a value was two vectors, each grown a byte at a time.
  - **0.64.0**: a value's bytes are taken once; a value of 8 bytes or fewer has no memory of its own; an ended local
    of 16 bytes or fewer keeps its buffers for its next use. The same program, 0.63.0 against 0.64.0, run
    alternately: 33.4 and 29.9 s against 19.1 and 18.8 s. Allocation is about a tenth of the profile now.
  - **What is left** is the interpreter's own statement and expression code, and layouts worked out at run time.
    More than this needs each function decoded once into fixed slots: an ADR and a rewrite of its core, not asked
    for.
- **O10 (open). The compiler's own sources must keep to what ceesafec 0.8.0 accepts**, and only the release
  bootstrap says so: `??`, a `var` of a view and a slice of a call's result in `src/interpreter/io.cee` built with
  the current compiler and failed the bootstrap for 0.62.0, twice. A check in the contract suite would say it sooner.
- **O11 (fixed in ceesafec 0.62.1). A windowed program's `main` was on the stack
  twice on iOS.** UIKit never returns from the first call, and the launched application calls `main` again inside it
  (ADR 0154). With the entry inlined (`-Os`), `main`'s frame was 621 KiB in the GPU host: twice on the main thread's
  1 MiB, so the app ended at once on an iPhone (the desktop host's frame is smaller, so it ran). Fixed in the
  compiler: a windowed program runs in a function of its own, never inlined, and `main` keeps nothing.
- **O13 (ClayWind's part fixed; CeeSafe reports it since 0.64.0, ADR 0163 part 1). A function's frame holds several
  copies of a large value.** The GPU host's `main` was 745 KiB when a `Raster` was 123 KiB (its shaping and bidi
  buffers inline): with 0.62.1 (O11) that still overflowed an iPhone's 1 MiB main stack when built by ceesafec
  directly (Xcode's `-Os` build, 621 KiB, fitted). ClayWind's part: the two buffers are in the Raster's memory now, a
  `Raster` is about 3 KiB and that `main` 10 KiB; `claywind-pack` also links iOS apps with a 16 MiB stack.
  - **Corrected (2026-11-17).** This entry first said the frame held the value five times because the generated C
    passes it through five variables. That counted the C's variables, not the binary's slots. CeeSafe's ADR 0162
    (temporary forwarding) removes the pass-through ones, and measured: the same host's `main` is 745,408 bytes
    without it and 745,456 with it at `-O2`, because clang already forwards those. It is a fifth smaller only as a
    debug build.
  - **What the copies are** in a release build: the call's `Result`, the local its payload ends in, and the struct
    made from it by value. ADR 0163 measured hand-edited C: passing the value by pointer alone, or using the payload
    where it is alone, leaves three; both together leave two.
  - **0.64.0 (ADR 0163 part 1)**: `ceesafec build --explain-frames` lists each sync function whose values of 4 KiB
    or more add up to 64 KiB or more, and warning N0406 names one at 256 KiB. The host at `7315bac` gets the warning
    on `main` (estimate 370,552 bytes; 745,408 in the binary, where clang inlined two callees). Today's ClayWind has
    no warning; the report names `Raster.create` at 245,504 bytes, which takes the two text buffers by value before
    it puts them in its memory.
  - **Not done, by decision**: the two compiler changes (ADR 0163 parts 2 and 3). A survey of the compiler,
    ClayWind, the chat client, CeeSafe.Web and the HTTP client found the shape only here.
- **O14 (ClayWind's own, fixed). An iPhone's text was boxes.** The font reader took a cmap of the Unicode platform
  only with encoding 3 or 4; Helvetica Neue's faces say 0/1, so no character had a glyph. The demo hid it (its
  fallback fonts drew the text, with no bold); the chat client showed boxes, which the owner saw on his iPhone.
  Every encoding of the Unicode platform is read now; a test builds such a cmap.
- **O15 (fixed in ceesafec 0.63.0). A soft keyboard had no key that said it would go.** The Return key reached the
  program as Enter, and ClayWind's fields leave on Enter, but the key said "return". ADR 0161: `Window.setReturnKey`.
  Asked for by the owner on his iPhone.
- **O16 (ClayWind's own, fixed). A tap away from a field did not put the keyboard away;** then, fixed at the press,
  the keyboard's going moved the Send button from under the finger (a second tap was needed); then, fixed at the
  release, the keyboard blinked when Send focused the field again. A field leaves at the end of the frame the tap
  ends in, unless it is focused again. Each found by the owner on his phones; a test covers the three.
- **O17 (ClayWind's own, fixed). `aJobsValuesReachTheUi` failed once in three runs** ("expected 55, got 36") while
  the machine's load was above 40. The test's collector, not the library: it polled, found nothing, then saw the job
  had returned and gave up, without taking what the job sent in between. It takes that now. Twelve runs passed on a
  calm machine; a run under sixteen busy loops was stopped unfinished (the machine's load passed 200).
- **O12 (ClayWind's own). The GPU host had its own list of fonts**, without a phone's, so it ended before Metal
  started; the Simulator hid it by reading the Mac's fonts. It uses claywind-desktop's `loadSystemFonts` now.
