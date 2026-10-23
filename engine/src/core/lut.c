#include "core/lut.h"
#include "core/hash.h"

#include <math.h>

#define LUT_STEPS   4096
#define LUT_QUARTER (LUT_STEPS / 4)

/* sin over [0, pi/2] only; the other three quadrants are mirrored from it, so the
 * full table is exactly symmetric and only 1025 libm calls feed it. */
static fx_t g_quarter[LUT_QUARTER + 1];
static fx_t g_sin[LUT_STEPS];
static int  g_ready;

void pc_lut_init(void)
{
    if (g_ready)
        return;

    const double step = (3.14159265358979323846 / 2.0) / LUT_QUARTER;
    for (int i = 0; i <= LUT_QUARTER; i++)
        g_quarter[i] = (fx_t)floor(sin(i * step) * 65536.0 + 0.5);

    for (int i = 0; i < LUT_STEPS; i++) {
        int q = i / LUT_QUARTER;
        int r = i % LUT_QUARTER;
        fx_t v = (q & 1) ? g_quarter[LUT_QUARTER - r] : g_quarter[r];
        g_sin[i] = (q & 2) ? -v : v;
    }
    g_ready = 1;
}

fx_t pc_sin(pc_angle a) { return g_sin[a >> 4]; }

fx_t pc_cos(pc_angle a) { return g_sin[(pc_angle)(a + PC_ANGLE_QUARTER) >> 4]; }

uint64_t pc_lut_checksum(void)
{
    return pc_hash64(g_sin, sizeof g_sin, PC_HASH_SEED);
}
