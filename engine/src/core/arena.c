#include "core/arena.h"

void pc_arena_init(pc_arena *a, void *mem, size_t cap)
{
    a->base = (uint8_t *)mem;
    a->cap = cap;
    a->used = 0;
    a->allocs = 0;
}

void *pc_arena_alloc(pc_arena *a, size_t size, size_t align)
{
    uintptr_t cur = (uintptr_t)a->base + a->used;
    uintptr_t aligned = (cur + (align - 1)) & ~(uintptr_t)(align - 1);
    size_t pad = (size_t)(aligned - cur);

    if (pad > a->cap - a->used || size > a->cap - a->used - pad)
        return NULL;

    a->used += pad + size;
    a->allocs++;
    return (void *)aligned;
}

void *pc_arena_alloc_array(pc_arena *a, size_t count, size_t size, size_t align)
{
    if (size != 0 && count > (size_t)-1 / size)
        return NULL;
    return pc_arena_alloc(a, count * size, align);
}

size_t pc_arena_save(const pc_arena *a) { return a->used; }

void pc_arena_restore(pc_arena *a, size_t mark) { a->used = mark; }

void pc_arena_reset(pc_arena *a) { a->used = 0; }
