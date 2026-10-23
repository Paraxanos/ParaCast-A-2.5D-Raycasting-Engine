#include "render/post/present.h"

void pc_present_rgba(const pc_fb *fb, const pc_assets *as, uint32_t *out)
{
    uint8_t *dst = (uint8_t *)out;
    const size_t n = (size_t)fb->w * fb->h;

    for (size_t i = 0; i < n; i++) {
        const pc_rgb *c = &as->palette[fb->pix[i]];
        dst[4 * i + 0] = c->r;
        dst[4 * i + 1] = c->g;
        dst[4 * i + 2] = c->b;
        dst[4 * i + 3] = 255;
    }
}
