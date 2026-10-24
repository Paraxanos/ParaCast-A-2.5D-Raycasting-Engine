#ifndef PC_SIM_PLAYER_H
#define PC_SIM_PLAYER_H

#include "map/gridmap.h"
#include "map/map.h"
#include "render/raycast.h"

/* The simulation advances in fixed ticks and never reads a clock. */
#define PC_TICK_HZ 35

/* Per-tick input axes, each -1, 0 or +1. */
typedef struct {
    int forward;   /* +1 = ahead */
    int strafe;    /* +1 = right */
    int turn;      /* +1 = clockwise on screen (increasing yaw) */
} pc_input;

/* Moves and turns the camera by one tick. Walls block movement; motion slides along
 * them because each axis is resolved on its own. */
void pc_player_step(pc_camera *cam, const pc_gridmap *map, pc_input in);

/* Sector-map player metrics, in world units. */
#define PC_EYE_HEIGHT   FX_C(0.8)    /* eye above the floor */
#define PC_PLAYER_H     FX_C(1.2)    /* headroom needed to walk through a gap */
#define PC_STEP_MAX     FX_C(0.5)    /* tallest step that can be climbed */

/* Puts the camera at the map's spawn point. */
void pc_player_spawn(pc_camera *cam, const pc_map *map);

/* One tick on a sector map. Walls, steps taller than PC_STEP_MAX and gaps lower than
 * PC_PLAYER_H block movement and push the player out; the camera sector follows the
 * player across portals and the eye height eases toward the new floor. */
void pc_player_step_map(pc_camera *cam, const pc_map *map, pc_input in);

#endif
