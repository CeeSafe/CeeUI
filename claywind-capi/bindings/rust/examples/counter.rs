// SPDX-License-Identifier: MIT
// The C example (examples/counter.c) in Rust: a counter, a theme switch, a greeting typed into a text field and a
// short list, laid out each frame from the program's state. `cargo run --example counter`.

fn main() {
    let mut count = 0;
    let mut dark = true;
    let mut name = String::from("World");
    let mut done = [true, false, false];
    let tasks = ["Build claywind-capi", "Link it from C", "Call it from Rust"];

    let result = claywind::run("ClayWind from Rust", 520, 520, |f| {
        f.set_dark(dark);
        f.row(12.0, |f| {
            f.heading("ClayWind from Rust");
            f.spacer();
            f.checkbox("dark", "Dark", &mut dark);
        });
        f.caption("Each frame is laid out by a Rust closure; ClayWind draws it.");
        f.panel(|f| {
            f.label(&format!("Pressed {count} time{}", if count == 1 { "" } else { "s" }));
            f.row(8.0, |f| {
                if f.button("add", "Add one") {
                    count += 1;
                }
                if f.button("reset", "Reset") {
                    count = 0;
                }
            });
        });
        f.panel(|f| {
            f.caption("Your name");
            f.text_field("name", "Your name", &mut name, 128);
            if !name.is_empty() {
                f.label(&format!("Hello, {name}!"));
            }
        });
        f.panel(|f| {
            f.label("Steps");
            for (k, task) in tasks.iter().enumerate() {
                f.checkbox(&format!("task-{k}"), task, &mut done[k]);
            }
        });
    });
    if result != claywind::RunResult::Closed {
        eprintln!("counter: ClayWind stopped ({result:?})");
        std::process::exit(1);
    }
}
