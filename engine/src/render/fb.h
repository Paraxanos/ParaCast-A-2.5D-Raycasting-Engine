#ifndef PC_RENDER_FB_H
#define PC_RENDER_FB_H

#include <stdint.h>
#include "core/arena.h"
#include "core/fx.h"

/* 8-bit indexed framebuffer, row-major, plus a per-column depth buffer. */
typedef struct {
    int      w, h;
    uint8_t *pix;
    fx_t    *depth;
} pc_fb;

int pc_fb_init(pc_fb *fb, pc_arena *arena, int w, int h);

#endif
