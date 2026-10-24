#ifndef PC_CORE_TYPES_H
#define PC_CORE_TYPES_H

#include <stdint.h>

/* Index into a world table. All world data is addressed this way, never by pointer. */
typedef uint32_t pc_id;
#define PC_NONE 0xFFFFFFFFu

#endif
