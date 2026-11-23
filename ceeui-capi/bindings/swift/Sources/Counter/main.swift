// SPDX-License-Identifier: MIT
// The C example (examples/counter.c) in Swift: a counter, a theme switch, a greeting typed into a text field and a
// short list, laid out each frame from the program's state.

import CeeUI

var count = 0
var dark = true
var name = "World"
var done = [true, false, false]
let tasks = ["Build ceeui-capi", "Link it from C", "Call it from Swift"]

let result = CeeUI.run(title: "CeeUI from Swift", width: 520, height: 520) { f in
    f.isDark = dark
    f.row(gap: 12) {
        f.heading("CeeUI from Swift")
        f.spacer()
        f.checkbox("dark", "Dark", isOn: &dark)
    }
    f.caption("Each frame is laid out by a Swift closure; CeeUI draws it.")
    f.panel {
        f.label("Pressed \(count) time\(count == 1 ? "" : "s")")
        f.row {
            if f.button("add", "Add one") { count += 1 }
            if f.button("reset", "Reset") { count = 0 }
        }
    }
    f.panel {
        f.caption("Your name")
        f.textField("name", name: "Your name", text: &name)
        if !name.isEmpty { f.label("Hello, \(name)!") }
    }
    f.panel {
        f.label("Steps")
        for k in tasks.indices {
            f.checkbox("task-\(k)", tasks[k], isOn: &done[k])
        }
    }
}
if result != .closed {
    print("counter: CeeUI stopped (\(result))")
}
