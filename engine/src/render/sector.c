#include "render/sector.h"

#include <limits.h>
#include <string.h>

#define FOV_PLANE    FX_C(0.66)      /* matches the grid raycaster */
#define MIN_DIST     ((fx_t)1 << 8)
#define PROJ_LIMIT   30000           /* pixel offsets are clamped so int16 storage is safe */
#define DIST_MAX     ((int64_t)1024 << FX_SHIFT)
#define LIGHT_SHIFT  16              /* one light level per unit of distance */
#define SIDE_BIAS    3               /* walls facing along y are a few levels darker */
#define SPRITE_NEAR  FX_C(0.1)
#define HIT_SLACK    4               /* raw units of rounding allowed at a segment's ends */

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* Pixels above the horizon of a point dz above the eye at distance 1/inv. */
static inline int proj(fx_t dz, fx_t inv, int h)
{
    int64_t p = ((int64_t)dz * inv * h) >> 32;
    return p > PROJ_LIMIT ? PROJ_LIMIT : p < -PROJ_LIMIT ? -PROJ_LIMIT : (int)p;
}

static inline int light_level(int64_t dist, int sector_light, int bias)
{
    int64_t l = (31 - sector_light) + (dist >> LIGHT_SHIFT) + bias;
    return l > PC_LIGHT_LEVELS - 1 ? PC_LIGHT_LEVELS - 1 : (int)l;
}

/* ---- setup ----------------------------------------------------------------- */

int pc_sector_renderer_init(pc_sector_renderer *sr, pc_arena *arena, int w, int h)
{
    pc_render_cfg cfg = { w, h, 1, 0, 0 };
    return pc_sector_renderer_init_cfg(sr, arena, &cfg);
}

/* Far more commands than any scene needs (a few thousand at 640x400), sized from the
 * screen so the worst case, every pixel its own span, still fits a quarter over. */
static uint32_t default_cmd_cap(int w, int h)
{
    uint32_t cap = (uint32_t)((int64_t)w * h / 4);
    return cap < (1u << 16) ? (1u << 16) : cap;
}

int pc_sector_renderer_init_cfg(pc_sector_renderer *sr, pc_arena *arena, const pc_render_cfg *cfg)
{
    const int w = cfg->w, h = cfg->h;
    memset(sr, 0, sizeof *sr);
    sr->w = w;
    sr->h = h;

    sr->ytop = (int16_t *)pc_arena_alloc_array(arena, (size_t)w, sizeof(int16_t), 64);
    sr->ybot = (int16_t *)pc_arena_alloc_array(arena, (size_t)w, sizeof(int16_t), 64);
    sr->cam_x = (fx_t *)pc_arena_alloc_array(arena, (size_t)w, sizeof(fx_t), 64);
    sr->ray_x = (fx_t *)pc_arena_alloc_array(arena, (size_t)w, sizeof(fx_t), 64);
    sr->ray_y = (fx_t *)pc_arena_alloc_array(arena, (size_t)w, sizeof(fx_t), 64);
    sr->depth = (fx_t *)pc_arena_alloc_array(arena, (size_t)w, sizeof(fx_t), 64);

    size_t n16 = (size_t)PC_MAX_VISITS * w;
    int16_t *i16 = (int16_t *)pc_arena_alloc_array(arena, n16 * 6, sizeof(int16_t), 64);
    uint8_t *u8 = (uint8_t *)pc_arena_alloc_array(arena, n16, 1, 64);
    if (!sr->ytop || !sr->ybot || !sr->cam_x || !sr->ray_x || !sr->ray_y || !sr->depth ||
        !i16 || !u8)
        return -1;

    uint32_t cap = cfg->max_cmds ? cfg->max_cmds : default_cmd_cap(w, h);
    if (pc_cmdbuf_init(&sr->cmds, arena, cap))
        return -1;

    pc_backend_cfg bc = { w, h, cfg->threads, cfg->strip_h, cap };
    pc_backend_cpu_make(&sr->be);
    if (sr->be.init(&sr->be, arena, &bc))
        return -1;

    for (int i = 0; i < PC_MAX_VISITS; i++) {
        pc_visit *v = &sr->visits[i];
        size_t o = (size_t)i * w;
        v->top = i16 + 0 * n16 + o;
        v->bot = i16 + 1 * n16 + o;
        v->ctop = i16 + 2 * n16 + o;
        v->cbot = i16 + 3 * n16 + o;
        v->ftop = i16 + 4 * n16 + o;
        v->fbot = i16 + 5 * n16 + o;
        v->wall = u8 + o;
    }

    for (int x = 0; x < w; x++)
        sr->cam_x[x] = (fx_t)(((int64_t)(2 * x - w) * FX_ONE) / w);
    return 0;
}

