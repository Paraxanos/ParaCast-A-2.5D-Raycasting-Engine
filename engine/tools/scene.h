#ifndef PC_TOOLS_SCENE_H
#define PC_TOOLS_SCENE_H

/* Shared setup for the demo, benchmark and viewer: one arena, the assets, a framebuffer
 * and either renderer, so each tool is only its own loop. */

#include "paracast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PC_MAPS_DIR
#define PC_MAPS_DIR "maps"
#endif

typedef struct {
    int           w, h;
    void         *mem;
    pc_arena      arena;
    pc_assets     assets;
    pc_fb         fb;
    uint32_t     *rgba;

    int           sector_mode;     /* 0 = grid raycaster (v0), 1 = sector renderer */
    pc_gridmap    grid;
    pc_raycaster  rc;
    pc_map        map;
    pc_sector_renderer sr;
} scene;

/* "nested" or "nested.map" resolve inside PC_MAPS_DIR; anything with a slash is a path. */
static inline const char *scene_map_path(const char *name, char *buf, size_t cap)
{
    if (strchr(name, '/') || strchr(name, '\\'))
        return name;
    int has_ext = strlen(name) > 4 && !strcmp(name + strlen(name) - 4, ".map");
    snprintf(buf, cap, "%s/%s%s", PC_MAPS_DIR, name, has_ext ? "" : ".map");
    return buf;
}

/* map_name == NULL selects the grid raycaster. Returns 0 on success. */
static inline int scene_init(scene *s, const char *map_name, int w, int h)
{
    memset(s, 0, sizeof *s);
    s->w = w;
    s->h = h;

    size_t mem_size = (size_t)16 << 20;
    s->mem = malloc(mem_size);
    if (!s->mem)
        return -1;
    pc_arena_init(&s->arena, s->mem, mem_size);
    pc_lut_init();

    if (pc_assets_init(&s->assets, &s->arena) || pc_fb_init(&s->fb, &s->arena, w, h)) {
        fprintf(stderr, "out of arena memory\n");
        return -1;
    }
    s->rgba = (uint32_t *)pc_arena_alloc_array(&s->arena, (size_t)w * h, 4, 64);
    if (!s->rgba)
        return -1;

    if (!map_name) {
        pc_gridmap_make_demo(&s->grid);
        return pc_raycaster_init(&s->rc, &s->arena, w, h);
    }

    char path_buf[512], err[160];
    const char *path = scene_map_path(map_name, path_buf, sizeof path_buf);
    size_t len;
    char *text = pc_file_read(path, &s->arena, &len);
    if (!text) {
        fprintf(stderr, "cannot read map %s\n", path);
        return -1;
    }
    if (pc_map_load(text, len, &s->arena, &s->map, err, sizeof err)) {
        fprintf(stderr, "%s: %s\n", path, err);
        return -1;
    }
    s->sector_mode = 1;
    return pc_sector_renderer_init(&s->sr, &s->arena, w, h);
}

static inline void scene_free(scene *s)
{
    free(s->mem);
    s->mem = NULL;
}

static inline pc_camera scene_spawn(const scene *s)
{
    pc_camera cam = { { FX_C(12.5), FX_C(12.5) }, PC_ANGLE_DEG(20.0), 0, 0 };
    if (s->sector_mode)
        pc_player_spawn(&cam, &s->map);
    return cam;
}

/* Puts the camera at (x, y) facing yaw; the sector and eye height follow. */
static inline int scene_place(const scene *s, pc_camera *cam, double x, double y, double yaw_deg)
{
    cam->pos.x = (fx_t)(x * 65536.0);
    cam->pos.y = (fx_t)(y * 65536.0);
    cam->yaw = PC_ANGLE_DEG(yaw_deg);
    if (!s->sector_mode)
        return 0;
    cam->sector = pc_map_find_sector(&s->map, cam->pos, PC_NONE);
    if (cam->sector == PC_NONE)
        return -1;
    cam->z = s->map.floor_h[cam->sector] + PC_EYE_HEIGHT;
    return 0;
}

static inline void scene_render(scene *s, const pc_camera *cam, pc_stats *st)
{
    if (s->sector_mode)
        pc_sector_render(&s->sr, &s->fb, &s->map, &s->assets, cam, st);
    else
        pc_raycaster_render(&s->rc, &s->fb, &s->grid, &s->assets, cam, st);
}

#endif
