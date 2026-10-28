#ifndef PC_RENDER_BACKEND_H
#define PC_RENDER_BACKEND_H

#include <stdint.h>
#include "assets/assets.h"
#include "core/arena.h"
#include "core/fx.h"
#include "render/cmdbuf/cmdbuf.h"
#include "render/fb.h"

/* A back-end turns a command buffer into pixels. The CPU strip rasterizer is the
 * reference; any other back-end is checked against its output. */

typedef struct {
    int      w, h;
    int      threads;       /* worker threads; 1 = draw on the calling thread */
    int      strip_h;       /* rows per strip, the unit of parallel work */
    uint32_t max_cmds;      /* largest command buffer that will be submitted */
} pc_backend_cfg;

/* Per-frame inputs the commands refer to by index or implicitly. */
typedef struct {
    int         w, h;
    pc_vec2     pos;        /* camera position, for floor and ceiling texturing */
    const fx_t *ray_x;      /* ray direction per column */
    const fx_t *ray_y;
} pc_frame_params;

typedef struct {
    uint32_t sprite_px;     /* sprite pixels drawn (transparent texels excluded) */
    uint32_t cmds_dropped;  /* commands left out because the bin table filled up */
} pc_backend_stats;

typedef struct pc_backend pc_backend;

struct pc_backend {
    const char *name;
    void       *state;

    int  (*init)(pc_backend *be, pc_arena *arena, const pc_backend_cfg *cfg);
    int  (*upload_assets)(pc_backend *be, const pc_assets *as);
    void (*begin_frame)(pc_backend *be, const pc_frame_params *fp, pc_fb *target);
    void (*submit)(pc_backend *be, const pc_cmdbuf *cb);
    void (*end_frame)(pc_backend *be);
    const uint8_t *(*framebuffer)(const pc_backend *be);
    void (*shutdown)(pc_backend *be);
    pc_backend_stats (*stats)(const pc_backend *be);
};

/* Fills the function table for the CPU strip rasterizer. Call init next. */
void pc_backend_cpu_make(pc_backend *be);

#endif