static int push_visit(pc_sector_renderer *sr, pc_id sector, int x0, int x1, int parent,
                      int parent_wall)
{
    pc_visit *v = &sr->visits[sr->nvisits];
    v->sector = sector;
    v->x0 = x0;
    v->x1 = x1;
    v->parent = parent;
    v->parent_wall = parent_wall;
    return sr->nvisits++;
}

/* ---- walls ----------------------------------------------------------------- */

/* Emits one column of a wall piece. y_top is the piece's unclamped top row: the texture
 * is anchored there and repeats every world unit, 64 texels per unit both ways. Only
 * bits 16..21 of the texel row ever select a texel, so v0 and dv are kept modulo 2^32:
 * the back-end sums them in 32 bits and gets exactly the rows a 64-bit sum would. */
static void emit_wall(pc_sector_renderer *sr, int vi, int wall, int x, int y0, int y1,
                      int y_top, int tex, int tx, fx_t t, int level)
{
    if (y0 >= y1)
        return;

    pc_cmd *c = pc_cmdbuf_push(&sr->cmds);
    if (!c)
        return;

    int64_t dv = ((int64_t)t * PC_TEX_SIZE) / sr->h;    /* texels per pixel, 16.16 */
    c->type = PC_CMD_WALL_COL;
    c->tex = (uint8_t)tex;
    c->level = (uint8_t)level;
    c->tx = (uint8_t)tx;
    c->x0 = (uint16_t)x;
    c->x1 = (uint16_t)(x + 1);
    c->y0 = (int16_t)y0;
    c->y1 = (int16_t)y1;
    c->v0 = (uint32_t)((int64_t)(y0 - y_top) * dv);
    c->dv = (uint32_t)dv;
    c->dist = 0;
    c->visit = (uint16_t)vi;
    c->wall = (uint16_t)wall;
    c->reserved = 0;
    sr->st.wall_px += (uint32_t)(y1 - y0);
}

