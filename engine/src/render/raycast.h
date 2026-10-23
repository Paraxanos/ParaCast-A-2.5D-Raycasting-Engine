#ifndef PC_RENDER_RAYCAST_H
#define PC_RENDER_RAYCAST_H

#include <stdint.h>
#include "assets/assets.h"
#include "core/arena.h"
#include "core/lut.h"
#include "map/gridmap.h"
#include "render/fb.h"

typedef struct {
    pc_vec2  pos;
    pc_angle yaw;
} pc_camera;

typedef struct {
    uint32_t columns;
    uint32_t dda_steps;
    uint32_t wall_px;
    uint32_t plane_px;
} pc_stats;

/* All per-frame scratch lives here, allocated once so a frame makes no allocations. */
typedef struct {
    int    w, h;
    int   *top;        /* first wall row per column */
    int   *bot;        /* one past the last wall row per column */
    fx_t  *cam_x;      /* column position in [-1, 1) */
    fx_t  *ray_x;      /* ray direction per column, rebuilt each frame */
    fx_t  *ray_y;
    fx_t  *row_dist;   /* perpendicular distance of the floor/ceiling at each row */
} pc_raycaster;

int pc_raycaster_init(pc_raycaster *rc, pc_arena *arena, int w, int h);

void pc_raycaster_render(pc_raycaster *rc, pc_fb *fb, const pc_gridmap *map,
                         const pc_assets *as, const pc_camera *cam, pc_stats *stats);

#endif
