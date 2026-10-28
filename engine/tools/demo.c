/* paracast_demo [out.ppm] [x y yaw_deg] [--map NAME|FILE] [--automap] [--clip]
 *               [--size WxH] [--threads N] [--strip H] [--dump FILE.pcmd]
 * Renders one frame to a PPM. Without --map it renders the v0 grid raycaster's demo map;
 * with it, the sector renderer from the map's spawn point (or from x y yaw if given).
 * --automap draws the top-down map instead; --clip overlays the portal clip windows;
 * --dump also writes the frame's command buffer for paracast_replay. */

#include "scene.h"
#include "platform/cmddump.h"

int main(int argc, char **argv)
{
    const char *out = "frame.ppm", *map = NULL, *dump = NULL;
    double xyz[3];
    int npos = 0, automap = 0, clip = 0, have_out = 0;
    scene_opts opts = scene_default_opts();

    for (int i = 1; i < argc; i++) {
        int r = scene_parse_opt(&opts, argc, argv, &i);
        if (r < 0) {
            fprintf(stderr, "bad value for %s\n", argv[i - 1]);
            return 2;
        }
        if (r)
            continue;

        if (!strcmp(argv[i], "--map") && i + 1 < argc)
            map = argv[++i];
        else if (!strcmp(argv[i], "--dump") && i + 1 < argc)
            dump = argv[++i];
        else if (!strcmp(argv[i], "--automap"))
            automap = 1;
        else if (!strcmp(argv[i], "--clip"))
            clip = 1;
        else if (!have_out && npos == 0 && strlen(argv[i]) > 4 &&
                 !strcmp(argv[i] + strlen(argv[i]) - 4, ".ppm")) {
            out = argv[i];
            have_out = 1;
        } else if (npos < 3)
            xyz[npos++] = atof(argv[i]);
        else {
            fprintf(stderr, "usage: paracast_demo [out.ppm] [x y yaw] [--map NAME] [--automap] "
                            "[--clip] [--size WxH] [--threads N] [--strip H] [--dump FILE]\n");
            return 2;
        }
    }
    if (npos != 0 && npos != 3) {
        fprintf(stderr, "give all of x y yaw, or none\n");
        return 2;
    }
    if (dump && !map) {
        fprintf(stderr, "--dump needs --map: only the sector renderer has a command buffer\n");
        return 2;
    }

    scene sc;
    if (scene_init(&sc, map, &opts))
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

    if (pc_ppm_write(out, sc.rgba, sc.w, sc.h)) {
        fprintf(stderr, "cannot write %s\n", out);
        return 1;
    }
    printf("wrote %s  hash=%016llx\n", out,
           (unsigned long long)pc_hash64(sc.fb.pix, (size_t)sc.w * sc.h, PC_HASH_SEED));
    if (sc.sector_mode)
        printf("  visits=%u portal_cols=%u wall_px=%u plane_px=%u sprite_px=%u commands=%u\n",
               st.sector_visits, st.portal_columns, st.wall_px, st.plane_px, st.sprite_px,
               st.commands);
    else
        printf("  dda_steps=%u wall_px=%u plane_px=%u\n", st.dda_steps, st.wall_px, st.plane_px);

    if (dump) {
        pc_frame_params fp;
        pc_sector_frame_params(&sc.sr, &fp);
        if (pc_cmddump_write(dump, &fp, &sc.sr.cmds, pc_assets_hash(&sc.assets))) {
            fprintf(stderr, "cannot write %s\n", dump);
            return 1;
        }
        printf("dumped %u commands to %s\n", sc.sr.cmds.count, dump);
    }
    scene_free(&sc);
    return 0;
}
