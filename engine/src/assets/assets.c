#include "assets/assets.h"

#include <stdlib.h>

#include "core/hash.h"

static const pc_rgb k_hues[16] = {
    {255, 255, 255}, {230,  40,  40}, {240, 140,  30}, {240, 225,  50},
    {140, 220,  50}, { 40, 180,  60}, { 40, 170, 150}, { 60, 210, 230},
    { 50, 130, 230}, { 50,  70, 220}, {130,  70, 210}, {210,  60, 170},
    {150,  95,  50}, {170,  60,  45}, {110, 120, 140}, { 40, 100,  50},
};

enum {
    HUE_WHITE = 0, HUE_RED = 1, HUE_YELLOW = 3, HUE_BLUE = 9,
    HUE_BROWN = 12, HUE_BRICK = 13, HUE_SLATE = 14
};

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

static void gen_metal(uint8_t *t)
{
    for (int y = 0; y < PC_TEX_SIZE; y++)
        for (int x = 0; x < PC_TEX_SIZE; x++) {
            int edge = (x % 32) == 0 || (y % 32) == 0;
            int rivet = ((x % 32) == 3 || (x % 32) == 28) && ((y % 32) == 3 || (y % 32) == 28);
            t[y * PC_TEX_SIZE + x] =
                PC_IDX(HUE_SLATE, edge ? 4 : rivet ? 12 : 7 + noise(x, y, 7) % 3);
        }
}

static void gen_carpet(uint8_t *t)
{
    for (int y = 0; y < PC_TEX_SIZE; y++)
        for (int x = 0; x < PC_TEX_SIZE; x++) {
            int diamond = ((x + y) / 16 + (x - y + 64) / 16) & 1;
            t[y * PC_TEX_SIZE + x] = PC_IDX(HUE_RED, (diamond ? 5 : 8) + noise(x, y, 8) % 2);
        }
}

/* A yellow arrow pointing toward increasing u: if it ever reads backwards on screen,
 * wall texturing is mirrored. */
static void gen_sign(uint8_t *t)
{
    for (int y = 0; y < PC_TEX_SIZE; y++)
        for (int x = 0; x < PC_TEX_SIZE; x++) {
            int border = x < 3 || y < 3 || x >= PC_TEX_SIZE - 3 || y >= PC_TEX_SIZE - 3;
            int shaft = x >= 8 && x < 40 && y >= 26 && y < 38;
            int head = x >= 40 && x < 58 && abs(y - 32) <= (57 - x) * 14 / 17;
            t[y * PC_TEX_SIZE + x] = border          ? PC_IDX(HUE_SLATE, 10)
                                     : shaft || head ? PC_IDX(HUE_YELLOW, 14)
                                                     : PC_IDX(HUE_SLATE, 3);
        }
}

/* Sprites mark transparent pixels with index 0, so every opaque shade starts at 1. */
static void gen_barrel(uint8_t *t)
{
    for (int y = 0; y < PC_TEX_SIZE; y++)
        for (int x = 0; x < PC_TEX_SIZE; x++) {
            int dx = abs(x - 32);
            int body = dx <= 22 && y >= 10;
            int cap = y < 10 && dx * dx * 36 + (y - 10) * (y - 10) * 484 <= 484 * 36;
            uint8_t px = 0;
            if (body || cap) {
                int shade = 4 + (22 - dx) * 9 / 22 + (int)(noise(x, y, 9) % 2);
                int band = (y % 24) < 3;
                px = band ? PC_IDX(HUE_SLATE, shade) : PC_IDX(HUE_BROWN, shade);
            }
            t[y * PC_TEX_SIZE + x] = px;
        }
}

static void gen_lamp(uint8_t *t)
{
    for (int y = 0; y < PC_TEX_SIZE; y++)
        for (int x = 0; x < PC_TEX_SIZE; x++) {
            int orb_sq = (x - 32) * (x - 32) + (y - 16) * (y - 16);
            uint8_t px = 0;
            if (orb_sq <= 14 * 14)
                px = PC_IDX(HUE_YELLOW, 15 - orb_sq * 7 / (14 * 14));
            else if (abs(x - 32) <= 1 && y >= 30)
                px = PC_IDX(HUE_SLATE, 9);
            else if (abs(x - 32) <= 7 && y >= 60)
                px = PC_IDX(HUE_SLATE, 7);
            t[y * PC_TEX_SIZE + x] = px;
        }
}

static void gen_pillar(uint8_t *t)
{
    for (int y = 0; y < PC_TEX_SIZE; y++)
        for (int x = 0; x < PC_TEX_SIZE; x++) {
            int dx = abs(x - 32);
            int capital = (y < 6 || y >= 58) && dx <= 30;
            int shaft = dx <= 22;
            uint8_t px = 0;
            if (capital)
                px = PC_IDX(HUE_WHITE, 11 + (int)(noise(x, y, 10) % 2));
            else if (shaft)
                px = PC_IDX(HUE_WHITE, 5 + (22 - dx) * 8 / 22 + (int)(noise(x, y, 10) % 2));
            t[y * PC_TEX_SIZE + x] = px;
        }
}

int pc_assets_init(pc_assets *as, pc_arena *arena)
{
    static void (*const gen[PC_TEX_COUNT])(uint8_t *) = {
        gen_brick, gen_tile, gen_stone, gen_wood, gen_floor,
        gen_ceil,  gen_metal, gen_carpet, gen_sign,
    };
    static void (*const sprite_gen[PC_SPRITE_COUNT])(uint8_t *) = {
        gen_barrel, gen_lamp, gen_pillar,
    };
    static const fx_t sprite_w[PC_SPRITE_COUNT] = { FX_C(0.6), FX_C(0.5), FX_C(0.7) };
    static const fx_t sprite_h[PC_SPRITE_COUNT] = { FX_C(0.8), FX_C(1.6), FX_C(2.2) };

    build_palette(as);
    build_colormap(as);

    for (int i = 0; i < PC_TEX_COUNT; i++) {
        uint8_t *t = (uint8_t *)pc_arena_alloc(arena, PC_TEX_SIZE * PC_TEX_SIZE, 16);
        if (!t)
            return -1;
        gen[i](t);
        as->tex[i] = t;
    }
    for (int i = 0; i < PC_SPRITE_COUNT; i++) {
        uint8_t *t = (uint8_t *)pc_arena_alloc(arena, PC_TEX_SIZE * PC_TEX_SIZE, 16);
        if (!t)
            return -1;
        sprite_gen[i](t);
        as->sprites[i].pix = t;
        as->sprites[i].width = sprite_w[i];
        as->sprites[i].height = sprite_h[i];
    }
    return 0;
}

uint64_t pc_assets_hash(const pc_assets *as)
{
    uint64_t h = pc_hash64(as->palette, sizeof as->palette, PC_HASH_SEED);
    h = pc_hash64(as->colormap, sizeof as->colormap, h);
    for (int i = 0; i < PC_TEX_COUNT; i++)
        h = pc_hash64(as->tex[i], PC_TEX_SIZE * PC_TEX_SIZE, h);
    for (int i = 0; i < PC_SPRITE_COUNT; i++) {
        h = pc_hash64(as->sprites[i].pix, PC_TEX_SIZE * PC_TEX_SIZE, h);
        h = pc_hash64(&as->sprites[i].width, sizeof(fx_t), h);
        h = pc_hash64(&as->sprites[i].height, sizeof(fx_t), h);
    }
    return h;
}
