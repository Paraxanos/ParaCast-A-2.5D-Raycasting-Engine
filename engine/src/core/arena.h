#ifndef PC_CORE_ARENA_H
#define PC_CORE_ARENA_H

#include <stddef.h>
#include <stdint.h>

/* Bump allocator over caller-supplied memory; the core never calls malloc. */
typedef struct {
    uint8_t *base;
    size_t   cap;
    size_t   used;
    uint32_t allocs;    /* lifetime allocation count, for the "zero allocs per frame" check */
} pc_arena;

void  pc_arena_init(pc_arena *a, void *mem, size_t cap);

/* align must be a power of two. Returns NULL when the arena is exhausted. */
void *pc_arena_alloc(pc_arena *a, size_t size, size_t align);

/* Returns NULL on count * size overflow as well as on exhaustion. */
void *pc_arena_alloc_array(pc_arena *a, size_t count, size_t size, size_t align);

size_t pc_arena_save(const pc_arena *a);
void   pc_arena_restore(pc_arena *a, size_t mark);
void   pc_arena_reset(pc_arena *a);

#endif
