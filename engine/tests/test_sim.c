#include "pc_test.h"
#include "paracast.h"
#include "sim/player.h"

#include <string.h>

static pc_camera start(double x, double y, double yaw_deg)
{
    pc_camera c = { { (fx_t)(x * 65536.0), (fx_t)(y * 65536.0) }, PC_ANGLE_DEG(yaw_deg), 0, 0 };
    return c;
}

static int in_wall(const pc_gridmap *m, const pc_camera *c)
{
    return pc_gridmap_cell(m, fx_to_int(c->pos.x), fx_to_int(c->pos.y)) != 0;
}

static void test_turn(void)
{
    pc_gridmap map;
    pc_gridmap_make_demo(&map);
    pc_camera c = start(12.5, 14.5, 0);
    pc_input right = { 0, 0, 1 };
    pc_input left = { 0, 0, -1 };

    pc_player_step(&c, &map, right);
    PC_CHECK(c.yaw == 624);
    pc_player_step(&c, &map, left);
    pc_player_step(&c, &map, left);
    PC_CHECK(c.yaw == (pc_angle)-624);   /* wraps through zero */
    PC_CHECK(c.pos.x == FX_C(12.5) && c.pos.y == FX_C(14.5));
}

static void test_walk_and_block(void)
{
    pc_lut_init();
    pc_gridmap map;
    pc_gridmap_make_demo(&map);

    /* Row 14 is clear to the east border at x = 23; the player must stop a radius short. */
    pc_camera c = start(12.5, 14.5, 0);
    pc_input fwd = { 1, 0, 0 };
    pc_player_step(&c, &map, fwd);
    PC_CHECK(c.pos.x > FX_C(12.5));

    for (int i = 0; i < 400; i++) {
        pc_player_step(&c, &map, fwd);
        PC_CHECK(!in_wall(&map, &c));
    }
    PC_CHECK(c.pos.x <= FX_C(23.0) - FX_C(0.25));
    PC_CHECK(c.pos.x >= FX_C(22.5));

    /* Pushing into the wall at 45 degrees slides along it instead of sticking. */
    c = start(20.5, 14.5, 45);
    fx_t y0 = c.pos.y;
    for (int i = 0; i < 200; i++) {
        pc_player_step(&c, &map, fwd);
        PC_CHECK(!in_wall(&map, &c));
    }
    PC_CHECK(c.pos.y > y0 + FX_C(3.0));
}

static void test_strafe_and_diagonal(void)
{
    pc_lut_init();
    pc_gridmap map;
    pc_gridmap_make_demo(&map);

    /* Facing east, right is +y on this y-down map. */
    pc_camera c = start(12.5, 12.5, 0);
    pc_input right = { 0, 1, 0 };
    pc_player_step(&c, &map, right);
    PC_CHECK(c.pos.y > FX_C(12.5) && c.pos.x == FX_C(12.5));

    /* Diagonal speed stays near one step, not 1.41. */
    pc_camera a = start(12.5, 12.5, 0), b = start(12.5, 12.5, 0);
    pc_input diag = { 1, 1, 0 }, straight = { 1, 0, 0 };
    pc_player_step(&a, &map, diag);
    pc_player_step(&b, &map, straight);
    fx_t da = fx_mul(a.pos.x - FX_C(12.5), a.pos.x - FX_C(12.5)) +
              fx_mul(a.pos.y - FX_C(12.5), a.pos.y - FX_C(12.5));
    fx_t db = fx_mul(b.pos.x - FX_C(12.5), b.pos.x - FX_C(12.5));
    PC_CHECK(fx_abs(da - db) < 64);   /* squared step length, about 0.01 */
}

/* ---- sector maps ------------------------------------------------------------ */

static uint8_t g_mem[512 * 1024];

static int load_map(const char *name, const char *text, pc_map *m)
{
    pc_arena a;
    pc_arena_init(&a, g_mem, sizeof g_mem);
    char path[512], err[160] = "";
    size_t len = text ? strlen(text) : 0;
    if (!text) {
        snprintf(path, sizeof path, "%s/%s.map", PC_MAPS_DIR, name);
        text = pc_file_read(path, &a, &len);
    }
    if (!text || pc_map_load(text, len, &a, m, err, sizeof err)) {
        printf("load_map(%s): %s\n", name, err);
        return -1;
    }
    return 0;
}

