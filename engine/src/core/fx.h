#ifndef PC_CORE_FX_H
#define PC_CORE_FX_H

#include <stdint.h>

/* 16.16 signed fixed point. real_t is the engine-wide scalar; a float build would swap it. */
typedef int32_t fx_t;
typedef fx_t real_t;

#define FX_SHIFT     16
#define FX_ONE       ((fx_t)1 << FX_SHIFT)
#define FX_HALF      (FX_ONE >> 1)
#define FX_FRAC_MASK (FX_ONE - 1)
#define FX_MAX       INT32_MAX
#define FX_MIN       INT32_MIN

/* Compile-time constant from a double literal, rounded half away from zero. */
#define FX_C(d) ((fx_t)((d) * 65536.0 + ((d) < 0 ? -0.5 : 0.5)))

typedef struct { fx_t x, y; } pc_vec2;

static inline fx_t fx_from_int(int32_t i) { return (fx_t)((int64_t)i * FX_ONE); }

/* Floor, not truncation: fx_to_int(-0.5) == -1. */
static inline int32_t fx_to_int(fx_t x) { return x >> FX_SHIFT; }

/* Fractional part in [0, FX_ONE), consistent with floor for negatives. */
static inline fx_t fx_frac(fx_t x) { return x & FX_FRAC_MASK; }

static inline fx_t fx_abs(fx_t x) { return x < 0 ? (x == FX_MIN ? FX_MAX : -x) : x; }

/* Truncates toward negative infinity; wraps if the result leaves the 16.16 range. */
static inline fx_t fx_mul(fx_t a, fx_t b)
{
    return (fx_t)(((int64_t)a * b) >> FX_SHIFT);
}

/* Truncates toward zero; saturates on overflow, and on b == 0 by the sign of a. */
static inline fx_t fx_div(fx_t a, fx_t b)
{
    if (b == 0)
        return a == 0 ? 0 : (a < 0 ? FX_MIN : FX_MAX);
    int64_t q = ((int64_t)a * FX_ONE) / b;
    if (q > FX_MAX) return FX_MAX;
    if (q < FX_MIN) return FX_MIN;
    return (fx_t)q;
}

/* Floor of the square root; exact for any 64-bit input. Applied to a raw 16.16 vector
 * length squared (scale 2^32) it yields the raw 16.16 length. */
static inline uint32_t pc_isqrt64(uint64_t n)
{
    uint64_t res = 0, bit = (uint64_t)1 << 62;
    while (bit > n)
        bit >>= 2;
    while (bit) {
        if (n >= res + bit) {
            n -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)res;
}

#endif
