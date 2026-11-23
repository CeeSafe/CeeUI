/* SPDX-License-Identifier: MIT
 * CeeUI for C: a window built a frame at a time, from C or from any language that calls C (Swift, Kotlin, Rust).
 *
 *   #include "ceeui.h"
 *   #include "ceesafe/embed.h"
 *
 *   static int count;
 *   static void frame(ceeui_frame *f, void *user) {
 *       ceeui_heading_s(f, "Counter");
 *       if (ceeui_button_s(f, "add", "Add one")) count++;
 *       char text[32]; snprintf(text, sizeof text, "%d", count);
 *       ceeui_label_s(f, text);
 *   }
 *   int main(void) {
 *       ceesafe_config config = {.size = sizeof(ceesafe_config)};
 *       if (ceesafe_init(&config) != 0) return 1;
 *       return ceeui_run_s("Counter", 480, 320, frame, NULL);
 *   }
 *
 * The model is immediate mode. CeeUI calls the frame function whenever the window needs a new frame (an input, an
 * animation, ceeui_request_frame), and the function lays out the whole window from the program's state. A control's
 * call returns what the user did with it: true when a button was pressed, a checkbox's new state, a text field's new
 * text. That result arrives in the frame after the one the user acted in, which CeeUI runs at once.
 *
 * Text is UTF-8, given as a pointer and a length; the _s macros take a NUL-terminated string. CeeUI copies what it
 * is given: nothing passed in needs to outlive the call. A control's id names it among the frame's controls and must
 * be unique in the frame; it stays the same from frame to frame.
 *
 * A ceeui_frame is valid only during the frame function's call, on the thread ceeui_run runs on. Columns, rows
 * and panels are closed with ceeui_end; what a frame leaves open is closed for it.
 *
 * Linking: out/ceeui_capi.a (ceesafec build in ceeui-capi), which holds CeeSafe's runtime. Call ceesafe_init
 * (ceesafe/embed.h, beside the archive) once before ceeui_run, on the thread that runs the window: the main thread
 * on macOS. On macOS also link -framework AppKit -framework QuartzCore -framework CoreText -framework CoreGraphics.
 */
#ifndef CEEUI_H
#define CEEUI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The API's version: major * 10000 + minor * 100 + patch; ceeui_version() gives the library's. */
#define CEEUI_VERSION 200

/* What ceeui_run returns. */
#define CEEUI_CLOSED 0            /* the window was closed */
#define CEEUI_NO_WINDOW 1         /* no window could be opened (or no frame function) */
#define CEEUI_NO_MEMORY 2         /* not enough memory for the UI or the renderer */
#define CEEUI_NO_FONTS 3          /* no system fonts found (set CEEUI_FONT_DIR) */
#define CEEUI_FRAME_TOO_LARGE 4   /* a frame had more elements than the UI holds */
#define CEEUI_WINDOW_REFUSED 5    /* the window refused a frame */

/* A frame being built: given to the frame function, valid during its call. */
typedef struct ceeui_frame ceeui_frame;

/* The frame function: lays out one frame from the program's state. `user` is what ceeui_run was given. */
typedef void (*ceeui_frame_fn)(ceeui_frame *frame, void *user);

int32_t ceeui_version(void);

/* Opens a window, `width` x `height` points, and runs it until it is closed (CEEUI_CLOSED) or cannot go on. */
int32_t ceeui_run(const uint8_t *title, size_t title_length, int32_t width, int32_t height, ceeui_frame_fn frame,
                     void *user);

/* Layout. Each start is closed by ceeui_end. `gap` is the space between children, in points. */
void ceeui_column(ceeui_frame *frame, float gap);
void ceeui_row(ceeui_frame *frame, float gap);
void ceeui_panel(ceeui_frame *frame);
void ceeui_spacer(ceeui_frame *frame);
void ceeui_end(ceeui_frame *frame);

/* Text. */
void ceeui_label(ceeui_frame *frame, const uint8_t *text, size_t length);
void ceeui_caption(ceeui_frame *frame, const uint8_t *text, size_t length);
void ceeui_heading(ceeui_frame *frame, const uint8_t *text, size_t length);

/* Controls. A button is true in the frame after it was pressed. A checkbox sets *value to the user's new state and
   returns true in the frame after it was flipped; otherwise it shows *value. */
bool ceeui_button(ceeui_frame *frame, const uint8_t *id, size_t id_length, const uint8_t *label,
                     size_t label_length);
bool ceeui_checkbox(ceeui_frame *frame, const uint8_t *id, size_t id_length, const uint8_t *label,
                       size_t label_length, bool *value);

/* A text field over the program's UTF-8 text: the first *length of capacity bytes at buffer. When the user changed the
   text in the last frame, the buffer and *length take the new text (cut at a character to fit; no NUL is written) and
   the call returns true; otherwise it shows the program's text, so the program may change the buffer itself. The
   caret and selection are CeeUI's. `name` is what a screen reader calls the field. */
bool ceeui_text_field(ceeui_frame *frame, const uint8_t *id, size_t id_length, const uint8_t *name,
                         size_t name_length, uint8_t *buffer, size_t capacity, size_t *length);

/* The window. */
bool ceeui_is_dark(ceeui_frame *frame);
void ceeui_set_dark(ceeui_frame *frame, bool dark);
void ceeui_request_frame(ceeui_frame *frame);

/* The same with NUL-terminated strings. */
#define CEEUI_S(s) (const uint8_t *)(s), strlen(s)
#define ceeui_run_s(title, width, height, frame, user) ceeui_run(CEEUI_S(title), (width), (height), (frame), (user))
#define ceeui_label_s(f, text) ceeui_label((f), CEEUI_S(text))
#define ceeui_caption_s(f, text) ceeui_caption((f), CEEUI_S(text))
#define ceeui_heading_s(f, text) ceeui_heading((f), CEEUI_S(text))
#define ceeui_button_s(f, id, label) ceeui_button((f), CEEUI_S(id), CEEUI_S(label))
#define ceeui_checkbox_s(f, id, label, value) ceeui_checkbox((f), CEEUI_S(id), CEEUI_S(label), (value))
#define ceeui_text_field_s(f, id, name, buffer, capacity, length) \
    ceeui_text_field((f), CEEUI_S(id), CEEUI_S(name), (buffer), (capacity), (length))

#ifdef __cplusplus
}
#endif

#endif
