#include "render/fb.h"

int pc_fb_init(pc_fb *fb, pc_arena *arena, int w, int h)
{
    fb->w = w;
    fb->h = h;
    fb->pix = (uint8_t *)pc_arena_alloc_array(arena, (size_t)w * h, 1, 64);
    fb->depth = (fx_t *)pc_arena_alloc_array(arena, (size_t)w, sizeof(fx_t), 64);
    return fb->pix && fb->depth ? 0 : -1;
}
