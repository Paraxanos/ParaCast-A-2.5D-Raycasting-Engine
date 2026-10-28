/* CPU strip rasterizer. Commands are binned by band of rows and each band is drawn on
 * its own: wall columns are clipped to the band, plane spans lie in one row, sprite
 * columns are clipped like walls. Bands write disjoint rows, so they can run on any
 * number of threads in any order and the picture is the same bit for bit. */

#include "render/backend.h"

#include <stddef.h>

#define LOCAL static

typedef struct {
    pc_backend_cfg   cfg;
    pc_bins          bins;
    uint32_t        *strip_sprite_px;
    const pc_assets *as;
    pc_frame_params  fp;
    pc_fb           *fb;
    pc_backend_stats stats;
} cpu_state;

LOCAL int cpu_init(pc_backend *be, pc_arena *arena, const pc_backend_cfg *cfg)
{
    cpu_state *st = (cpu_state *)pc_arena_alloc(arena, sizeof *st, 64);
    if (!st)
        return -1;
    st->cfg = *cfg;
    if (st->cfg.threads < 1)
        st->cfg.threads = 1;
    if (st->cfg.strip_h < 1)
        st->cfg.strip_h = 32;

    /* A tall wall column lands in every strip it crosses, so entries outnumber commands. */
    int nstrips = (cfg->h + st->cfg.strip_h - 1) / st->cfg.strip_h;
    size_t entries_cap = (size_t)cfg->max_cmds + (size_t)cfg->w * (size_t)nstrips * 6;
    if (pc_bins_init(&st->bins, arena, cfg->h, st->cfg.strip_h, entries_cap))
        return -1;
    st->strip_sprite_px = (uint32_t *)pc_arena_alloc_array(arena, (size_t)st->bins.nstrips,
                                                           sizeof(uint32_t), 64);
    if (!st->strip_sprite_px)
        return -1;

    st->as = NULL;
    st->fb = NULL;
    st->stats.sprite_px = st->stats.cmds_dropped = 0;
    be->state = st;
    return 0;
}

LOCAL int cpu_upload_assets(pc_backend *be, const pc_assets *as)
{
    ((cpu_state *)be->state)->as = as;     /* nothing to copy: textures are read in place */
    return 0;
}

LOCAL void cpu_begin_frame(pc_backend *be, const pc_frame_params *fp, pc_fb *target)
{
    cpu_state *st = (cpu_state *)be->state;
    st->fp = *fp;
    st->fb = target;
    st->stats.sprite_px = st->stats.cmds_dropped = 0;
}

/* ---- drawing a strip ----------------------------------------------------------- */

LOCAL void draw_wall(const cpu_state *st, const pc_cmd *c, int s0, int s1)
{
    const int ya = c->y0 > s0 ? c->y0 : s0;
    const int yb = c->y1 < s1 ? c->y1 : s1;
    if (ya >= yb)
        return;

    const uint8_t *tex = st->as->tex[c->tex];
    const uint8_t *cm = st->as->colormap[c->level];
    const int w = st->fb->w;
    uint8_t *dst = st->fb->pix + (size_t)ya * w + c->x0;
    uint32_t v = c->v0 + (uint32_t)(ya - c->y0) * c->dv;

    for (int y = ya; y < yb; y++) {
        int ty = (int)(v >> FX_SHIFT) & (PC_TEX_SIZE - 1);
        *dst = cm[tex[(ty << PC_TEX_SHIFT) + c->tx]];
        dst += w;
        v += c->dv;
    }
}

LOCAL void draw_span(const cpu_state *st, const pc_cmd *c)
{
    const uint8_t *tex = st->as->tex[c->tex];
    const uint8_t *cm = st->as->colormap[c->level];
    uint8_t *row = st->fb->pix + (size_t)c->y0 * st->fb->w;
    const pc_vec2 pos = st->fp.pos;

    for (int x = c->x0; x < c->x1; x++) {
        fx_t wx = pos.x + fx_mul(c->dist, st->fp.ray_x[x]);
        fx_t wy = pos.y + fx_mul(c->dist, st->fp.ray_y[x]);
        int tx = fx_frac(wx) >> (FX_SHIFT - PC_TEX_SHIFT);
        int ty = fx_frac(wy) >> (FX_SHIFT - PC_TEX_SHIFT);
        row[x] = cm[tex[(ty << PC_TEX_SHIFT) + tx]];
    }
}

