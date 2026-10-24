#include "platform/file.h"

#include <stdio.h>

char *pc_file_read(const char *path, pc_arena *arena, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;

    /* Read in chunks straight into the arena's free space; no size probe needed. */
    size_t cap = arena->cap - arena->used;
    char *buf = (char *)pc_arena_alloc(arena, 0, 1);
    size_t n = buf ? fread(buf, 1, cap > 0 ? cap - 1 : 0, f) : 0;
    int too_big = buf && n == cap - 1 && fgetc(f) != EOF;
    fclose(f);

    if (!buf || too_big || !pc_arena_alloc(arena, n + 1, 1))
        return NULL;
    buf[n] = '\0';
    if (len)
        *len = n;
    return buf;
}
