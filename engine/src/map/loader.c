/* Map text loader. Two passes over the text: the first validates syntax and counts
 * sectors, walls and things so the tables can be allocated exactly; the second fills
 * them. Geometry rules (convex sectors, matching portals) are checked at the end.
 * No libc I/O, no float parsing: numbers are decimal text turned into 16.16 integers. */

#include "map/map.h"

#include "assets/assets.h"

#define MAX_TOKENS 24

typedef struct { const char *s; size_t n; } tok;

typedef struct {
    const char *text;
    size_t      len, pos;
    int         line;
    char       *err;
    size_t      err_cap;
} lexer;

/* Per-sector scratch while its "v" lines are read. */
typedef struct {
    int     open;
    pc_id   id;
    fx_t    floor_h, ceil_h;
    int     ftex, ctex, light;
    int     nv;
    pc_vec2 v[PC_MAX_SECTOR_WALLS];
    pc_id   portal[PC_MAX_SECTOR_WALLS];
    int     mid[PC_MAX_SECTOR_WALLS], up[PC_MAX_SECTOR_WALLS], low[PC_MAX_SECTOR_WALLS];
} sector_scratch;

/* ---- errors ---------------------------------------------------------------- */

static void put_str(char *dst, size_t cap, size_t *at, const char *s)
{
    while (*s && *at + 1 < cap)
        dst[(*at)++] = *s++;
}

static int fail(lexer *lx, int line, const char *msg)
{
    if (lx->err && lx->err_cap) {
        size_t at = 0;
        if (line > 0) {
            char digits[12];
            int nd = 0;
            for (unsigned n = (unsigned)line; ; n /= 10) {
                digits[nd++] = (char)('0' + n % 10);
                if (n < 10)
                    break;
            }
            put_str(lx->err, lx->err_cap, &at, "line ");
            while (nd > 0 && at + 1 < lx->err_cap)
                lx->err[at++] = digits[--nd];
            put_str(lx->err, lx->err_cap, &at, ": ");
        }
        put_str(lx->err, lx->err_cap, &at, msg);
        lx->err[at] = '\0';
    }
    return -1;
}

/* ---- lexing ---------------------------------------------------------------- */

/* Returns 1 with the next non-blank line tokenized, 0 at end of text, -1 on overflow. */
static int next_line(lexer *lx, tok *t, int *nt)
{
    while (lx->pos < lx->len) {
        size_t start = lx->pos, end = start;
        while (end < lx->len && lx->text[end] != '\n')
            end++;
        lx->pos = end < lx->len ? end + 1 : end;
        lx->line++;

        int n = 0;
        size_t i = start;
        while (i < end && lx->text[i] != '#') {
            char c = lx->text[i];
            if (c == ' ' || c == '\t' || c == '\r') {
                i++;
                continue;
            }
            size_t j = i;
            while (j < end && lx->text[j] != ' ' && lx->text[j] != '\t' &&
                   lx->text[j] != '\r' && lx->text[j] != '#')
                j++;
            if (n == MAX_TOKENS)
                return -1;
            t[n].s = lx->text + i;
            t[n].n = j - i;
            n++;
            i = j;
        }
        if (n > 0) {
            *nt = n;
            return 1;
        }
    }
    return 0;
}

static int tok_eq(tok t, const char *lit)
{
    size_t i = 0;
    for (; i < t.n; i++)
        if (lit[i] == '\0' || lit[i] != t.s[i])
            return 0;
    return lit[i] == '\0';
}

static int parse_int(tok t, int *out)
{
    size_t i = 0;
    int neg = 0;
    if (i < t.n && t.s[i] == '-') {
        neg = 1;
        i++;
    }
    if (i == t.n || t.n - i > 9)
        return -1;
    int v = 0;
    for (; i < t.n; i++) {
        if (t.s[i] < '0' || t.s[i] > '9')
            return -1;
        v = v * 10 + (t.s[i] - '0');
    }
    *out = neg ? -v : v;
    return 0;
}