LOCAL uint32_t draw_sprite_col(const cpu_state *st, const pc_cmd *c, int s0, int s1)
{
    const int ya = c->y0 > s0 ? c->y0 : s0;
    const int yb = c->y1 < s1 ? c->y1 : s1;
    if (ya >= yb)
        return 0;

    const uint8_t *pix = st->as->sprites[c->tex].pix;
    const uint8_t *cm = st->as->colormap[c->level];
    const int w = st->fb->w;
    uint8_t *dst = st->fb->pix + (size_t)ya * w + c->x0;
    uint32_t v = c->v0 + (uint32_t)(ya - c->y0) * c->dv;
    uint32_t drawn = 0;

    for (int y = ya; y < yb; y++, dst += w, v += c->dv) {
        int ty = (int)(v >> FX_SHIFT);
        if (ty >= PC_TEX_SIZE)
            ty = PC_TEX_SIZE - 1;
        uint8_t px = pix[(ty << PC_TEX_SHIFT) + c->tx];
        if (px) {
            *dst = cm[px];
            drawn++;
        }
    }
    return drawn;
}

LOCAL void draw_strip(cpu_state *st, const pc_cmdbuf *cb, int s)
{
    const int s0 = s * st->bins.strip_h;
    const int s1 = s0 + st->bins.strip_h < st->fb->h ? s0 + st->bins.strip_h : st->fb->h;
    uint32_t sprite_px = 0;

    for (uint32_t k = st->bins.offsets[s]; k < st->bins.offsets[s + 1]; k++) {
        const pc_cmd *c = &cb->cmds[st->bins.entries[k]];
        switch (c->type) {
        case PC_CMD_WALL_COL:   draw_wall(st, c, s0, s1); break;
        case PC_CMD_PLANE_SPAN: draw_span(st, c); break;
        case PC_CMD_SPRITE_COL: sprite_px += draw_sprite_col(st, c, s0, s1); break;
        default: break;
        }
    }
    st->strip_sprite_px[s] = sprite_px;
}

LOCAL void cpu_submit(pc_backend *be, const pc_cmdbuf *cb)
{
    cpu_state *st = (cpu_state *)be->state;
    st->stats.cmds_dropped = pc_bins_build(&st->bins, cb, st->fb->h);

    const int n = st->bins.nstrips;
#ifdef _OPENMP
    /* dynamic: strips differ a lot in cost (a sky-high wall strip vs plain floor) */
    if (st->cfg.threads > 1) {
#pragma omp parallel for schedule(dynamic, 1) num_threads(st->cfg.threads)
        for (int s = 0; s < n; s++)
            draw_strip(st, cb, s);
    } else
#endif
    {
        for (int s = 0; s < n; s++)
            draw_strip(st, cb, s);
    }

    /* summed in strip order, so the total never depends on thread timing */
    uint32_t sprite_px = 0;
    for (int s = 0; s < n; s++)
        sprite_px += st->strip_sprite_px[s];
    st->stats.sprite_px = sprite_px;
}

LOCAL void cpu_end_frame(pc_backend *be)
{
    (void)be;       /* strips draw straight into the target; nothing to resolve */
}

LOCAL const uint8_t *cpu_framebuffer(const pc_backend *be)
{
    const cpu_state *st = (const cpu_state *)be->state;
    return st->fb ? st->fb->pix : NULL;
}

LOCAL void cpu_shutdown(pc_backend *be)
{
    be->state = NULL;       /* the arena owns the memory */
}

LOCAL pc_backend_stats cpu_stats(const pc_backend *be)
{
    return ((const cpu_state *)be->state)->stats;
}

void pc_backend_cpu_make(pc_backend *be)
{
    be->name = "cpu-strips";
    be->state = NULL;
    be->init = cpu_init;
    be->upload_assets = cpu_upload_assets;
    be->begin_frame = cpu_begin_frame;
    be->submit = cpu_submit;
    be->end_frame = cpu_end_frame;
    be->framebuffer = cpu_framebuffer;
    be->shutdown = cpu_shutdown;
    be->stats = cpu_stats;
}
