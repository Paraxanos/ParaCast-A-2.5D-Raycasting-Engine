/* paracast_bench [frames] [csv_path] [--map NAME|FILE]: headless benchmark over a
 * deterministic camera path. Without --map it times the v0 grid raycaster; with it, the
 * sector renderer. Reports median / p95 / p99 frame time and a hash chained over every
 * frame, so a performance change that also changes pixels is visible in the same line. */

#include "scene.h"

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

/* Grid: one turn of orbit around the room centre, yaw spinning three times as fast.
 * Sector map: a small circle around the spawn point with the view spinning twice, so
 * the portal graph is seen from every direction without leaving the spawn sector. */
static pc_camera path(const scene *sc, uint32_t i, uint32_t frames)
{
    pc_angle a = (pc_angle)((uint64_t)i * PC_ANGLE_FULL / frames);
    pc_camera c = scene_spawn(sc);

    if (!sc->sector_mode) {
        c.pos.x = FX_C(12.5) + 3 * pc_cos(a);
        c.pos.y = FX_C(12.5) + 3 * pc_sin(a);
        c.yaw = (pc_angle)(3u * a);
        return c;
    }
    c.pos.x += fx_mul(pc_cos(a), FX_C(0.4));
    c.pos.y += fx_mul(pc_sin(a), FX_C(0.4));
    c.yaw = (pc_angle)(2u * a);
    return c;
}

int main(int argc, char **argv)
{
    uint32_t frames = 600;
    const char *csv = NULL, *map = NULL;
    int npos = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--map") && i + 1 < argc)
            map = argv[++i];
        else if (npos == 0 && argv[i][0] >= '0' && argv[i][0] <= '9') {
            frames = (uint32_t)atoi(argv[i]);
            npos++;
        } else if (!csv)
            csv = argv[i];
        else {
            fprintf(stderr, "usage: paracast_bench [frames] [csv] [--map NAME]\n");
            return 2;
        }
    }
    if (frames < 2)
        return 1;

    uint64_t *ns = (uint64_t *)malloc(frames * sizeof *ns);
    scene sc;
    if (!ns || scene_init(&sc, map, WIDTH, HEIGHT))
        return 1;

    pc_stats st = { 0 };
    for (uint32_t i = 0; i < WARMUP; i++) {
        pc_camera cam = path(&sc, i, frames);
        scene_render(&sc, &cam, &st);
    }

    uint32_t allocs_before = sc.arena.allocs;
    uint64_t chain = PC_HASH_SEED;
    uint64_t work = 0, visits = 0;
    for (uint32_t i = 0; i < frames; i++) {
        pc_camera cam = path(&sc, i, frames);
        uint64_t t0 = pc_time_ns();
        scene_render(&sc, &cam, &st);
        ns[i] = pc_time_ns() - t0;
        chain = pc_hash64(sc.fb.pix, (size_t)WIDTH * HEIGHT, chain);
        work += sc.sector_mode ? st.portal_columns : st.dda_steps;
        visits += st.sector_visits;
    }
    uint32_t frame_allocs = sc.arena.allocs - allocs_before;

    uint64_t total = 0;
    for (uint32_t i = 0; i < frames; i++)
        total += ns[i];
    qsort(ns, frames, sizeof *ns, cmp_u64);

    double med = ns[frames / 2] / 1e6;
    double p95 = ns[(uint64_t)frames * 95 / 100] / 1e6;
    double p99 = ns[(uint64_t)frames * 99 / 100] / 1e6;
    double mean = total / 1e6 / frames;
    double ns_px = ns[frames / 2] / (double)(WIDTH * HEIGHT);
    const char *label = map ? map : "grid";

    printf("paracast_bench %s (%s) %s %dx%d, %u frames\n", PC_GIT_HASH, PC_BUILD_TYPE, label,
           WIDTH, HEIGHT, frames);
    printf("  median %.3f ms  p95 %.3f ms  p99 %.3f ms  mean %.3f ms\n", med, p95, p99, mean);
    if (sc.sector_mode)
        printf("  %.2f ns/pixel  %.1f sector visits/frame  %.0f portal columns/frame  "
               "%u allocs in frame loop\n", ns_px, (double)visits / frames,
               (double)work / frames, frame_allocs);
    else
        printf("  %.2f ns/pixel  %.0f dda steps/frame  %u allocs in frame loop\n", ns_px,
               (double)work / frames, frame_allocs);
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
            fputs("commit,build,map,width,height,frames,median_ms,p95_ms,p99_ms,mean_ms,"
                  "ns_per_px,work_per_frame,visits_per_frame,run_hash\n", f);
        fprintf(f, "%s,%s,%s,%d,%d,%u,%.4f,%.4f,%.4f,%.4f,%.3f,%.1f,%.2f,%016llx\n",
                PC_GIT_HASH, PC_BUILD_TYPE, label, WIDTH, HEIGHT, frames, med, p95, p99, mean,
                ns_px, (double)work / frames, (double)visits / frames,
                (unsigned long long)chain);
        fclose(f);
    }

    free(ns);
    scene_free(&sc);
    return 0;
}
