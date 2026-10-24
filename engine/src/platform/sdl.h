#ifndef PC_PLATFORM_SDL_H
#define PC_PLATFORM_SDL_H

#include <stdint.h>
#include "sim/player.h"

/* SDL2 window and keyboard. Only built with ENGINE_SDL; the engine itself never
 * includes SDL headers. */
typedef struct pc_window pc_window;

/* Opens a w x h logical surface shown at `scale` times its size. Returns NULL on failure. */
pc_window *pc_window_open(const char *title, int w, int h, int scale, int vsync);

/* One-shot key presses seen during a poll (not held state). */
typedef struct {
    int automap;     /* Tab */
    int clipview;    /* C */
} pc_ui;

/* Pumps events and fills `in` from the held keys (WASD move, arrows or Q/E turn) and
 * `ui` from keys pressed since the last call. Returns 0 once the user asked to quit
 * (close button or Esc). */
int pc_window_poll(pc_window *win, pc_input *in, pc_ui *ui);

/* Uploads an RGBA8888 frame of the opened size and flips. */
void pc_window_present(pc_window *win, const uint32_t *rgba);

void pc_window_set_title(pc_window *win, const char *title);
void pc_window_close(pc_window *win);

#endif
