// SPDX-License-Identifier: MIT
// The C example (examples/counter.c) in Kotlin: a counter, a theme switch, a greeting typed into a text field and a
// short list, laid out each frame from the program's state. build.sh builds and runs it.

package org.ceeui.counter

import org.ceeui.CeeUI
import org.ceeui.RunResult
import kotlin.system.exitProcess

fun main() {
    val result = counter()
    if (result != RunResult.Closed) {
        System.err.println("counter: CeeUI stopped ($result)")
        exitProcess(1)
    }
}

/** The counter's window, until it is closed: the desktop's `main` runs it, and on Android the activity (android/). */
fun counter(): RunResult {
    var count = 0
    var dark = true
    var name = "World"
    val done = booleanArrayOf(true, false, false)
    val tasks = listOf("Build ceeui-capi", "Link it from C", "Call it from Kotlin")

    return CeeUI.run("CeeUI from Kotlin", 520, 520) {
        isDark = dark
        row(gap = 12f) {
            heading("CeeUI from Kotlin")
            spacer()
            dark = checkbox("dark", "Dark", dark)
        }
        caption("Each frame is laid out by a Kotlin function; CeeUI draws it.")
        panel {
            label("Pressed $count time${if (count == 1) "" else "s"}")
            row {
                if (button("add", "Add one")) count++
                if (button("reset", "Reset")) count = 0
            }
        }
        panel {
            caption("Your name")
            name = textField("name", "Your name", name, maxBytes = 128)
            if (name.isNotEmpty()) label("Hello, $name!")
        }
        panel {
            label("Steps")
            tasks.forEachIndexed { k, task -> done[k] = checkbox("task-$k", task, done[k]) }
        }
    }
}