static void process_visit(pc_sector_renderer *sr, int vi)
{
    pc_visit *v = &sr->visits[vi];
    const pc_map *m = sr->map;
    const pc_id s = v->sector;
    const pc_id w0 = m->first_wall[s];
    const int nw = m->wall_count[s];
    const pc_vec2 pos = sr->cam.pos;
    const fx_t z = sr->cam.z;
    const int h = sr->h;
    const int horizon = h / 2;

    /* f[i]: how far inside wall i the camera is (cross(e, pos - a), scale 2^16). It is
     * negative for walls the camera is outside of, which happens in every sector but
     * the first: the ray only reaches those sectors through a portal. */
    int64_t f[PC_MAX_SECTOR_WALLS];
    int cmin_x[PC_MAX_SECTOR_WALLS], cmax_x[PC_MAX_SECTOR_WALLS];
    int nportals = 0;
    for (int i = 0; i < nw; i++) {
        pc_id wi = w0 + (pc_id)i;
        f[i] = ((int64_t)m->e[wi].x * (pos.y - m->a[wi].y) -
                (int64_t)m->e[wi].y * (pos.x - m->a[wi].x)) >> FX_SHIFT;
        cmin_x[i] = INT_MAX;
        cmax_x[i] = -1;
        nportals += m->back[wi] != PC_NONE;
    }

    /* If the visit budget cannot cover every portal here, draw them as solid walls. */
    const int can_recurse = sr->nvisits + nportals <= PC_MAX_VISITS;
    if (!can_recurse)
        sr->st.visit_overflow++;

    const fx_t floor_h = m->floor_h[s], ceil_h = m->ceil_h[s];
    const fx_t dz_c = ceil_h - z, dz_f = floor_h - z;
    const int sector_light = m->light[s];

    v->cmin = v->fmin = h;
    v->cmax = v->fmax = 0;

    const pc_visit *parent = v->parent >= 0 ? &sr->visits[v->parent] : NULL;

    for (int x = v->x0; x < v->x1; x++) {
        v->ctop[x] = v->cbot[x] = v->ftop[x] = v->fbot[x] = 0;
        v->wall[x] = 0xFF;

        if (parent && parent->wall[x] != v->parent_wall) {
            v->top[x] = v->bot[x] = 0;      /* this column goes through a different portal */
            continue;
        }
        const int yt = sr->ytop[x], yb = sr->ybot[x];
        v->top[x] = (int16_t)yt;
        v->bot[x] = (int16_t)yb;
        if (yt >= yb)
            continue;

        /* The ray leaves a convex sector through the wall it is heading out of whose
         * segment contains the hit: 0 <= u <= 1, i.e. 0 <= nu <= g with g > 0. Testing
         * the segment, not just the line, matters because collinear walls (a door
         * frame split into pieces) share one line and so one intersection distance. */
        const fx_t rdx = sr->ray_x[x], rdy = sr->ray_y[x];
        int best = -1, line = -1, behind = -1;
        int64_t best_t = INT64_MAX, best_g = 0, best_nu = 0;
        int64_t line_t = INT64_MAX, line_g = 0, line_nu = 0;
        int64_t behind_t = INT64_MIN, behind_g = 0, behind_nu = 0;
        for (int i = 0; i < nw; i++) {
            pc_id wi = w0 + (pc_id)i;
            int64_t g = ((int64_t)m->e[wi].y * rdx - (int64_t)m->e[wi].x * rdy) >> FX_SHIFT;
            if (g <= 0)
                continue;
            int64_t t = f[i] * FX_ONE / g;
            int64_t nu = (((int64_t)(pos.y - m->a[wi].y) * rdx -
                           (int64_t)(pos.x - m->a[wi].x) * rdy) >> FX_SHIFT);

            if (t >= 0) {
                if (nu >= -HIT_SLACK && nu <= g + HIT_SLACK && t < best_t) {
                    best_t = t; best_g = g; best_nu = nu; best = i;
                }
                if (t < line_t) {
                    line_t = t; line_g = g; line_nu = nu; line = i;
                }
            } else if (t > behind_t) {
                behind_t = t; behind_g = g; behind_nu = nu; behind = i;
            }
        }
        if (best < 0 && line >= 0) {
            /* No segment contains the hit: only rounding slivers at the edge of a window
             * get here. Fall back to the nearest wall line so the column is still drawn. */
            best = line; best_t = line_t; best_g = line_g; best_nu = line_nu;
        }
        if (best < 0 && behind >= 0) {
            best = behind; best_t = 0; best_g = behind_g; best_nu = behind_nu;
        }
        if (best < 0) {
            sr->ytop[x] = sr->ybot[x] = 0;
            continue;
        }

        const pc_id wi = w0 + (pc_id)best;
        v->wall[x] = (uint8_t)best;

        fx_t t = best_t > FX_MAX ? FX_MAX : (fx_t)best_t;
        if (t < MIN_DIST)
            t = MIN_DIST;
        const fx_t inv = fx_div(FX_ONE, t);

        const int y_c = horizon - proj(dz_c, inv, h);
        const int y_f = horizon - proj(dz_f, inv, h);
        const int Yc = clampi(y_c, yt, yb);
        const int Yf = clampi(y_f, yt, yb);

        v->ctop[x] = (int16_t)yt;
        v->cbot[x] = (int16_t)Yc;
        v->ftop[x] = (int16_t)Yf;
        v->fbot[x] = (int16_t)yb;
        if (Yc > yt) {
            if (yt < v->cmin) v->cmin = yt;
            if (Yc > v->cmax) v->cmax = Yc;
        }
        if (yb > Yf) {
            if (Yf < v->fmin) v->fmin = Yf;
            if (yb > v->fmax) v->fmax = yb;
        }

        /* Texture column: distance along the wall, from the intersection parameter. */
        int64_t u = best_nu * FX_ONE / best_g;
        u = u < 0 ? 0 : u > FX_ONE ? FX_ONE : u;
        const int tx = fx_frac(fx_mul((fx_t)u, m->len[wi])) >> (FX_SHIFT - PC_TEX_SHIFT);

        const int bias = fx_abs(m->e[wi].x) > fx_abs(m->e[wi].y) ? 0 : SIDE_BIAS;
        const int level = light_level(t, sector_light, bias);
        const pc_id back = m->back[wi];

        if (back == PC_NONE || !can_recurse) {
            emit_wall(sr, vi, best, x, Yc, Yf, y_c, m->tex_mid[wi], tx, t, level);
            sr->depth[x] = t;
            sr->ytop[x] = sr->ybot[x] = 0;
            continue;
        }

        const int y_bc = horizon - proj(m->ceil_h[back] - z, inv, h);
        const int y_bf = horizon - proj(m->floor_h[back] - z, inv, h);
        const int Ybc = clampi(y_bc, Yc, Yf);
        int Ybf = clampi(y_bf, Yc, Yf);
        if (Ybf < Ybc)
            Ybf = Ybc;

        emit_wall(sr, vi, best, x, Yc, Ybc, y_c, m->tex_up[wi], tx, t, level);
        emit_wall(sr, vi, best, x, Ybf, Yf, y_bf, m->tex_low[wi], tx, t, level);

        if (Ybc < Ybf) {
            sr->ytop[x] = (int16_t)Ybc;
            sr->ybot[x] = (int16_t)Ybf;
            if (x < cmin_x[best]) cmin_x[best] = x;
            if (x > cmax_x[best]) cmax_x[best] = x;
            sr->st.portal_columns++;
        } else {
            sr->depth[x] = t;           /* the portal is shut at this column */
            sr->ytop[x] = sr->ybot[x] = 0;
        }
    }

    if (!can_recurse)
        return;
    for (int i = 0; i < nw; i++)
        if (cmax_x[i] >= 0)
            push_visit(sr, m->back[w0 + (pc_id)i], cmin_x[i], cmax_x[i] + 1, vi, i);
}

