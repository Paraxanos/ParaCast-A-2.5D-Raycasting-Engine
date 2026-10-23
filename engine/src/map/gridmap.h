#ifndef PC_MAP_GRIDMAP_H
#define PC_MAP_GRIDMAP_H

#include <stdint.h>
#include "core/fx.h"

/* Phase 1 world: a uniform grid, 0 = empty, 1..N = wall texture + 1. Cells outside
 * the grid read as solid, so every ray terminates. */
#define PC_GRID_MAX 64

typedef struct {
    int            w, h;
    const uint8_t *cells;
} pc_gridmap;

typedef struct {
    fx_t    dist;    /* perpendicular distance, in units of the ray's forward component */
    fx_t    u;       /* position along the wall face in [0, FX_ONE), already mirrored for the viewer */
    int     side;    /* 0 = crossed a vertical grid line (x), 1 = a horizontal one (y) */
    uint8_t cell;
    int     steps;
} pc_grid_hit;

/* 24x24 test room with pillars and blocks; the central 7x7 stays open. */
void pc_gridmap_make_demo(pc_gridmap *m);

int pc_gridmap_cell(const pc_gridmap *m, int cx, int cy);

/* DDA from pos along rd. rd need not be normalized. */
void pc_grid_cast(const pc_gridmap *m, pc_vec2 pos, pc_vec2 rd, pc_grid_hit *out);

#endif
