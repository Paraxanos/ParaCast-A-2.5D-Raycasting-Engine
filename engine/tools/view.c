/* paracast_view [--map NAME|FILE | --grid] [--scale N] [--novsync] [--frames N]
 * Walk a map in an SDL2 window. Default is the sector map "nested"; --grid runs the v0
 * grid raycaster instead.
 *   WASD move, arrows or Q/E turn, Tab automap, C clip-window overlay, Esc quit.
 * --size WxH sets the resolution, --threads N / --strip H the CPU back-end.
 * --frames exits after N frames (smoke test). */

#include "scene.h"
#include "platform/sdl.h"

#define TICK_NS     (1000000000ULL / PC_TICK_HZ)
#define MAX_LAG_NS  250000000ULL    /* after a stall, drop time instead of fast-forwarding */
#define TITLE_NS    500000000ULL

int main(int argc, char **argv)
{
    int scale = 2, vsync = 1;
    long max_frames = 0;
    const char *map = "nested";
    scene_opts opts = scene_default_opts();

    for (int i = 1; i < argc; i++) {
        int r = scene_parse_opt(&opts, argc, argv, &i);
        if (r < 0) {
            fprintf(stderr, "bad value for %s\n", argv[i - 1]);
            return 2;
        }
        if (r)
            continue;

        if (!strcmp(argv[i], "--scale") && i + 1 < argc)
            scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            max_frames = atol(argv[++i]);
        else if (!strcmp(argv[i], "--map") && i + 1 < argc)
            map = argv[++i];
        else if (!strcmp(argv[i], "--grid"))
            map = NULL;
        else if (!strcmp(argv[i], "--novsync"))
            vsync = 0;
        else {
            fprintf(stderr, "usage: paracast_view [--map NAME | --grid] [--scale N] "
                            "[--novsync] [--frames N] [--size WxH] [--threads N] [--strip H]\n");
            return 2;
        }
    }
    if (scale < 1)
        scale = 1;

    scene sc;
    if (scene_init(&sc, map, &opts))
        return 1;

    pc_window *win = pc_window_open("Paracast", sc.w, sc.h, scale, vsync);
    if (!win)
        return 1;

    pc_camera cam = scene_spawn(&sc);
    pc_input in = { 0, 0, 0 };
    pc_ui ui = { 0, 0 };
    pc_stats st;
    int show_automap = 0, show_clip = 0;

    uint64_t last = pc_time_ns();
    uint64_t acc = 0, title_acc = 0, render_ns = 0;
    long frames = 0, title_frames = 0;

    while (pc_window_poll(win, &in, &ui) && (max_frames == 0 || frames < max_frames)) {
        uint64_t now = pc_time_ns();
        uint64_t dt = now - last;
        last = now;

        if (ui.automap)
            show_automap = !show_automap;
        if (ui.clipview)
            show_clip = !show_clip;

        acc += dt;
        if (acc > MAX_LAG_NS)
            acc = MAX_LAG_NS;
        while (acc >= TICK_NS) {
            if (sc.sector_mode)
                pc_player_step_map(&cam, &sc.map, in);
            else
                pc_player_step(&cam, &sc.grid, in);
            acc -= TICK_NS;
        }

        uint64_t t0 = pc_time_ns();
        scene_render(&sc, &cam, &st);
        render_ns += pc_time_ns() - t0;
        if (sc.sector_mode && show_automap)
            pc_automap_draw(&sc.fb, &sc.map, &cam);
        else if (sc.sector_mode && show_clip)
            pc_clipview_draw(&sc.fb, &sc.sr);
        pc_present_rgba(&sc.fb, &sc.assets, sc.rgba);
        pc_window_present(win, sc.rgba);
        frames++;
        title_frames++;

        title_acc += dt;
        if (title_acc >= TITLE_NS) {
            char title[128];
            if (sc.sector_mode)
                snprintf(title, sizeof title, "Paracast | %.0f fps | render %.3f ms | sector %u, %u visits",
                         title_frames * 1e9 / (double)title_acc,
                         render_ns / 1e6 / (double)title_frames, (unsigned)cam.sector,
                         st.sector_visits);
            else
                snprintf(title, sizeof title, "Paracast (grid v0) | %.0f fps | render %.3f ms",
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
    scene_free(&sc);
    return 0;
}
