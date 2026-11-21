/* SPDX-License-Identifier: MIT
 * ClayWind for C: a window built a frame at a time, from C or from any language that calls C (Swift, Kotlin, Rust).
 *
 *   #include "claywind.h"
 *   #include "ceesafe/embed.h"
 *
 *   static int count;
 *   static void frame(claywind_frame *f, void *user) {
 *       claywind_heading_s(f, "Counter");
 *       if (claywind_button_s(f, "add", "Add one")) count++;
 *       char text[32]; snprintf(text, sizeof text, "%d", count);
 *       claywind_label_s(f, text);
 *   }
 *   int main(void) {
 *       ceesafe_config config = {.size = sizeof(ceesafe_config)};
 *       if (ceesafe_init(&config) != 0) return 1;
 *       return claywind_run_s("Counter", 480, 320, frame, NULL);
 *   }
 *
 * The model is immediate mode. ClayWind calls the frame function whenever the window needs a new frame (an input, an
 * animation, claywind_request_frame), and the function lays out the whole window from the program's state. A control's
 * call returns what the user did with it: true when a button was pressed, a checkbox's new state, a text field's new
 * text. That result arrives in the frame after the one the user acted in, which ClayWind runs at once.
 *
 * Text is UTF-8, given as a pointer and a length; the _s macros take a NUL-terminated string. ClayWind copies what it
 * is given: nothing passed in needs to outlive the call. A control's id names it among the frame's controls and must
 * be unique in the frame; it stays the same from frame to frame.
 *
 * A claywind_frame is valid only during the frame function's call, on the thread claywind_run runs on. Columns, rows
 * and panels are closed with claywind_end; what a frame leaves open is closed for it.
 *
 * Linking: out/claywind_capi.a (ceesafec build in claywind-capi), which holds CeeSafe's runtime. Call ceesafe_init
 * (ceesafe/embed.h, beside the archive) once before claywind_run, on the thread that runs the window: the main thread
 * on macOS. On macOS also link -framework AppKit -framework QuartzCore -framework CoreText -framework CoreGraphics.
 */
#ifndef CLAYWIND_H
#define CLAYWIND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The API's version: major * 10000 + minor * 100 + patch; claywind_version() gives the library's. */
#define CLAYWIND_VERSION 200

/* What claywind_run returns. */
#define CLAYWIND_CLOSED 0            /* the window was closed */
#define CLAYWIND_NO_WINDOW 1         /* no window could be opened (or no frame function) */
#define CLAYWIND_NO_MEMORY 2         /* not enough memory for the UI or the renderer */
#define CLAYWIND_NO_FONTS 3          /* no system fonts found (set CLAYWIND_FONT_DIR) */
#define CLAYWIND_FRAME_TOO_LARGE 4   /* a frame had more elements than the UI holds */
#define CLAYWIND_WINDOW_REFUSED 5    /* the window refused a frame */

/* A frame being built: given to the frame function, valid during its call. */
typedef struct claywind_frame claywind_frame;

/* The frame function: lays out one frame from the program's state. `user` is what claywind_run was given. */
typedef void (*claywind_frame_fn)(claywind_frame *frame, void *user);

int32_t claywind_version(void);

/* Opens a window, `width` x `height` points, and runs it until it is closed (CLAYWIND_CLOSED) or cannot go on. */
int32_t claywind_run(const uint8_t *title, size_t title_length, int32_t width, int32_t height, claywind_frame_fn frame,
                     void *user);

/* Layout. Each start is closed by claywind_end. `gap` is the space between children, in points. */
void claywind_column(claywind_frame *frame, float gap);
void claywind_row(claywind_frame *frame, float gap);
void claywind_panel(claywind_frame *frame);
void claywind_spacer(claywind_frame *frame);
void claywind_end(claywind_frame *frame);

/* Text. */
void claywind_label(claywind_frame *frame, const uint8_t *text, size_t length);
void claywind_caption(claywind_frame *frame, const uint8_t *text, size_t length);
void claywind_heading(claywind_frame *frame, const uint8_t *text, size_t length);

/* Controls. A button is true in the frame after it was pressed. A checkbox sets *value to the user's new state and
   returns true in the frame after it was flipped; otherwise it shows *value. */
bool claywind_button(claywind_frame *frame, const uint8_t *id, size_t id_length, const uint8_t *label,
                     size_t label_length);
bool claywind_checkbox(claywind_frame *frame, const uint8_t *id, size_t id_length, const uint8_t *label,
                       size_t label_length, bool *value);

/* A text field over the program's UTF-8 text: the first *length of capacity bytes at buffer. When the user changed the
   text in the last frame, the buffer and *length take the new text (cut at a character to fit; no NUL is written) and
   the call returns true; otherwise it shows the program's text, so the program may change the buffer itself. The
   caret and selection are ClayWind's. `name` is what a screen reader calls the field. */
bool claywind_text_field(claywind_frame *frame, const uint8_t *id, size_t id_length, const uint8_t *name,
                         size_t name_length, uint8_t *buffer, size_t capacity, size_t *length);

/* The window. */
bool claywind_is_dark(claywind_frame *frame);
void claywind_set_dark(claywind_frame *frame, bool dark);
void claywind_request_frame(claywind_frame *frame);

/* The same with NUL-terminated strings. */
#define CLAYWIND_S(s) (const uint8_t *)(s), strlen(s)
#define claywind_run_s(title, width, height, frame, user) claywind_run(CLAYWIND_S(title), (width), (height), (frame), (user))
#define claywind_label_s(f, text) claywind_label((f), CLAYWIND_S(text))
#define claywind_caption_s(f, text) claywind_caption((f), CLAYWIND_S(text))
#define claywind_heading_s(f, text) claywind_heading((f), CLAYWIND_S(text))
#define claywind_button_s(f, id, label) claywind_button((f), CLAYWIND_S(id), CLAYWIND_S(label))
#define claywind_checkbox_s(f, id, label, value) claywind_checkbox((f), CLAYWIND_S(id), CLAYWIND_S(label), (value))
#define claywind_text_field_s(f, id, name, buffer, capacity, length) \
    claywind_text_field((f), CLAYWIND_S(id), CLAYWIND_S(name), (buffer), (capacity), (length))

#ifdef __cplusplus
}
#endif

#endif
