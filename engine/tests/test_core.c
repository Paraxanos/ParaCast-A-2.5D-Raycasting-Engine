#include "pc_test.h"
#include "paracast.h"

#include <stdlib.h>

static void test_fx(void)
{
    PC_CHECK(fx_mul(FX_C(1.5), FX_C(2.0)) == FX_C(3.0));
    PC_CHECK(fx_mul(FX_C(-1.5), FX_C(2.0)) == FX_C(-3.0));
    PC_CHECK(fx_mul(FX_C(0.5), FX_C(0.5)) == FX_C(0.25));
    PC_CHECK(fx_mul(1, FX_HALF) == 0);      /* truncates toward -inf ... */
    PC_CHECK(fx_mul(-1, FX_HALF) == -1);    /* ... so negatives round down */

    PC_CHECK(fx_div(FX_C(3.0), FX_C(2.0)) == FX_C(1.5));
    PC_CHECK(fx_div(FX_C(-3.0), FX_C(2.0)) == FX_C(-1.5));
    PC_CHECK(fx_div(FX_ONE, 0) == FX_MAX);
    PC_CHECK(fx_div(-FX_ONE, 0) == FX_MIN);
    PC_CHECK(fx_div(0, 0) == 0);
    PC_CHECK(fx_div(FX_MAX, FX_HALF) == FX_MAX);
    PC_CHECK(fx_div(FX_MIN, FX_C(-0.5)) == FX_MAX);

    PC_CHECK(fx_to_int(FX_C(-0.5)) == -1);
    PC_CHECK(fx_to_int(FX_C(2.99)) == 2);
    PC_CHECK(fx_frac(FX_C(-0.25)) == FX_C(0.75));
    PC_CHECK(fx_from_int(-7) == FX_C(-7.0));
    PC_CHECK(fx_abs(FX_MIN) == FX_MAX);
    PC_CHECK(fx_abs(FX_C(-2.0)) == FX_C(2.0));
}

static void test_arena(void)
{
    static uint8_t mem[1024];
    pc_arena a;
    pc_arena_init(&a, mem, sizeof mem);

    void *p = pc_arena_alloc(&a, 3, 1);
    void *q = pc_arena_alloc(&a, 8, 64);
    PC_CHECK(p != NULL && q != NULL);
    PC_CHECK(((uintptr_t)q & 63) == 0);
    PC_CHECK(a.allocs == 2);

    size_t used = a.used;
    PC_CHECK(pc_arena_alloc(&a, sizeof mem, 1) == NULL);
    PC_CHECK(a.used == used);
    PC_CHECK(pc_arena_alloc_array(&a, (size_t)-1, 16, 16) == NULL);

    size_t mark = pc_arena_save(&a);
    PC_CHECK(pc_arena_alloc(&a, 100, 4) != NULL);
    pc_arena_restore(&a, mark);
    PC_CHECK(a.used == mark);

    pc_arena_reset(&a);
    PC_CHECK(a.used == 0);
}

static void test_hash(void)
{
    PC_CHECK(pc_hash64("", 0, PC_HASH_SEED) == PC_HASH_SEED);
    PC_CHECK(pc_hash64("a", 1, PC_HASH_SEED) == 0xaf63dc4c8601ec8cULL);
    PC_CHECK(pc_hash64("ab", 2, PC_HASH_SEED) ==
             pc_hash64("b", 1, pc_hash64("a", 1, PC_HASH_SEED)));
}

static void test_lut(void)
{
    pc_lut_init();

    PC_CHECK(pc_sin(0) == 0);
    PC_CHECK(pc_sin(PC_ANGLE_QUARTER) == FX_ONE);
    PC_CHECK(pc_sin(2 * PC_ANGLE_QUARTER) == 0);
    PC_CHECK(pc_sin(3 * PC_ANGLE_QUARTER) == -FX_ONE);
    PC_CHECK(pc_cos(0) == FX_ONE);
    PC_CHECK(pc_cos(2 * PC_ANGLE_QUARTER) == -FX_ONE);
    PC_CHECK(abs(pc_sin(PC_ANGLE_DEG(30.0)) - FX_HALF) < 200);   /* 4096-step table */

    for (uint32_t a = 0; a < PC_ANGLE_FULL; a += 16) {
        fx_t s = pc_sin((pc_angle)a), c = pc_cos((pc_angle)a);
        PC_CHECK(pc_sin((pc_angle)(a + 2 * PC_ANGLE_QUARTER)) == -s);
        fx_t err = fx_mul(s, s) + fx_mul(c, c) - FX_ONE;
        PC_CHECK(err > -8 && err < 8);
    }

    /* Pinned so a libm that rounds differently is caught instead of silently
     * changing every golden frame. */
    PC_CHECK(pc_lut_checksum() == 0x00361da115eb6196ULL);
}

int main(void)
{
    test_fx();
    test_arena();
    test_hash();
    test_lut();
    return PC_TEST_RESULT();
}
