/* SPDX-License-Identifier: MIT
 * A CeeUI window from C: a counter, a theme switch, a greeting typed into a text field and a short list, laid out
 * each frame from the program's state.
 * Build it with examples/build.sh. */
#include <stdio.h>

#include "ceesafe/embed.h"
#include "ceeui.h"

struct state {
    int count;
    bool dark;
    bool done[3];
    uint8_t name[128];
    size_t name_length;
};

static const char *const tasks[3] = {"Build ceeui-capi", "Link it from C", "Call it from Swift"};

static void frame(ceeui_frame *f, void *user) {
    struct state *s = user;
    ceeui_set_dark(f, s->dark);

    ceeui_row(f, 12);
    ceeui_heading_s(f, "CeeUI from C");
    ceeui_spacer(f);
    ceeui_checkbox_s(f, "dark", "Dark", &s->dark);
    ceeui_end(f);
    ceeui_caption_s(f, "Each frame is laid out by a C function; CeeUI draws it.");

    ceeui_panel(f);
    char text[64];
    snprintf(text, sizeof text, "Pressed %d time%s", s->count, s->count == 1 ? "" : "s");
    ceeui_label_s(f, text);
    ceeui_row(f, 8);
    if (ceeui_button_s(f, "add", "Add one")) {
        s->count++;
    }
    if (ceeui_button_s(f, "reset", "Reset")) {
        s->count = 0;
    }
    ceeui_end(f);
    ceeui_end(f);

    ceeui_panel(f);
    ceeui_caption_s(f, "Your name");
    ceeui_text_field_s(f, "name", "Your name", s->name, sizeof s->name, &s->name_length);
    if (s->name_length > 0) {
        snprintf(text, sizeof text, "Hello, %.*s!", (int)s->name_length, (const char *)s->name);
        ceeui_label_s(f, text);
    }
    ceeui_end(f);

    ceeui_panel(f);
    ceeui_label_s(f, "Steps");
    for (int k = 0; k < 3; k++) {
        char id[16];
        snprintf(id, sizeof id, "task-%d", k);
        ceeui_checkbox_s(f, id, tasks[k], &s->done[k]);
    }
    ceeui_end(f);
}

int main(int argc, char **argv) {
    ceesafe_config config = {0};
    config.size = sizeof config;
    config.flags = CEESAFE_INIT_ENVIRONMENT;
    config.argc = argc;
    config.argv = (const char *const *)argv;
    if (ceesafe_init(&config) != 0) {
        fprintf(stderr, "counter: CeeSafe's runtime did not start\n");
        return 1;
    }
    struct state s = {0, true, {true, false, false}, "World", 5};
    int32_t result = ceeui_run_s("CeeUI from C", 520, 520, frame, &s);
    if (result != CEEUI_CLOSED) {
        fprintf(stderr, "counter: ceeui_run stopped (%d)\n", result);
    }
    ceesafe_shutdown();
    return result == CEEUI_CLOSED ? 0 : 1;
}