/* ---- floors and ceilings --------------------------------------------------- */

static void emit_plane(pc_sector_renderer *sr, int vi, int is_ceil)
{
    const pc_visit *v = &sr->visits[vi];
    const pc_map *m = sr->map;
    const pc_id s = v->sector;
    const int h = sr->h;
    const int16_t *pt = is_ceil ? v->ctop : v->ftop;
    const int16_t *pb = is_ceil ? v->cbot : v->fbot;
    const int ymin = is_ceil ? v->cmin : v->fmin;
    const int ymax = is_ceil ? v->cmax : v->fmax;
    if (ymin >= ymax)
        return;

    const fx_t height = is_ceil ? m->ceil_h[s] : m->floor_h[s];
    const int64_t dz = is_ceil ? (int64_t)height - sr->cam.z : (int64_t)sr->cam.z - height;
    if (dz <= 0)
        return;

    const int tex = is_ceil ? m->ceil_tex[s] : m->floor_tex[s];

    for (int y = ymin; y < ymax; y++) {
        /* Row distance: twice the rows' offset from the horizon in half pixels. */
        const int q = is_ceil ? h - 1 - 2 * y : 2 * y + 1 - h;
        if (q < 1)
            continue;
        int64_t dist = dz * 2 * h / q;
        if (dist > DIST_MAX)
            dist = DIST_MAX;
        const int level = light_level(dist, m->light[s], 0);

        int x = v->x0;
        while (x < v->x1) {
            if (!(pt[x] <= y && y < pb[x])) {
                x++;
                continue;
            }
            const int xa = x;
            while (x < v->x1 && pt[x] <= y && y < pb[x])
                x++;

            pc_cmd *c = pc_cmdbuf_push(&sr->cmds);
            if (!c)
                continue;
            c->type = PC_CMD_PLANE_SPAN;
            c->tex = (uint8_t)tex;
            c->level = (uint8_t)level;
            c->tx = 0;
            c->x0 = (uint16_t)xa;
            c->x1 = (uint16_t)x;
            c->y0 = (int16_t)y;
            c->y1 = (int16_t)(y + 1);
            c->v0 = c->dv = 0;
            c->dist = (fx_t)dist;
            c->visit = (uint16_t)vi;
            c->wall = 0;
            c->reserved = 0;
            sr->st.plane_px += (uint32_t)(x - xa);
        }
    }
}

