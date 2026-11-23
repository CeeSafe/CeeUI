# The web

This page covers the web host, `claywind-web`: a ClayWind app as a WebAssembly module in a browser. It says how an
app is written for it, how it is built and served, what a page gets, and what is not there.

## The web host

The app is compiled to one 64-bit WebAssembly module. The software renderer draws it onto a canvas that fills the
browser's window, at the screen's pixel ratio. It redraws and shows only what changed, and sleeps while nothing
does.

| Browser | Runs it |
|---|---|
| Chrome | Yes |
| Firefox | Yes |
| Safari | Not yet: it does not run 64-bit WebAssembly |

The module's program is a loop that waits for the next frame. A page may not wait, so the module runs in a worker
of the page, and the two share memory. That is why the page must be cross-origin isolated: see
[Serving](#serving).

## An app for the web

A web app is a library package with `profile = "freestanding"` that depends on `claywind_web` by path. It exports
one function, `run`, which loops for as long as the page lives. This is the demo's, from
`claywind-hosts/web/src/main.cee`:

```ceesafe skip
@export("run")
fn run() -> i32 {
    var uiBlock = WebBlock.take(8388608);
    var rasterBlock = WebBlock.take(WEB_RASTER_BYTES);
    var damageBlock = WebBlock.take(4194304);
    let uiStorage = uiBlock.bytes();
    let rasterStorage = rasterBlock.bytes();
    let damageStorage = damageBlock.bytes();
    if (uiStorage.length == 0 || rasterStorage.length == 0 || damageStorage.length == 0) {
        return 0;
    }
    let uiMemory = FixedBufferAllocator.over(mut uiStorage);
    let rasterMemory = FixedBufferAllocator.over(mut rasterStorage);
    let damageMemory = FixedBufferAllocator.over(mut damageStorage);
    var ui = match (Ui.create(uiMemory, UiCapacity.of(2048))) {
        Ok(u) => u,
        Err(_) => { return 0; }
    };
    var raster = match (Raster.create(rasterMemory, webRasterCapacity())) {
        Ok(r) => r,
        Err(_) => { return 0; }
    };
    var damage = match (Damage.create(damageMemory, 16384)) {
        Ok(d) => d,
        Err(_) => { return 0; }
    };
    let fonts = WebFonts.open();
    if (!raster.loadFont(FONT_18, fonts.font(0)) || !raster.loadFont(FONT_BOLD, fonts.font(1)) ||
        !raster.loadFont(FONT_MONO, fonts.font(2))) {
        return 0;
    }
    var platform = WebPlatform.open();
    var app = DemoApp.create();
    ui.setDarkMode(true);
    ui.setSecondaryIsCommand(platform.apple);
    loop {
        platform.beginFrame(mut ui);
        ui.begin(mut platform);
        platform.editInto(mut ui);
        demoFrame(mut ui, mut app, raster);
        match (ui.endFrame(mut platform, raster)) {
            Ok() => { platform.show(ui, mut raster, mut damage, Color.hex(pageBg(ui))); }
            Err(_) => {} // a frame that does not fit the Ui's capacity is not shown
        }
        platform.finishFrame(ui);
    }
}
```

| Call | What it does |
|---|---|
| `WebBlock.take(size)` | Takes `size` bytes of the module's memory, for a `FixedBufferAllocator`. The memory is fixed when the module is built and nothing is given back, so an app takes its blocks once, as it starts |
| `WebFonts.open()`, `fonts.font(index)` | The fonts the page was given, in the order of `--font`. A font past the last is empty |
| `webRasterCapacity()` | A `Raster`'s capacity for the web: frames up to 2880 by 1800 pixels, and room for fallback fonts. It needs `WEB_RASTER_BYTES` of memory |
| `WebPlatform.open()` | The platform of the page's canvas. Its `apple` field is true on an Apple system |
| `platform.beginFrame(ui)` | Before `ui.begin`: waits for the browser's next frame and takes the events the page queued |
| `platform.editInto(ui)` | After `ui.begin`: the text for a Paste, and what an input method is composing |
| `platform.show(ui, raster, damage, clear)` | After a frame that fit: draws what changed and shows only that |
| `platform.finishFrame(ui)` | After the frame: the clipboard, the pointer's shape, the text input, then a sleep while nothing changes |

A canvas larger than 2880 by 1800 pixels at its pixel ratio is drawn at a lower ratio.

## Building

`claywind-pack web`, run in the app's package directory, packs the app into `out/pack/web`:

```sh
cd claywind-hosts/web
../../claywind-pack/out/claywind_pack web --name "ClayWind" \
    --font Arial.ttf --font "Arial Bold.ttf" --font "Courier New.ttf" \
    --fallback "Arial Unicode.ttf"
```

| Option | What it does |
|---|---|
| `--name NAME` | The page's title |
| `--font FILE` | A font the app loads. Give it once for each font, in the order the app asks for them |
| `--fallback FILE` | A font to fetch when text has a character the app's fonts lack. Give it once for each, in the order they are tried |

It runs `ceesafec` to get the package's C, compiles and links that for 64-bit WebAssembly, and puts the web host's
page beside the module:

| File in `out/pack/web` | What it is |
|---|---|
| `web.wasm` | The app |
| `index.html`, `web.js`, `worker.js` | The page, and the worker that runs the module |
| `fonts/` and `app.json` | The fonts, and the list of them in the app's order |
| `serve.py` | A server for the directory |

It needs a clang with the `wasm64` target and `wasm-ld`. The clang is the one named by `CLANG`, else Homebrew's
llvm, else `clang`. The linker is the one named by `WASM_LD`, else `wasm-ld`.

For the demo, `claywind-hosts/web/build.sh` builds `claywind-pack` and runs it with the demo's three fonts and a
fallback. `FONT_DIR` says where the fonts are.

```sh
cd claywind-hosts/web
sh build.sh
```

[Packing](./tools#packing) has `claywind-pack`'s other targets.

## Serving

```sh
python3 out/pack/web/serve.py          # then open http://localhost:8017/
python3 out/pack/web/serve.py 9000     # another port
```

`serve.py` serves its directory on 127.0.0.1 with the two headers that make a page cross-origin isolated, which
shared memory needs:

| Header | Value |
|---|---|
| `Cross-Origin-Opener-Policy` | `same-origin` |
| `Cross-Origin-Embedder-Policy` | `require-corp` |

Any server that sends these two headers can serve the directory. `serve.py` also sends `Cache-Control: no-store`
and serves `.wasm` as `application/wasm`.

## What a page gets

### Input

The app has the pointer, the wheel, the keyboard, touches, the pointer's shape, and copy, cut and paste with the
system's clipboard.

- A finger is a touch, for the `Ui`'s gestures. The first finger is the pointer too.
- The page keeps its own shortcuts, such as reload and the developer tools. Every other key is the app's.
- A copy or a cut made by a shortcut is put on the clipboard inside the browser's own event. A copy the page did not
  ask for, such as a button's, may be refused by the browser without a recent key or click.

### Text fields

Each of the app's text fields is a text input of the page, unseen, where the field is drawn. It holds the field's
text, caret and selection. While a field has the app's focus, its input has the page's.

- A hardware key is typed into the app directly and never reaches the input.
- A phone shows its keyboard for the input. The keyboard is of the field's kind and has its Return key
  (`ui.reportTextKind` and `ui.reportReturnKey`: see [Phones](./phones#the-soft-keyboard)).
- An input method composes in the input. What it composes shows in the field as the `Ui`'s composition, and its
  result is typed text. A dead key's result comes the same way.
- A screen reader reads the field there, by character as the caret moves.

### Fallback fonts

The fonts given with `--fallback` are not loaded with the page. When a frame's text has a character the app's fonts
lack, the next fallback font is fetched and added to the renderer's fallback chain, until one has the character.
Text in the app's own fonts fetches nothing.

### Screen readers

A canvas is pixels, so the page keeps the app's elements as unseen elements of its own over the canvas, in reading
order. Each has its role, name, state and place. A reader's press, focus and slider step go to the app, and the
app's focus is the page's.

Lists, menus, tabs and the data grid are ARIA's containers: a listbox of options, a menu, a tablist, a grid of rows
of gridcells under columnheaders. A virtual list gives its places and counts.

[Accessibility](./accessibility) says what an app reports and how.

## What is not there

- Colour emoji.
- The system's own fonts: the app has the fonts it was packed with.
- File dialogs.
- Drag and drop from outside the page.
- A second window.
- Safari, until it runs 64-bit WebAssembly.
