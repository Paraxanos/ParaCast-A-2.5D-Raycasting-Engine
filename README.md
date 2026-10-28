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
- **Phase 3** (done): the sector renderer is split into a front-end that emits a command
  buffer and a back-end that draws it; a multithreaded CPU strip rasterizer (OpenMP) whose
  output is bit-identical at any thread count; command-buffer dump and replay; heavier
  benchmark scenes. See [engine/docs/render-pipeline.md](engine/docs/render-pipeline.md).

## Build

Requires gcc and CMake. On Windows, build inside WSL.

```sh
cmake -S . -B build/release          # Release by default
cmake --build build/release -j
ctest --test-dir build/release

./build/release/engine/paracast_demo out.ppm --map nested     # sector renderer, from the spawn point
./build/release/engine/paracast_demo out.ppm 12.5 12.5 20     # v0 grid raycaster: x y yaw_degrees
./build/release/engine/paracast_bench 600 bench.csv --map maze  # frames, optional CSV, optional map
./build/release/engine/paracast_bench 300 --map hall --size 1920x1080 --threads 8
```

`--map` takes a name from `engine/maps/` (`steps`, `window`, `nested`, `maze`, and the
benchmark scenes `corridor` and `hall`) or a path. `--size WxH`, `--threads N` and
`--strip H` set the resolution and the CPU back-end (threads need OpenMP; the picture is the
same either way).
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

Capture a frame's command buffer and replay it through the back-end alone:

```sh
./build/release/engine/paracast_demo out.ppm --map nested --dump frame.pcmd
./build/release/engine/paracast_replay frame.pcmd --threads 4 --repeat 200
```

Options: `-DENGINE_SDL=AUTO|ON|OFF`, `-DENGINE_OPENMP=AUTO|ON|OFF`, `-DENGINE_SANITIZE=ON`
(ASan + UBSan, use with `-DCMAKE_BUILD_TYPE=Debug`), `-DENGINE_TESTS=OFF`. If you configured
an older build directory, pass `-DENGINE_OPENMP=ON` once: the option used to be a plain
on/off switch and a cached `OFF` is kept.

## Layout

```
engine/
  include/paracast.h   only public header
  src/core/            fixed point, arenas, sine table, hash (no I/O, no malloc)
  src/assets/          palette, light colormap, procedural textures and sprites
  src/map/             grid map + DDA (v0), sector map tables, text loader
  src/render/          framebuffer, grid raycaster, sector/portal front-end, present
  src/render/cmdbuf/   command records and strip binning
  src/render/backend_cpu/  multithreaded CPU strip rasterizer (the backend interface is render/backend.h)
  src/debug/           automap, clip-window overlay
  src/sim/             fixed-tick player movement and collision
  src/platform/        PPM writer, file reader, timer, command dump, SDL2 window (optional)
  maps/                test maps (.map text); tools/gen_maze.py and gen_stress.py generate some
  tools/               demo, benchmark, replay and viewer executables
  tests/               unit, golden-frame, fuzz-style and determinism tests
  docs/                architecture, map format, render pipeline, feature log
```

## License

MIT
