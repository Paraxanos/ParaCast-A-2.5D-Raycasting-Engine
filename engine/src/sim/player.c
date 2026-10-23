#include "sim/player.h"

#define MOVE_SPEED  FX_C(0.1)        /* units per tick: 3.5 units per second */
#define TURN_STEP   624u             /* angle units per tick: about 120 degrees per second */
#define RADIUS      FX_C(0.25)
#define DIAG_SCALE  FX_C(0.70710678) /* keeps diagonal movement from being faster */

static int blocked(const pc_gridmap *map, fx_t x, fx_t y)
{
    return pc_gridmap_cell(map, fx_to_int(x - RADIUS), fx_to_int(y - RADIUS)) ||
           pc_gridmap_cell(map, fx_to_int(x + RADIUS), fx_to_int(y - RADIUS)) ||
           pc_gridmap_cell(map, fx_to_int(x - RADIUS), fx_to_int(y + RADIUS)) ||
           pc_gridmap_cell(map, fx_to_int(x + RADIUS), fx_to_int(y + RADIUS));
}

void pc_player_step(pc_camera *cam, const pc_gridmap *map, pc_input in)
{
    cam->yaw = (pc_angle)(cam->yaw + in.turn * (int)TURN_STEP);

    if (in.forward == 0 && in.strafe == 0)
        return;

    fx_t speed = in.forward && in.strafe ? fx_mul(MOVE_SPEED, DIAG_SCALE) : MOVE_SPEED;
    fx_t dx = pc_cos(cam->yaw), dy = pc_sin(cam->yaw);

    /* Forward is (dx, dy); right is that rotated a quarter turn, (-dy, dx). */
    fx_t vx = fx_mul(dx, fx_from_int(in.forward)) - fx_mul(dy, fx_from_int(in.strafe));
    fx_t vy = fx_mul(dy, fx_from_int(in.forward)) + fx_mul(dx, fx_from_int(in.strafe));
    vx = fx_mul(vx, speed);
    vy = fx_mul(vy, speed);

    if (!blocked(map, cam->pos.x + vx, cam->pos.y))
        cam->pos.x += vx;
    if (!blocked(map, cam->pos.x, cam->pos.y + vy))
        cam->pos.y += vy;
}