static pc_camera spawn_at(const pc_map *m, double x, double y, double yaw_deg)
{
    pc_camera c;
    pc_player_spawn(&c, m);
    c.pos.x = (fx_t)(x * 65536.0);
    c.pos.y = (fx_t)(y * 65536.0);
    c.yaw = PC_ANGLE_DEG(yaw_deg);
    c.sector = pc_map_find_sector(m, c.pos, PC_NONE);
    c.z = m->floor_h[c.sector] + PC_EYE_HEIGHT;
    return c;
}

/* Runs n ticks of one input and checks the camera never ends up outside its sector. */
static void run(const pc_map *m, pc_camera *c, pc_input in, int n)
{
    for (int i = 0; i < n; i++) {
        pc_player_step_map(c, m, in);
        PC_CHECK(c->sector < m->nsectors && pc_map_contains(m, c->sector, c->pos));
    }
}

static void test_spawn(void)
{
    pc_map m;
    PC_CHECK(load_map("steps", NULL, &m) == 0);
    pc_camera c;
    pc_player_spawn(&c, &m);
    PC_CHECK(c.pos.x == FX_C(2.0) && c.pos.y == FX_C(4.0) && c.sector == 0);
    PC_CHECK(c.z == FX_C(0.8));
}

static void test_climb_stairs(void)
{
    pc_map m;
    PC_CHECK(load_map("steps", NULL, &m) == 0);
    pc_camera c = spawn_at(&m, 2, 4, 0);
    pc_input fwd = { 1, 0, 0 };

    /* hall (0) -> step 1 (+0.4) -> step 2 (+0.4), then the east wall at x = 20 */
    run(&m, &c, fwd, 400);
    PC_CHECK(c.sector == 2);
    PC_CHECK(c.pos.x <= FX_C(20.0) - FX_C(0.25) && c.pos.x >= FX_C(19.6));
    PC_CHECK(c.z == FX_C(0.8) + PC_EYE_HEIGHT);      /* eased all the way up */

    /* the eye eases rather than snapping: one tick after the first step it is mid-rise */
    c = spawn_at(&m, 11.6, 4, 0);
    fx_t z0 = c.z;
    for (int i = 0; i < 40 && c.sector == 0; i++)
        pc_player_step_map(&c, &m, fwd);
    PC_CHECK(c.sector == 1);
    PC_CHECK(c.z > z0 && c.z < FX_C(0.4) + PC_EYE_HEIGHT);
}

static void test_pit(void)
{
    pc_map m;
    PC_CHECK(load_map("steps", NULL, &m) == 0);
    pc_camera c = spawn_at(&m, 2, 4, 180);
    pc_input fwd = { 1, 0, 0 };

    /* drop into the pit through the doorway at x = 0 (y 2..6), stop at its far wall */
    run(&m, &c, fwd, 200);
    PC_CHECK(c.sector == 3);
    PC_CHECK(c.pos.x >= FX_C(-4.0) + FX_C(0.25) && c.pos.x <= FX_C(-3.7));
    PC_CHECK(c.z == FX_C(-0.4) + PC_EYE_HEIGHT);

    /* the 0.4 climb back out is within the step limit */
    c.yaw = PC_ANGLE_DEG(0.0);
    run(&m, &c, fwd, 80);
    PC_CHECK(c.sector == 0);
}

static void test_doorway_and_window(void)
{
    pc_map m;
    PC_CHECK(load_map("window", NULL, &m) == 0);
    pc_input fwd = { 1, 0, 0 };

    /* straight through the doorway (y 2..4) and on to room B's east wall */
    pc_camera c = spawn_at(&m, 2, 3, 0);
    run(&m, &c, fwd, 300);
    PC_CHECK(c.sector == 1);
    PC_CHECK(c.pos.x > FX_C(11.0) && c.pos.x <= FX_C(21.0) - FX_C(0.25));

    /* the window (y 6..8) has a 0.9 sill and only 1.0 of opening: not passable */
    c = spawn_at(&m, 2, 7, 0);
    run(&m, &c, fwd, 300);
    PC_CHECK(c.sector == 0);
    PC_CHECK(c.pos.x <= FX_C(10.0) - FX_C(0.25) && c.pos.x >= FX_C(9.5));

    /* a doorway too narrow for the circle's clearance is wall; the jamb at y = 4 stops
     * the centre line y = 4.1 from getting through a gap spanning 2..4 */
    c = spawn_at(&m, 2, 4.1, 0);
    run(&m, &c, fwd, 300);
    PC_CHECK(c.sector == 0);
}

