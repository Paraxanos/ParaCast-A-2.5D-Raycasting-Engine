#ifndef PC_PLATFORM_FILE_H
#define PC_PLATFORM_FILE_H

#include <stddef.h>
#include "core/arena.h"

/* Reads a whole file into the arena, NUL-terminated. Returns NULL if the file cannot
 * be read or the arena is too small; *len (optional) receives the size without the NUL. */
char *pc_file_read(const char *path, pc_arena *arena, size_t *len);

#endif
