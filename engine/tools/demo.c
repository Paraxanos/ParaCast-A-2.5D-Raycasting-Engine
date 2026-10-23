/* paracast_demo [out.ppm] [x y yaw_deg]: renders one frame of the demo map to a PPM. */

#include "paracast.h"

#include <stdio.h>
#include <stdlib.h>

#define WIDTH  640
#define HEIGHT 400

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : "frame.ppm";
    double px = argc > 4 ? atof(argv[2]) : 12.5;
    double py = argc > 4 ? atof(argv[3]) : 12.5;
    double yaw = argc > 4 ? atof(argv[4]) : 20.0;

    size_t mem_size = (size_t)8 << 20;
    void *mem = malloc(mem_size);
    if (!mem)
        return 1;

    pc_arena arena;
    pc_arena_init(&arena, mem, mem_size);
    pc_lut_init();

    pc_assets assets;
    pc_gridmap map;
    pc_fb fb;
    pc_raycaster rc;
    pc_gridmap_make_demo(&map);

    if (pc_assets_init(&assets, &arena) || pc_fb_init(&fb, &arena, WIDTH, HEIGHT) ||
        pc_raycaster_init(&rc, &arena, WIDTH, HEIGHT)) {
        fprintf(stderr, "out of arena memory\n");
        return 1;
    }
    uint32_t *rgba = (uint32_t *)pc_arena_alloc_array(&arena, (size_t)WIDTH * HEIGHT, 4, 64);
    if (!rgba)
        return 1;

    pc_camera cam = { { FX_C(12.5), FX_C(12.5) }, PC_ANGLE_DEG(20.0) };
    cam.pos.x = (fx_t)(px * 65536.0);
    cam.pos.y = (fx_t)(py * 65536.0);
    cam.yaw = PC_ANGLE_DEG(yaw);

    pc_stats st;
    pc_raycaster_render(&rc, &fb, &map, &assets, &cam, &st);
    pc_present_rgba(&fb, &assets, rgba);

    if (pc_ppm_write(out, rgba, WIDTH, HEIGHT)) {
        fprintf(stderr, "cannot write %s\n", out);
        return 1;
    }
    printf("wrote %s  hash=%016llx  dda_steps=%u wall_px=%u plane_px=%u\n", out,
           (unsigned long long)pc_hash64(fb.pix, (size_t)WIDTH * HEIGHT, PC_HASH_SEED),
           st.dda_steps, st.wall_px, st.plane_px);
    free(mem);
    return 0;
}
