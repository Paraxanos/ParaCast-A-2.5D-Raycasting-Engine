#include "render/raycast.h"

#include <string.h>

#define FOV_PLANE    FX_C(0.66)   /* half-width of the view plane for a ~66 degree field of view */
#define MIN_DIST     ((fx_t)1 << 8)
#define LIGHT_SHIFT  15           /* one light level per 0.5 units of distance */
#define SIDE_BIAS    3            /* y-facing walls are a few levels darker */

static inline int light_level(fx_t dist, int bias)
{
    int l = (int)(dist >> LIGHT_SHIFT) + bias;
    return l > PC_LIGHT_LEVELS - 1 ? PC_LIGHT_LEVELS - 1 : l;
}

int pc_raycaster_init(pc_raycaster *rc, pc_arena *arena, int w, int h)
{
    rc->w = w;
    rc->h = h;
    rc->top = (int *)pc_arena_alloc_array(arena, (size_t)w, sizeof(int), 64);
    rc->bot = (int *)pc_arena_alloc_array(arena, (size_t)w, sizeof(int), 64);
    rc->cam_x = (fx_t *)pc_arena_alloc_array(arena, (size_t)w, sizeof(fx_t), 64);
    rc->ray_x = (fx_t *)pc_arena_alloc_array(arena, (size_t)w, sizeof(fx_t), 64);
    rc->ray_y = (fx_t *)pc_arena_alloc_array(arena, (size_t)w, sizeof(fx_t), 64);
    rc->row_dist = (fx_t *)pc_arena_alloc_array(arena, (size_t)h, sizeof(fx_t), 64);
    if (!rc->top || !rc->bot || !rc->cam_x || !rc->ray_x || !rc->ray_y || !rc->row_dist)
        return -1;

    for (int x = 0; x < w; x++)
        rc->cam_x[x] = (fx_t)(((int64_t)(2 * x - w) * FX_ONE) / w);

    /* The camera sits half a wall-height up, so the row p pixels below the horizon
     * sees the floor at distance h / (2p); rows are offset by half a pixel. */
    for (int y = 0; y < h; y++) {
        int q = y >= h / 2 ? 2 * y - h + 1 : h - 1 - 2 * y;
        if (q < 1)
            q = 1;
        rc->row_dist[y] = fx_div(fx_from_int(h), fx_from_int(q));
    }
    return 0;
}

void pc_raycaster_render(pc_raycaster *rc, pc_fb *fb, const pc_gridmap *map,
                         const pc_assets *as, const pc_camera *cam, pc_stats *stats)
{
    const int w = rc->w;
    const int h = rc->h;
    uint32_t dda_steps = 0, wall_px = 0, plane_px = 0;

    pc_vec2 dir = { pc_cos(cam->yaw), pc_sin(cam->yaw) };
    pc_vec2 plane = { -fx_mul(dir.y, FOV_PLANE), fx_mul(dir.x, FOV_PLANE) };

    /* Pass 1: one DDA ray per column draws the wall slice and records its extent. */
    for (int x = 0; x < w; x++) {
        pc_vec2 rd = { dir.x + fx_mul(plane.x, rc->cam_x[x]),
                       dir.y + fx_mul(plane.y, rc->cam_x[x]) };
        rc->ray_x[x] = rd.x;
        rc->ray_y[x] = rd.y;

        pc_grid_hit hit;
        pc_grid_cast(map, cam->pos, rd, &hit);
        dda_steps += (uint32_t)hit.steps;

        fx_t dist = hit.dist < MIN_DIST ? MIN_DIST : hit.dist;
        fb->depth[x] = dist;

        int64_t line_h = fx_to_int(fx_div(fx_from_int(h), dist));
        if (line_h < 1)
            line_h = 1;
        int64_t start = (h - line_h) / 2;
        int64_t end = (h + line_h) / 2;
        int top = (int)(start < 0 ? 0 : start);
        int bot = (int)(end > h ? h : end);
        rc->top[x] = top;
        rc->bot[x] = bot;

        const uint8_t *tex = as->tex[(hit.cell - 1) % PC_WALL_TEX_COUNT];
        const uint8_t *cmap = as->colormap[light_level(dist, hit.side ? SIDE_BIAS : 0)];
        int tx = hit.u >> (FX_SHIFT - PC_TEX_SHIFT);

        int64_t step = ((int64_t)PC_TEX_SIZE * FX_ONE) / line_h;
        int64_t tpos = (top - start) * step;
        uint8_t *dst = fb->pix + (size_t)top * w + x;
        for (int y = top; y < bot; y++) {
            int ty = (int)(tpos >> FX_SHIFT) & (PC_TEX_SIZE - 1);
            *dst = cmap[tex[(ty << PC_TEX_SHIFT) + tx]];
            dst += w;
            tpos += step;
        }
        wall_px += (uint32_t)(bot - top);
    }

    /* Pass 2: floor and ceiling fill only the pixels the walls left uncovered. */
    for (int y = 0; y < h; y++) {
        const int is_floor = y >= h / 2;
        const fx_t rdist = rc->row_dist[y];
        const uint8_t *tex = as->tex[is_floor ? PC_TEX_FLOOR : PC_TEX_CEIL];
        const uint8_t *cmap = as->colormap[light_level(rdist, 0)];
        uint8_t *row = fb->pix + (size_t)y * w;

        for (int x = 0; x < w; x++) {
            if (is_floor ? y < rc->bot[x] : y >= rc->top[x])
                continue;
            fx_t wx = cam->pos.x + fx_mul(rdist, rc->ray_x[x]);
            fx_t wy = cam->pos.y + fx_mul(rdist, rc->ray_y[x]);
            int tx = fx_frac(wx) >> (FX_SHIFT - PC_TEX_SHIFT);
            int ty = fx_frac(wy) >> (FX_SHIFT - PC_TEX_SHIFT);
            row[x] = cmap[tex[(ty << PC_TEX_SHIFT) + tx]];
            plane_px++;
        }
    }

    if (stats) {
        memset(stats, 0, sizeof *stats);
        stats->columns = (uint32_t)w;
        stats->dda_steps = dda_steps;
        stats->wall_px = wall_px;
        stats->plane_px = plane_px;
    }
}
