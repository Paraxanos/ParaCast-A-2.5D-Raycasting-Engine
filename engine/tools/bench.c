/* paracast_bench [frames] [csv_path] [--map NAME|FILE] [--size WxH] [--threads N]
 *                [--strip H] [--convert] [--look DEG]
 * Headless benchmark over a deterministic camera path. Without --map it times the v0 grid
 * raycaster; with it, the sector renderer, split into front-end (traversal, emitting
 * commands) and back-end (drawing them). Reports median / p95 / p99 frame time and a hash
 * chained over every frame, so a performance change that also changes pixels shows up in
 * the same line. --convert adds the indexed-to-RGBA expansion to the timing, as its own
 * column, to approximate what a presented frame costs. */

#include "scene.h"

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

static double median_ms(uint64_t *v, uint32_t n)
{
    qsort(v, n, sizeof *v, cmp_u64);
    return v[n / 2] / 1e6;
}

/* --look DEG holds the view near one direction (sector maps only) instead of spinning. */
static int      g_have_look;
static pc_angle g_look;

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
    if (g_have_look) {
        /* look direction plus a +-10 degree sway, so frames still differ */
        int sway = (int)(((int64_t)pc_sin((pc_angle)(2u * a)) * 1820) >> FX_SHIFT);
        c.yaw = (pc_angle)(g_look + sway);
    }
    return c;
}

static void render_split(scene *sc, const pc_camera *cam, pc_stats *st, uint64_t *front_ns,
                         uint64_t *back_ns)
{
    if (!sc->sector_mode) {
        uint64_t t0 = pc_time_ns();
        scene_render(sc, cam, st);
        *front_ns = 0;
        *back_ns = pc_time_ns() - t0;
        return;
    }
    uint64_t t0 = pc_time_ns();
    pc_sector_frontend(&sc->sr, &sc->map, &sc->assets, cam, st);
    uint64_t t1 = pc_time_ns();
    pc_sector_backend(&sc->sr, &sc->fb, st);
    uint64_t t2 = pc_time_ns();
    *front_ns = t1 - t0;
    *back_ns = t2 - t1;
}

