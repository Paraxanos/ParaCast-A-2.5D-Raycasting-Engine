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

## Benchmark noise between sessions

The v0 baseline above was recorded at 0.50 ms median. A later session measured 0.265 ms and
a third 0.42 ms for the same binary behaviour and an identical run hash (`48b464b9...`), so
the work is the same and the timing is not: WSL2 and Windows scheduling and power state move
it by up to 2x between sessions. Within one session three runs agree to about 1%. Compare
only numbers taken back to back in the same session; the v0 grid is re-run alongside every
new measurement for that reason.

## SDL2 viewer and CI

`paracast_view` (WASD, arrows, Esc) with 35 Hz fixed-tick movement, and a GitHub Actions
workflow running release, ASan/UBSan and SDL-less builds plus a benchmark smoke step that
pins the v0 run hash. No performance effect: the viewer only presents the frames the
renderers already produce.

## Phase 2: sector and portal renderer

Convex sectors joined by portals, with per-sector floor and ceiling heights, upper and
lower wall pieces, per-column clip ranges, a floor/ceiling plane per visit, and billboard
sprites clipped against the portal windows. The map loader, an automap, a clip-window
overlay and sector collision came with it. The v0 grid raycaster is untouched and still
selectable (`paracast_view --grid`, `paracast_bench` without `--map`).

Measured back to back in one session: `paracast_bench 600 --map <name>`, 640x400,
single thread, three runs each (medians shown as the range across runs).

| Scene | Median frame | Per pixel | Sector visits / frame | Portal columns / frame |
| --- | --- | --- | --- | --- |
| v0 grid (same session) | 0.416-0.419 ms | 1.63 ns | n/a (8066 DDA steps) | n/a |
| `steps` | 0.379-0.383 ms | 1.49 ns | 2.0 | 294 |
| `window` | 0.373-0.389 ms | 1.46 ns | 1.9 | 83 |
| `nested` | 0.453-0.455 ms | 1.78 ns | 3.4 | 420 |
| `maze` | 0.378-0.384 ms | 1.49 ns | 1.5 | 67 |

Every run makes 0 allocations in the frame loop, and each map's run hash is identical
across its three runs (`steps` `6f1798c2...`, `window` `1af7b62d...`, `nested`
`cdcdcb00...`, `maze` `981f5318...`).

Read these with care. The benchmark camera circles the spawn point inside one room and
spins, so it sees only 1.5-3.4 sectors per frame: a light portal workload, not a stress
test. The comparison with the grid renderer says the new path is not a regression on
these scenes, not that it is faster in general (the scenes differ). A heavier traversal
benchmark (a long corridor of sectors seen end to end) is still to be written.

How it is tested: every sector of every shipped map is rendered from its centre and from
points toward each corner, in nine directions each, plus cameras a hair from walls and
corners; every pixel must be written exactly once (walls + planes = screen, nothing
unwritten). Depths along known rays are checked exactly, including through two portals.
Twelve frames are pinned by hash (and by eye) across all four maps, identical when the
arena sits at a different alignment. Sector movement is fuzzed with scrambled input on
every map under ASan/UBSan.

Bugs found while building it, worth remembering:
- Walls split into collinear pieces share one line, so "nearest wall line the ray exits
  through" picks the wrong piece. The exit wall must be the one whose *segment* contains
  the hit (0 <= u <= 1); picking by distance alone gave stripes and hid a doorway.
- `memcmp` on a struct compares padding; the first determinism test for the player failed
  at -O0 for that reason alone.
