#include "pc_test.h"
#include "paracast.h"
#include "platform/cmddump.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 640
#define H 400
#define SENTINEL 0xEE

/* ---- command buffer and bins ---------------------------------------------- */

static pc_cmd make(int type, int x0, int x1, int y0, int y1)
{
    pc_cmd c;
    memset(&c, 0, sizeof c);
    c.type = (uint8_t)type;
    c.x0 = (uint16_t)x0;
    c.x1 = (uint16_t)x1;
    c.y0 = (int16_t)y0;
    c.y1 = (int16_t)y1;
    return c;
}

static void test_cmdbuf(void)
{
    PC_CHECK(sizeof(pc_cmd) == 32);

    static uint8_t mem[4096];
    pc_arena a;
    pc_arena_init(&a, mem, sizeof mem);
    pc_cmdbuf cb;
    PC_CHECK(pc_cmdbuf_init(&cb, &a, 4) == 0);
    cb.strict = 0;

    int got = 0;
    for (int i = 0; i < 6; i++)
        got += pc_cmdbuf_push(&cb) != NULL;
    PC_CHECK(got == 4);
    PC_CHECK(cb.count == 4 && cb.dropped == 2);

    pc_cmdbuf_reset(&cb);
    PC_CHECK(cb.count == 0 && cb.dropped == 0);
    PC_CHECK(pc_cmdbuf_push(&cb) != NULL);

    pc_arena tiny;
    uint8_t small[64];
    pc_arena_init(&tiny, small, sizeof small);
    PC_CHECK(pc_cmdbuf_init(&cb, &tiny, 1000) != 0);     /* arena too small */
}

static void test_bins(void)
{
    static uint8_t mem[64 * 1024];
    pc_arena a;
    pc_arena_init(&a, mem, sizeof mem);

    pc_bins b;
    PC_CHECK(pc_bins_init(&b, &a, 100, 32, 100) == 0);
    PC_CHECK(b.nstrips == 4);                              /* rows 0-31, 32-63, 64-95, 96-99 */

    pc_cmdbuf cb;
    PC_CHECK(pc_cmdbuf_init(&cb, &a, 16) == 0);
    cb.cmds[0] = make(PC_CMD_WALL_COL, 5, 6, 0, 100);      /* every strip */
    cb.cmds[1] = make(PC_CMD_PLANE_SPAN, 0, 10, 40, 41);   /* strip 1 only */
    cb.cmds[2] = make(PC_CMD_SPRITE_COL, 7, 8, 30, 34);    /* strips 0 and 1 */
    cb.cmds[3] = make(PC_CMD_WALL_COL, 9, 10, 50, 50);     /* empty: nowhere */
    cb.cmds[4] = make(PC_CMD_WALL_COL, 11, 12, 96, 100);   /* last, short strip */
    cb.cmds[5] = make(PC_CMD_PLANE_SPAN, 0, 10, 99, 100);  /* last row */
    cb.cmds[6] = make(PC_CMD_WALL_COL, 13, 14, 31, 32);    /* exactly the end of strip 0 */
    cb.cmds[7] = make(PC_CMD_WALL_COL, 15, 16, 32, 33);    /* exactly the start of strip 1 */
    cb.count = 8;

    PC_CHECK(pc_bins_build(&b, &cb, 100) == 0);

    const uint32_t s0[] = { 0, 2, 6 };
    const uint32_t s1[] = { 0, 1, 2, 7 };
    const uint32_t s2[] = { 0 };
    const uint32_t s3[] = { 0, 4, 5 };
    const uint32_t *want[4] = { s0, s1, s2, s3 };
    const uint32_t nwant[4] = { 3, 4, 1, 3 };
    /* strip 1 also holds command 7 (it starts at row 32): checked in order below */
    for (int s = 0; s < 4; s++) {
        uint32_t n = b.offsets[s + 1] - b.offsets[s];
        PC_CHECK(n == nwant[s]);
        for (uint32_t k = 0; k < n && k < nwant[s]; k++)
            PC_CHECK(b.entries[b.offsets[s] + k] == want[s][k]);
    }
    PC_CHECK(b.offsets[0] == 0);

    /* with a table too small for everything, a prefix is binned and the rest reported */
    pc_bins small;
    PC_CHECK(pc_bins_init(&small, &a, 100, 32, 5) == 0);
    uint32_t dropped = pc_bins_build(&small, &cb, 100);
    PC_CHECK(dropped > 0);
    PC_CHECK(small.offsets[4] <= 5);
    for (int s = 0; s < 4; s++)
        for (uint32_t k = small.offsets[s]; k < small.offsets[s + 1]; k++)
            PC_CHECK(small.entries[k] < cb.count - dropped);   /* only the prefix that fit */

    /* a one-row strip height makes every row its own strip */
    pc_bins rows;
    PC_CHECK(pc_bins_init(&rows, &a, 100, 1, 1000) == 0);
    PC_CHECK(rows.nstrips == 100);
    PC_CHECK(pc_bins_build(&rows, &cb, 100) == 0);
    PC_CHECK(rows.offsets[100] - rows.offsets[99] == 3);   /* wall 0, plane 5, wall 4 touch row 99 */
}

