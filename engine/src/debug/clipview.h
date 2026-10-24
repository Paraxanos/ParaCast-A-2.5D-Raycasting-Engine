#ifndef PC_DEBUG_CLIPVIEW_H
#define PC_DEBUG_CLIPVIEW_H

#include "render/fb.h"
#include "render/sector.h"

/* Overlays the clip windows of the last sector_render on the framebuffer. Each visit
 * gets its own colour: a dot at the top and bottom of the open range it was entered
 * with, in every column it owns, and a vertical tick at the edges of its column
 * window. Portal-clipping bugs show up as ranges that cross, leak or end early. */
void pc_clipview_draw(pc_fb *fb, const pc_sector_renderer *sr);

#endif
