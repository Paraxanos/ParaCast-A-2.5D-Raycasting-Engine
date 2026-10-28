#include "render/cmdbuf/cmdbuf.h"

#include <stdlib.h>

#include "assets/assets.h"

int pc_cmdbuf_init(pc_cmdbuf *cb, pc_arena *arena, uint32_t cap)
{
    cb->cmds = (pc_cmd *)pc_arena_alloc_array(arena, cap, sizeof(pc_cmd), 64);
    cb->count = 0;
    cb->cap = cap;
    cb->dropped = 0;
#ifdef NDEBUG
    cb->strict = 0;
#else
    cb->strict = 1;
#endif
    return cb->cmds ? 0 : -1;
}

void pc_cmdbuf_overflow(pc_cmdbuf *cb)
{
    cb->dropped++;
    if (cb->strict)
        abort();
}

int pc_bins_init(pc_bins *b, pc_arena *arena, int h, int strip_h, size_t entries_cap)
{
    if (strip_h < 1)
        strip_h = 1;
    b->strip_h = strip_h;
    b->nstrips = (h + strip_h - 1) / strip_h;
    b->entries_cap = entries_cap;
    b->offsets = (uint32_t *)pc_arena_alloc_array(arena, (size_t)b->nstrips + 1, sizeof(uint32_t), 64);
    b->cursor = (uint32_t *)pc_arena_alloc_array(arena, (size_t)b->nstrips, sizeof(uint32_t), 64);
    b->entries = (uint32_t *)pc_arena_alloc_array(arena, entries_cap, sizeof(uint32_t), 64);
    return b->offsets && b->cursor && b->entries ? 0 : -1;
}

/* First and last strip a command touches. */
static inline void strip_range(const pc_cmd *c, int strip_h, int h, int *s0, int *s1)
{
    int ya = c->y0, yb = c->type == PC_CMD_PLANE_SPAN ? c->y0 + 1 : c->y1;
    if (ya < 0)
        ya = 0;
    if (yb > h)
        yb = h;
    if (yb <= ya) {
        *s0 = 0;
        *s1 = -1;           /* empty */
        return;
    }
    *s0 = ya / strip_h;
    *s1 = (yb - 1) / strip_h;
}

uint32_t pc_bins_build(pc_bins *b, const pc_cmdbuf *cb, int h)
{
    const int n = b->nstrips;
    uint32_t fit = cb->count;
    size_t total = 0;

    for (int s = 0; s <= n; s++)
        b->offsets[s] = 0;

    /* pass 1: count entries per strip, stopping at the first command that would overflow */
    for (uint32_t i = 0; i < cb->count; i++) {
        int s0, s1;
        strip_range(&cb->cmds[i], b->strip_h, h, &s0, &s1);
        size_t add = s1 >= s0 ? (size_t)(s1 - s0 + 1) : 0;
        if (total + add > b->entries_cap) {
            fit = i;
            break;
        }
        for (int s = s0; s <= s1; s++)
            b->offsets[s + 1]++;
        total += add;
    }

    for (int s = 0; s < n; s++)
        b->offsets[s + 1] += b->offsets[s];
    for (int s = 0; s < n; s++)
        b->cursor[s] = b->offsets[s];

    /* pass 2: place indices; walking commands in order keeps each strip in submission order */
    for (uint32_t i = 0; i < fit; i++) {
        int s0, s1;
        strip_range(&cb->cmds[i], b->strip_h, h, &s0, &s1);
        for (int s = s0; s <= s1; s++)
            b->entries[b->cursor[s]++] = i;
    }
    return cb->count - fit;
}

int pc_cmdbuf_validate(const pc_cmdbuf *cb, int w, int h, uint32_t *bad)
{
    for (uint32_t i = 0; i < cb->count; i++) {
        const pc_cmd *c = &cb->cmds[i];
        int ok = c->level < PC_LIGHT_LEVELS && c->x0 < c->x1 && c->x1 <= w;

        switch (c->type) {
        case PC_CMD_WALL_COL:
            ok = ok && c->x1 == c->x0 + 1 && c->tex < PC_TEX_COUNT && c->tx < PC_TEX_SIZE &&
                 c->y0 >= 0 && c->y0 < c->y1 && c->y1 <= h;
            break;
        case PC_CMD_PLANE_SPAN:
            ok = ok && c->tex < PC_TEX_COUNT && c->y0 >= 0 && c->y0 < h;
            break;
        case PC_CMD_SPRITE_COL:
            ok = ok && c->x1 == c->x0 + 1 && c->tex < PC_SPRITE_COUNT && c->tx < PC_TEX_SIZE &&
                 c->y0 >= 0 && c->y0 < c->y1 && c->y1 <= h;
            break;
        default:
            ok = 0;
        }
        if (!ok) {
            if (bad)
                *bad = i;
            return -1;
        }
    }
    return 0;
}