static void test_validate(void)
{
    static uint8_t mem[8192];
    pc_arena a;
    pc_arena_init(&a, mem, sizeof mem);
    pc_cmdbuf cb;
    PC_CHECK(pc_cmdbuf_init(&cb, &a, 4) == 0);

    cb.count = 1;
    uint32_t bad = 99;
    cb.cmds[0] = make(PC_CMD_WALL_COL, 5, 6, 10, 20);
    PC_CHECK(pc_cmdbuf_validate(&cb, W, H, &bad) == 0);

    pc_cmd good_plane = make(PC_CMD_PLANE_SPAN, 0, W, 5, 6);
    pc_cmd good_sprite = make(PC_CMD_SPRITE_COL, 3, 4, 0, H);
    cb.cmds[0] = good_plane;
    PC_CHECK(pc_cmdbuf_validate(&cb, W, H, NULL) == 0);
    cb.cmds[0] = good_sprite;
    PC_CHECK(pc_cmdbuf_validate(&cb, W, H, NULL) == 0);

    struct { pc_cmd c; const char *why; } cases[12];
    int n = 0;
    pc_cmd c;

    c = make(0, 5, 6, 10, 20);                          cases[n].c = c; cases[n++].why = "type 0";
    c = make(9, 5, 6, 10, 20);                          cases[n].c = c; cases[n++].why = "unknown type";
    c = make(PC_CMD_WALL_COL, 6, 6, 10, 20);            cases[n].c = c; cases[n++].why = "empty columns";
    c = make(PC_CMD_WALL_COL, 5, 8, 10, 20);            cases[n].c = c; cases[n++].why = "wall wider than a column";
    c = make(PC_CMD_WALL_COL, W, W + 1, 10, 20);        cases[n].c = c; cases[n++].why = "column off screen";
    c = make(PC_CMD_WALL_COL, 5, 6, -1, 20);            cases[n].c = c; cases[n++].why = "negative row";
    c = make(PC_CMD_WALL_COL, 5, 6, 10, H + 1);         cases[n].c = c; cases[n++].why = "row off screen";
    c = make(PC_CMD_WALL_COL, 5, 6, 20, 10);            cases[n].c = c; cases[n++].why = "inverted rows";
    c = make(PC_CMD_PLANE_SPAN, 0, 10, H, H + 1);       cases[n].c = c; cases[n++].why = "span row off screen";
    c = make(PC_CMD_WALL_COL, 5, 6, 10, 20); c.tex = PC_TEX_COUNT;   cases[n].c = c; cases[n++].why = "texture id";
    c = make(PC_CMD_SPRITE_COL, 5, 6, 10, 20); c.tex = PC_SPRITE_COUNT; cases[n].c = c; cases[n++].why = "sprite id";
    c = make(PC_CMD_WALL_COL, 5, 6, 10, 20); c.level = PC_LIGHT_LEVELS; cases[n].c = c; cases[n++].why = "light level";

    for (int i = 0; i < n; i++) {
        cb.cmds[0] = cases[i].c;
        int r = pc_cmdbuf_validate(&cb, W, H, &bad);
        if (r == 0)
            printf("validate accepted: %s\n", cases[i].why);
        PC_CHECK(r != 0);
        PC_CHECK(bad == 0);
    }

    /* the bad record's index is reported */
    cb.count = 3;
    cb.cmds[0] = good_plane;
    cb.cmds[1] = good_sprite;
    cb.cmds[2] = cases[0].c;
    PC_CHECK(pc_cmdbuf_validate(&cb, W, H, &bad) != 0 && bad == 2);

    PC_CHECK(pc_cmdbuf_validate(&cb, 4, H, &bad) != 0);   /* a smaller screen rejects wide records */
}

/* ---- full frames through different back-end settings ---------------------- */

typedef struct {
    void               *mem;
    pc_arena            arena;
    pc_assets           assets;
    pc_map              map;
    pc_fb               fb;
    pc_sector_renderer  sr;
} scene;

