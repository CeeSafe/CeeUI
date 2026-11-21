// SPDX-License-Identifier: MIT
//! The C API as declared in claywind.h, and CeeSafe's runtime start (ceesafe/embed.h).

use std::ffi::c_void;

/// A frame being built (claywind_frame): opaque.
#[repr(C)]
pub struct ClaywindFrame {
    _private: [u8; 0],
}

pub type FrameFn = unsafe extern "C" fn(frame: *mut ClaywindFrame, user: *mut c_void);

/// ceesafe_config (ceesafe/embed.h), field for field: ceesafe_init knows it by its size.
#[repr(C)]
pub struct CeesafeConfig {
    pub size: u32,
    pub flags: u32,
    pub argc: i32,
    pub argv: *const *const u8,
    pub fail_alloc_at: u64,
    pub alloc_report: bool,
    pub allocator: *const c_void,
    pub panic_hook: *const c_void,
    pub panic_context: *mut c_void,
}

pub const CEESAFE_INIT_ENVIRONMENT: u32 = 1;
pub const CEESAFE_INIT_AGAIN: i32 = 1;

extern "C" {
    pub fn ceesafe_init(config: *const CeesafeConfig) -> i32;

    pub fn claywind_version() -> i32;
    pub fn claywind_run(
        title: *const u8,
        title_length: usize,
        width: i32,
        height: i32,
        frame: Option<FrameFn>,
        user: *mut c_void,
    ) -> i32;

    pub fn claywind_column(frame: *mut ClaywindFrame, gap: f32);
    pub fn claywind_row(frame: *mut ClaywindFrame, gap: f32);
    pub fn claywind_panel(frame: *mut ClaywindFrame);
    pub fn claywind_spacer(frame: *mut ClaywindFrame);
    pub fn claywind_end(frame: *mut ClaywindFrame);

    pub fn claywind_label(frame: *mut ClaywindFrame, text: *const u8, length: usize);
    pub fn claywind_caption(frame: *mut ClaywindFrame, text: *const u8, length: usize);
    pub fn claywind_heading(frame: *mut ClaywindFrame, text: *const u8, length: usize);

    pub fn claywind_button(
        frame: *mut ClaywindFrame,
        id: *const u8,
        id_length: usize,
        label: *const u8,
        label_length: usize,
    ) -> bool;
    pub fn claywind_checkbox(
        frame: *mut ClaywindFrame,
        id: *const u8,
        id_length: usize,
        label: *const u8,
        label_length: usize,
        value: *mut bool,
    ) -> bool;
    pub fn claywind_text_field(
        frame: *mut ClaywindFrame,
        id: *const u8,
        id_length: usize,
        name: *const u8,
        name_length: usize,
        buffer: *mut u8,
        capacity: usize,
        length: *mut usize,
    ) -> bool;

    pub fn claywind_is_dark(frame: *mut ClaywindFrame) -> bool;
    pub fn claywind_set_dark(frame: *mut ClaywindFrame, dark: bool);
    pub fn claywind_request_frame(frame: *mut ClaywindFrame);
}
