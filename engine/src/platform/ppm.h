#ifndef PC_PLATFORM_PPM_H
#define PC_PLATFORM_PPM_H

#include <stdint.h>

/* Writes an RGBA8888 buffer as a binary P6 PPM (alpha dropped). Returns 0 on success. */
int pc_ppm_write(const char *path, const uint32_t *rgba, int w, int h);

#endif
