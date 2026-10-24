#include "pc_test.h"
#include "paracast.h"

#include <stdlib.h>
#include <string.h>

#define W 640
#define H 400
#define SENTINEL 0xEE      /* hue 14 shade 14: no texture or sprite produces it */

typedef struct {
    void               *mem;
    pc_arena            arena;
    pc_assets           assets;
    pc_map              map;
    pc_fb               fb;
    pc_sector_renderer  sr;
} scene;

/* Loads a shipped map, or, when text is given, that map text instead. */
static int scene_load(scene *s, const char *map_name, const char *text, size_t align_offset)
{
    size_t size = (size_t)8 << 20;
    s->mem = malloc(size + 64);
    if (!s->mem)
        return -1;
    pc_arena_init(&s->arena, (uint8_t *)s->mem + align_offset, size);
    pc_lut_init();

    char path[512], err[160] = "";
    size_t len = text ? strlen(text) : 0;
    if (!text) {
        snprintf(path, sizeof path, "%s/%s.map", PC_MAPS_DIR, map_name);
        text = pc_file_read(path, &s->arena, &len);
    }
    if (!text || pc_assets_init(&s->assets, &s->arena) ||
        pc_map_load(text, len, &s->arena, &s->map, err, sizeof err) ||
        pc_fb_init(&s->fb, &s->arena, W, H) || pc_sector_renderer_init(&s->sr, &s->arena, W, H)) {
        printf("scene_load(%s) failed %s\n", map_name, err);
        return -1;
    }
    return 0;
}

static int scene_init(scene *s, const char *map_name, size_t align_offset)
{
    return scene_load(s, map_name, NULL, align_offset);
}

static pc_camera cam_at(const scene *s, double x, double y, double yaw_deg)
{
    pc_camera c = { { (fx_t)(x * 65536.0), (fx_t)(y * 65536.0) }, PC_ANGLE_DEG(yaw_deg), 0, 0 };
    c.sector = pc_map_find_sector(&s->map, c.pos, PC_NONE);
    if (c.sector != PC_NONE)
        c.z = s->map.floor_h[c.sector] + PC_EYE_HEIGHT;
    return c;
}

static uint64_t render(scene *s, const pc_camera *c, pc_stats *st)
{
    memset(s->fb.pix, SENTINEL, (size_t)W * H);
    pc_sector_render(&s->sr, &s->fb, &s->map, &s->assets, c, st);
    return pc_hash64(s->fb.pix, (size_t)W * H, PC_HASH_SEED);
}

static int unwritten(const scene *s)
{
    int n = 0;
    for (size_t i = 0; i < (size_t)W * H; i++)
        n += s->fb.pix[i] == SENTINEL;
    return n;
}

/* ---- invariants over many poses ------------------------------------------- */

static void check_pose(scene *s, const pc_camera *c, const char *name)
{
    pc_stats st;
    render(s, c, &st);

    int bad = unwritten(s);
    int partition = st.wall_px + st.plane_px == (uint32_t)(W * H);
    if (bad || !partition || st.visit_overflow || st.sector_visits < 1)
        printf("%s: pos (%.2f, %.2f) yaw %u sector %u: %d unwritten, wall+plane=%u, visits=%u, "
               "overflow=%u\n", name, c->pos.x / 65536.0, c->pos.y / 65536.0, c->yaw,
               (unsigned)c->sector, bad, st.wall_px + st.plane_px, st.sector_visits,
               st.visit_overflow);
    PC_CHECK(bad == 0);
    PC_CHECK(partition);
    PC_CHECK(st.visit_overflow == 0);
    PC_CHECK(st.sector_visits >= 1 && st.sector_visits <= PC_MAX_VISITS);
}

/* Every pixel is covered exactly once, from many places in every sector, in every
 * direction. This is the clip-buffer artifact check: a leak, gap or double draw shows
 * up as an unwritten pixel or a wall+plane count that is not the screen size. */
