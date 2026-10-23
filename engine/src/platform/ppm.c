#include "platform/ppm.h"

#include <stdio.h>

int pc_ppm_write(const char *path, const uint32_t *rgba, int w, int h)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;

    const uint8_t *src = (const uint8_t *)rgba;
    int ok = fprintf(f, "P6\n%d %d\n255\n", w, h) > 0;

    for (long i = 0; ok && i < (long)w * h; i++)
        ok = fwrite(src + 4 * i, 1, 3, f) == 3;

    if (fclose(f) != 0)
        ok = 0;
    return ok ? 0 : -1;
}
