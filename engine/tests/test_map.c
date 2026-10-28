#include "pc_test.h"
#include "paracast.h"

#include <string.h>

static uint8_t g_mem[512 * 1024];

/* Loads text into a fresh arena; returns 0 on success. */
static int load(const char *text, pc_map *m, char *err, size_t cap)
{
    pc_arena a;
    pc_arena_init(&a, g_mem, sizeof g_mem);
    return pc_map_load(text, strlen(text), &a, m, err, cap);
}

static int load_file(const char *name, pc_map *m)
{
    char path[512], err[160];
    snprintf(path, sizeof path, "%s/%s.map", PC_MAPS_DIR, name);

    pc_arena a;
    pc_arena_init(&a, g_mem, sizeof g_mem);
    size_t len;
    char *text = pc_file_read(path, &a, &len);
    if (!text) {
        printf("cannot read %s\n", path);
        return -1;
    }
    int r = pc_map_load(text, len, &a, m, err, sizeof err);
    if (r)
        printf("%s: %s\n", path, err);
    return r;
}

static const char *k_square =
    "player 1 1 0\n"
    "sector 0 floor 0 ceil 3\n"
    "  v 0 0\n  v 4 0\n  v 4 4\n  v 0 4\n"
    "end\n";

static int err_has(const char *err, const char *needle)
{
    return strstr(err, needle) != NULL;
}

static void expect_error(const char *text, const char *needle)
{
    pc_map m;
    char err[160];
    int r = load(text, &m, err, sizeof err);
    if (r == 0 || !err_has(err, needle))
        printf("expected error containing \"%s\", got %s \"%s\"\n", needle,
               r ? "error" : "success", err);
    PC_CHECK(r != 0 && err_has(err, needle));
}

static void test_shipped_maps(void)
{
    struct { const char *name; uint32_t sectors; } maps[] = {
        { "steps", 4 }, { "window", 4 }, { "nested", 5 }, { "maze", 36 },
        { "corridor", 24 }, { "hall", 1 },
    };
    for (size_t i = 0; i < sizeof maps / sizeof maps[0]; i++) {
        pc_map m;
        PC_CHECK(load_file(maps[i].name, &m) == 0);
        PC_CHECK(m.nsectors == maps[i].sectors);
        PC_CHECK(m.spawn_sector < m.nsectors);
        PC_CHECK(pc_map_contains(&m, m.spawn_sector, m.spawn));

        /* Every wall obeys the loader's guarantees: interior on the left, edge matches. */
        for (pc_id s = 0; s < m.nsectors; s++) {
            PC_CHECK(m.ceil_h[s] - m.floor_h[s] >= FX_HALF);
            for (pc_id w = m.first_wall[s]; w < m.first_wall[s] + m.wall_count[s]; w++) {
                pc_id nxt = w + 1 < m.first_wall[s] + m.wall_count[s] ? w + 1 : m.first_wall[s];
                PC_CHECK(m.b[w].x == m.a[nxt].x && m.b[w].y == m.a[nxt].y);
                PC_CHECK(m.e[w].x == m.b[w].x - m.a[w].x && m.e[w].y == m.b[w].y - m.a[w].y);
                PC_CHECK(m.len[w] >= PC_MIN_EDGE);
            }
        }
        for (uint32_t t = 0; t < m.nthings; t++)
            PC_CHECK(pc_map_contains(&m, m.thing_sector[t], m.thing_pos[t]));
    }
}

static void test_values(void)
{
    pc_map m;
    PC_CHECK(load_file("steps", &m) == 0);
    PC_CHECK(m.floor_h[3] == FX_C(-0.4));
    PC_CHECK(m.ceil_h[0] == FX_C(3.2));
    PC_CHECK(m.spawn.x == FX_C(2.0) && m.spawn.y == FX_C(4.0) && m.spawn_yaw == 0);
    PC_CHECK(m.spawn_sector == 0);

    PC_CHECK(load_file("nested", &m) == 0);
    PC_CHECK(m.spawn_yaw == PC_ANGLE_DEG(45.0));
}

