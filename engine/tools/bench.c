/* paracast_bench [frames] [csv_path]: headless benchmark over a deterministic camera orbit.
 * Reports median / p95 / p99 frame time and a hash chained over every frame, so a
 * performance change that also changes pixels is visible in the same line. */

#include "paracast.h"

#include <stdio.h>
#include <stdlib.h>

#define WIDTH   640
#define HEIGHT  400
#define WARMUP  30

#ifndef PC_GIT_HASH
#define PC_GIT_HASH "unknown"
#endif
#ifndef PC_BUILD_TYPE
#define PC_BUILD_TYPE "unknown"
#endif

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

/* One full turn of orbit around the room centre; yaw spins three times as fast. */
static pc_camera path(uint32_t i, uint32_t frames)
{
    pc_angle a = (pc_angle)((uint64_t)i * PC_ANGLE_FULL / frames);
    pc_camera c;
    c.pos.x = FX_C(12.5) + 3 * pc_cos(a);
    c.pos.y = FX_C(12.5) + 3 * pc_sin(a);
    c.yaw = (pc_angle)(3u * a);
    return c;
}

int main(int argc, char **argv)
{
    uint32_t frames = argc > 1 ? (uint32_t)atoi(argv[1]) : 600;
    const char *csv = argc > 2 ? argv[2] : NULL;
    if (frames < 2)
        return 1;

    size_t mem_size = (size_t)8 << 20;
    void *mem = malloc(mem_size);
    uint64_t *ns = (uint64_t *)malloc(frames * sizeof *ns);
    if (!mem || !ns)
        return 1;

    pc_arena arena;
    pc_arena_init(&arena, mem, mem_size);
    pc_lut_init();

    pc_assets assets;
    pc_gridmap map;
    pc_fb fb;
    pc_raycaster rc;
    pc_gridmap_make_demo(&map);
    if (pc_assets_init(&assets, &arena) || pc_fb_init(&fb, &arena, WIDTH, HEIGHT) ||
        pc_raycaster_init(&rc, &arena, WIDTH, HEIGHT)) {
        fprintf(stderr, "out of arena memory\n");
        return 1;
    }

    pc_stats st = { 0 };
    for (uint32_t i = 0; i < WARMUP; i++) {
        pc_camera cam = path(i, frames);
        pc_raycaster_render(&rc, &fb, &map, &assets, &cam, &st);
    }

    uint32_t allocs_before = arena.allocs;
    uint64_t chain = PC_HASH_SEED;
    uint64_t steps = 0;
    for (uint32_t i = 0; i < frames; i++) {
        pc_camera cam = path(i, frames);
        uint64_t t0 = pc_time_ns();
        pc_raycaster_render(&rc, &fb, &map, &assets, &cam, &st);
        ns[i] = pc_time_ns() - t0;
        chain = pc_hash64(fb.pix, (size_t)WIDTH * HEIGHT, chain);
        steps += st.dda_steps;
    }
    uint32_t frame_allocs = arena.allocs - allocs_before;

    uint64_t total = 0;
    for (uint32_t i = 0; i < frames; i++)
        total += ns[i];
    qsort(ns, frames, sizeof *ns, cmp_u64);

    double med = ns[frames / 2] / 1e6;
    double p95 = ns[(uint64_t)frames * 95 / 100] / 1e6;
    double p99 = ns[(uint64_t)frames * 99 / 100] / 1e6;
    double mean = total / 1e6 / frames;
    double ns_px = ns[frames / 2] / (double)(WIDTH * HEIGHT);

    printf("paracast_bench %s (%s) %dx%d, %u frames\n", PC_GIT_HASH, PC_BUILD_TYPE, WIDTH,
           HEIGHT, frames);
    printf("  median %.3f ms  p95 %.3f ms  p99 %.3f ms  mean %.3f ms\n", med, p95, p99, mean);
    printf("  %.2f ns/pixel  %.0f dda steps/frame  %u allocs in frame loop\n", ns_px,
           (double)steps / frames, frame_allocs);
    printf("  run hash %016llx\n", (unsigned long long)chain);

    if (csv) {
        FILE *probe = fopen(csv, "rb");
        int fresh = probe == NULL;
        if (probe)
            fclose(probe);
        FILE *f = fopen(csv, "ab");
        if (!f) {
            fprintf(stderr, "cannot open %s\n", csv);
            return 1;
        }
        if (fresh)
            fputs("commit,build,width,height,frames,median_ms,p95_ms,p99_ms,mean_ms,"
                  "ns_per_px,dda_steps,run_hash\n", f);
        fprintf(f, "%s,%s,%d,%d,%u,%.4f,%.4f,%.4f,%.4f,%.3f,%.0f,%016llx\n", PC_GIT_HASH,
                PC_BUILD_TYPE, WIDTH, HEIGHT, frames, med, p95, p99, mean, ns_px,
                (double)steps / frames, (unsigned long long)chain);
        fclose(f);
    }

    free(ns);
    free(mem);
    return 0;
}