/* ---- sprites --------------------------------------------------------------- */

static void emit_sprite(pc_sector_renderer *sr, int thing, fx_t d, fx_t lat)
{
    const pc_map *m = sr->map;
    const pc_sprite *sp = &sr->as->sprites[m->thing_sprite[thing]];
    const pc_id s = m->thing_sector[thing];
    const int sprite_id = m->thing_sprite[thing];
    const int w = sr->w, h = sr->h;

    const fx_t inv = fx_div(FX_ONE, d);
    const fx_t cam_x = fx_div(lat, fx_mul(d, FOV_PLANE));
    const int cx = w / 2 + fx_to_int(fx_mul(cam_x, fx_from_int(w / 2)));
    const int wpx = fx_to_int(fx_div(fx_mul(sp->width, fx_from_int(w / 2)), fx_mul(d, FOV_PLANE)));
    const int hpx = (int)(((int64_t)sp->height * inv * h) >> 32);
    if (wpx < 1 || hpx < 1 || wpx > 8 * w || hpx > 8 * h)
        return;

    const int y_bottom = h / 2 - proj(m->floor_h[s] - sr->cam.z, inv, h);
    const int y_top = y_bottom - hpx;
    const int sx0 = cx - wpx / 2;
    const int sx1 = sx0 + wpx;
    const int64_t dv = ((int64_t)PC_TEX_SIZE * FX_ONE) / hpx;
    const int level = light_level(d, m->light[s], 0);

    for (int vi = 0; vi < sr->nvisits; vi++) {
        const pc_visit *v = &sr->visits[vi];
        if (v->sector != s)
            continue;

        const int xa = sx0 > v->x0 ? sx0 : v->x0;
        const int xb = sx1 < v->x1 ? sx1 : v->x1;
        for (int x = xa; x < xb; x++) {
            if (v->top[x] >= v->bot[x] || d >= sr->depth[x])
                continue;

            const int y0 = y_top > v->top[x] ? y_top : v->top[x];
            const int y1 = y_bottom < v->bot[x] ? y_bottom : v->bot[x];
            if (y0 >= y1)
                continue;

            /* The depth and portal-window tests happen here, so the back-end gets a
             * column that is already clipped and needs no depth buffer. */
            pc_cmd *c = pc_cmdbuf_push(&sr->cmds);
            if (!c)
                continue;
            c->type = PC_CMD_SPRITE_COL;
            c->tex = (uint8_t)sprite_id;
            c->level = (uint8_t)level;
            c->tx = (uint8_t)((int64_t)(x - sx0) * PC_TEX_SIZE / wpx);
            c->x0 = (uint16_t)x;
            c->x1 = (uint16_t)(x + 1);
            c->y0 = (int16_t)y0;
            c->y1 = (int16_t)y1;
            c->v0 = (uint32_t)((int64_t)(y0 - y_top) * dv);
            c->dv = (uint32_t)dv;
            c->dist = d;
            c->visit = (uint16_t)vi;
            c->wall = 0;
            c->reserved = 0;
        }
    }
}

static void emit_sprites(pc_sector_renderer *sr, pc_vec2 dir)
{
    const pc_map *m = sr->map;
    int n = 0;

    for (uint32_t i = 0; i < m->nthings; i++) {
        int seen = 0;
        for (int vi = 0; vi < sr->nvisits && !seen; vi++)
            seen = sr->visits[vi].sector == m->thing_sector[i];
        if (!seen)
            continue;

        pc_vec2 rel = { m->thing_pos[i].x - sr->cam.pos.x, m->thing_pos[i].y - sr->cam.pos.y };
        fx_t d = fx_mul(rel.x, dir.x) + fx_mul(rel.y, dir.y);
        if (d < SPRITE_NEAR)
            continue;

        /* far to near: insertion sort, thing counts are small */
        int j = n++;
        while (j > 0 && sr->sprite_depth[sr->sprite_order[j - 1]] < d) {
            sr->sprite_order[j] = sr->sprite_order[j - 1];
            j--;
        }
        sr->sprite_order[j] = (int)i;
        sr->sprite_depth[i] = d;
        sr->sprite_lat[i] = fx_mul(rel.y, dir.x) - fx_mul(rel.x, dir.y);
    }

    for (int k = 0; k < n; k++) {
        int i = sr->sprite_order[k];
        emit_sprite(sr, i, sr->sprite_depth[i], sr->sprite_lat[i]);
    }
}

