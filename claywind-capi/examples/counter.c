/* SPDX-License-Identifier: MIT
 * A ClayWind window from C: a counter, a theme switch and a short list, laid out each frame from the program's state.
 * Build it with examples/build.sh. */
#include <stdio.h>

#include "ceesafe/embed.h"
#include "claywind.h"

struct state {
    int count;
    bool dark;
    bool done[3];
};

static const char *const tasks[3] = {"Build claywind-capi", "Link it from C", "Call it from Swift"};

static void frame(claywind_frame *f, void *user) {
    struct state *s = user;
    claywind_set_dark(f, s->dark);

    claywind_row(f, 12);
    claywind_heading_s(f, "ClayWind from C");
    claywind_spacer(f);
    claywind_checkbox_s(f, "dark", "Dark", &s->dark);
    claywind_end(f);
    claywind_caption_s(f, "Each frame is laid out by a C function; ClayWind draws it.");

    claywind_panel(f);
    char text[64];
    snprintf(text, sizeof text, "Pressed %d time%s", s->count, s->count == 1 ? "" : "s");
    claywind_label_s(f, text);
    claywind_row(f, 8);
    if (claywind_button_s(f, "add", "Add one")) {
        s->count++;
    }
    if (claywind_button_s(f, "reset", "Reset")) {
        s->count = 0;
    }
    claywind_end(f);
    claywind_end(f);

    claywind_panel(f);
    claywind_label_s(f, "Steps");
    for (int k = 0; k < 3; k++) {
        char id[16];
        snprintf(id, sizeof id, "task-%d", k);
        claywind_checkbox_s(f, id, tasks[k], &s->done[k]);
    }
    claywind_end(f);
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
    struct state s = {0, true, {true, false, false}};
    int32_t result = claywind_run_s("ClayWind from C", 520, 420, frame, &s);
    if (result != CLAYWIND_CLOSED) {
        fprintf(stderr, "counter: claywind_run stopped (%d)\n", result);
    }
    ceesafe_shutdown();
    return result == CLAYWIND_CLOSED ? 0 : 1;
}
