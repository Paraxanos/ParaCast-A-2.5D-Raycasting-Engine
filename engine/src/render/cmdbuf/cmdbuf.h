#ifndef PC_RENDER_CMDBUF_CMDBUF_H
#define PC_RENDER_CMDBUF_CMDBUF_H

#include <stddef.h>
#include <stdint.h>
#include "core/arena.h"
#include "core/fx.h"

/* The seam between traversal and drawing. The front-end decides what to draw and writes
 * these fixed-size records; a back-end draws them. Records are plain data with no
 * pointers, so a buffer can be dumped to a file, replayed through any back-end, or
 * uploaded to a GPU as it is. */

enum {
    PC_CMD_WALL_COL    = 1,    /* one column of one wall piece */
    PC_CMD_PLANE_SPAN  = 2,    /* a run of one floor or ceiling row */
    PC_CMD_SPRITE_COL  = 3     /* one column of a billboard, already clipped */
};

/* Field use by type:
 *   WALL_COL    x0 = column, y0..y1 = rows, tex = texture id, tx = texel column,
 *               v0/dv = texel row at y0 and per row (only bits 16..21 matter, so the
 *               values are kept modulo 2^32), level = light level
 *   PLANE_SPAN  x0..x1 = columns, y0 = the row, tex = texture id, dist = row distance
 *               (16.16), level = light level
 *   SPRITE_COL  x0 = column, y0..y1 = visible rows, tex = sprite id, tx = texel column,
 *               v0/dv = 16.16 texel row at y0 and per row (clamped to the last texel),
 *               level = light level; palette index 0 is transparent
 * visit and wall name the sector visit and wall that produced the record. Drawing
 * ignores them; they exist for debugging and for ordering repeated visits later. */
typedef struct {
    uint8_t  type, tex, level, tx;
    uint16_t x0, x1;
    int16_t  y0, y1;
    uint32_t v0, dv;
    int32_t  dist;
    uint16_t visit, wall;
    uint32_t reserved;
} pc_cmd;

typedef char pc_cmd_is_32_bytes[sizeof(pc_cmd) == 32 ? 1 : -1];

typedef struct {
    pc_cmd   *cmds;
    uint32_t  count, cap;
    uint32_t  dropped;     /* pushes refused because the buffer was full */
    int       strict;      /* abort on overflow: on in debug builds, off in release */
} pc_cmdbuf;

int pc_cmdbuf_init(pc_cmdbuf *cb, pc_arena *arena, uint32_t cap);

static inline void pc_cmdbuf_reset(pc_cmdbuf *cb)
{
    cb->count = 0;
    cb->dropped = 0;
}

void pc_cmdbuf_overflow(pc_cmdbuf *cb);

/* Next free record, or NULL if the buffer is full (the push is counted as dropped).
 * In strict mode a full buffer aborts instead, so tests and debug builds fail loudly. */
static inline pc_cmd *pc_cmdbuf_push(pc_cmdbuf *cb)
{
    if (cb->count >= cb->cap) {
        pc_cmdbuf_overflow(cb);
        return NULL;
    }
    return &cb->cmds[cb->count++];
}

/* True if every record is something a back-end can draw safely at this size: a known
 * type, columns and rows inside the screen, ids inside the asset tables. Dumps read from
 * disk go through this before they are drawn. Returns the index of the first bad record
 * through *bad (optional). */
int pc_cmdbuf_validate(const pc_cmdbuf *cb, int w, int h, uint32_t *bad);

/* ---- screen strips ----------------------------------------------------------- */

/* Commands grouped by horizontal band of rows. A command is listed in every band it
 * touches, in submission order, so sprites still draw after the walls they sit on and
 * far sprites before near ones. Walls and planes never overlap each other, so their
 * relative order does not matter; clipping to the band is the drawing side's job. */
typedef struct {
    int       nstrips, strip_h;
    uint32_t *offsets;      /* nstrips + 1; entries[offsets[s] .. offsets[s+1]) is strip s */
    uint32_t *cursor;       /* scratch */
    uint32_t *entries;      /* command indices */
    size_t    entries_cap;
} pc_bins;

int pc_bins_init(pc_bins *b, pc_arena *arena, int h, int strip_h, size_t entries_cap);

/* Bins the first commands that fit; returns how many commands were left out because
 * the entry table filled up (0 in any sane frame). */
uint32_t pc_bins_build(pc_bins *b, const pc_cmdbuf *cb, int h);

#endif
