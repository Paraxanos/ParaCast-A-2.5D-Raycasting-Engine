#ifndef PC_CORE_HASH_H
#define PC_CORE_HASH_H

#include <stddef.h>
#include <stdint.h>

#define PC_HASH_SEED 14695981039346656037ULL

/* FNV-1a, 64-bit. Chain calls by passing the previous result as seed. */
uint64_t pc_hash64(const void *data, size_t len, uint64_t seed);

#endif
