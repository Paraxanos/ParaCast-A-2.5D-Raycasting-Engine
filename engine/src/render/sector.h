#ifndef PC_RENDER_SECTOR_H
#define PC_RENDER_SECTOR_H

#include <stdint.h>
#include "assets/assets.h"
#include "core/arena.h"
#include "map/map.h"
#include "render/backend.h"
#include "render/cmdbuf/cmdbuf.h"
#include "render/fb.h"
#include "render/raycast.h"

/* Portal renderer. The front-end starts in the camera's sector and walks the portal
 * graph breadth first. Because sectors are convex, a ray leaves each one through exactly
 * one wall, so every screen column sees a single chain of sectors and a single open
 * vertical range [top, bot) that each portal narrows. A "visit" is one sector entered
 * over one contiguous range of columns. The front-end writes no pixels: it emits wall
 * columns, plane spans and sprite columns into a command buffer, and a back-end draws
 * them (see render/backend.h). */

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
    int       cmin, cmax;      /* rows the ceiling / floor planes touch */
    int       fmin, fmax;
} pc_visit;

/* Render settings. threads and strip_h configure the CPU back-end; max_cmds sizes the
 * command buffer (0 picks a size from the resolution). */
typedef struct {
    int      w, h;
    int      threads;          /* 1 = single thread, the default */
    int      strip_h;          /* rows per strip; 0 = 32 */
    uint32_t max_cmds;
} pc_render_cfg;

typedef struct {
    int       w, h;

    /* read by the debug overlays after a frame */
    pc_visit  visits[PC_MAX_VISITS];
    int       nvisits;

    /* what the front-end produced, and what draws it */
    pc_cmdbuf cmds;
    pc_backend be;

    /* scratch, allocated once */
    int16_t  *ytop, *ybot;     /* open range of each column while traversing */
    fx_t     *cam_x, *ray_x, *ray_y;
    fx_t     *depth;           /* distance to the wall that closes each column */

    int       sprite_order[PC_MAX_THINGS];
    fx_t      sprite_depth[PC_MAX_THINGS];
    fx_t      sprite_lat[PC_MAX_THINGS];

    /* per-frame context */
    const pc_map    *map;
    const pc_assets *as;
    const pc_assets *uploaded;
    pc_camera        cam;
    pc_stats         st;
} pc_sector_renderer;

/* Single-threaded CPU back-end with default strips. */
int pc_sector_renderer_init(pc_sector_renderer *sr, pc_arena *arena, int w, int h);
int pc_sector_renderer_init_cfg(pc_sector_renderer *sr, pc_arena *arena, const pc_render_cfg *cfg);

/* Front-end only: traverses the map and fills sr->cmds. Writes no pixels. */
void pc_sector_frontend(pc_sector_renderer *sr, const pc_map *map, const pc_assets *as,
                        const pc_camera *cam, pc_stats *stats);

/* Inputs the commands in sr->cmds need to be drawn (valid until the next frontend call). */
void pc_sector_frame_params(const pc_sector_renderer *sr, pc_frame_params *fp);

/* Back-end only: draws sr->cmds into fb and fills in stats->sprite_px. */
void pc_sector_backend(pc_sector_renderer *sr, pc_fb *fb, pc_stats *stats);

/* Both stages, then copies the per-column depth into fb->depth. Every pixel of fb is
 * written exactly once by walls and planes; sprites are drawn on top. */
void pc_sector_render(pc_sector_renderer *sr, pc_fb *fb, const pc_map *map,
                      const pc_assets *as, const pc_camera *cam, pc_stats *stats);

#endif
