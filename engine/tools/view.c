/* paracast_view [--scale N] [--novsync] [--frames N]: walk the demo map in an SDL2 window.
 * WASD moves, arrows or Q/E turn, Esc quits. --frames exits after N frames (smoke test). */

#include "paracast.h"
#include "platform/sdl.h"
#include "sim/player.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIDTH   640
#define HEIGHT  400

#define TICK_NS     (1000000000ULL / PC_TICK_HZ)
#define MAX_LAG_NS  250000000ULL    /* after a stall, drop time instead of fast-forwarding */
#define TITLE_NS    500000000ULL

int main(int argc, char **argv)
{
    int scale = 2, vsync = 1;
    long max_frames = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--scale") && i + 1 < argc)
            scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            max_frames = atol(argv[++i]);
        else if (!strcmp(argv[i], "--novsync"))
            vsync = 0;
        else {
            fprintf(stderr, "usage: paracast_view [--scale N] [--novsync] [--frames N]\n");
            return 2;
        }
    }
    if (scale < 1)
        scale = 1;

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

    pc_window *win = pc_window_open("Paracast", WIDTH, HEIGHT, scale, vsync);
    if (!win)
        return 1;

    pc_camera cam = { { FX_C(12.5), FX_C(12.5) }, PC_ANGLE_DEG(20.0) };
    pc_input in = { 0, 0, 0 };
    pc_stats st;

    uint64_t last = pc_time_ns();
    uint64_t acc = 0, title_acc = 0, render_ns = 0;
    long frames = 0, title_frames = 0;

    while (pc_window_poll(win, &in) && (max_frames == 0 || frames < max_frames)) {
        uint64_t now = pc_time_ns();
        uint64_t dt = now - last;
        last = now;

        acc += dt;
        if (acc > MAX_LAG_NS)
            acc = MAX_LAG_NS;
        while (acc >= TICK_NS) {
            pc_player_step(&cam, &map, in);
            acc -= TICK_NS;
        }

        uint64_t t0 = pc_time_ns();
        pc_raycaster_render(&rc, &fb, &map, &assets, &cam, &st);
        render_ns += pc_time_ns() - t0;
        pc_present_rgba(&fb, &assets, rgba);
        pc_window_present(win, rgba);
        frames++;
        title_frames++;

        title_acc += dt;
        if (title_acc >= TITLE_NS) {
            char title[96];
            snprintf(title, sizeof title, "Paracast | %.0f fps | render %.3f ms",
                     title_frames * 1e9 / (double)title_acc,
                     render_ns / 1e6 / (double)title_frames);
            pc_window_set_title(win, title);
            title_acc = 0;
            title_frames = 0;
            render_ns = 0;
        }
    }

    pc_window_close(win);
    printf("paracast_view: %ld frames, final pos (%.2f, %.2f)\n", frames,
           cam.pos.x / 65536.0, cam.pos.y / 65536.0);
    free(mem);
    return 0;
}