static int scene_init(scene *s, const char *map_name, int threads, int strip_h, size_t offset)
{
    size_t size = (size_t)32 << 20;
    s->mem = malloc(size + 64);
    if (!s->mem)
        return -1;
    pc_arena_init(&s->arena, (uint8_t *)s->mem + offset, size);
    pc_lut_init();

    char path[512], err[160] = "";
    snprintf(path, sizeof path, "%s/%s.map", PC_MAPS_DIR, map_name);
    size_t len;
    char *text = pc_file_read(path, &s->arena, &len);
    pc_render_cfg cfg = { W, H, threads, strip_h, 0 };
    if (!text || pc_assets_init(&s->assets, &s->arena) ||
        pc_map_load(text, len, &s->arena, &s->map, err, sizeof err) ||
        pc_fb_init(&s->fb, &s->arena, W, H) || pc_sector_renderer_init_cfg(&s->sr, &s->arena, &cfg)) {
        printf("scene_init(%s) failed %s\n", map_name, err);
        return -1;
    }
    return 0;
}

static pc_camera cam_at(const scene *s, double x, double y, double yaw_deg)
{
    pc_camera c = { { (fx_t)(x * 65536.0), (fx_t)(y * 65536.0) }, PC_ANGLE_DEG(yaw_deg), 0, 0 };
    c.sector = pc_map_find_sector(&s->map, c.pos, PC_NONE);
    if (c.sector != PC_NONE)
        c.z = s->map.floor_h[c.sector] + PC_EYE_HEIGHT;
    return c;
}

static uint64_t fb_hash(const scene *s)
{
    return pc_hash64(s->fb.pix, (size_t)W * H, PC_HASH_SEED);
}

static uint64_t cmds_hash(const scene *s)
{
    return pc_hash64(s->sr.cmds.cmds, (size_t)s->sr.cmds.count * sizeof(pc_cmd), PC_HASH_SEED);
}

typedef struct { const char *map; double x, y, yaw; } pose;

static const pose k_poses[] = {
    { "steps", 9.0, 4.0, 0.0 },
    { "steps", -3.5, 4.0, 0.0 },
    { "window", 2.0, 3.0, 0.0 },
    { "window", 15.0, 5.0, 180.0 },
    { "nested", 3.0, 3.0, 45.0 },
    { "nested", 10.0, 10.0, 200.0 },
    { "maze", 20.0, 20.0, 0.0 },
};

/* The picture must not depend on how many threads draw it or how tall a strip is: strips
 * write disjoint rows. The commands the front-end emits must not depend on either. */
static void test_thread_and_strip_independence(void)
{
    const int threads[] = { 1, 2, 3, 8 };
    const int strips[] = { 0, 1, 7, 32, 33, 64, 400, 999 };

    const char *loaded = NULL;
    scene ref;
    for (size_t p = 0; p < sizeof k_poses / sizeof k_poses[0]; p++) {
        const pose *ps = &k_poses[p];
        if (!loaded || strcmp(loaded, ps->map)) {
            if (loaded)
                free(ref.mem);
            PC_CHECK(scene_init(&ref, ps->map, 1, 0, 0) == 0);
            loaded = ps->map;
        }
        pc_camera c = cam_at(&ref, ps->x, ps->y, ps->yaw);
        pc_stats rs;
        pc_sector_render(&ref.sr, &ref.fb, &ref.map, &ref.assets, &c, &rs);
        const uint64_t want_px = fb_hash(&ref), want_cmds = cmds_hash(&ref);

        for (size_t ti = 0; ti < sizeof threads / sizeof threads[0]; ti++)
            for (size_t si = 0; si < sizeof strips / sizeof strips[0]; si++) {
                scene s;
                PC_CHECK(scene_init(&s, ps->map, threads[ti], strips[si], 24) == 0);
                pc_stats st;
                pc_sector_render(&s.sr, &s.fb, &s.map, &s.assets, &c, &st);
                if (fb_hash(&s) != want_px || cmds_hash(&s) != want_cmds)
                    printf("%s pose %zu: %d threads, strip %d differs\n", ps->map, p, threads[ti],
                           strips[si]);
                PC_CHECK(fb_hash(&s) == want_px);
                PC_CHECK(cmds_hash(&s) == want_cmds);
                PC_CHECK(st.sprite_px == rs.sprite_px && st.cmds_dropped == 0);
                free(s.mem);
            }
    }
    if (loaded)
        free(ref.mem);
}

