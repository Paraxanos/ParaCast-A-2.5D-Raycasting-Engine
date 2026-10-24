#ifndef PC_ASSETS_ASSETS_H
#define PC_ASSETS_ASSETS_H

#include <stdint.h>
#include "core/arena.h"
#include "core/fx.h"

#define PC_PAL_SIZE     256
#define PC_LIGHT_LEVELS 32
#define PC_TEX_SIZE     64
#define PC_TEX_SHIFT    6

/* Textures 0..3 are the grid map's walls (cell value 1..4). The sector renderer's maps
 * address every texture by these ids, for walls, floors and ceilings alike. Ids are
 * stable: new textures are only ever appended. */
enum {
    PC_TEX_BRICK,
    PC_TEX_TILE,
    PC_TEX_STONE,
    PC_TEX_WOOD,
    PC_TEX_FLOOR,
    PC_TEX_CEIL,
    PC_TEX_METAL,
    PC_TEX_CARPET,
    PC_TEX_SIGN,    /* asymmetric arrow, so a mirrored wall is easy to spot */
    PC_TEX_COUNT
};
#define PC_WALL_TEX_COUNT 4

/* Billboard sprites: PC_TEX_SIZE^2 indices, palette index 0 is transparent. */
enum {
    PC_SPRITE_BARREL,
    PC_SPRITE_LAMP,
    PC_SPRITE_PILLAR,
    PC_SPRITE_COUNT
};

typedef struct {
    const uint8_t *pix;
    fx_t           width;    /* world units */
    fx_t           height;
} pc_sprite;

/* Palette index = (hue << 4) | shade. Shade 0 is black in every hue, so the light
 * colormap can darken by lowering the shade nibble alone. */
#define PC_IDX(hue, shade) ((uint8_t)(((hue) << 4) | (shade)))

typedef struct { uint8_t r, g, b; } pc_rgb;

typedef struct {
    pc_rgb         palette[PC_PAL_SIZE];
    uint8_t        colormap[PC_LIGHT_LEVELS][PC_PAL_SIZE];   /* [light][index] -> index */
    const uint8_t *tex[PC_TEX_COUNT];                        /* PC_TEX_SIZE^2, row-major */
    pc_sprite      sprites[PC_SPRITE_COUNT];
} pc_assets;

/* Everything is generated procedurally with integer math; returns 0 on success. */
int pc_assets_init(pc_assets *as, pc_arena *arena);

#endif