/* Decimal such as "-12.375" to 16.16, rounded to nearest. At most 6 integer digits. */
static int parse_fx(tok t, fx_t *out)
{
    size_t i = 0;
    int neg = 0;
    if (i < t.n && (t.s[i] == '-' || t.s[i] == '+')) {
        neg = t.s[i] == '-';
        i++;
    }
    int64_t ip = 0;
    size_t nd = 0;
    for (; i < t.n && t.s[i] != '.'; i++, nd++) {
        if (t.s[i] < '0' || t.s[i] > '9' || nd >= 6)
            return -1;
        ip = ip * 10 + (t.s[i] - '0');
    }
    int64_t frac = 0, scale = 1;
    if (i < t.n) {
        i++;
        size_t used = 0;
        for (; i < t.n; i++) {
            if (t.s[i] < '0' || t.s[i] > '9')
                return -1;
            if (used < 6) {
                frac = frac * 10 + (t.s[i] - '0');
                scale *= 10;
                used++;
            }
            nd++;
        }
    }
    if (nd == 0)
        return -1;
    int64_t raw = ip * FX_ONE + (frac * FX_ONE * 2 + scale) / (scale * 2);
    *out = (fx_t)(neg ? -raw : raw);
    return 0;
}

static int in_limit(fx_t v, int limit_units)
{
    return v >= -fx_from_int(limit_units) && v <= fx_from_int(limit_units);
}

/* ---- geometry checks ------------------------------------------------------- */

static int64_t cross64(pc_vec2 e, pc_vec2 d)
{
    return (int64_t)e.x * d.y - (int64_t)e.y * d.x;
}

static pc_vec2 sub(pc_vec2 p, pc_vec2 q)
{
    pc_vec2 r = { p.x - q.x, p.y - q.y };
    return r;
}

/* Checks the sector is a convex polygon and reverses it if needed so the interior is
 * on the left of every edge. Returns an error message or NULL. */
static const char *normalize_sector(sector_scratch *s)
{
    int k = s->nv;
    if (k < 3)
        return "sector needs at least 3 vertices";

    for (int i = 0; i < k; i++) {
        pc_vec2 e = sub(s->v[(i + 1) % k], s->v[i]);
        uint64_t sq = (uint64_t)((int64_t)e.x * e.x + (int64_t)e.y * e.y);
        if (pc_isqrt64(sq) < (uint32_t)PC_MIN_EDGE)
            return "edge shorter than 1/16 unit";
    }

    int sign = 0;
    for (int i = 0; i < k; i++) {
        pc_vec2 e = sub(s->v[(i + 1) % k], s->v[i]);
        for (int j = 0; j < k; j++) {
            if (j == i || j == (i + 1) % k)
                continue;
            int64_t c = cross64(e, sub(s->v[j], s->v[i]));
            if (c == 0)
                continue;
            int sg = c > 0 ? 1 : -1;
            if (sign == 0)
                sign = sg;
            else if (sg != sign)
                return "sector is not convex";
        }
    }
    if (sign == 0)
        return "sector has zero area";

    if (sign < 0) {
        pc_vec2 v2[PC_MAX_SECTOR_WALLS];
        pc_id p2[PC_MAX_SECTOR_WALLS];
        int m2[PC_MAX_SECTOR_WALLS], u2[PC_MAX_SECTOR_WALLS], l2[PC_MAX_SECTOR_WALLS];
        for (int i = 0; i < k; i++) {
            /* New edge i runs v[k-1-i] -> v[k-2-i], the old edge k-2-i reversed. */
            int old_edge = ((k - 2 - i) % k + k) % k;
            v2[i] = s->v[k - 1 - i];
            p2[i] = s->portal[old_edge];
            m2[i] = s->mid[old_edge];
            u2[i] = s->up[old_edge];
            l2[i] = s->low[old_edge];
        }
        for (int i = 0; i < k; i++) {
            s->v[i] = v2[i];
            s->portal[i] = p2[i];
            s->mid[i] = m2[i];
            s->up[i] = u2[i];
            s->low[i] = l2[i];
        }
    }
    return NULL;
}

/* ---- parsing --------------------------------------------------------------- */

typedef struct {
    uint32_t nsectors, nwalls, nthings;
    int      have_player;
    pc_vec2  player;
    fx_t     player_yaw_deg;     /* 16.16 degrees */
} counts;

