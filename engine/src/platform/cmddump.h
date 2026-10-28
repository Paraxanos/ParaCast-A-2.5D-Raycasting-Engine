#ifndef PC_PLATFORM_CMDDUMP_H
#define PC_PLATFORM_CMDDUMP_H

#include <stdint.h>
#include "core/arena.h"
#include "render/backend.h"
#include "render/cmdbuf/cmdbuf.h"

/* One frame's command buffer on disk, so a back-end can be run (and debugged, and timed)
 * without rerunning the simulation or the front-end.
 *
 * Layout, all 32-bit words in native byte order (little-endian hosts only):
 *   magic 'PCMD', version, w, h, ncmds, pos.x, pos.y, assets hash (low, high)
 *   ray_x[w], ray_y[w], then ncmds records of 32 bytes.
 * The assets hash says which art the frame was built against; drawing a dump with
 * different assets is legal but produces different pixels. */

int pc_cmddump_write(const char *path, const pc_frame_params *fp, const pc_cmdbuf *cb,
                     uint64_t assets_hash);

/* Reads a dump into the arena. fp->ray_x / ray_y and cb->cmds point into the arena. The
 * records are validated (see pc_cmdbuf_validate); a malformed file returns -1 and, if
 * err is not NULL, a short reason. */
int pc_cmddump_read(const char *path, pc_arena *arena, pc_frame_params *fp, pc_cmdbuf *cb,
                    uint64_t *assets_hash, const char **err);

#endif