/* ---- frame ----------------------------------------------------------------- */

void pc_sector_frontend(pc_sector_renderer *sr, const pc_map *map, const pc_assets *as,
                        const pc_camera *cam, pc_stats *stats)
{
    const int w = sr->w;

    sr->map = map;
    sr->as = as;
    sr->cam = *cam;
    memset(&sr->st, 0, sizeof sr->st);
    sr->nvisits = 0;
    pc_cmdbuf_reset(&sr->cmds);

    if (cam->sector >= map->nsectors) {
        if (stats)
            *stats = sr->st;
        return;
    }

    pc_vec2 dir = { pc_cos(cam->yaw), pc_sin(cam->yaw) };
    pc_vec2 plane = { -fx_mul(dir.y, FOV_PLANE), fx_mul(dir.x, FOV_PLANE) };
    for (int x = 0; x < w; x++) {
        sr->ray_x[x] = dir.x + fx_mul(plane.x, sr->cam_x[x]);
        sr->ray_y[x] = dir.y + fx_mul(plane.y, sr->cam_x[x]);
        sr->ytop[x] = 0;
        sr->ybot[x] = (int16_t)sr->h;
        sr->depth[x] = FX_MAX;
    }

    push_visit(sr, cam->sector, 0, w, -1, -1);
    for (int vi = 0; vi < sr->nvisits; vi++)
        process_visit(sr, vi);

    for (int vi = 0; vi < sr->nvisits; vi++) {
        emit_plane(sr, vi, 1);
        emit_plane(sr, vi, 0);
    }
    emit_sprites(sr, dir);

    sr->st.columns = (uint32_t)w;
    sr->st.sector_visits = (uint32_t)sr->nvisits;
    sr->st.commands = sr->cmds.count;
    sr->st.cmds_dropped = sr->cmds.dropped;
    if (stats)
        *stats = sr->st;
}

void pc_sector_frame_params(const pc_sector_renderer *sr, pc_frame_params *fp)
{
    fp->w = sr->w;
    fp->h = sr->h;
    fp->pos = sr->cam.pos;
    fp->ray_x = sr->ray_x;
    fp->ray_y = sr->ray_y;
}

void pc_sector_backend(pc_sector_renderer *sr, pc_fb *fb, pc_stats *stats)
{
    if (sr->uploaded != sr->as) {
        sr->be.upload_assets(&sr->be, sr->as);
        sr->uploaded = sr->as;
    }

    pc_frame_params fp;
    pc_sector_frame_params(sr, &fp);
    sr->be.begin_frame(&sr->be, &fp, fb);
    sr->be.submit(&sr->be, &sr->cmds);
    sr->be.end_frame(&sr->be);

    pc_backend_stats bs = sr->be.stats(&sr->be);
    sr->st.sprite_px = bs.sprite_px;
    sr->st.cmds_dropped = sr->cmds.dropped + bs.cmds_dropped;
    if (stats) {
        stats->sprite_px = sr->st.sprite_px;
        stats->cmds_dropped = sr->st.cmds_dropped;
    }
}

void pc_sector_render(pc_sector_renderer *sr, pc_fb *fb, const pc_map *map,
                      const pc_assets *as, const pc_camera *cam, pc_stats *stats)
{
    pc_sector_frontend(sr, map, as, cam, stats);
    if (cam->sector >= map->nsectors)
        return;

    pc_sector_backend(sr, fb, stats);
    for (int x = 0; x < sr->w; x++)
        fb->depth[x] = sr->depth[x];
}
