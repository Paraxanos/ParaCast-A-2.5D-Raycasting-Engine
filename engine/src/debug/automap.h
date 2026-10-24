#ifndef PC_DEBUG_AUTOMAP_H
#define PC_DEBUG_AUTOMAP_H

#include "map/map.h"
#include "render/fb.h"
#include "render/raycast.h"

/* Draws a top-down view of the whole map over the framebuffer (clearing it): solid
 * walls bright, portals with a step in orange, level portals dim, the camera sector
 * outlined in cyan, things as dots, and the camera with its view cone. The map is
 * scaled to fit the screen. */
void pc_automap_draw(pc_fb *fb, const pc_map *map, const pc_camera *cam);

#endif