static void test_find_sector(void)
{
    pc_map m;
    PC_CHECK(load_file("steps", &m) == 0);
    pc_vec2 in_hall = { FX_C(5.0), FX_C(4.0) }, in_step2 = { FX_C(18.0), FX_C(1.0) };
    pc_vec2 in_pit = { FX_C(-2.0), FX_C(4.0) }, outside = { FX_C(30.0), FX_C(4.0) };

    PC_CHECK(pc_map_find_sector(&m, in_hall, PC_NONE) == 0);
    PC_CHECK(pc_map_find_sector(&m, in_step2, PC_NONE) == 2);
    PC_CHECK(pc_map_find_sector(&m, in_pit, PC_NONE) == 3);
    PC_CHECK(pc_map_find_sector(&m, outside, PC_NONE) == PC_NONE);

    /* a hint of the wrong sector, near or far, must not change the answer */
    PC_CHECK(pc_map_find_sector(&m, in_step2, 0) == 2);
    PC_CHECK(pc_map_find_sector(&m, in_pit, 2) == 3);
    PC_CHECK(pc_map_find_sector(&m, in_hall, 99) == 0);
}

static void test_winding_is_normalized(void)
{
    /* Sector 0 is written clockwise (in the y-up sense), sector 1 counter-clockwise. The
     * loader reverses sector 0, carrying its portal edge along, so the two still mirror
     * each other and the interior is on the left of every edge. */
    const char *cw =
        "player 1 1 0\n"
        "sector 0 floor 0 ceil 3\n  v 0 0\n  v 0 4\n  v 4 4\n  v 4 0 portal 1\nend\n"
        "sector 1 floor 0 ceil 3\n  v 0 -4\n  v 4 -4\n  v 4 0 portal 0\n  v 0 0\nend\n";
    pc_map m;
    char err[160];
    int r = load(cw, &m, err, sizeof err);
    if (r)
        printf("%s\n", err);
    PC_CHECK(r == 0);
    if (r)
        return;

    pc_vec2 centre0 = { FX_C(2.0), FX_C(2.0) }, centre1 = { FX_C(2.0), FX_C(-2.0) };
    PC_CHECK(pc_map_contains(&m, 0, centre0) && !pc_map_contains(&m, 0, centre1));
    PC_CHECK(pc_map_contains(&m, 1, centre1) && !pc_map_contains(&m, 1, centre0));

    int portals = 0;
    for (pc_id w = 0; w < m.nwalls; w++)
        portals += m.back[w] != PC_NONE;
    PC_CHECK(portals == 2);
}