static void test_frontend_writes_no_pixels(void)
{
    scene s;
    PC_CHECK(scene_init(&s, "nested", 1, 0, 0) == 0);
    pc_camera c = cam_at(&s, 3, 3, 45);
    memset(s.fb.pix, SENTINEL, (size_t)W * H);

    pc_stats st;
    pc_sector_frontend(&s.sr, &s.map, &s.assets, &c, &st);
    int touched = 0;
    for (size_t i = 0; i < (size_t)W * H; i++)
        touched += s.fb.pix[i] != SENTINEL;
    PC_CHECK(touched == 0);
    PC_CHECK(st.commands > 100 && st.commands == s.sr.cmds.count);
    PC_CHECK(st.wall_px + st.plane_px == (uint32_t)(W * H));   /* emitted, not yet drawn */

    pc_sector_backend(&s.sr, &s.fb, &st);
    touched = 0;
    for (size_t i = 0; i < (size_t)W * H; i++)
        touched += s.fb.pix[i] == SENTINEL;
    PC_CHECK(touched == 0);
    free(s.mem);
}

/* A command buffer that is too small loses commands but never crashes, and says so. */
static void test_overflow(void)
{
    scene s;
    PC_CHECK(scene_init(&s, "nested", 1, 0, 0) == 0);
    pc_camera c = cam_at(&s, 3, 3, 45);

    s.sr.cmds.cap = 50;
    s.sr.cmds.strict = 0;
    pc_stats st;
    memset(s.fb.pix, SENTINEL, (size_t)W * H);
    pc_sector_render(&s.sr, &s.fb, &s.map, &s.assets, &c, &st);
    PC_CHECK(st.cmds_dropped > 0);
    PC_CHECK(st.commands == 50);
    PC_CHECK(st.wall_px + st.plane_px < (uint32_t)(W * H));   /* the shortfall shows up */
    free(s.mem);

    /* the same frame with the normal buffer loses nothing */
    PC_CHECK(scene_init(&s, "nested", 1, 0, 0) == 0);
    c = cam_at(&s, 3, 3, 45);
    pc_sector_render(&s.sr, &s.fb, &s.map, &s.assets, &c, &st);
    PC_CHECK(st.cmds_dropped == 0 && st.commands < s.sr.cmds.cap);
    free(s.mem);
}

/* ---- dump, replay, and corrupt files -------------------------------------- */

#define DUMP_PATH "pc_test_dump.pcmd"

