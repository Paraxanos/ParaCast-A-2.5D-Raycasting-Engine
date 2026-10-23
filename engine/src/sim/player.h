#ifndef PC_SIM_PLAYER_H
#define PC_SIM_PLAYER_H

#include "map/gridmap.h"
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

#endif
