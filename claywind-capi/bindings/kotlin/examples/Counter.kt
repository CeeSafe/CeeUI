// SPDX-License-Identifier: MIT
// The C example (examples/counter.c) in Kotlin: a counter, a theme switch, a greeting typed into a text field and a
// short list, laid out each frame from the program's state. build.sh builds and runs it.

import org.claywind.ClayWind
import org.claywind.RunResult
import kotlin.system.exitProcess

fun main() {
    var count = 0
    var dark = true
    var name = "World"
    val done = booleanArrayOf(true, false, false)
    val tasks = listOf("Build claywind-capi", "Link it from C", "Call it from Kotlin")

    val result = ClayWind.run("ClayWind from Kotlin", 520, 520) {
        isDark = dark
        row(gap = 12f) {
            heading("ClayWind from Kotlin")
            spacer()
            dark = checkbox("dark", "Dark", dark)
        }
        caption("Each frame is laid out by a Kotlin function; ClayWind draws it.")
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
    if (result != RunResult.Closed) {
        System.err.println("counter: ClayWind stopped ($result)")
        exitProcess(1)
    }
}
