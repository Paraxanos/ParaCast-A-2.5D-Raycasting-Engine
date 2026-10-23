#ifndef PC_RENDER_POST_PRESENT_H
#define PC_RENDER_POST_PRESENT_H

#include <stdint.h>
#include "assets/assets.h"
#include "render/fb.h"

/* Expands the indexed framebuffer to RGBA8888 (bytes R, G, B, A in memory order).
 * out must hold fb->w * fb->h entries. */
void pc_present_rgba(const pc_fb *fb, const pc_assets *as, uint32_t *out);

#endif
