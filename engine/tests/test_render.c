#include "pc_test.h"
#include "paracast.h"

#include <stdlib.h>
#include <string.h>

#define W 640
#define H 400
#define SENTINEL 0xEE

/* Regenerate with paracast_demo (12.5 12.5 20) after an intentional visual change. */
#define GOLDEN_HASH 0xff5f459024ade5cfULL

static pc_vec2 vec(double x, double y)
{
    pc_vec2 v = { (fx_t)(x * 65536.0), (fx_t)(y * 65536.0) };
    return v;
}

static void test_cast(void)
{
    pc_gridmap map;
    pc_gridmap_make_demo(&map);
    pc_grid_hit h;

    /* Row 14 and column 14 are clear all the way to the border. */
    pc_grid_cast(&map, vec(12.5, 14.5), vec(1, 0), &h);
    PC_CHECK(h.dist == FX_C(10.5) && h.side == 0 && h.cell == 1);

    pc_grid_cast(&map, vec(12.5, 14.5), vec(-1, 0), &h);
    PC_CHECK(h.dist == FX_C(11.5) && h.side == 0 && h.cell == 1);

    pc_grid_cast(&map, vec(14.5, 12.5), vec(0, 1), &h);
    PC_CHECK(h.dist == FX_C(10.5) && h.side == 1 && h.cell == 1);

    /* The row-12 pillar at x = 18 is a texture-4 block, 5.5 units away. */
    pc_grid_cast(&map, vec(12.5, 12.5), vec(1, 0), &h);
    PC_CHECK(h.dist == FX_C(5.5) && h.side == 0 && h.cell == 4);

    /* Exact corner tie: ties go to the y step, entering block cell (17,18) first. */
    pc_grid_cast(&map, vec(12.5, 12.5), vec(1, 1), &h);
    PC_CHECK(h.dist == FX_C(5.5) && h.side == 1 && h.cell == 2);

    /* A ray with no x component must still terminate. */
    pc_grid_cast(&map, vec(12.5, 12.5), vec(0, -1), &h);
    PC_CHECK(h.side == 1 && h.cell != 0);

    PC_CHECK(h.u >= 0 && h.u < FX_ONE);
}

static void test_orbit_is_open(void)
{
    pc_gridmap map;
    pc_gridmap_make_demo(&map);
    for (int y = 9; y <= 15; y++)
        for (int x = 9; x <= 15; x++)
            PC_CHECK(pc_gridmap_cell(&map, x, y) == 0);
}

typedef struct {
    pc_arena arena;
    pc_assets assets;
    pc_gridmap map;
    pc_fb fb;
    pc_raycaster rc;
    void *mem;
} scene;

static int scene_init(scene *s, size_t offset)
{
    size_t size = (size_t)4 << 20;
    s->mem = malloc(size + 64);
    if (!s->mem)
        return -1;
    pc_arena_init(&s->arena, (uint8_t *)s->mem + offset, size);
    pc_lut_init();
    pc_gridmap_make_demo(&s->map);
    return pc_assets_init(&s->assets, &s->arena) || pc_fb_init(&s->fb, &s->arena, W, H) ||
           pc_raycaster_init(&s->rc, &s->arena, W, H);
}

static uint64_t render_golden(scene *s, pc_stats *st)
{
    pc_camera cam = { { FX_C(12.5), FX_C(12.5) }, PC_ANGLE_DEG(20.0), 0, 0 };
    memset(s->fb.pix, SENTINEL, (size_t)W * H);
    pc_raycaster_render(&s->rc, &s->fb, &s->map, &s->assets, &cam, st);
    return pc_hash64(s->fb.pix, (size_t)W * H, PC_HASH_SEED);
}

static void test_frame(void)
{
    scene a, b;
    PC_CHECK(scene_init(&a, 0) == 0);
    PC_CHECK(scene_init(&b, 40) == 0);   /* different base alignment must not matter */

    pc_stats st, st2;
    uint32_t allocs = a.arena.allocs;
    uint64_t ha = render_golden(&a, &st);
    PC_CHECK(a.arena.allocs == allocs);  /* the frame loop never allocates */

    uint64_t hb = render_golden(&b, &st2);
    PC_CHECK(ha == hb);
    PC_CHECK(ha == render_golden(&a, &st));

    /* Walls and planes must partition the screen: every pixel written exactly once. */
    PC_CHECK(st.wall_px + st.plane_px == (uint32_t)(W * H));
    int unwritten = 0;
    for (size_t i = 0; i < (size_t)W * H; i++)
        unwritten += a.fb.pix[i] == SENTINEL;
    PC_CHECK(unwritten == 0);

    PC_CHECK(ha == GOLDEN_HASH);

    free(a.mem);
    free(b.mem);
}

int main(void)
{
    test_cast();
    test_orbit_is_open();
    test_frame();
    return PC_TEST_RESULT();
}
