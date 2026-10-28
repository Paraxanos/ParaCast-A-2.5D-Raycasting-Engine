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

## Phase 3: command buffer, backend interface, threaded CPU strip rasterizer

The sector renderer now runs in two stages. The front-end walks the portal graph and emits
32-byte records (wall columns, plane spans, sprite columns); a back-end draws them. The CPU
back-end bins the records by band of rows and draws bands on OpenMP threads. Frames are
bit-identical to Phase 2 at every thread count and strip height. Details are in
`engine/docs/render-pipeline.md`. The v0 grid raycaster does not use the command buffer and
is unchanged.

### Gate: identical frames

The 12 golden frames from Phase 2 pass unchanged through the new path, and the chained hash
over all 600 frames of the benchmark path matches the Phase 2 binary on every scene, run
side by side in one session:

| Scene | Run hash (Phase 2 and Phase 3 alike) |
| --- | --- |
| v0 grid | `48b464b95d006180` |
| `steps` | `6f1798c2c6d3334e` |
| `window` | `1af7b62d7922b9df` |
| `nested` | `cdcdcb00d32cf0f6` |
| `maze` | `981f5318d1225ca1` |

Across every thread count (1, 2, 4, 8, 16), resolution and round in the scaling runs below
there was exactly one run hash per scene and resolution.

### Cost of the split, single thread

Phase 2 binary against Phase 3 with `--threads 1`, 640x400, `paracast_bench 600`, three
interleaved rounds in one session, pinned to one CPU (median of the three rounds):

| Scene | Phase 2 | Phase 3, 1 thread | Change | Front-end | Back-end |
| --- | --- | --- | --- | --- | --- |
| v0 grid | 0.415 ms | 0.415 ms | none (same code) | n/a | n/a |
| `steps` | 0.385 ms | 0.427 ms | +11% | 0.104 | 0.326 |
| `window` | 0.378 ms | 0.408 ms | +8% | 0.096 | 0.319 |
| `nested` | 0.460 ms | 0.488 ms | +6% | 0.109 | 0.368 |
| `maze` | 0.375 ms | 0.424 ms | +13% | 0.101 | 0.324 |

Writing 1,300 to 2,500 records (about 42 to 80 KB) and binning them costs 0.03 to 0.05 ms,
paid back as soon as a second thread helps. About a quarter of the frame is front-end.

### Thread scaling

Median over three interleaved rounds, 300 frames each, `OMP_PROC_BIND=true` plus `taskset` to
one logical CPU per thread (see the caveat below). Total frame time, speedup over 1 thread in
brackets, and the back-end alone:

| Scene / size | 1 thread | 2 | 4 | 8 | 16 | Back-end alone, 1 -> 8 threads |
| --- | --- | --- | --- | --- | --- | --- |
| `nested` 640x400 | 0.486 ms | 0.334 (1.46x) | 0.243 (2.01x) | 0.211 (2.31x) | 0.225 (2.16x) | 0.37 -> 0.10 ms (3.7x) |
| `nested` 1280x800 | 1.841 | 1.221 (1.51x) | 0.826 (2.23x) | 0.671 (2.74x) | 0.662 (2.78x) | 1.41 -> 0.30 (4.7x) |
| `nested` 1920x1080 | 3.715 | 2.409 (1.54x) | 1.652 (2.25x) | 1.292 (2.88x) | 1.255 (2.96x) | 2.85 -> 0.57 (5.0x) |
| `corridor` 640x400 | 0.510 | 0.395 (1.29x) | 0.293 (1.74x) | 0.264 (1.93x) | 0.261 (1.95x) | 0.37 -> 0.11 (3.4x) |
| `corridor` 1280x800 | 1.858 | 1.270 (1.46x) | 0.943 (1.97x) | 0.752 (2.47x) | 0.730 (2.55x) | 1.43 -> 0.32 (4.5x) |
| `corridor` 1920x1080 | 3.645 | 2.458 (1.48x) | 1.763 (2.07x) | 1.396 (2.61x) | 1.365 (2.67x) | 2.86 -> 0.61 (4.7x) |
| `hall` 640x400 | 0.526 | 0.375 (1.40x) | 0.284 (1.85x) | 0.246 (2.14x) | 0.235 (2.24x) | 0.38 -> 0.10 (3.8x) |
| `hall` 1280x800 | 2.000 | 1.358 (1.47x) | 1.019 (1.96x) | 0.825 (2.42x) | 0.773 (2.59x) | 1.50 -> 0.33 (4.5x) |
| `hall` 1920x1080 | 4.089 | 2.739 (1.49x) | 1.993 (2.05x) | 1.592 (2.57x) | 1.546 (2.64x) | 3.11 -> 0.61 (5.1x) |

