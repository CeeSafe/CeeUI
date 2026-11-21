// SPDX-License-Identifier: MIT
//! ClayWind for Rust, over ClayWind's C API (claywind.h). A program runs a window with a closure that lays out each
//! frame from its state, in immediate mode:
//!
//! ```no_run
//! let mut count = 0;
//! claywind::run("Counter", 480, 320, |f| {
//!     f.heading("Counter");
//!     if f.button("add", "Add one") {
//!         count += 1;
//!     }
//!     f.label(&format!("Pressed {count} times"));
//! });
//! ```
//!
//! A control's result arrives in the frame after the user acted, as in C; ClayWind asks for that frame, and for one
//! after it, so what the closure changes shows at once. Columns, rows and panels take a closure for their children
//! and end themselves. A `Frame` lives only for the closure's call.

pub mod sys;

use std::ffi::c_void;
use std::marker::PhantomData;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::Once;

/// Why [`run`] returned.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RunResult {
    /// The window was closed.
    Closed,
    /// No window could be opened.
    NoWindow,
    /// Not enough memory for the UI or the renderer.
    NoMemory,
    /// No system fonts were found (set CLAYWIND_FONT_DIR).
    NoFonts,
    /// A frame had more elements than the UI holds.
    FrameTooLarge,
    /// The window refused a frame.
    WindowRefused,
    /// CeeSafe's runtime did not start.
    NoRuntime,
    /// The frame closure panicked; the window was closed.
    Panicked,
}

impl RunResult {
    fn from_code(code: i32) -> RunResult {
        match code {
            0 => RunResult::Closed,
            1 => RunResult::NoWindow,
            2 => RunResult::NoMemory,
            3 => RunResult::NoFonts,
            4 => RunResult::FrameTooLarge,
            _ => RunResult::WindowRefused,
        }
    }
}

/// A frame being laid out: what the frame closure is given. It lives only for that call.
pub struct Frame<'a> {
    raw: *mut sys::ClaywindFrame,
    _call: PhantomData<&'a mut ()>,
}

impl<'a> Frame<'a> {
    // ---- layout

    /// Children top to bottom, `gap` points apart.
    pub fn column(&mut self, gap: f32, children: impl FnOnce(&mut Self)) {
        unsafe { sys::claywind_column(self.raw, gap) };
        children(self);
        unsafe { sys::claywind_end(self.raw) };
    }

    /// Children left to right, centred across, `gap` points apart.
    pub fn row(&mut self, gap: f32, children: impl FnOnce(&mut Self)) {
        unsafe { sys::claywind_row(self.raw, gap) };
        children(self);
        unsafe { sys::claywind_end(self.raw) };
    }

    /// A raised, rounded panel around its children.
    pub fn panel(&mut self, children: impl FnOnce(&mut Self)) {
        unsafe { sys::claywind_panel(self.raw) };
        children(self);
        unsafe { sys::claywind_end(self.raw) };
    }

    /// Space that takes what a row or column has left.
    pub fn spacer(&mut self) {
        unsafe { sys::claywind_spacer(self.raw) };
    }

    // ---- text

    pub fn label(&mut self, text: &str) {
        unsafe { sys::claywind_label(self.raw, text.as_ptr(), text.len()) };
    }

    pub fn caption(&mut self, text: &str) {
        unsafe { sys::claywind_caption(self.raw, text.as_ptr(), text.len()) };
    }

    pub fn heading(&mut self, text: &str) {
        unsafe { sys::claywind_heading(self.raw, text.as_ptr(), text.len()) };
    }

    // ---- controls

    /// A button named `id` (unique in the frame); true in the frame after it was pressed.
    pub fn button(&mut self, id: &str, label: &str) -> bool {
        unsafe { sys::claywind_button(self.raw, id.as_ptr(), id.len(), label.as_ptr(), label.len()) }
    }

    /// A checkbox over `on`; when the user flipped it in the last frame, `on` takes the new state and the result is
    /// true.
    pub fn checkbox(&mut self, id: &str, label: &str, on: &mut bool) -> bool {
        unsafe { sys::claywind_checkbox(self.raw, id.as_ptr(), id.len(), label.as_ptr(), label.len(), on) }
    }

