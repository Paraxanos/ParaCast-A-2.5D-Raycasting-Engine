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

/* Movement vector for this tick, shared by both map types. */
static pc_vec2 step_vector(pc_camera *cam, pc_input in)
{
    pc_vec2 v = { 0, 0 };
    cam->yaw = (pc_angle)(cam->yaw + in.turn * (int)TURN_STEP);

    if (in.forward == 0 && in.strafe == 0)
        return v;

    fx_t speed = in.forward && in.strafe ? fx_mul(MOVE_SPEED, DIAG_SCALE) : MOVE_SPEED;
    fx_t dx = pc_cos(cam->yaw), dy = pc_sin(cam->yaw);

    /* Forward is (dx, dy); right is that rotated a quarter turn, (-dy, dx). */
    v.x = fx_mul(dx, fx_from_int(in.forward)) - fx_mul(dy, fx_from_int(in.strafe));
    v.y = fx_mul(dy, fx_from_int(in.forward)) + fx_mul(dx, fx_from_int(in.strafe));
    v.x = fx_mul(v.x, speed);
    v.y = fx_mul(v.y, speed);
    return v;
}

void pc_player_step(pc_camera *cam, const pc_gridmap *map, pc_input in)
{
    pc_vec2 v = step_vector(cam, in);

    if (!blocked(map, cam->pos.x + v.x, cam->pos.y))
        cam->pos.x += v.x;
    if (!blocked(map, cam->pos.x, cam->pos.y + v.y))
        cam->pos.y += v.y;
}

/* ---- sector maps ----------------------------------------------------------- */

#define EYE_EASE  FX_C(0.06)    /* most the eye may rise or fall per tick */
#define PUSH_PASSES 3

static int wall_blocks(const pc_map *m, pc_id w, pc_id owner, fx_t ref_floor)
{
    pc_id back = m->back[w];
    if (back == PC_NONE)
        return 1;
    if (m->floor_h[back] - ref_floor > PC_STEP_MAX)
        return 1;

    fx_t top = m->ceil_h[owner] < m->ceil_h[back] ? m->ceil_h[owner] : m->ceil_h[back];
    fx_t bot = m->floor_h[owner] > m->floor_h[back] ? m->floor_h[owner] : m->floor_h[back];
    return top - bot < PC_PLAYER_H;
}

/* Pushes p out of segment w if it is closer than the player radius. */
static void push_from_wall(const pc_map *m, pc_id w, pc_vec2 *p)
{
    const pc_vec2 a = m->a[w], e = m->e[w];
    const pc_vec2 ap = { p->x - a.x, p->y - a.y };

    int64_t num = ((int64_t)ap.x * e.x + (int64_t)ap.y * e.y) >> FX_SHIFT;   /* scale 2^16 */
    int64_t den = ((int64_t)e.x * e.x + (int64_t)e.y * e.y) >> FX_SHIFT;
    pc_vec2 c;
    if (num <= 0) {
        c = a;
    } else if (num >= den) {
        c = m->b[w];
    } else {
        fx_t r = (fx_t)(num * FX_ONE / den);
        c.x = a.x + fx_mul(e.x, r);
        c.y = a.y + fx_mul(e.y, r);
    }

    pc_vec2 d = { p->x - c.x, p->y - c.y };
    uint64_t sq = (uint64_t)((int64_t)d.x * d.x + (int64_t)d.y * d.y);
    if (sq >= (uint64_t)((int64_t)RADIUS * RADIUS))
        return;

    fx_t dist = (fx_t)pc_isqrt64(sq);
    fx_t push = RADIUS - dist;
    if (dist == 0) {
        /* centre exactly on the wall: step toward the interior (left normal of e) */
        fx_t len = m->len[w];
        p->x += fx_mul(fx_div(-e.y, len), push);
        p->y += fx_mul(fx_div(e.x, len), push);
    } else {
        p->x += fx_mul(fx_div(d.x, dist), push);
        p->y += fx_mul(fx_div(d.y, dist), push);
    }
}

static void collide(const pc_map *m, pc_id sector, pc_vec2 *p)
{
    const fx_t ref_floor = m->floor_h[sector];

    for (int pass = 0; pass < PUSH_PASSES; pass++) {
        pc_id end = m->first_wall[sector] + m->wall_count[sector];
        for (pc_id w = m->first_wall[sector]; w < end; w++) {
            if (wall_blocks(m, w, sector, ref_floor))
                push_from_wall(m, w, p);

            /* The player's circle can overlap walls of the sector behind a portal while
             * its centre is still here, so those count too. */
            pc_id nb = m->back[w];
            if (nb == PC_NONE)
                continue;
            pc_id nend = m->first_wall[nb] + m->wall_count[nb];
            for (pc_id w2 = m->first_wall[nb]; w2 < nend; w2++)
                if (wall_blocks(m, w2, nb, ref_floor))
                    push_from_wall(m, w2, p);
        }
    }
}

void pc_player_spawn(pc_camera *cam, const pc_map *map)
{
    cam->pos = map->spawn;
    cam->yaw = map->spawn_yaw;
    cam->sector = map->spawn_sector;
    cam->z = map->floor_h[map->spawn_sector] + PC_EYE_HEIGHT;
}

void pc_player_step_map(pc_camera *cam, const pc_map *map, pc_input in)
{
    pc_vec2 v = step_vector(cam, in);

    if (v.x != 0 || v.y != 0) {
        pc_vec2 p = { cam->pos.x + v.x, cam->pos.y + v.y };
        collide(map, cam->sector, &p);

        pc_id s = pc_map_find_sector(map, p, cam->sector);
        if (s != PC_NONE) {
            cam->pos = p;
            cam->sector = s;
        }
    }

    fx_t target = map->floor_h[cam->sector] + PC_EYE_HEIGHT;
    fx_t dz = target - cam->z;
    cam->z += dz > EYE_EASE ? EYE_EASE : dz < -EYE_EASE ? -EYE_EASE : dz;
}