static void test_coverage(void)
{
    const char *names[] = { "steps", "window", "nested", "maze" };
    for (int mi = 0; mi < 4; mi++) {
        scene s;
        PC_CHECK(scene_init(&s, names[mi], 0) == 0);

        for (pc_id sec = 0; sec < s.map.nsectors; sec++) {
            /* centroid of the vertices lies inside a convex sector */
            int64_t cx = 0, cy = 0;
            pc_id w0 = s.map.first_wall[sec], n = s.map.wall_count[sec];
            for (pc_id w = w0; w < w0 + n; w++) {
                cx += s.map.a[w].x;
                cy += s.map.a[w].y;
            }
            pc_vec2 c = { (fx_t)(cx / n), (fx_t)(cy / n) };
            PC_CHECK(pc_map_contains(&s.map, sec, c));

            /* the centroid, then points a third of the way to each vertex */
            for (pc_id v = w0; v <= w0 + n; v++) {
                pc_vec2 p = c;
                if (v < w0 + n) {
                    p.x = c.x + (s.map.a[v].x - c.x) / 3;
                    p.y = c.y + (s.map.a[v].y - c.y) / 3;
                }
                for (int yaw = 0; yaw < 360; yaw += 40) {
                    pc_camera cam = { p, PC_ANGLE_DEG((double)yaw + (double)(v % 7)), 0, sec };
                    cam.z = s.map.floor_h[sec] + PC_EYE_HEIGHT;
                    check_pose(&s, &cam, names[mi]);
                }
            }
        }
        free(s.mem);
    }
}

/* Cameras standing very close to portals and corners, where window edges are slivers. */
static void test_coverage_near_edges(void)
{
    scene s;
    PC_CHECK(scene_init(&s, "maze", 0) == 0);

    for (pc_id sec = 0; sec < s.map.nsectors; sec += 5) {
        for (pc_id w = s.map.first_wall[sec]; w < s.map.first_wall[sec] + s.map.wall_count[sec]; w++) {
            /* a hair inside the wall's midpoint and a hair inside its first corner */
            pc_vec2 mid = { s.map.a[w].x + s.map.e[w].x / 2, s.map.a[w].y + s.map.e[w].y / 2 };
            for (int k = 0; k < 2; k++) {
                pc_vec2 p = k ? s.map.a[w] : mid;
                /* nudge inward: the left normal of e is (-e.y, e.x) */
                p.x += (fx_t)(-s.map.e[w].y / 4096);
                p.y += (fx_t)(s.map.e[w].x / 4096);
                if (!pc_map_contains(&s.map, sec, p))
                    continue;
                for (int yaw = 0; yaw < 360; yaw += 45) {
                    pc_camera cam = { p, PC_ANGLE_DEG((double)yaw), 0, sec };
                    cam.z = s.map.floor_h[sec] + PC_EYE_HEIGHT;
                    check_pose(&s, &cam, "maze-edge");
                }
            }
        }
    }
    free(s.mem);
}

/* ---- known geometry ------------------------------------------------------- */

static void test_depths(void)
{
    scene s;
    pc_stats st;

    /* steps: from (9, 4) looking east the middle column ends on the far wall at x = 20,
     * through two portals (hall -> step 1 -> step 2). */
    PC_CHECK(scene_init(&s, "steps", 0) == 0);
    pc_camera c = cam_at(&s, 9, 4, 0);
    render(&s, &c, &st);
    PC_CHECK(s.fb.depth[W / 2] == FX_C(11.0));
    PC_CHECK(st.sector_visits == 3);

    /* facing west the nearest wall is x = 0, 2 units off; at y = 7.5 the view (+-1.3
     * wide there) stays south of the pit's doorway, which spans y = 2..6 */
    c = cam_at(&s, 2, 7.5, 180);
    render(&s, &c, &st);
    PC_CHECK(s.fb.depth[W / 2] == FX_C(2.0));
    PC_CHECK(st.sector_visits == 1);
    free(s.mem);

    /* window: the doorway is dead ahead of (2, 3); the middle column passes through it
     * and ends on room B's east wall at x = 21. */
    PC_CHECK(scene_init(&s, "window", 0) == 0);
    c = cam_at(&s, 2, 3, 0);
    render(&s, &c, &st);
    PC_CHECK(s.fb.depth[W / 2] == FX_C(19.0));
    PC_CHECK(st.sector_visits >= 3);        /* A, the doorway, B (and the window, aside) */
    free(s.mem);

    /* a wall straight ahead hides everything behind it */
    PC_CHECK(scene_init(&s, "window", 0) == 0);
    c = cam_at(&s, 2, 5, 0);
    render(&s, &c, &st);
    PC_CHECK(s.fb.depth[W / 2] == FX_C(8.0));    /* the solid stretch (10,4)-(10,6) */
    free(s.mem);
}

