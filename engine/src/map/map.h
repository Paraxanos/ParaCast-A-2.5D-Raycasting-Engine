#ifndef PC_MAP_MAP_H
#define PC_MAP_MAP_H

#include <stddef.h>
#include <stdint.h>
#include "core/arena.h"
#include "core/fx.h"
#include "core/lut.h"
#include "core/types.h"

/* Sector map: convex sectors joined by portals. Tables are structure-of-arrays and
 * addressed by pc_id, so the whole map is plain data. See docs/map-format.md. */

#define PC_MAX_SECTORS        256
#define PC_MAX_WALLS          4096
#define PC_MAX_SECTOR_WALLS   64
#define PC_MAX_THINGS         256

/* Coordinates and heights are limited so every cross product and projection in the
 * renderer fits its integer type; the loader rejects anything beyond them. */
#define PC_COORD_LIMIT        512
#define PC_MIN_EDGE           FX_C(0.0625)

typedef struct {
    uint32_t  nsectors, nwalls, nthings;

    /* per sector */
    fx_t     *floor_h, *ceil_h;
    uint8_t  *floor_tex, *ceil_tex;
    uint8_t  *light;            /* 0 (dark) .. 31 (full bright) */
    pc_id    *first_wall;
    uint16_t *wall_count;

    /* per wall. Walls of a sector run in order and close into a loop (b of the last
     * is a of the first). Interior is on the left of a->b: cross(e, p - a) >= 0. */
    pc_vec2  *a, *b, *e;        /* e = b - a */
    fx_t     *len;
    pc_id    *back;             /* PC_NONE = solid, else the sector behind a portal */
    uint8_t  *tex_mid, *tex_up, *tex_low;

    /* per thing */
    pc_vec2  *thing_pos;
    pc_id    *thing_sector;
    uint8_t  *thing_sprite;

    pc_vec2   spawn;
    pc_angle  spawn_yaw;
    pc_id     spawn_sector;
} pc_map;

/* Parses map text (not necessarily NUL-terminated) into tables allocated from arena.
 * Returns 0 on success; on failure returns -1 and writes "line N: reason" to err. */
int pc_map_load(const char *text, size_t len, pc_arena *arena, pc_map *map,
                char *err, size_t err_cap);

/* True if p is inside sector s; the boundary counts as inside. */
int pc_map_contains(const pc_map *m, pc_id s, pc_vec2 p);

/* Sector containing p, or PC_NONE. Tries hint and its portal neighbours before
 * scanning every sector, so frame-to-frame lookups are cheap. */
pc_id pc_map_find_sector(const pc_map *m, pc_vec2 p, pc_id hint);

#endif