static int tex_value(lexer *lx, int line, tok t, int *out)
{
    if (parse_int(t, out) || *out < 0 || *out >= PC_TEX_COUNT)
        return fail(lx, line, "texture id out of range");
    return 0;
}

static int parse_pass(lexer *lx, int pass, pc_map *m, counts *c)
{
    tok t[MAX_TOKENS];
    int nt, r;
    sector_scratch cur;
    uint32_t sec_i = 0, wall_i = 0, thing_i = 0;

    cur.open = 0;
    lx->pos = 0;
    lx->line = 0;
    if (pass == 0)
        c->nsectors = c->nwalls = c->nthings = 0;

    while ((r = next_line(lx, t, &nt)) > 0) {
        int line = lx->line;

        if (tok_eq(t[0], "sector")) {
            if (cur.open)
                return fail(lx, line, "sector inside sector (missing 'end')");
            int id;
            if (nt < 2 || parse_int(t[1], &id) || id < 0 || (uint32_t)id != sec_i)
                return fail(lx, line, "sector ids must be 0, 1, 2, ... in order");
            if (sec_i >= PC_MAX_SECTORS)
                return fail(lx, line, "too many sectors");

            cur.open = 1;
            cur.id = (pc_id)id;
            cur.floor_h = 0;
            cur.ceil_h = fx_from_int(3);
            cur.ftex = PC_TEX_FLOOR;
            cur.ctex = PC_TEX_CEIL;
            cur.light = 24;
            cur.nv = 0;
            int have_floor = 0, have_ceil = 0;

            if ((nt - 2) % 2)
                return fail(lx, line, "sector attributes come in key value pairs");
            for (int i = 2; i < nt; i += 2) {
                if (tok_eq(t[i], "floor")) {
                    if (parse_fx(t[i + 1], &cur.floor_h) || !in_limit(cur.floor_h, PC_COORD_LIMIT / 2))
                        return fail(lx, line, "bad floor height");
                    have_floor = 1;
                } else if (tok_eq(t[i], "ceil")) {
                    if (parse_fx(t[i + 1], &cur.ceil_h) || !in_limit(cur.ceil_h, PC_COORD_LIMIT / 2))
                        return fail(lx, line, "bad ceil height");
                    have_ceil = 1;
                } else if (tok_eq(t[i], "ftex")) {
                    if (tex_value(lx, line, t[i + 1], &cur.ftex))
                        return -1;
                } else if (tok_eq(t[i], "ctex")) {
                    if (tex_value(lx, line, t[i + 1], &cur.ctex))
                        return -1;
                } else if (tok_eq(t[i], "light")) {
                    if (parse_int(t[i + 1], &cur.light) || cur.light < 0 || cur.light > 31)
                        return fail(lx, line, "light must be 0..31");
                } else {
                    return fail(lx, line, "unknown sector attribute");
                }
            }
            if (!have_floor || !have_ceil)
                return fail(lx, line, "sector needs floor and ceil");
            if (cur.ceil_h - cur.floor_h < FX_HALF)
                return fail(lx, line, "ceiling must be at least 0.5 above the floor");

        } else if (tok_eq(t[0], "v")) {
            if (!cur.open)
                return fail(lx, line, "'v' outside a sector");
            if (cur.nv >= PC_MAX_SECTOR_WALLS)
                return fail(lx, line, "too many vertices in sector");
            if (nt < 3 || (nt - 3) % 2)
                return fail(lx, line, "expected: v x y [portal N] [mid N] [up N] [low N]");

            int k = cur.nv;
            if (parse_fx(t[1], &cur.v[k].x) || parse_fx(t[2], &cur.v[k].y) ||
                !in_limit(cur.v[k].x, PC_COORD_LIMIT) || !in_limit(cur.v[k].y, PC_COORD_LIMIT))
                return fail(lx, line, "bad vertex coordinate (limit is +-512)");
            cur.portal[k] = PC_NONE;
            cur.mid[k] = 0;
            cur.up[k] = cur.low[k] = -1;

            for (int i = 3; i < nt; i += 2) {
                int val;
                if (tok_eq(t[i], "portal")) {
                    if (parse_int(t[i + 1], &val) || val < 0 || (uint32_t)val >= PC_MAX_SECTORS)
                        return fail(lx, line, "bad portal sector id");
                    cur.portal[k] = (pc_id)val;
                } else if (tok_eq(t[i], "mid")) {
                    if (tex_value(lx, line, t[i + 1], &cur.mid[k]))
                        return -1;
                } else if (tok_eq(t[i], "up")) {
                    if (tex_value(lx, line, t[i + 1], &cur.up[k]))
                        return -1;
                } else if (tok_eq(t[i], "low")) {
                    if (tex_value(lx, line, t[i + 1], &cur.low[k]))
                        return -1;
                } else {
                    return fail(lx, line, "unknown vertex attribute");
                }
            }
            cur.nv++;

        } else if (tok_eq(t[0], "end")) {
            if (!cur.open)
                return fail(lx, line, "'end' without a sector");
            const char *msg = normalize_sector(&cur);
            if (msg)
                return fail(lx, line, msg);

            if (pass == 1) {
                m->floor_h[sec_i] = cur.floor_h;
                m->ceil_h[sec_i] = cur.ceil_h;
                m->floor_tex[sec_i] = (uint8_t)cur.ftex;
                m->ceil_tex[sec_i] = (uint8_t)cur.ctex;
                m->light[sec_i] = (uint8_t)cur.light;
                m->first_wall[sec_i] = wall_i;
                m->wall_count[sec_i] = (uint16_t)cur.nv;

                for (int i = 0; i < cur.nv; i++, wall_i++) {
                    pc_vec2 a = cur.v[i], b = cur.v[(i + 1) % cur.nv];
                    pc_vec2 e = sub(b, a);
                    if (cur.portal[i] != PC_NONE && (cur.portal[i] >= c->nsectors || cur.portal[i] == cur.id))
                        return fail(lx, line, "portal to an unknown sector or to itself");
                    m->a[wall_i] = a;
                    m->b[wall_i] = b;
                    m->e[wall_i] = e;
                    m->len[wall_i] = (fx_t)pc_isqrt64((uint64_t)((int64_t)e.x * e.x + (int64_t)e.y * e.y));
                    m->back[wall_i] = cur.portal[i];
                    m->tex_mid[wall_i] = (uint8_t)cur.mid[i];
                    m->tex_up[wall_i] = (uint8_t)(cur.up[i] >= 0 ? cur.up[i] : cur.mid[i]);
                    m->tex_low[wall_i] = (uint8_t)(cur.low[i] >= 0 ? cur.low[i] : cur.mid[i]);
                }
            } else {
                if (c->nwalls + (uint32_t)cur.nv > PC_MAX_WALLS)
                    return fail(lx, line, "too many walls");
                c->nwalls += (uint32_t)cur.nv;
            }
            sec_i++;
            if (pass == 0)
                c->nsectors = sec_i;
            cur.open = 0;

        } else if (tok_eq(t[0], "thing")) {
            if (cur.open)
                return fail(lx, line, "'thing' inside a sector");
            int sprite;
            pc_vec2 p;
            if (nt != 4 || parse_fx(t[1], &p.x) || parse_fx(t[2], &p.y) ||
                !in_limit(p.x, PC_COORD_LIMIT) || !in_limit(p.y, PC_COORD_LIMIT) ||
                parse_int(t[3], &sprite) || sprite < 0 || sprite >= PC_SPRITE_COUNT)
                return fail(lx, line, "expected: thing x y sprite");
            if (pass == 1) {
                m->thing_pos[thing_i] = p;
                m->thing_sprite[thing_i] = (uint8_t)sprite;
            } else if (c->nthings >= PC_MAX_THINGS) {
                return fail(lx, line, "too many things");
            } else {
                c->nthings++;
            }
            thing_i++;

        } else if (tok_eq(t[0], "player")) {
            if (cur.open)
                return fail(lx, line, "'player' inside a sector");
            if (nt != 4 || parse_fx(t[1], &c->player.x) || parse_fx(t[2], &c->player.y) ||
                parse_fx(t[3], &c->player_yaw_deg) ||
                !in_limit(c->player.x, PC_COORD_LIMIT) || !in_limit(c->player.y, PC_COORD_LIMIT))
                return fail(lx, line, "expected: player x y yaw_degrees");
            if (pass == 0 && c->have_player)
                return fail(lx, line, "more than one 'player' line");
            c->have_player = 1;

        } else {
            return fail(lx, line, "unknown directive");
        }
    }
    if (r < 0)
        return fail(lx, lx->line + 1, "too many tokens on one line");
    if (cur.open)
        return fail(lx, lx->line, "unterminated sector (missing 'end')");
    return 0;
}

