# Feature log

One entry per feature: what was added, what it cost or gained, and how it was measured.

**Reference machine:** Intel i7-14700HX, WSL2 Ubuntu 24.04, gcc 13.3, Release (`-O3 -DNDEBUG`),
single thread. Benchmark: `paracast_bench 600`, 640x400, camera orbit.

## v0 baseline: DDA raycaster, 16.16 fixed point

Textured walls (one DDA ray per column), then floor and ceiling in the pixels the walls left
open. Row-major 8-bit framebuffer, per-ray `int64` divides, no SIMD, no threads.

| Metric | Value |
| --- | --- |
| Median frame | 0.50 ms |
| p95 / p99 | 0.54-0.66 ms / 0.63-0.83 ms (3 runs) |
| Per pixel | about 2.0 ns |
| DDA steps per frame | 8066 |
| Allocations in frame loop | 0 |
| Run hash (600 frames) | `48b464b95d006180`, identical across runs |

Notes: wall and plane pixels partition the screen exactly (tested), so overdraw is 1.0 by
construction. Optimization candidates to measure against this row: column-major framebuffer,
reciprocal LUT for `delta_dist`, a DDA specialised per ray octant.
