#include "map/map.h"

int pc_map_contains(const pc_map *m, pc_id s, pc_vec2 p)
{
    pc_id first = m->first_wall[s];
    pc_id end = first + m->wall_count[s];

    for (pc_id w = first; w < end; w++) {
        int64_t cross = (int64_t)m->e[w].x * (p.y - m->a[w].y) -
                        (int64_t)m->e[w].y * (p.x - m->a[w].x);
        if (cross < 0)
            return 0;
    }
    return 1;
}

pc_id pc_map_find_sector(const pc_map *m, pc_vec2 p, pc_id hint)
{
    if (hint < m->nsectors) {
        if (pc_map_contains(m, hint, p))
            return hint;

        pc_id end = m->first_wall[hint] + m->wall_count[hint];
        for (pc_id w = m->first_wall[hint]; w < end; w++) {
            pc_id nb = m->back[w];
            if (nb != PC_NONE && pc_map_contains(m, nb, p))
                return nb;
        }
    }
    for (pc_id s = 0; s < m->nsectors; s++)
        if (pc_map_contains(m, s, p))
            return s;
    return PC_NONE;
}