static int alloc_tables(pc_map *m, pc_arena *ar, const counts *c)
{
    size_t ns = c->nsectors, nw = c->nwalls, nt = c->nthings ? c->nthings : 1;
#define ARR(field, n, T) (m->field = (T *)pc_arena_alloc_array(ar, (n), sizeof(T), 8))
    return ARR(floor_h, ns, fx_t) && ARR(ceil_h, ns, fx_t) && ARR(floor_tex, ns, uint8_t) &&
           ARR(ceil_tex, ns, uint8_t) && ARR(light, ns, uint8_t) && ARR(first_wall, ns, pc_id) &&
           ARR(wall_count, ns, uint16_t) && ARR(a, nw, pc_vec2) && ARR(b, nw, pc_vec2) &&
           ARR(e, nw, pc_vec2) && ARR(len, nw, fx_t) && ARR(back, nw, pc_id) &&
           ARR(tex_mid, nw, uint8_t) && ARR(tex_up, nw, uint8_t) && ARR(tex_low, nw, uint8_t) &&
           ARR(thing_pos, nt, pc_vec2) && ARR(thing_sector, nt, pc_id) &&
           ARR(thing_sprite, nt, uint8_t);
#undef ARR
}

static int check_portals(lexer *lx, const pc_map *m)
{
    for (pc_id s = 0; s < m->nsectors; s++) {
        for (pc_id w = m->first_wall[s]; w < m->first_wall[s] + m->wall_count[s]; w++) {
            pc_id nb = m->back[w];
            if (nb == PC_NONE)
                continue;

            int matched = 0;
            for (pc_id w2 = m->first_wall[nb]; w2 < m->first_wall[nb] + m->wall_count[nb]; w2++)
                if (m->back[w2] == s && m->a[w2].x == m->b[w].x && m->a[w2].y == m->b[w].y &&
                    m->b[w2].x == m->a[w].x && m->b[w2].y == m->a[w].y)
                    matched = 1;
            if (!matched)
                return fail(lx, 0, "portal has no matching reverse wall in the sector behind it");
        }
    }
    return 0;
}