static void test_rejections(void)
{
    expect_error("player 1 1 0\nsector 0 floor 0 ceil 3\n  v 0 0\n  v 4 0\n  v 4 2\n"
                 "  v 2 2\n  v 2 4\n  v 0 4\nend\n", "not convex");
    expect_error("player 1 1 0\nsector 0 floor 0 ceil 3\n  v 0 0\n  v 4 4\n  v 4 0\n"
                 "  v 0 4\nend\n", "not convex");                       /* bow tie */
    expect_error("player 1 1 0\nsector 0 floor 0 ceil 3\n  v 0 0\n  v 4 0\n  v 8 0\nend\n",
                 "zero area");
    expect_error("player 1 1 0\nsector 0 floor 0 ceil 3\n  v 0 0\n  v 4 0\nend\n",
                 "at least 3 vertices");
    expect_error("player 1 1 0\nsector 0 floor 0 ceil 3\n  v 0 0\n  v 0.01 0\n  v 0 4\nend\n",
                 "shorter than 1/16");
    expect_error("player 1 1 0\nsector 1 floor 0 ceil 3\n  v 0 0\n  v 4 0\n  v 4 4\nend\n",
                 "ids must be 0, 1, 2");
    expect_error("player 1 1 0\nsector 0 floor 0 ceil 0.2\n  v 0 0\n  v 4 0\n  v 4 4\nend\n",
                 "at least 0.5 above");
    expect_error("player 1 1 0\nsector 0 floor 0\n  v 0 0\n  v 4 0\n  v 4 4\nend\n",
                 "needs floor and ceil");
    expect_error("player 1 1 0\nsector 0 floor 0 ceil 3 light 40\n  v 0 0\n  v 4 0\n  v 4 4\nend\n",
                 "light must be 0..31");
    expect_error("player 1 1 0\nsector 0 floor 0 ceil 3 ftex 99\n  v 0 0\n  v 4 0\n  v 4 4\nend\n",
                 "texture id");
    expect_error("player 1 1 0\nsector 0 floor 0 ceil 3\n  v 0 0 mid 99\n  v 4 0\n  v 4 4\nend\n",
                 "texture id");
    expect_error("player 1 1 0\nsector 0 floor 0 ceil 3\n  v 0 0\n  v 600 0\n  v 4 4\nend\n",
                 "coordinate");
    expect_error("player 1 1 0\nsector 0 floor 0 ceil 3\n  v 0 0\n  v 4 0 portal 7\n  v 4 4\nend\n",
                 "unknown sector");
    expect_error("player 1 1 0\nsector 0 floor 0 ceil 3\n  v 0 0\n  v 4 0 portal 0\n  v 4 4\nend\n",
                 "unknown sector or to itself");
    expect_error("player 1 1 0\nsector 0 floor 0 ceil 3\n  v 0 0\n  v 4 0\n  v 4 4\n",
                 "unterminated");
    expect_error("player 1 1 0\nsector 0 floor 0 ceil 3\n  v 0 0\n  v 4 0\n  v 4 4\n"
                 "sector 1 floor 0 ceil 3\n", "missing 'end'");
    expect_error("player 1 1 0\nbogus\n", "unknown directive");
    expect_error("sector 0 floor 0 ceil 3\n  v 0 0\n  v 4 0\n  v 4 4\nend\n", "no 'player'");
    expect_error("player 1 1 0\nplayer 2 2 0\n", "more than one");
    expect_error("player 9 9 0\nsector 0 floor 0 ceil 3\n  v 0 0\n  v 4 0\n  v 4 4\nend\n",
                 "player starts outside");
    expect_error("player 1 1 0\nsector 0 floor 0 ceil 3\n  v 0 0\n  v 4 0\n  v 4 4\nend\n"
                 "thing 30 30 0\n", "thing is outside");
    expect_error("player 1 1 0\nsector 0 floor 0 ceil 3\n  v 0 0\n  v 4 0\n  v 4 4\nend\n"
                 "thing 1 1 9\n", "thing x y sprite");
    expect_error("player 1 1 0\n", "no sectors");

    /* portal whose far side does not mirror it */
    expect_error("player 1 1 0\n"
                 "sector 0 floor 0 ceil 3\n  v 0 0\n  v 4 0\n  v 4 4 portal 1\n  v 0 4\nend\n"
                 "sector 1 floor 0 ceil 3\n  v 4 4\n  v 8 4\n  v 8 8\n  v 4 8\nend\n",
                 "matching reverse wall");
    /* portal pointing at a sector whose matching wall points elsewhere */
    expect_error("player 1 1 0\n"
                 "sector 0 floor 0 ceil 3\n  v 0 0\n  v 4 0\n  v 4 4 portal 1\n  v 0 4\nend\n"
                 "sector 1 floor 0 ceil 3\n  v 4 0\n  v 8 0\n  v 8 4\n  v 4 4\nend\n",
                 "matching reverse wall");
}

static void test_error_reports_line(void)
{
    pc_map m;
    char err[160];
    const char *text = "# comment\n\nplayer 1 1 0\nsector 0 floor 0 ceil 3\n  v 0 0\n  v 4 0 mid 99\n";
    PC_CHECK(load(text, &m, err, sizeof err) != 0);
    PC_CHECK(strncmp(err, "line 6:", 7) == 0);

    /* a tiny error buffer is truncated, never overrun */
    char small[8];
    PC_CHECK(load(text, &m, small, sizeof small) != 0);
    PC_CHECK(strlen(small) < sizeof small);
}

static void test_tolerates_format_noise(void)
{
    /* comments, CRLF endings, tabs, no trailing newline, decimals without leading digits */
    const char *text =
        "# a map\r\n"
        "player\t1 1 90   # facing south\r\n"
        "\r\n"
        "sector 0 floor -0.25 ceil 3.5 light 12\r\n"
        "\tv 0 0 mid 1\r\n  v 4 0\r\n  v 4 4\r\n  v 0 4\r\n"
        "end";
    pc_map m;
    char err[160];
    PC_CHECK(load(text, &m, err, sizeof err) == 0);
    PC_CHECK(m.floor_h[0] == FX_C(-0.25) && m.ceil_h[0] == FX_C(3.5) && m.light[0] == 12);
    PC_CHECK(m.tex_mid[0] == 1 && m.tex_up[0] == 1 && m.tex_low[0] == 1);  /* default to mid */
    PC_CHECK(m.spawn_yaw == PC_ANGLE_DEG(90.0));

    PC_CHECK(load(k_square, &m, err, sizeof err) == 0);
}

int main(void)
{
    test_shipped_maps();
    test_values();
    test_find_sector();
    test_winding_is_normalized();
    test_rejections();
    test_error_reports_line();
    test_tolerates_format_noise();
    return PC_TEST_RESULT();
}
