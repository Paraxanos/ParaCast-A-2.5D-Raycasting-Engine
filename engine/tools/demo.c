/* paracast_demo [out.ppm] [x y yaw_deg] [--map NAME|FILE] [--automap] [--clip]
 * Renders one frame to a PPM. Without --map it renders the v0 grid raycaster's demo map;
 * with it, the sector renderer from the map's spawn point (or from x y yaw if given).
 * --automap draws the top-down map instead; --clip overlays the portal clip windows. */

#include "scene.h"

#define WIDTH   640
#define HEIGHT  400

int main(int argc, char **argv)
{
    const char *out = "frame.ppm", *map = NULL;
    double xyz[3];
    int npos = 0, automap = 0, clip = 0, have_out = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--map") && i + 1 < argc)
            map = argv[++i];
        else if (!strcmp(argv[i], "--automap"))
            automap = 1;
        else if (!strcmp(argv[i], "--clip"))
            clip = 1;
        else if (!have_out && npos == 0 && strchr(argv[i], '.') && strlen(argv[i]) > 4 &&
                 !strcmp(argv[i] + strlen(argv[i]) - 4, ".ppm")) {
            out = argv[i];
            have_out = 1;
        } else if (npos < 3)
            xyz[npos++] = atof(argv[i]);
        else {
            fprintf(stderr, "usage: paracast_demo [out.ppm] [x y yaw] [--map NAME] [--automap] [--clip]\n");
            return 2;
        }
    }
    if (npos != 0 && npos != 3) {
        fprintf(stderr, "give all of x y yaw, or none\n");
        return 2;
    }

    scene sc;
    if (scene_init(&sc, map, WIDTH, HEIGHT))
        return 1;

    pc_camera cam = scene_spawn(&sc);
    if (npos == 3 && scene_place(&sc, &cam, xyz[0], xyz[1], xyz[2])) {
        fprintf(stderr, "(%.2f, %.2f) is outside every sector\n", xyz[0], xyz[1]);
        return 1;
    }

    pc_stats st;
    scene_render(&sc, &cam, &st);
    if (sc.sector_mode && automap)
        pc_automap_draw(&sc.fb, &sc.map, &cam);
    else if (sc.sector_mode && clip)
        pc_clipview_draw(&sc.fb, &sc.sr);
    pc_present_rgba(&sc.fb, &sc.assets, sc.rgba);

    if (pc_ppm_write(out, sc.rgba, WIDTH, HEIGHT)) {
        fprintf(stderr, "cannot write %s\n", out);
        return 1;
    }
    printf("wrote %s  hash=%016llx\n", out,
           (unsigned long long)pc_hash64(sc.fb.pix, (size_t)WIDTH * HEIGHT, PC_HASH_SEED));
    if (sc.sector_mode)
        printf("  visits=%u portal_cols=%u wall_px=%u plane_px=%u sprite_px=%u\n",
               st.sector_visits, st.portal_columns, st.wall_px, st.plane_px, st.sprite_px);
    else
        printf("  dda_steps=%u wall_px=%u plane_px=%u\n", st.dda_steps, st.wall_px, st.plane_px);
    scene_free(&sc);
    return 0;
}
