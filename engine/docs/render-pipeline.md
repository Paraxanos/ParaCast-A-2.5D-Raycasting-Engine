# Render pipeline

The sector renderer is split in two with a plain-data seam between the halves.

```
map + camera ──▶ front-end ──▶ command buffer ──▶ back-end ──▶ framebuffer (8-bit indexed)
                (traversal,      (32-byte records,   (CPU strips,         │
                 no pixels)       no pointers)        any thread count)   ▼
                                                                    present: RGBA, PPM, SDL
```

The v0 grid raycaster does not use this path; it draws directly and stays the baseline.

## Front-end (`render/sector.c`)

Walks the portal graph and decides what to draw, writing no pixels. `pc_sector_frontend`
emits records into `sr->cmds`; `pc_sector_frame_params` gives the per-frame inputs those
records need; `pc_sector_backend` draws them; `pc_sector_render` does both and copies the
per-column depth into `fb->depth`.

Sprites are depth-tested and clipped to their portal windows **in the front-end**, so a
sprite column arrives at the back-end already clipped and the back-end needs no depth
buffer. (The architecture doc has the back-end do that test; it is a free choice for a CPU
renderer and a GPU back-end can move it later without changing the records.)

## Command records (`render/cmdbuf/cmdbuf.h`)

Every record is 32 bytes, with no pointers:

| Type | One record is | Key fields |
| --- | --- | --- |
| `WALL_COL` | one column of one wall piece | `x0`, rows `y0..y1`, `tex`, `tx`, `v0`/`dv`, `level` |
| `PLANE_SPAN` | a run of one floor or ceiling row | columns `x0..x1`, row `y0`, `tex`, `dist`, `level` |
| `SPRITE_COL` | one column of a billboard, already clipped | `x0`, rows `y0..y1`, `tex` (sprite id), `tx`, `v0`/`dv`, `level` |

Wall texture rows are `((v0 + k*dv) >> 16) & 63` computed in 32-bit arithmetic: only bits
16 to 21 ever select a texel, so keeping `v0` and `dv` modulo 2^32 gives exactly the rows a
64-bit sum would. `visit` and `wall` name the traversal step that produced a record; drawing
ignores them.

The buffer is sized once. A full buffer counts the lost records in `dropped` (visible as
`cmds_dropped` in the frame stats) and, in debug builds, aborts, so tests fail loudly.

## Back-end (`render/backend.h`, `render/backend_cpu/`)

A function table: `init`, `upload_assets`, `begin_frame`, `submit`, `end_frame`,
`framebuffer`, `shutdown`, `stats`. The CPU strip rasterizer bins the commands by band of
rows (`pc_bins_build`, a stable counting sort), then draws each band independently. A tall
wall column appears in every band it crosses and is clipped to each; spans lie in one row.
Bands write disjoint rows, so any number of threads in any order produce the same bits.

- `threads` > 1 uses OpenMP (`schedule(dynamic, 1)` over bands) when built with it
  (`ENGINE_OPENMP`, on by default if found). Without OpenMP every thread count runs on the
  calling thread, with identical output.
- `strip_h` is rows per band, default 32. A sweep showed 16 to 64 within a few percent of
  each other at 640x400 and 1920x1080; very small bands lose to the duplicated wall columns.

## Dump and replay

`paracast_demo --map NAME --dump f.pcmd` writes the frame's command buffer, per-column ray
directions, camera position and a hash of the assets. `paracast_replay f.pcmd` draws it
through the CPU back-end without the front-end, printing the framebuffer hash
(`--threads`, `--strip`, `--repeat K` for back-end-only timing, `--out` for a PPM).
Files are validated before drawing: wrong sizes, unknown types, or any record outside the
screen or the asset tables are refused. The format is 32-bit words in native (little-endian)
byte order; see `platform/cmddump.h`.

This is the tool for back-end bugs: a frame that draws wrongly can be captured once and
replayed against any change, and a future GPU back-end can be checked against the CPU
output for the same dump.
