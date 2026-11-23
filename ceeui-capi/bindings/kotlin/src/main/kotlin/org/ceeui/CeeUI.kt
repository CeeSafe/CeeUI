// SPDX-License-Identifier: MIT
// CeeUI for Kotlin, over the C API (ceeui.h) through JNI (src/main/c/ceeui_jni.c). A program runs a window
// with a function that lays out each frame from its state, in immediate mode:
//
//     var count = 0
//     CeeUI.run("Counter", 480, 320) {
//         heading("Counter")
//         if (button("add", "Add one")) count++
//         label("Pressed $count times")
//     }
//
// A control's result arrives in the frame after the user acted, as in C; CeeUI asks for that frame, and for one
// after it, so the function's changes show at once. A checkbox and a text field return their value, the user's change
// included: `dark = checkbox("dark", "Dark", dark)`. A Frame is valid only during the function's call.
//
// On macOS the JVM must run main on the process's first thread, where AppKit's window lives: java -XstartOnFirstThread.

package org.ceeui

/** The JNI functions (ceeui_jni.c); text is UTF-8 in byte arrays. */
internal object Native {
    init {
        System.loadLibrary("ceeui_jni")
    }

    @JvmStatic external fun start(): Int
    @JvmStatic external fun version(): Int
    @JvmStatic external fun run(title: ByteArray, width: Int, height: Int, callback: FrameCallback): Int
    @JvmStatic external fun column(frame: Long, gap: Float)
    @JvmStatic external fun row(frame: Long, gap: Float)
    @JvmStatic external fun panel(frame: Long)
    @JvmStatic external fun spacer(frame: Long)
    @JvmStatic external fun end(frame: Long)
    @JvmStatic external fun text(frame: Long, kind: Int, text: ByteArray)
    @JvmStatic external fun button(frame: Long, id: ByteArray, label: ByteArray): Boolean
    @JvmStatic external fun checkbox(frame: Long, id: ByteArray, label: ByteArray, value: Boolean): Int
    @JvmStatic external fun textField(frame: Long, id: ByteArray, name: ByteArray, buffer: ByteArray, length: Int): Int
    @JvmStatic external fun isDark(frame: Long): Boolean
    @JvmStatic external fun setDark(frame: Long, dark: Boolean)
    @JvmStatic external fun requestFrame(frame: Long)
}

/** What the JNI side calls for each frame. */
internal fun interface FrameCallback {
    fun frame(handle: Long)
}

/** Why [CeeUI.run] returned. */
enum class RunResult {
    Closed, NoWindow, NoMemory, NoFonts, FrameTooLarge, WindowRefused,
    /** CeeSafe's runtime did not start. */
    NoRuntime,
    /** The frame function threw; it was printed, and the window drew nothing more until it was closed. */
    Threw,
}

/** A frame being laid out: the receiver of the frame function, valid only during its call. */
class Frame internal constructor(private val handle: Long) {
    // ---- layout

    /** Children top to bottom, [gap] points apart. */
    fun column(gap: Float = 8f, children: Frame.() -> Unit) {
        Native.column(handle, gap)
        children()
        Native.end(handle)
    }

    /** Children left to right, centred across, [gap] points apart. */
    fun row(gap: Float = 8f, children: Frame.() -> Unit) {
        Native.row(handle, gap)
        children()
        Native.end(handle)
    }

    /** A raised, rounded panel around its children. */
    fun panel(children: Frame.() -> Unit) {
        Native.panel(handle)
        children()
        Native.end(handle)
    }

    /** Space that takes what a row or column has left. */
    fun spacer() = Native.spacer(handle)

    // ---- text

    fun label(text: String) = Native.text(handle, 0, text.encodeToByteArray())
    fun caption(text: String) = Native.text(handle, 1, text.encodeToByteArray())
    fun heading(text: String) = Native.text(handle, 2, text.encodeToByteArray())

    // ---- controls

    /** A button named [id] (unique in the frame); true in the frame after it was pressed. */
    fun button(id: String, label: String): Boolean =
        Native.button(handle, id.encodeToByteArray(), label.encodeToByteArray())

    /** A checkbox showing [checked]; its state, flipped when the user flipped it in the last frame. */
    fun checkbox(id: String, label: String, checked: Boolean): Boolean =
        (Native.checkbox(handle, id.encodeToByteArray(), label.encodeToByteArray(), checked) and 1) != 0

    /**
     * A text field showing [text], of at most [maxBytes] bytes of UTF-8; its text, the user's when they changed it in
     * the last frame. [name] is what a screen reader calls it.
     */
    fun textField(id: String, name: String, text: String, maxBytes: Int = 4096): String {
        val buffer = ByteArray(maxOf(maxBytes, 1))
        val utf8 = text.encodeToByteArray()
        var length = minOf(utf8.size, buffer.size)
        while (length > 0 && length < utf8.size && (utf8[length].toInt() and 0xC0) == 0x80) length-- // a whole character
        utf8.copyInto(buffer, 0, 0, length)
        val changed = Native.textField(handle, id.encodeToByteArray(), name.encodeToByteArray(), buffer, length)
        return if (changed < 0) text else buffer.decodeToString(0, changed)
    }

    // ---- the window

    /** Whether the UI is in its dark theme; setting it changes the theme from this frame on. */
    var isDark: Boolean
        get() = Native.isDark(handle)
        set(value) = Native.setDark(handle, value)

    /** Asks for another frame soon, for state that changed outside the controls (a timer, a download). */
    fun requestFrame() = Native.requestFrame(handle)
}

object CeeUI {
    /** The C API's version (major * 10000 + minor * 100 + patch). */
    val version: Int get() = Native.version()

    /**
     * Opens a window, [width] x [height] points, and runs it until it is closed, calling [frame] for each frame. Call
     * it on the main thread (on macOS, java -XstartOnFirstThread). It starts CeeSafe's runtime the first time.
     */
    fun run(title: String, width: Int, height: Int, frame: Frame.() -> Unit): RunResult {
        if (Native.start() != 0) return RunResult.NoRuntime
        val result = Native.run(title.encodeToByteArray(), width, height) { handle -> Frame(handle).frame() }
        return when (result) {
            0 -> RunResult.Closed
            1 -> RunResult.NoWindow
            2 -> RunResult.NoMemory
            3 -> RunResult.NoFonts
            4 -> RunResult.FrameTooLarge
            -2 -> RunResult.Threw
            else -> RunResult.WindowRefused
        }
    }
}
