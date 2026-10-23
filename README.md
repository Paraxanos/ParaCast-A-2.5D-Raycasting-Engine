# Paracast

A deterministic, pure-C99 software renderer in the Wolfenstein 3D / Doom lineage. It starts as a
DDA grid raycaster and grows into a sector/portal renderer with voxels and a swappable CPU/CUDA
back-end. Math is 16.16 fixed point, so a given input produces bit-identical frames.

The full design is in [engine/docs/Portal-Voxel Software Renderer Architecture.md](engine/docs/Portal-Voxel%20Software%20Renderer%20Architecture.md).
Measured results go in [engine/docs/feature-log.md](engine/docs/feature-log.md).

## Status

Phase 1 (foundations and raycaster) is in progress: fixed-point core, arenas, procedural assets,
textured walls with floor and ceiling, light colormap, PPM output, tests, and a benchmark harness.

## Build

Requires gcc and CMake. On Windows, build inside WSL.

```sh
cmake -S . -B build/release          # Release by default
cmake --build build/release -j
ctest --test-dir build/release

./build/release/engine/paracast_demo out.ppm 12.5 12.5 20   # x y yaw_degrees
./build/release/engine/paracast_bench 600 bench.csv         # frames, optional CSV to append to
```

Options: `-DENGINE_SANITIZE=ON` (ASan + UBSan, use with `-DCMAKE_BUILD_TYPE=Debug`),
`-DENGINE_OPENMP=ON` (linked now, used from Phase 3), `-DENGINE_TESTS=OFF`.

## Layout

```
engine/
  include/paracast.h   only public header
  src/core/            fixed point, arenas, sine table, hash (no I/O, no malloc)
  src/assets/          palette, light colormap, procedural textures
  src/map/             grid map and DDA ray cast
  src/render/          framebuffer, raycaster, indexed-to-RGBA present
  src/platform/        PPM writer, timer
  tools/               demo and benchmark executables
  tests/               unit and golden-frame tests
  docs/                architecture and feature log
```

## License

MIT
