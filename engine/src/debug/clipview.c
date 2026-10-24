#include "debug/clipview.h"

void pc_clipview_draw(pc_fb *fb, const pc_sector_renderer *sr)
{
    for (int vi = 0; vi < sr->nvisits; vi++) {
        const pc_visit *v = &sr->visits[vi];
        /* hues 1..14 skip white and slate, so neighbouring visits stay distinguishable */
        const uint8_t col = PC_IDX(1 + vi % 14, 15);

        for (int x = v->x0; x < v->x1; x++) {
            if (v->top[x] >= v->bot[x])
                continue;
            fb->pix[(size_t)v->top[x] * fb->w + x] = col;
            fb->pix[(size_t)(v->bot[x] - 1) * fb->w + x] = col;
        }
        for (int y = 0; y < fb->h; y += 2) {
            fb->pix[(size_t)y * fb->w + v->x0] = col;
            fb->pix[(size_t)y * fb->w + v->x1 - 1] = col;
        }
    }
}