    /// A text field over `text`, of at most `max_bytes` bytes of UTF-8; when the user changed it in the last frame,
    /// `text` takes the new text and the result is true. `name` is what a screen reader calls it.
    pub fn text_field(&mut self, id: &str, name: &str, text: &mut String, max_bytes: usize) -> bool {
        let mut buffer = vec![0u8; max_bytes.max(1)];
        let mut length = text.len().min(buffer.len());
        while !text.is_char_boundary(length) {
            length -= 1;
        }
        buffer[..length].copy_from_slice(&text.as_bytes()[..length]);
        let changed = unsafe {
            sys::claywind_text_field(
                self.raw,
                id.as_ptr(),
                id.len(),
                name.as_ptr(),
                name.len(),
                buffer.as_mut_ptr(),
                buffer.len(),
                &mut length,
            )
        };
        if changed {
            // ClayWind writes whole characters of UTF-8; anything else would be its bug, so it is replaced, not
            // trusted.
            *text = String::from_utf8_lossy(&buffer[..length.min(buffer.len())]).into_owned();
        }
        changed
    }

    // ---- the window

    /// Whether the UI is in its dark theme.
    pub fn is_dark(&self) -> bool {
        unsafe { sys::claywind_is_dark(self.raw) }
    }

    /// The dark theme on or off, from this frame on.
    pub fn set_dark(&mut self, dark: bool) {
        unsafe { sys::claywind_set_dark(self.raw, dark) };
    }

    /// Asks for another frame soon, for state that changed outside the controls (a timer, a download).
    pub fn request_frame(&mut self) {
        unsafe { sys::claywind_request_frame(self.raw) };
    }
}

/// The C API's version (major * 10000 + minor * 100 + patch).
pub fn version() -> i32 {
    unsafe { sys::claywind_version() }
}

static RUNTIME: Once = Once::new();
static mut RUNTIME_STARTED: bool = false;

/// Starts CeeSafe's runtime once; whether it is running.
fn start_runtime() -> bool {
    RUNTIME.call_once(|| {
        let config = sys::CeesafeConfig {
            size: std::mem::size_of::<sys::CeesafeConfig>() as u32,
            flags: sys::CEESAFE_INIT_ENVIRONMENT,
            argc: 0,
            argv: std::ptr::null(),
            fail_alloc_at: 0,
            alloc_report: false,
            allocator: std::ptr::null(),
            panic_hook: std::ptr::null(),
            panic_context: std::ptr::null_mut(),
        };
        let started = unsafe { sys::ceesafe_init(&config) };
        unsafe { RUNTIME_STARTED = started == 0 || started == sys::CEESAFE_INIT_AGAIN };
    });
    unsafe { RUNTIME_STARTED }
}

/// The closure and whether it has panicked, behind the C API's `user` pointer.
struct Host<'f> {
    body: &'f mut dyn FnMut(&mut Frame),
    panicked: bool,
}

unsafe extern "C" fn frame_callback(frame: *mut sys::ClaywindFrame, user: *mut c_void) {
    if frame.is_null() || user.is_null() {
        return;
    }
    let host = &mut *(user as *mut Host);
    if host.panicked {
        return;
    }
    let mut f = Frame { raw: frame, _call: PhantomData };
    // A panic must not unwind into C: it is caught, and the frames after it draw nothing until run returns.
    if catch_unwind(AssertUnwindSafe(|| (host.body)(&mut f))).is_err() {
        host.panicked = true;
    }
}

/// Opens a window, `width` x `height` points, and runs it until it is closed, calling `frame` for each frame. Call
/// it on the main thread (on macOS the window must be there). It starts CeeSafe's runtime the first time.
pub fn run(title: &str, width: i32, height: i32, mut frame: impl FnMut(&mut Frame)) -> RunResult {
    if !start_runtime() {
        return RunResult::NoRuntime;
    }
    let mut host = Host { body: &mut frame, panicked: false };
    let code = unsafe {
        sys::claywind_run(
            title.as_ptr(),
            title.len(),
            width,
            height,
            Some(frame_callback),
            &mut host as *mut Host as *mut c_void,
        )
    };
    if host.panicked {
        return RunResult::Panicked;
    }
    RunResult::from_code(code)
}
