#include "debug/automap.h"

#include <string.h>

#include "assets/assets.h"

#define MARGIN 12

enum {
    COL_BG     = 0,
    COL_SOLID  = PC_IDX(3, 15),    /* yellow */
    COL_STEP   = PC_IDX(2, 12),    /* orange */
    COL_LEVEL  = PC_IDX(14, 7),    /* slate */
    COL_HERE   = PC_IDX(7, 15),    /* cyan */
    COL_THING  = PC_IDX(5, 15),    /* green */
    COL_PLAYER = PC_IDX(1, 15),    /* red */
    COL_CONE   = PC_IDX(1, 8)
};

typedef struct {
    fx_t minx, miny, scale;
    int  ox, oy;
} view;

static int sx(const view *v, fx_t x) { return v->ox + fx_to_int(fx_mul(x - v->minx, v->scale)); }
static int sy(const view *v, fx_t y) { return v->oy + fx_to_int(fx_mul(y - v->miny, v->scale)); }

static void put(pc_fb *fb, int x, int y, uint8_t c)
{
    if (x >= 0 && y >= 0 && x < fb->w && y < fb->h)
        fb->pix[(size_t)y * fb->w + x] = c;
}

/* Bresenham. */
static void line(pc_fb *fb, int x0, int y0, int x1, int y1, uint8_t c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx_ = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy_ = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        put(fb, x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx_; }
        if (e2 <= dx) { err += dx; y0 += sy_; }
    }
}

static void dot(pc_fb *fb, int x, int y, int r, uint8_t c)
{
    for (int j = -r; j <= r; j++)
        for (int i = -r; i <= r; i++)
            put(fb, x + i, y + j, c);
}

void pc_automap_draw(pc_fb *fb, const pc_map *m, const pc_camera *cam)
{
    memset(fb->pix, COL_BG, (size_t)fb->w * fb->h);

    fx_t minx = FX_MAX, miny = FX_MAX, maxx = FX_MIN, maxy = FX_MIN;
    for (uint32_t w = 0; w < m->nwalls; w++) {
        if (m->a[w].x < minx) minx = m->a[w].x;
        if (m->a[w].x > maxx) maxx = m->a[w].x;
        if (m->a[w].y < miny) miny = m->a[w].y;
        if (m->a[w].y > maxy) maxy = m->a[w].y;
    }

    view v;
    fx_t spanx = maxx - minx > FX_ONE ? maxx - minx : FX_ONE;
    fx_t spany = maxy - miny > FX_ONE ? maxy - miny : FX_ONE;
    fx_t scx = fx_div(fx_from_int(fb->w - 2 * MARGIN), spanx);
    fx_t scy = fx_div(fx_from_int(fb->h - 2 * MARGIN), spany);
    v.scale = scx < scy ? scx : scy;
    v.minx = minx;
    v.miny = miny;
    v.ox = (fb->w - fx_to_int(fx_mul(spanx, v.scale))) / 2;
    v.oy = (fb->h - fx_to_int(fx_mul(spany, v.scale))) / 2;

    /* Level portals first so solid walls and the current sector draw over them. */
    for (int pass = 0; pass < 3; pass++) {
        for (pc_id s = 0; s < m->nsectors; s++) {
            for (pc_id w = m->first_wall[s]; w < m->first_wall[s] + m->wall_count[s]; w++) {
                uint8_t c;
                int kind;
                if (m->back[w] == PC_NONE) {
                    c = COL_SOLID;
                    kind = 1;
                } else if (m->floor_h[m->back[w]] != m->floor_h[s] ||
                           m->ceil_h[m->back[w]] != m->ceil_h[s]) {
                    c = COL_STEP;
                    kind = 1;
                } else {
                    c = COL_LEVEL;
                    kind = 0;
                }
                if (s == cam->sector) {
                    c = COL_HERE;
                    kind = 2;
                }
                if (kind != pass)
                    continue;
                line(fb, sx(&v, m->a[w].x), sy(&v, m->a[w].y), sx(&v, m->b[w].x),
                     sy(&v, m->b[w].y), c);
            }
        }
    }

    for (uint32_t i = 0; i < m->nthings; i++)
        dot(fb, sx(&v, m->thing_pos[i].x), sy(&v, m->thing_pos[i].y), 1, COL_THING);

    /* camera and a view cone matching the renderer's 0.66 plane */
    const int px = sx(&v, cam->pos.x), py = sy(&v, cam->pos.y);
    const fx_t dx = pc_cos(cam->yaw), dy = pc_sin(cam->yaw);
    const fx_t reach = FX_C(2.5);
    const fx_t side = fx_mul(FX_C(0.66), reach);
    line(fb, px, py, sx(&v, cam->pos.x + fx_mul(dx, reach) - fx_mul(dy, side)),
         sy(&v, cam->pos.y + fx_mul(dy, reach) + fx_mul(dx, side)), COL_CONE);
    line(fb, px, py, sx(&v, cam->pos.x + fx_mul(dx, reach) + fx_mul(dy, side)),
         sy(&v, cam->pos.y + fx_mul(dy, reach) - fx_mul(dx, side)), COL_CONE);
    dot(fb, px, py, 2, COL_PLAYER);
}
