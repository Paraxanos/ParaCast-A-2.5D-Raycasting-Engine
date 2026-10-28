/* paracast_replay FILE.pcmd [--threads N] [--strip H] [--repeat K] [--out FILE.ppm]
 * Draws a dumped command buffer through the CPU back-end without running the front-end:
 * prints the framebuffer hash, and with --repeat K the median back-end time over K draws.
 * Handy for checking that a back-end change keeps a frame bit-identical, and for timing
 * the back-end alone. */

#include "paracast.h"
#include "platform/cmddump.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

int main(int argc, char **argv)
{
    const char *path = NULL, *out = NULL;
    int threads = 1, strip_h = 0, repeat = 1;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--threads") && i + 1 < argc)
            threads = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--strip") && i + 1 < argc)
            strip_h = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--repeat") && i + 1 < argc)
            repeat = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc)
            out = argv[++i];
        else if (!path && argv[i][0] != '-')
            path = argv[i];
        else {
            fprintf(stderr, "usage: paracast_replay FILE.pcmd [--threads N] [--strip H] "
                            "[--repeat K] [--out FILE.ppm]\n");
            return 2;
        }
    }
    if (!path || threads < 1 || repeat < 1)
        return 2;

    size_t mem_size = (size_t)256 << 20;
    void *mem = malloc(mem_size);
    if (!mem)
        return 1;
    pc_arena arena;
    pc_arena_init(&arena, mem, mem_size);

    pc_frame_params fp;
    pc_cmdbuf cb;
    uint64_t dump_assets;
    const char *err = NULL;
    if (pc_cmddump_read(path, &arena, &fp, &cb, &dump_assets, &err)) {
        fprintf(stderr, "%s: %s\n", path, err ? err : "unreadable");
        return 1;
    }

    pc_assets assets;
    pc_fb fb;
    if (pc_assets_init(&assets, &arena) || pc_fb_init(&fb, &arena, fp.w, fp.h)) {
        fprintf(stderr, "out of arena memory\n");
        return 1;
    }
    if (pc_assets_hash(&assets) != dump_assets)
        fprintf(stderr, "warning: the dump was built against different assets\n");

    pc_backend be;
    pc_backend_cfg cfg = { fp.w, fp.h, threads, strip_h, cb.count ? cb.count : 1 };
    pc_backend_cpu_make(&be);
    if (be.init(&be, &arena, &cfg) || be.upload_assets(&be, &assets)) {
        fprintf(stderr, "backend init failed\n");
        return 1;
    }

    uint64_t *ns = (uint64_t *)malloc((size_t)repeat * sizeof *ns);
    if (!ns)
        return 1;
    for (int r = 0; r < repeat; r++) {
        uint64_t t0 = pc_time_ns();
        be.begin_frame(&be, &fp, &fb);
        be.submit(&be, &cb);
        be.end_frame(&be);
        ns[r] = pc_time_ns() - t0;
    }

    pc_backend_stats bs = be.stats(&be);
    printf("%s: %ux%u, %u commands, %d thread(s)\n", path, (unsigned)fp.w, (unsigned)fp.h,
           cb.count, threads);
    printf("  hash=%016llx  sprite_px=%u  dropped=%u\n",
           (unsigned long long)pc_hash64(fb.pix, (size_t)fp.w * fp.h, PC_HASH_SEED),
           bs.sprite_px, bs.cmds_dropped);
    if (repeat > 1) {
        qsort(ns, (size_t)repeat, sizeof *ns, cmp_u64);
        printf("  back-end median %.3f ms over %d draws\n", ns[repeat / 2] / 1e6, repeat);
    }

    if (out) {
        uint32_t *rgba = (uint32_t *)pc_arena_alloc_array(&arena, (size_t)fp.w * fp.h, 4, 64);
        if (!rgba)
            return 1;
        pc_present_rgba(&fb, &assets, rgba);
        if (pc_ppm_write(out, rgba, fp.w, fp.h)) {
            fprintf(stderr, "cannot write %s\n", out);
            return 1;
        }
        printf("  wrote %s\n", out);
    }
    be.shutdown(&be);
    free(ns);
    free(mem);
    return 0;
}