/* Builds two rooms joined by a portal whose far side has the given floor and ceiling. */
static const char *two_rooms(char *buf, size_t cap, const char *floor, const char *ceil)
{
    snprintf(buf, cap,
             "player 2 2 0\n"
             "sector 0 floor 0 ceil 3\n  v 0 0\n  v 4 0 portal 1\n  v 4 4\n  v 0 4\nend\n"
             "sector 1 floor %s ceil %s\n  v 4 0\n  v 8 0\n  v 8 4\n  v 4 4 portal 0\nend\n",
             floor, ceil);
    return buf;
}

static int crosses(const char *floor, const char *ceil)
{
    char text[512];
    pc_map m;
    if (load_map("two", two_rooms(text, sizeof text, floor, ceil), &m))
        return -1;
    pc_camera c = spawn_at(&m, 2, 2, 0);
    pc_input fwd = { 1, 0, 0 };
    run(&m, &c, fwd, 200);
    return c.sector == 1;
}

static void test_step_and_headroom_limits(void)
{
    PC_CHECK(crosses("0", "3") == 1);          /* level */
    PC_CHECK(crosses("0.5", "3.5") == 1);      /* exactly the step limit */
    PC_CHECK(crosses("0.6", "3.6") == 0);      /* a little too high */
    PC_CHECK(crosses("-5", "3") == 1);         /* drops of any depth are fine */
    PC_CHECK(crosses("-5", "-2") == 0);        /* but not into a room whose ceiling is below our floor */
    PC_CHECK(crosses("0", "1.2") == 1);        /* exactly the clearance needed */
    PC_CHECK(crosses("0", "1.0") == 0);        /* too low to stand in */
}

/* The same scripted input must give the same camera, and wandering a maze with
 * scrambled input must never put the camera outside every sector. */
static pc_camera wander(const pc_map *m, uint32_t seed)
{
    pc_camera c;
    pc_player_spawn(&c, m);
    uint32_t s = seed;
    pc_input in = { 0, 0, 0 };
    for (int i = 0; i < 6000; i++) {
        s = s * 1103515245u + 12345u;
        if ((i % 20) == 0) {
            in.forward = (int)((s >> 16) % 3) - 1;
            in.strafe = (int)((s >> 18) % 3) - 1;
            in.turn = (int)((s >> 20) % 3) - 1;
            if ((s >> 24) % 4)
                in.forward = 1;      /* mostly press on, so it gets somewhere */
        }
        pc_player_step_map(&c, m, in);
        if (!(c.sector < m->nsectors && pc_map_contains(m, c.sector, c.pos))) {
            printf("wander seed %u left its sector at tick %d\n", seed, i);
            PC_CHECK(0);
            break;
        }
    }
    return c;
}

static void test_wander_is_safe_and_deterministic(void)
{
    const char *names[] = { "steps", "window", "nested", "maze" };
    for (int i = 0; i < 4; i++) {
        pc_map m;
        PC_CHECK(load_map(names[i], NULL, &m) == 0);
        for (uint32_t seed = 1; seed <= 6; seed++) {
            pc_camera a = wander(&m, seed), b = wander(&m, seed);
            /* field by field: memcmp would also compare the struct's padding bytes */
            PC_CHECK(a.pos.x == b.pos.x && a.pos.y == b.pos.y && a.yaw == b.yaw &&
                     a.z == b.z && a.sector == b.sector);
        }
    }
}

int main(void)
{
    test_turn();
    test_walk_and_block();
    test_strafe_and_diagonal();
    test_spawn();
    test_climb_stairs();
    test_pit();
    test_doorway_and_window();
    test_step_and_headroom_limits();
    test_wander_is_safe_and_deterministic();
    return PC_TEST_RESULT();
}
