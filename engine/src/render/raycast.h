#ifndef PC_RENDER_RAYCAST_H
#define PC_RENDER_RAYCAST_H

#include <stdint.h>
#include "assets/assets.h"
#include "core/arena.h"
#include "core/lut.h"
#include "core/types.h"
#include "map/gridmap.h"
#include "render/fb.h"

/* z and sector are used by the sector renderer only; the grid raycaster ignores them. */
typedef struct {
    pc_vec2  pos;
    pc_angle yaw;
    fx_t     z;          /* eye height */
    pc_id    sector;
} pc_camera;

/* Counters from the last frame. wall_px + plane_px covers the screen exactly once;
 * sprite_px is extra overdraw on top. */
typedef struct {
    uint32_t columns;
    uint32_t dda_steps;      /* grid raycaster */
    uint32_t wall_px;
    uint32_t plane_px;
    uint32_t sector_visits;  /* sector renderer */
    uint32_t portal_columns;
    uint32_t sprite_px;
    uint32_t visit_overflow; /* portals refused because the visit budget ran out */
    uint32_t commands;       /* records the front-end emitted */
    uint32_t cmds_dropped;   /* records lost to a full command buffer or bin table */
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