`corridor` runs with `--look 0`, the view held down the corridor through 24 portals; `nested`
sees about 3.4 sectors and `hall` one. Commands per frame grow with resolution (walls are per
column, spans per row): about 1,300 at 640x400 to 4,000 at 1080p for `nested` and `hall`, and
2,500 to 7,400 for `corridor`. In the cases I inspected the three rounds agreed to within 1 to
2%.

What it says:
- **The back-end scales; the frame as a whole does not, because the front-end is still one
  thread.** The back-end alone speeds up 3.4x to 5.1x at 8 threads, better at higher
  resolutions. The whole frame tops out at about 1.9x to 3.0x. The front-end is 19% to 27% of
  the single-thread frame, which by Amdahl caps the total at roughly 3.7x to 5.3x, and its
  absolute cost grows with resolution (0.11 to 0.14 ms at 640x400, 0.7 to 1.0 ms at 1080p)
  because it works per column.
- **At 640x400 the gain is real but modest** (about 1.9x to 2.3x at 8 threads; `nested` goes
  from 0.49 ms to 0.21 ms). Fork and join cost, plus the unthreaded front-end, leave little
  to gain past 4 threads.
- **Past 8 threads there is almost nothing left**: 16 threads adds at most about 7% and
  sometimes loses, as expected once the back-end is down to roughly the front-end's own cost.
- **The strip height default of 32 holds up.** At 8 threads the back-end took 0.102 to
  0.118 ms for strips of 16 to 64 rows at 640x400 and 0.56 to 0.61 ms at 1080p; strips of 4
  rows were 35% to 140% slower than 32-row strips from duplicated wall columns, and 128-row
  strips were about 80% slower at 640x400 (too few strips to share out). Single-threaded, taller strips are slightly cheaper.

Caveat on the pinning: WSL2 reports 14 cores with two threads each and does not say which are
P-cores on this i7-14700HX; Windows decides where the VM's CPUs actually run. The runs pin to
one reported CPU per thread, which reduces migration noise but does not guarantee P-cores.
The 16-thread column (8 cores with their second threads) is the most likely to be skewed by
this. Numbers from a native Linux or Windows build could differ.

### Present cost

`paracast_bench --convert` adds the 8-bit to RGBA expansion as its own column: 0.223 to
0.225 ms at 640x400, about 45% on top of a 0.49 ms single-thread frame. The earlier SDL
measurement of about 3.2 ms per frame with vsync off means most of the on-screen time is the
texture upload, scaling and WSLg's copy, not the conversion.

### Tested

Command layout and overflow (a full buffer drops records, counts them, and aborts only in
strict/debug mode), binning (ordering, empty and edge-row records, table overflow), and
validation of every kind of malformed record. Thread counts 1, 2, 3 and 8 against strip
heights 0, 1, 7, 32, 33, 64, 400 and 999 on seven poses across four maps: the picture and
the emitted commands are identical every time. The front-end is shown to write no pixels.
Dump, read and replay reproduce the live frame bit for bit, and 600 randomly damaged dump
files (plus every truncation, bad magic, bad version and trailing data) are either refused or
drawn without any memory error under ASan and UBSan. The two new maps join the coverage
tests (every pixel written exactly once from every sector, 24 visits deep for `corridor`)
and add four goldens.

Gotcha: `ENGINE_OPENMP` used to be a plain on/off option, so an existing build directory keeps
a cached `OFF` and silently builds single-threaded; a first threaded benchmark ran no faster
for exactly that reason. Pass `-DENGINE_OPENMP=ON` once, or use a fresh build directory.

### Not done, and what the numbers suggest next

- No SIMD and no layout changes, as decided: the profile does not point at them. Every span
  is textured (per-pixel lookups), where SIMD helps far less than on solid fills.
- The front-end is the next bottleneck: 19% to 27% of a single-thread frame and the whole
  ceiling on threaded speedup. Overlapping the front-end of frame N+1 with the back-end of
  frame N, or splitting traversal by column range, would be the evidence-backed next step.
- The benchmark camera spins in place, so `nested`, `maze` and the others still see few
  sectors per frame; `corridor` with `--look 0` is the deep-traversal case.
