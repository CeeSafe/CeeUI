// SPDX-License-Identifier: MIT
// ClayWind for Swift, over the C API (claywind.h). A program runs a window with a closure that lays out each frame
// from its state, in immediate mode:
//
//     var count = 0
//     ClayWind.run(title: "Counter", width: 480, height: 320) { f in
//         f.heading("Counter")
//         if f.button("add", "Add one") { count += 1 }
//         f.label("Pressed \(count) times")
//     }
//
// A control's result arrives in the frame after the user acted, as in C. Columns, rows and panels take a closure for
// their children and end themselves. A Frame is valid only during the closure's call.

import CClayWind

/// Why `ClayWind.run` returned.
public enum RunResult: Int32 {
    case closed = 0
    case noWindow = 1
    case noMemory = 2
    case noFonts = 3
    case frameTooLarge = 4
    case windowRefused = 5
    /// CeeSafe's runtime did not start (ceesafe_init failed).
    case noRuntime = -1
}

/// A frame being laid out: what the frame closure is given.
public struct Frame {
    let raw: OpaquePointer

    // ---- layout

    /// Children top to bottom, `gap` points apart.
    public func column(gap: Float = 8, _ children: () -> Void) {
        claywind_column(raw, gap)
        children()
        claywind_end(raw)
    }

    /// Children left to right, centred across, `gap` points apart.
    public func row(gap: Float = 8, _ children: () -> Void) {
        claywind_row(raw, gap)
        children()
        claywind_end(raw)
    }

    /// A raised, rounded panel around its children.
    public func panel(_ children: () -> Void) {
        claywind_panel(raw)
        children()
        claywind_end(raw)
    }

    /// Space that takes what a row or column has left.
    public func spacer() {
        claywind_spacer(raw)
    }

    // ---- text

    public func label(_ text: String) {
        utf8(text) { claywind_label(raw, $0, $1) }
    }

    public func caption(_ text: String) {
        utf8(text) { claywind_caption(raw, $0, $1) }
    }

    public func heading(_ text: String) {
        utf8(text) { claywind_heading(raw, $0, $1) }
    }

    // ---- controls

    /// A button named `id` (unique in the frame); true in the frame after it was pressed.
    public func button(_ id: String, _ label: String) -> Bool {
        utf8(id) { i, il in utf8(label) { l, ll in claywind_button(raw, i, il, l, ll) } }
    }

    /// A checkbox over `isOn`; when the user flipped it in the last frame, `isOn` takes the new state and the result
    /// is true.
    @discardableResult
    public func checkbox(_ id: String, _ label: String, isOn: inout Bool) -> Bool {
        var value = isOn
        let changed = utf8(id) { i, il in utf8(label) { l, ll in claywind_checkbox(raw, i, il, l, ll, &value) } }
        isOn = value
        return changed
    }

    /// A text field over `text`; when the user changed it in the last frame, `text` takes the new text and the result
    /// is true. `name` is what a screen reader calls it; the text holds at most `maxBytes` bytes of UTF-8.
    @discardableResult
    public func textField(_ id: String, name: String, text: inout String, maxBytes: Int = 4096) -> Bool {
        var buffer = [UInt8](repeating: 0, count: max(maxBytes, 1))
        var length = 0
        for b in text.utf8.prefix(buffer.count) {
            buffer[length] = b
            length += 1
        }
        let capacity = buffer.count
        let changed = buffer.withUnsafeMutableBufferPointer { b in
            utf8(id) { i, il in
                utf8(name) { n, nl in claywind_text_field(raw, i, il, n, nl, b.baseAddress, capacity, &length) }
            }
        }
        if changed {
            text = String(decoding: buffer[0..<length], as: UTF8.self)
        }
        return changed
    }

    // ---- the window

    /// Whether the UI is in its dark theme; setting it changes the theme from this frame on.
    public var isDark: Bool {
        get { claywind_is_dark(raw) }
        nonmutating set { claywind_set_dark(raw, newValue) }
    }

    /// Asks for another frame soon, for state that changed outside the controls (a timer, a download).
    public func requestFrame() {
        claywind_request_frame(raw)
    }
}

/// `text`'s UTF-8 bytes as a pointer and a length, for the call.
@inline(__always)
func utf8<R>(_ text: String, _ body: (UnsafePointer<UInt8>?, Int) -> R) -> R {
    var copy = text
    return copy.withUTF8 { body($0.baseAddress, $0.count) }
}

/// The frame closure behind the C API's `user` pointer.
final class FrameBox {
    let body: (Frame) -> Void
    init(_ body: @escaping (Frame) -> Void) { self.body = body }
}

public enum ClayWind {
    /// The C API's version (major * 10000 + minor * 100 + patch).
    public static var version: Int32 { claywind_version() }

    /// Opens a window and runs it until it is closed, calling `frame` for each frame. Call it on the main thread. It
    /// starts CeeSafe's runtime the first time.
    ///
    /// On iOS the window lives in the application, which `run` starts (CeeSafe ADR 0200): the first call runs it and
    /// does not return, and the launched application runs the program's top-level code again, where `run` goes on.
    /// So what `main.swift` does before `run` happens twice on iOS: keep it to declarations.
    @discardableResult
    public static func run(title: String, width: Int32, height: Int32, frame: @escaping (Frame) -> Void) -> RunResult {
        ceesafe_window_start()
        if !runtimeStarted {
            var config = ceesafe_config()
            config.size = UInt32(MemoryLayout<ceesafe_config>.size)
            config.flags = UInt32(CEESAFE_INIT_ENVIRONMENT)
            let started = ceesafe_init(&config)
            if started != 0 && started != CEESAFE_INIT_AGAIN {
                return .noRuntime
            }
            runtimeStarted = true
        }
        let box = Unmanaged.passRetained(FrameBox(frame))
        defer { box.release() }
        let callback: claywind_frame_fn = { handle, user in
            guard let handle = handle, let user = user else { return }
            Unmanaged<FrameBox>.fromOpaque(user).takeUnretainedValue().body(Frame(raw: handle))
        }
        let result = utf8(title) { t, tl in claywind_run(t, tl, width, height, callback, box.toOpaque()) }
        return RunResult(rawValue: result) ?? .windowRefused
    }

    nonisolated(unsafe) static var runtimeStarted = false
}