int main(int argc, char **argv)
{
    uint32_t frames = 600;
    const char *csv = NULL, *map = NULL;
    int npos = 0, convert = 0;
    scene_opts opts = scene_default_opts();

    for (int i = 1; i < argc; i++) {
        int r = scene_parse_opt(&opts, argc, argv, &i);
        if (r < 0) {
            fprintf(stderr, "bad value for %s\n", argv[i - 1]);
            return 2;
        }
        if (r)
            continue;

        if (!strcmp(argv[i], "--map") && i + 1 < argc)
            map = argv[++i];
        else if (!strcmp(argv[i], "--convert"))
            convert = 1;
        else if (!strcmp(argv[i], "--look") && i + 1 < argc) {
            g_look = PC_ANGLE_DEG(atof(argv[++i]));
            g_have_look = 1;
        }
        else if (npos == 0 && argv[i][0] >= '0' && argv[i][0] <= '9') {
            frames = (uint32_t)atoi(argv[i]);
            npos++;
        } else if (!csv)
            csv = argv[i];
        else {
            fprintf(stderr, "usage: paracast_bench [frames] [csv] [--map NAME] [--size WxH] "
                            "[--threads N] [--strip H] [--convert]\n");
            return 2;
        }
    }
    if (frames < 2)
        return 1;
    if (!map && (opts.threads != 1 || opts.strip_h))
        fprintf(stderr, "note: --threads and --strip only affect the sector renderer\n");

    uint64_t *total = (uint64_t *)malloc(frames * sizeof *total);
    uint64_t *front = (uint64_t *)malloc(frames * sizeof *front);
    uint64_t *back = (uint64_t *)malloc(frames * sizeof *back);
    uint64_t *conv = (uint64_t *)malloc(frames * sizeof *conv);
    scene sc;
    if (!total || !front || !back || !conv || scene_init(&sc, map, &opts))
        return 1;
    const int W = sc.w, H = sc.h;

    pc_stats st = { 0 };
    uint64_t f_ns, b_ns;
    for (uint32_t i = 0; i < WARMUP; i++) {
        pc_camera cam = path(&sc, i, frames);
        render_split(&sc, &cam, &st, &f_ns, &b_ns);
    }

    uint32_t allocs_before = sc.arena.allocs;
    uint64_t chain = PC_HASH_SEED;
    uint64_t work = 0, visits = 0, cmds = 0, dropped = 0;
    for (uint32_t i = 0; i < frames; i++) {
        pc_camera cam = path(&sc, i, frames);
        render_split(&sc, &cam, &st, &front[i], &back[i]);
        total[i] = front[i] + back[i];

        conv[i] = 0;
        if (convert) {
            uint64_t t0 = pc_time_ns();
            pc_present_rgba(&sc.fb, &sc.assets, sc.rgba);
            conv[i] = pc_time_ns() - t0;
        }
        chain = pc_hash64(sc.fb.pix, (size_t)W * H, chain);
        work += sc.sector_mode ? st.portal_columns : st.dda_steps;
        visits += st.sector_visits;
        cmds += st.commands;
        dropped += st.cmds_dropped;
    }
    uint32_t frame_allocs = sc.arena.allocs - allocs_before;

    uint64_t sum = 0;
    for (uint32_t i = 0; i < frames; i++)
        sum += total[i];
    double mean = sum / 1e6 / frames;
    double front_ms = median_ms(front, frames), back_ms = median_ms(back, frames);
    double conv_ms = convert ? median_ms(conv, frames) : 0.0;
    qsort(total, frames, sizeof *total, cmp_u64);
    double med = total[frames / 2] / 1e6;
    double p95 = total[(uint64_t)frames * 95 / 100] / 1e6;
    double p99 = total[(uint64_t)frames * 99 / 100] / 1e6;
    double ns_px = total[frames / 2] / (double)((uint64_t)W * H);
    const char *label = map ? map : "grid";

    printf("paracast_bench %s (%s) %s %dx%d, %u frames, %d thread(s)\n", PC_GIT_HASH,
           PC_BUILD_TYPE, label, W, H, frames, sc.sector_mode ? opts.threads : 1);
    printf("  median %.3f ms  p95 %.3f ms  p99 %.3f ms  mean %.3f ms\n", med, p95, p99, mean);
    if (sc.sector_mode) {
        printf("  front-end %.3f ms  back-end %.3f ms (medians)", front_ms, back_ms);
        if (convert)
            printf("  rgba convert %.3f ms", conv_ms);
        printf("\n  %.2f ns/pixel  %.1f sector visits/frame  %.0f portal columns/frame  "
               "%.0f commands/frame  %u allocs in frame loop\n", ns_px, (double)visits / frames,
               (double)work / frames, (double)cmds / frames, frame_allocs);
        if (dropped)
            printf("  WARNING: %llu commands dropped\n", (unsigned long long)dropped);
    } else {
        printf("  %.2f ns/pixel  %.0f dda steps/frame  %u allocs in frame loop\n", ns_px,
               (double)work / frames, frame_allocs);
        if (convert)
            printf("  rgba convert %.3f ms\n", conv_ms);
    }
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
            fputs("commit,build,map,width,height,threads,strip_h,frames,median_ms,p95_ms,p99_ms,"
                  "mean_ms,front_ms,back_ms,convert_ms,ns_per_px,work_per_frame,"
                  "visits_per_frame,commands_per_frame,run_hash\n", f);
        fprintf(f, "%s,%s,%s,%d,%d,%d,%d,%u,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.3f,%.1f,%.2f,"
                   "%.0f,%016llx\n", PC_GIT_HASH, PC_BUILD_TYPE, label, W, H,
                sc.sector_mode ? opts.threads : 1, opts.strip_h, frames, med, p95, p99, mean,
                front_ms, back_ms, conv_ms, ns_px, (double)work / frames,
                (double)visits / frames, (double)cmds / frames, (unsigned long long)chain);
        fclose(f);
    }

    free(total);
    free(front);
    free(back);
    free(conv);
    scene_free(&sc);
    return 0;
}