/* Two squares side by side joined by a portal. B's floor is level with A's ceiling when
 * b_floor is 3, so the portal has no opening at all. */
static const char *two_rooms(char *buf, size_t cap, int b_floor)
{
    snprintf(buf, cap,
             "player 2 2 0\n"
             "sector 0 floor 0 ceil 3\n  v 0 0\n  v 4 0 portal 1\n  v 4 4\n  v 0 4\nend\n"
             "sector 1 floor %d ceil %d\n  v 4 0\n  v 8 0\n  v 8 4\n  v 4 4 portal 0\nend\n",
             b_floor, b_floor + 3);
    return buf;
}

/* A portal with no opening at a column must stop the view there: the column closes at
 * the portal's depth and the room behind is never visited. Open it up and the same
 * column runs on to the far wall. */
static void test_closed_portal(void)
{
    scene s;
    pc_stats st;
    char text[512];

    PC_CHECK(scene_load(&s, "shut", two_rooms(text, sizeof text, 3), 0) == 0);
    pc_camera c = cam_at(&s, 2, 2, 0);
    render(&s, &c, &st);
    PC_CHECK(unwritten(&s) == 0);
    PC_CHECK(st.sector_visits == 1);
    PC_CHECK(s.fb.depth[W / 2] == FX_C(2.0));
    PC_CHECK(st.wall_px + st.plane_px == (uint32_t)(W * H));
    free(s.mem);

    PC_CHECK(scene_load(&s, "open", two_rooms(text, sizeof text, 0), 0) == 0);
    c = cam_at(&s, 2, 2, 0);
    render(&s, &c, &st);
    PC_CHECK(unwritten(&s) == 0);
    PC_CHECK(st.sector_visits == 2);
    PC_CHECK(s.fb.depth[W / 2] == FX_C(6.0));
    free(s.mem);

    /* a step the eye can see over but not walk over: B's floor 1.5 above A's: the lower
     * wall rises above eye level (0.8), hiding B's floor, but the room is still visited
     * above it, and the frame still covers every pixel */
    PC_CHECK(scene_load(&s, "step", two_rooms(text, sizeof text, 1), 0) == 0);
    c = cam_at(&s, 2, 2, 0);
    render(&s, &c, &st);
    PC_CHECK(unwritten(&s) == 0);
    PC_CHECK(st.sector_visits == 2);
    free(s.mem);
}

/* ---- sprites -------------------------------------------------------------- */

static void test_sprites(void)
{
    scene s;
    pc_stats st;

    /* window: barrel at (3, 3), one unit from (2, 3), dead ahead */
    PC_CHECK(scene_init(&s, "window", 0) == 0);
    pc_camera c = cam_at(&s, 2, 3, 0);
    render(&s, &c, &st);
    PC_CHECK(st.sprite_px > 5000);

    /* facing away from every thing: none are drawn */
    c = cam_at(&s, 2, 3, 180);
    render(&s, &c, &st);
    PC_CHECK(st.sprite_px == 0);
    free(s.mem);

    /* steps, standing in the pit facing its solid west wall: the hall's things are in a
     * sector the view never reaches, so none are drawn even though they exist */
    PC_CHECK(scene_init(&s, "steps", 0) == 0);
    c = cam_at(&s, -3.5, 4, 180);       /* the pit's own lamp, at x = -2, is behind us too */
    PC_CHECK(c.sector == 3);
    render(&s, &c, &st);
    PC_CHECK(st.sector_visits == 1);
    PC_CHECK(st.sprite_px == 0);

    /* turned round it looks out through the pit's portal into the hall */
    c = cam_at(&s, -3.5, 4, 0);
    render(&s, &c, &st);
    PC_CHECK(st.sector_visits >= 2);
    PC_CHECK(st.sprite_px > 0);
    free(s.mem);
}

