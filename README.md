# Paracast

A deterministic, pure-C99 software renderer in the Wolfenstein 3D / Doom lineage. It started
as a DDA grid raycaster and is growing into a sector/portal renderer with voxels and a
swappable CPU/CUDA back-end. Math is 16.16 fixed point, so a given input produces
bit-identical frames.

The full design is in [engine/docs/Portal-Voxel Software Renderer Architecture.md](engine/docs/Portal-Voxel%20Software%20Renderer%20Architecture.md).
Measured results go in [engine/docs/feature-log.md](engine/docs/feature-log.md); the map
format is in [engine/docs/map-format.md](engine/docs/map-format.md).

## Status

- **Phase 1** (done): fixed-point core, arenas, procedural assets, the grid raycaster kept as
  the `v0` baseline, PPM output, SDL2 viewer, tests, benchmark harness, CI.
- **Phase 2** (done): sector maps with a text format and loader, portal renderer with
  per-sector floor and ceiling heights, steps, lowered ceilings and billboard sprites,
  sector collision, automap and clip-window overlay, four test maps.

## Build

Requires gcc and CMake. On Windows, build inside WSL.

```sh
cmake -S . -B build/release          # Release by default
cmake --build build/release -j
ctest --test-dir build/release

./build/release/engine/paracast_demo out.ppm --map nested     # sector renderer, from the spawn point
./build/release/engine/paracast_demo out.ppm 12.5 12.5 20     # v0 grid raycaster: x y yaw_degrees
./build/release/engine/paracast_bench 600 bench.csv --map maze  # frames, optional CSV, optional map
```

`--map` takes a name from `engine/maps/` (`steps`, `window`, `nested`, `maze`) or a path.
Without it the tools use the v0 grid raycaster. The demo also takes `--automap` and
`--clip` to render the top-down map or the portal clip windows instead of the view.

### Interactive viewer

Needs SDL2 (`sudo apt install libsdl2-dev` on Ubuntu/WSL). It is detected automatically; the
engine builds and tests without it.

```sh
./build/release/engine/paracast_view                  # the "nested" map
./build/release/engine/paracast_view --map maze
./build/release/engine/paracast_view --grid           # v0 grid raycaster
./build/release/engine/paracast_view --scale 3 --novsync
```

WASD moves, arrows or Q/E turn, **Tab** toggles the automap, **C** the clip-window overlay,
Esc quits. The title bar shows fps, render time and the current sector. Under WSL2 the
window appears through WSLg.

Options: `-DENGINE_SDL=AUTO|ON|OFF`, `-DENGINE_SANITIZE=ON` (ASan + UBSan, use with
`-DCMAKE_BUILD_TYPE=Debug`), `-DENGINE_OPENMP=ON` (linked now, used from Phase 3),
`-DENGINE_TESTS=OFF`.

## Layout

```
engine/
  include/paracast.h   only public header
  src/core/            fixed point, arenas, sine table, hash (no I/O, no malloc)
  src/assets/          palette, light colormap, procedural textures and sprites
  src/map/             grid map + DDA (v0), sector map tables, text loader
  src/render/          framebuffer, grid raycaster, sector/portal renderer, present
  src/debug/           automap, clip-window overlay
  src/sim/             fixed-tick player movement and collision
  src/platform/        PPM writer, file reader, timer, SDL2 window (optional)
  maps/                test maps (.map text) ; tools/gen_maze.py generates maze.map
  tools/               demo, benchmark and viewer executables
  tests/               unit, golden-frame, fuzz-style and determinism tests
  docs/                architecture, map format, feature log
```

## License

MIT