int pc_map_load(const char *text, size_t len, pc_arena *arena, pc_map *map,
                char *err, size_t err_cap)
{
    lexer lx;
    counts c;

    lx.text = text;
    lx.len = len;
    lx.err = err;
    lx.err_cap = err_cap;
    if (err && err_cap)
        err[0] = '\0';
    c.have_player = 0;
    c.nsectors = c.nwalls = c.nthings = 0;

    if (parse_pass(&lx, 0, NULL, &c))
        return -1;
    if (c.nsectors == 0)
        return fail(&lx, 0, "map has no sectors");
    if (!c.have_player)
        return fail(&lx, 0, "map has no 'player' line");

    if (!alloc_tables(map, arena, &c))
        return fail(&lx, 0, "out of arena memory");
    map->nsectors = c.nsectors;
    map->nwalls = c.nwalls;
    map->nthings = c.nthings;

    if (parse_pass(&lx, 1, map, &c))
        return -1;
    if (check_portals(&lx, map))
        return -1;

    for (uint32_t i = 0; i < map->nthings; i++) {
        pc_id s = pc_map_find_sector(map, map->thing_pos[i], PC_NONE);
        if (s == PC_NONE)
            return fail(&lx, 0, "a thing is outside every sector");
        map->thing_sector[i] = s;
    }

    map->spawn = c.player;
    /* degrees (16.16) to 65536-per-turn angle units, rounded, wrapped into one turn */
    map->spawn_yaw = (pc_angle)(((int64_t)c.player_yaw_deg * 2 + 360) / 720);
    map->spawn_sector = pc_map_find_sector(map, c.player, PC_NONE);
    if (map->spawn_sector == PC_NONE)
        return fail(&lx, 0, "the player starts outside every sector");
    return 0;
}
