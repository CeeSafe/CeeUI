// SPDX-License-Identifier: MIT
//! The C API as declared in ceeui.h, and CeeSafe's runtime start (ceesafe/embed.h).

use std::ffi::c_void;

/// A frame being built (ceeui_frame): opaque.
#[repr(C)]
pub struct CeeUIFrame {
    _private: [u8; 0],
}

pub type FrameFn = unsafe extern "C" fn(frame: *mut CeeUIFrame, user: *mut c_void);

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

    pub fn ceeui_version() -> i32;
    pub fn ceeui_run(
        title: *const u8,
        title_length: usize,
        width: i32,
        height: i32,
        frame: Option<FrameFn>,
        user: *mut c_void,
    ) -> i32;

    pub fn ceeui_column(frame: *mut CeeUIFrame, gap: f32);
    pub fn ceeui_row(frame: *mut CeeUIFrame, gap: f32);
    pub fn ceeui_panel(frame: *mut CeeUIFrame);
    pub fn ceeui_spacer(frame: *mut CeeUIFrame);
    pub fn ceeui_end(frame: *mut CeeUIFrame);

    pub fn ceeui_label(frame: *mut CeeUIFrame, text: *const u8, length: usize);
    pub fn ceeui_caption(frame: *mut CeeUIFrame, text: *const u8, length: usize);
    pub fn ceeui_heading(frame: *mut CeeUIFrame, text: *const u8, length: usize);

    pub fn ceeui_button(
        frame: *mut CeeUIFrame,
        id: *const u8,
        id_length: usize,
        label: *const u8,
        label_length: usize,
    ) -> bool;
    pub fn ceeui_checkbox(
        frame: *mut CeeUIFrame,
        id: *const u8,
        id_length: usize,
        label: *const u8,
        label_length: usize,
        value: *mut bool,
    ) -> bool;
    pub fn ceeui_text_field(
        frame: *mut CeeUIFrame,
        id: *const u8,
        id_length: usize,
        name: *const u8,
        name_length: usize,
        buffer: *mut u8,
        capacity: usize,
        length: *mut usize,
    ) -> bool;

    pub fn ceeui_is_dark(frame: *mut CeeUIFrame) -> bool;
    pub fn ceeui_set_dark(frame: *mut CeeUIFrame, dark: bool);
    pub fn ceeui_request_frame(frame: *mut CeeUIFrame);
}