static uint8_t *slurp(const char *path, size_t *n)
{
    *n = 0;
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc((size_t)len);
    if (buf && fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    *n = (size_t)len;
    return buf;
}

static int spit(const char *path, const uint8_t *buf, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    size_t w = fwrite(buf, 1, n, f);
    fclose(f);
    return w == n ? 0 : -1;
}

/* Draws a loaded dump through a fresh back-end; returns the framebuffer hash. */
static uint64_t replay(pc_arena *a, const pc_assets *as, const pc_frame_params *fp,
                       const pc_cmdbuf *cb, int threads)
{
    pc_fb fb;
    pc_backend be;
    pc_backend_cfg bc = { fp->w, fp->h, threads, 0, cb->count ? cb->count : 1 };
    pc_backend_cpu_make(&be);
    if (pc_fb_init(&fb, a, fp->w, fp->h) || be.init(&be, a, &bc) || be.upload_assets(&be, as))
        return 0;
    be.begin_frame(&be, fp, &fb);
    be.submit(&be, cb);
    be.end_frame(&be);
    uint64_t h = pc_hash64(fb.pix, (size_t)fp->w * fp->h, PC_HASH_SEED);
    be.shutdown(&be);
    return h;
}

static void test_dump_roundtrip(void)
{
    scene s;
    PC_CHECK(scene_init(&s, "window", 1, 0, 0) == 0);
    pc_camera c = cam_at(&s, 15, 5, 180);
    pc_stats st;
    pc_sector_render(&s.sr, &s.fb, &s.map, &s.assets, &c, &st);
    const uint64_t direct = fb_hash(&s);

    pc_frame_params fp;
    pc_sector_frame_params(&s.sr, &fp);
    PC_CHECK(pc_cmddump_write(DUMP_PATH, &fp, &s.sr.cmds, pc_assets_hash(&s.assets)) == 0);

    size_t size = (size_t)64 << 20;
    void *mem = malloc(size);
    pc_arena a;
    pc_arena_init(&a, mem, size);

    pc_frame_params fp2;
    pc_cmdbuf cb2;
    uint64_t assets_hash = 0;
    const char *err = NULL;
    PC_CHECK(pc_cmddump_read(DUMP_PATH, &a, &fp2, &cb2, &assets_hash, &err) == 0);
    PC_CHECK(assets_hash == pc_assets_hash(&s.assets));
    PC_CHECK(fp2.w == W && fp2.h == H && fp2.pos.x == fp.pos.x && fp2.pos.y == fp.pos.y);
    PC_CHECK(cb2.count == s.sr.cmds.count);
    PC_CHECK(memcmp(cb2.cmds, s.sr.cmds.cmds, (size_t)cb2.count * sizeof(pc_cmd)) == 0);
    PC_CHECK(memcmp(fp2.ray_x, fp.ray_x, W * sizeof(fx_t)) == 0);

    /* the whole point: drawing the dump gives the picture the live frame gave */
    size_t mark = pc_arena_save(&a);
    PC_CHECK(replay(&a, &s.assets, &fp2, &cb2, 1) == direct);
    pc_arena_restore(&a, mark);
    PC_CHECK(replay(&a, &s.assets, &fp2, &cb2, 4) == direct);
    pc_arena_restore(&a, mark);

    /* ---- damaged files: refused, or drawn without touching memory they should not ---- */
    size_t n = 0;
    uint8_t *good = slurp(DUMP_PATH, &n);
    PC_CHECK(good != NULL && n > 200);
    const char *bad_path = "pc_test_bad.pcmd";

    /* every kind of truncation */
    const size_t cuts[] = { 0, 3, 8, 35, 36, 37, 36 + 4 * W, 36 + 8 * W, 36 + 8 * W + 31,
                            n - 33, n - 1 };
    for (size_t i = 0; i < sizeof cuts / sizeof cuts[0]; i++) {
        PC_CHECK(spit(bad_path, good, cuts[i]) == 0);
        PC_CHECK(pc_cmddump_read(bad_path, &a, &fp2, &cb2, NULL, &err) != 0);
        pc_arena_restore(&a, mark);
    }

    /* trailing garbage, wrong magic, wrong version */
    uint8_t *ext = (uint8_t *)malloc(n + 1);
    memcpy(ext, good, n);
    ext[n] = 0;
    PC_CHECK(spit(bad_path, ext, n + 1) == 0);
    PC_CHECK(pc_cmddump_read(bad_path, &a, &fp2, &cb2, NULL, &err) != 0);
    pc_arena_restore(&a, mark);
    free(ext);

    uint8_t *mut = (uint8_t *)malloc(n);
    memcpy(mut, good, n);
    mut[0] ^= 0xFF;
    PC_CHECK(spit(bad_path, mut, n) == 0);
    PC_CHECK(pc_cmddump_read(bad_path, &a, &fp2, &cb2, NULL, &err) != 0);
    pc_arena_restore(&a, mark);
    memcpy(mut, good, n);
    mut[4] = 2;
    PC_CHECK(spit(bad_path, mut, n) == 0);
    PC_CHECK(pc_cmddump_read(bad_path, &a, &fp2, &cb2, NULL, &err) != 0);
    pc_arena_restore(&a, mark);

    /* random byte damage anywhere: a file that still loads must also draw safely (ASan
     * is what checks "safely" when the sanitizer build runs this test) */
    uint32_t seed = 12345;
    int loaded = 0, refused = 0;
    for (int iter = 0; iter < 600; iter++) {
        memcpy(mut, good, n);
        int flips = 1 + iter % 4;
        for (int k = 0; k < flips; k++) {
            seed = seed * 1103515245u + 12345u;
            size_t at = (seed >> 8) % n;
            seed = seed * 1103515245u + 12345u;
            mut[at] ^= (uint8_t)(1u << ((seed >> 12) % 8));
        }
        PC_CHECK(spit(bad_path, mut, n) == 0);
        if (pc_cmddump_read(bad_path, &a, &fp2, &cb2, NULL, &err) == 0) {
            loaded++;
            replay(&a, &s.assets, &fp2, &cb2, 1 + iter % 3);
        } else {
            refused++;
        }
        pc_arena_restore(&a, mark);
    }
    PC_CHECK(refused > 0 && loaded > 0);     /* the damage hits both header and payload */

    free(mut);
    free(good);
    free(mem);
    remove(DUMP_PATH);
    remove(bad_path);
    free(s.mem);
}

int main(void)
{
    test_cmdbuf();
    test_bins();
    test_validate();
    test_frontend_writes_no_pixels();
    test_overflow();
    test_thread_and_strip_independence();
    test_dump_roundtrip();
    return PC_TEST_RESULT();
}
