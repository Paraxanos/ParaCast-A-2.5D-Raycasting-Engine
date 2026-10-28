#include "platform/cmddump.h"

#include <stdio.h>

#define MAGIC    0x444D4350u     /* 'P' 'C' 'M' 'D' as little-endian bytes */
#define VERSION  1u
#define NWORDS   9
#define MAX_DIM  16384u
#define MAX_CMDS (1u << 24)

int pc_cmddump_write(const char *path, const pc_frame_params *fp, const pc_cmdbuf *cb,
                     uint64_t assets_hash)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;

    uint32_t head[NWORDS] = {
        MAGIC, VERSION, (uint32_t)fp->w, (uint32_t)fp->h, cb->count,
        (uint32_t)fp->pos.x, (uint32_t)fp->pos.y,
        (uint32_t)assets_hash, (uint32_t)(assets_hash >> 32),
    };
    size_t w = (size_t)fp->w;
    int ok = fwrite(head, sizeof head[0], NWORDS, f) == NWORDS &&
             fwrite(fp->ray_x, sizeof(fx_t), w, f) == w &&
             fwrite(fp->ray_y, sizeof(fx_t), w, f) == w &&
             fwrite(cb->cmds, sizeof(pc_cmd), cb->count, f) == cb->count;
    if (fclose(f) != 0)
        ok = 0;
    return ok ? 0 : -1;
}

static int bad(const char **err, const char *msg)
{
    if (err)
        *err = msg;
    return -1;
}

int pc_cmddump_read(const char *path, pc_arena *arena, pc_frame_params *fp, pc_cmdbuf *cb,
                    uint64_t *assets_hash, const char **err)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return bad(err, "cannot open file");

    uint32_t head[NWORDS];
    if (fread(head, sizeof head[0], NWORDS, f) != NWORDS) {
        fclose(f);
        return bad(err, "file too short for a header");
    }
    if (head[0] != MAGIC || head[1] != VERSION) {
        fclose(f);
        return bad(err, "not a version 1 command dump");
    }
    const uint32_t w = head[2], h = head[3], n = head[4];
    if (w < 1 || h < 1 || w > MAX_DIM || h > MAX_DIM || n > MAX_CMDS) {
        fclose(f);
        return bad(err, "implausible size in header");
    }

    fx_t *rx = (fx_t *)pc_arena_alloc_array(arena, w, sizeof(fx_t), 64);
    fx_t *ry = (fx_t *)pc_arena_alloc_array(arena, w, sizeof(fx_t), 64);
    pc_cmd *cmds = (pc_cmd *)pc_arena_alloc_array(arena, n ? n : 1, sizeof(pc_cmd), 64);
    if (!rx || !ry || !cmds) {
        fclose(f);
        return bad(err, "out of arena memory");
    }

    int ok = fread(rx, sizeof(fx_t), w, f) == w && fread(ry, sizeof(fx_t), w, f) == w &&
             fread(cmds, sizeof(pc_cmd), n, f) == n;
    int trailing = ok && fgetc(f) != EOF;
    fclose(f);
    if (!ok)
        return bad(err, "file shorter than its header says");
    if (trailing)
        return bad(err, "extra data after the last record");

    fp->w = (int)w;
    fp->h = (int)h;
    fp->pos.x = (fx_t)head[5];
    fp->pos.y = (fx_t)head[6];
    fp->ray_x = rx;
    fp->ray_y = ry;
    cb->cmds = cmds;
    cb->count = n;
    cb->cap = n;
    cb->dropped = 0;
    cb->strict = 0;
    if (assets_hash)
        *assets_hash = (uint64_t)head[7] | ((uint64_t)head[8] << 32);

    uint32_t first_bad;
    if (pc_cmdbuf_validate(cb, (int)w, (int)h, &first_bad))
        return bad(err, "a record is out of range for this screen size");
    return 0;
}
