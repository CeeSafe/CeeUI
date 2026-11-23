# CeeUI for Rust

A Rust crate over CeeUI's C API (`../../include/ceeui.h`): a window laid out each frame by a closure, in
immediate mode, with `&str` text, `&mut` state and closures for nesting. `examples/counter.rs` is the C example in
Rust.

```rust
let mut count = 0;
let mut name = String::from("World");
ceeui::run("Counter", 480, 360, |f| {
    f.heading("Counter");
    f.panel(|f| {
        f.label(&format!("Pressed {count} times"));
        if f.button("add", "Add one") {
            count += 1;
        }
    });
    f.text_field("name", "Your name", &mut name, 128);
    f.label(&format!("Hello, {name}!"));
});
```

## Building

The crate links ceeui-capi's archive, so build that first:

```sh
cd ceeui-capi
ceesafec build                 # out/ceeui_capi.a
cd bindings/rust
cargo run --example counter
```

`build.rs` passes the archive to the linker by its path (it is `ceeui_capi.a`, not `libceeui_capi.a`);
`CEEUI_CAPI_ARCHIVE` names another one. On macOS it links AppKit, QuartzCore, CoreText and CoreGraphics.

## The API

- `ceeui::run(title, width, height, frame)` opens a window and runs it until it is closed (`RunResult`). It starts
  CeeSafe's runtime the first time. Call it on the main thread.
- `Frame`: `column`, `row` and `panel` take a closure for their children and end themselves; `spacer`, `label`,
  `caption`, `heading`; `button` (true in the frame after a press), `checkbox(id, label, &mut bool)`,
  `text_field(id, name, &mut String, max_bytes)`; `is_dark`, `set_dark` and `request_frame`.
- As in C, a control's result arrives in the frame after the user acted; CeeUI asks for that frame, and for one
  after it, so the closure's changes show at once. A `Frame` lives only for the closure's call.
- A panic in the closure does not unwind into C: it is caught, later frames draw nothing, and `run` returns
  `RunResult::Panicked` once the window is closed. (The C API has no call to close the window from the program.)
- `ceeui::sys` has the raw declarations.

## Status

Checked on macOS 27 with real mouse and key events: typed text reached the `String` and its greeting, Add one
counted, a step's checkbox ticked, the Dark checkbox switched the theme, and closing the window ended the program
with exit 0. Not checked: the panic path, Linux and Windows. rustfmt and clippy were not installed here, so the crate
has not been through them.
