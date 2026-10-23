#include "pc_test.h"
#include "paracast.h"
#include "sim/player.h"

static pc_camera start(double x, double y, double yaw_deg)
{
    pc_camera c = { { (fx_t)(x * 65536.0), (fx_t)(y * 65536.0) }, PC_ANGLE_DEG(yaw_deg) };
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

int main(void)
{
    test_turn();
    test_walk_and_block();
    test_strafe_and_diagonal();
    return PC_TEST_RESULT();
}
