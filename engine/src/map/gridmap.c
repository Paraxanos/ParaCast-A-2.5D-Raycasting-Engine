#include "map/gridmap.h"

#define DEMO_N 24

/* Larger than any distance the grid can produce, small enough that 2 * PC_GRID_MAX
 * additions to a side distance cannot overflow fx_t. */
#define DELTA_MAX ((fx_t)1 << 22)

static uint8_t g_demo[DEMO_N * DEMO_N];

static void fill(int x0, int y0, int x1, int y1, uint8_t v)
{
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            g_demo[y * DEMO_N + x] = v;
}

void pc_gridmap_make_demo(pc_gridmap *m)
{
    fill(0, 0, DEMO_N - 1, DEMO_N - 1, 1);
    fill(1, 1, DEMO_N - 2, DEMO_N - 2, 0);

    fill(4, 4, 6, 5, 2);
    fill(17, 4, 19, 5, 3);
    fill(4, 18, 6, 19, 3);
    fill(17, 18, 19, 19, 2);
    fill(11, 4, 12, 5, 4);
    fill(11, 18, 12, 19, 4);
    fill(4, 11, 5, 12, 4);
    fill(18, 11, 19, 12, 4);
    fill(8, 1, 8, 2, 3);
    fill(15, 21, 15, 22, 3);

    m->w = DEMO_N;
    m->h = DEMO_N;
    m->cells = g_demo;
}

int pc_gridmap_cell(const pc_gridmap *m, int cx, int cy)
{
    if (cx < 0 || cy < 0 || cx >= m->w || cy >= m->h)
        return 1;
    return m->cells[cy * m->w + cx];
}

static fx_t delta_dist(fx_t r)
{
    if (r == 0)
        return DELTA_MAX;
    fx_t d = fx_div(FX_ONE, fx_abs(r));
    return d > DELTA_MAX ? DELTA_MAX : d;
}

void pc_grid_cast(const pc_gridmap *m, pc_vec2 pos, pc_vec2 rd, pc_grid_hit *out)
{
    int cx = fx_to_int(pos.x);
    int cy = fx_to_int(pos.y);
    fx_t dx = delta_dist(rd.x);
    fx_t dy = delta_dist(rd.y);

    int sx = rd.x < 0 ? -1 : 1;
    int sy = rd.y < 0 ? -1 : 1;
    fx_t sdx = fx_mul(rd.x < 0 ? fx_frac(pos.x) : FX_ONE - fx_frac(pos.x), dx);
    fx_t sdy = fx_mul(rd.y < 0 ? fx_frac(pos.y) : FX_ONE - fx_frac(pos.y), dy);

    int side = 0;
    int steps = 0;
    int cell;

    for (;;) {
        if (sdx < sdy) {
            sdx += dx;
            cx += sx;
            side = 0;
        } else {
            sdy += dy;
            cy += sy;
            side = 1;
        }
        steps++;
        cell = pc_gridmap_cell(m, cx, cy);
        if (cell)
            break;
    }

    fx_t dist = side == 0 ? sdx - dx : sdy - dy;
    fx_t wall = side == 0 ? pos.y + fx_mul(dist, rd.y) : pos.x + fx_mul(dist, rd.x);
    fx_t u = fx_frac(wall);
    if ((side == 0 && rd.x > 0) || (side == 1 && rd.y < 0))
        u = FX_FRAC_MASK - u;

    out->dist = dist;
    out->u = u;
    out->side = side;
    out->cell = (uint8_t)cell;
    out->steps = steps;
}
