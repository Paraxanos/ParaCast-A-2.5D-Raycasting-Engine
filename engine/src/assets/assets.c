#include "assets/assets.h"

static const pc_rgb k_hues[16] = {
    {255, 255, 255}, {230,  40,  40}, {240, 140,  30}, {240, 225,  50},
    {140, 220,  50}, { 40, 180,  60}, { 40, 170, 150}, { 60, 210, 230},
    { 50, 130, 230}, { 50,  70, 220}, {130,  70, 210}, {210,  60, 170},
    {150,  95,  50}, {170,  60,  45}, {110, 120, 140}, { 40, 100,  50},
};

enum { HUE_WHITE = 0, HUE_BLUE = 9, HUE_BROWN = 12, HUE_BRICK = 13, HUE_SLATE = 14 };

static void build_palette(pc_assets *as)
{
    for (int h = 0; h < 16; h++)
        for (int s = 0; s < 16; s++) {
            pc_rgb *c = &as->palette[(h << 4) | s];
            c->r = (uint8_t)(k_hues[h].r * s / 15);
            c->g = (uint8_t)(k_hues[h].g * s / 15);
            c->b = (uint8_t)(k_hues[h].b * s / 15);
        }
}

static void build_colormap(pc_assets *as)
{
    for (int l = 0; l < PC_LIGHT_LEVELS; l++)
        for (int i = 0; i < PC_PAL_SIZE; i++) {
            int shade = (i & 0x0F) * (PC_LIGHT_LEVELS - 1 - l) / (PC_LIGHT_LEVELS - 1);
            as->colormap[l][i] = (uint8_t)((i & 0xF0) | shade);
        }
}

/* Small integer hash so texture noise is identical on every platform. */
static unsigned noise(unsigned x, unsigned y, unsigned seed)
{
    unsigned h = x * 374761393u + y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static void gen_brick(uint8_t *t)
{
    for (int y = 0; y < PC_TEX_SIZE; y++)
        for (int x = 0; x < PC_TEX_SIZE; x++) {
            int row = y / 16;
            int bx = (x + (row & 1) * 16) % 32;
            if ((y % 16) == 0 || bx == 0)
                t[y * PC_TEX_SIZE + x] = PC_IDX(HUE_WHITE, 8);
            else
                t[y * PC_TEX_SIZE + x] = PC_IDX(HUE_BRICK, 9 + noise(x, y, 1) % 5);
        }
}

static void gen_tile(uint8_t *t)
{
    for (int y = 0; y < PC_TEX_SIZE; y++)
        for (int x = 0; x < PC_TEX_SIZE; x++) {
            int dark = ((x / 16) + (y / 16)) & 1;
            t[y * PC_TEX_SIZE + x] = dark ? PC_IDX(HUE_BLUE, 11 + noise(x, y, 2) % 2)
                                          : PC_IDX(HUE_WHITE, 13 + noise(x, y, 2) % 2);
        }
}

static void gen_stone(uint8_t *t)
{
    for (int y = 0; y < PC_TEX_SIZE; y++)
        for (int x = 0; x < PC_TEX_SIZE; x++) {
            int seam = (x % 32) == 0 || (y % 32) == 0;
            t[y * PC_TEX_SIZE + x] = PC_IDX(HUE_SLATE, seam ? 3 : 6 + noise(x, y, 3) % 7);
        }
}

static void gen_wood(uint8_t *t)
{
    for (int y = 0; y < PC_TEX_SIZE; y++)
        for (int x = 0; x < PC_TEX_SIZE; x++) {
            if ((x % 16) == 0)
                t[y * PC_TEX_SIZE + x] = PC_IDX(HUE_BROWN, 4);
            else
                t[y * PC_TEX_SIZE + x] =
                    PC_IDX(HUE_BROWN, 7 + ((x / 4) % 3) * 2 + noise(x / 2, y, 4) % 2);
        }
}

static void gen_floor(uint8_t *t)
{
    for (int y = 0; y < PC_TEX_SIZE; y++)
        for (int x = 0; x < PC_TEX_SIZE; x++) {
            int alt = ((x / 32) + (y / 32)) & 1;
            t[y * PC_TEX_SIZE + x] = PC_IDX(HUE_SLATE, (alt ? 7 : 10) + noise(x, y, 5) % 2);
        }
}

static void gen_ceil(uint8_t *t)
{
    for (int y = 0; y < PC_TEX_SIZE; y++)
        for (int x = 0; x < PC_TEX_SIZE; x++) {
            int grid = (x % 32) == 0 || (y % 32) == 0;
            t[y * PC_TEX_SIZE + x] = PC_IDX(HUE_SLATE, grid ? 2 : 4 + noise(x, y, 6) % 2);
        }
}

int pc_assets_init(pc_assets *as, pc_arena *arena)
{
    static void (*const gen[PC_TEX_COUNT])(uint8_t *) = {
        gen_brick, gen_tile, gen_stone, gen_wood, gen_floor, gen_ceil,
    };

    build_palette(as);
    build_colormap(as);

    for (int i = 0; i < PC_TEX_COUNT; i++) {
        uint8_t *t = (uint8_t *)pc_arena_alloc(arena, PC_TEX_SIZE * PC_TEX_SIZE, 16);
        if (!t)
            return -1;
        gen[i](t);
        as->tex[i] = t;
    }
    return 0;
}
