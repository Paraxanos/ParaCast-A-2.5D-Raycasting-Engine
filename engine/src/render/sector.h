#ifndef PC_RENDER_SECTOR_H
#define PC_RENDER_SECTOR_H

#include <stdint.h>
#include "assets/assets.h"
#include "core/arena.h"
#include "map/map.h"
#include "render/fb.h"
#include "render/raycast.h"

/* Portal renderer. Traversal starts in the camera's sector and walks the portal graph
 * breadth first. Because sectors are convex, a ray leaves each one through exactly one
 * wall, so every screen column sees a single chain of sectors and a single open
 * vertical range [top, bot) that each portal narrows. A "visit" is one sector entered
 * over one contiguous range of columns; floors and ceilings are recorded per visit and
 * drawn row by row after traversal (a visplane per visit). */

#define PC_MAX_VISITS 128

typedef struct {
    pc_id     sector;
    int       x0, x1;          /* columns [x0, x1) */
    int       parent;          /* visit this one was entered from, or -1 for the camera sector */
    int       parent_wall;     /* the parent's wall (index in sector) it came through */
    int16_t  *top, *bot;       /* open row range entering this sector, per column */
    int16_t  *ctop, *cbot;     /* ceiling plane coverage [ctop, cbot) per column */
    int16_t  *ftop, *fbot;     /* floor plane coverage [ftop, fbot) per column */
    uint8_t  *wall;            /* wall (index in sector) each column leaves through */
    int       cmin, cmax;      /* rows the ceiling / floor planes touch, for drawing */
    int       fmin, fmax;
} pc_visit;

typedef struct {
    int       w, h;

    /* read by the debug overlays after a frame */
    pc_visit  visits[PC_MAX_VISITS];
    int       nvisits;

    /* scratch, allocated once */
    int16_t  *ytop, *ybot;     /* open range of each column while traversing */
    fx_t     *cam_x, *ray_x, *ray_y;

    int       sprite_order[PC_MAX_THINGS];
    fx_t      sprite_depth[PC_MAX_THINGS];
    fx_t      sprite_lat[PC_MAX_THINGS];

    /* per-frame context */
    const pc_map    *map;
    const pc_assets *as;
    pc_fb           *fb;
    pc_camera        cam;
    pc_stats         st;
} pc_sector_renderer;

int pc_sector_renderer_init(pc_sector_renderer *sr, pc_arena *arena, int w, int h);

/* Renders the map from cam (cam->sector must contain cam->pos). Every pixel of fb is
 * written exactly once by walls and planes; sprites are drawn on top. */
void pc_sector_render(pc_sector_renderer *sr, pc_fb *fb, const pc_map *map,
                      const pc_assets *as, const pc_camera *cam, pc_stats *stats);

#endif
