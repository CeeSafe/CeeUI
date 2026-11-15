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
- **J2 (fixed on CeeSafe branch `fix/mingw-system-libs`, awaiting a release). With GCC on Windows (MinGW), a program
  that uses the runtime's sockets or windows does not link.** Fixed at the source: ceesafec passes `-lws2_32 -lgdi32
  -lbcrypt` to gcc on Windows (tooling.md §8.3), the runtime builds without gcc's warnings (the three below), and the
  release workflow builds and runs a link check with MinGW's gcc. Met again with 0.50.0 building the desktop host on
  the x64 laptop; a compiler built from the branch builds it with no manifest change. `claywind-hosts/gpu`'s
  `[target.windows] links` goes once a release has it. The report as made: The
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
    `-Wmaybe-uninitialized`). The first two are fixed on the branch; the third is a false positive (a whole-struct
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
