#ifndef PC_CORE_LUT_H
#define PC_CORE_LUT_H

#include <stdint.h>
#include "core/fx.h"

/* Angles are 16-bit binary: 65536 units per turn, so wraparound is free. */
typedef uint16_t pc_angle;

#define PC_ANGLE_FULL    65536u
#define PC_ANGLE_QUARTER 16384u
#define PC_ANGLE_DEG(d)  ((pc_angle)(uint32_t)((d) * (65536.0 / 360.0) + 0.5))

/* Builds the sine table; idempotent. Must run before pc_sin / pc_cos. */
void pc_lut_init(void);

/* Table resolution is 4096 steps per turn (the top 12 bits of the angle). */
fx_t pc_sin(pc_angle a);
fx_t pc_cos(pc_angle a);

/* Hash of the whole sine table, pinned by a test so a libm difference is caught. */
uint64_t pc_lut_checksum(void);

#endif