/* ---- determinism and golden frames ---------------------------------------- */

typedef struct {
    const char *map;
    double x, y, yaw;
    uint64_t hash;
} golden;

/* Regenerate by running this test after an intentional visual change: failures print
 * the new value. Check the frame with paracast_demo before pinning it. */
static const golden k_golden[] = {
    { "steps",  2.0,  4.0,   0.0, 0x2828c2210f8ec30eULL },
    { "steps",  9.0,  4.0,   0.0, 0xceb898373f508411ULL },
    { "steps", -3.5,  4.0,   0.0, 0x00f0e9f421eb0372ULL },
    { "window", 2.0,  3.0,   0.0, 0xc7cec54db00905e7ULL },
    { "window", 2.0,  3.0, -20.0, 0x62e9743fd07554e3ULL },
    { "window", 15.0, 5.0, 180.0, 0x84f1657b877312d8ULL },
    { "nested", 3.0,  3.0,  45.0, 0xd30b744ef9570d13ULL },
    { "nested", 10.0, 10.0, 200.0, 0xad9805990e5f7920ULL },
    { "nested", 10.0, 17.0, -90.0, 0x0919adbd8a2bfec5ULL },
    { "maze",   4.0,  4.0,  90.0, 0x4231381122c006d6ULL },
    { "maze",  20.0, 20.0,   0.0, 0x1f1e3bdfe3dbb144ULL },
    { "maze",  28.0, 12.0, 135.0, 0x5eb0123c14c56429ULL },
};

static void test_golden(void)
{
    scene a, b;
    const char *loaded = NULL;
    for (size_t i = 0; i < sizeof k_golden / sizeof k_golden[0]; i++) {
        const golden *g = &k_golden[i];
        if (!loaded || strcmp(loaded, g->map)) {
            if (loaded) {
                free(a.mem);
                free(b.mem);
            }
            PC_CHECK(scene_init(&a, g->map, 0) == 0);
            PC_CHECK(scene_init(&b, g->map, 40) == 0);   /* another alignment must not matter */
            loaded = g->map;
        }

        pc_camera ca = cam_at(&a, g->x, g->y, g->yaw), cb = cam_at(&b, g->x, g->y, g->yaw);
        PC_CHECK(ca.sector != PC_NONE);
        pc_stats sa, sb;
        uint32_t allocs = a.arena.allocs;
        uint64_t ha = render(&a, &ca, &sa);
        PC_CHECK(a.arena.allocs == allocs);              /* no allocation in the frame */
        uint64_t hb = render(&b, &cb, &sb);

        PC_CHECK(ha == hb);
        PC_CHECK(ha == render(&a, &ca, &sa));            /* rendering twice changes nothing */
        PC_CHECK(memcmp(&sa, &sb, sizeof sa) == 0);
        if (ha != g->hash)
            printf("golden %s (%.1f, %.1f, %.1f): got %016llx\n", g->map, g->x, g->y, g->yaw,
                   (unsigned long long)ha);
        PC_CHECK(ha == g->hash);
    }
    if (loaded) {
        free(a.mem);
        free(b.mem);
    }
}

/* The sector renderer must not disturb v0: the grid golden in test_render still holds,
 * and an out-of-range camera sector draws nothing instead of crashing. */
static void test_bad_camera_sector(void)
{
    scene s;
    PC_CHECK(scene_init(&s, "steps", 0) == 0);
    pc_camera c = cam_at(&s, 2, 4, 0);
    c.sector = 999;
    pc_stats st;
    memset(s.fb.pix, SENTINEL, (size_t)W * H);
    pc_sector_render(&s.sr, &s.fb, &s.map, &s.assets, &c, &st);
    PC_CHECK(st.sector_visits == 0 && st.wall_px == 0);
    free(s.mem);
}

int main(void)
{
    test_depths();
    test_closed_portal();
    test_sprites();
    test_bad_camera_sector();
    test_coverage();
    test_coverage_near_edges();
    test_golden();
    return PC_TEST_RESULT();
}
