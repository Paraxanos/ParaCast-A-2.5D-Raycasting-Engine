# Portal-Voxel Software Renderer: Architecture

Oct 20, 2026 · @Pranjal Srivastava

## 1. Overview

This is a deterministic, pure-C99 software renderer in the Doom/Build lineage, where one spatial structure, the sector/portal graph, drives visibility, lighting, sound, and AI. Voxels add volumetric walls and voxel entities, and the rasterizer is split from traversal so it can run on CPU strips today and CUDA later.

**Goals**

- Pixels on screen early (Wolf3D-style DDA raycaster), then grow into a portal renderer without rewrites.
- Efficiency as a headline metric: every optimization has a measured before/after.
- Reproducible runs: the same input log produces bit-identical frames.
- A backend interface clean enough that a CUDA rasterizer is a new module, not a rewrite.

**Non-goals (for v1)**

- Full GPU-native rendering (OpenGL/Vulkan), dynamic per-pixel lighting, networking, a map editor, or a general-purpose game framework.

**Novel contributions (the project's story)**

1. Portal graph as a universal index (PVS, light flood, sound flood, AI perception).
2. Volumetric wall columns with brick-bitmask voxel DDA, plus runtime wall deformation.
3. Non-Euclidean portals (per-portal transforms).
4. A deferred command-buffer renderer with interchangeable CPU and CUDA back-ends.
5. Deterministic replay, rewind, and a benchmark harness built in from day one.

## 2. Design principles and constraints

Six rules keep the engine fast, portable, and testable; every later section follows from them.

| Principle | Rule | Why |
| --- | --- | --- |
| Pure C99 core | No OS, no libc I/O inside `core/`, `map/`, `render/` | Portable, headless-testable, easy to port to CUDA C |
| No `malloc` in the frame loop | Arenas sized once at startup; per-frame arena reset | No allocator latency, no fragmentation, deterministic memory |
| Structure-of-arrays | Walls, sectors, sprites, commands stored as parallel arrays | Cache-friendly scans, SIMD- and GPU-friendly |
| Traversal emits, back-end draws | Front-end never touches pixels | Enables sorting, threading, CUDA, and replay |
| Deterministic by default | Fixed timestep, seeded RNG, no wall-clock reads in sim | Reproducible benchmarks, replay, rewind |
| Measure first | Optimization only after a profile shows the hot spot | Credible before/after numbers |

**Numeric policy.** Start with `float` for clarity, behind a `real_t` typedef and math macros. Keep a 16.16 fixed-point build option for determinism across compilers and for the retro path. Cross-platform bit-identical floats are not guaranteed, so determinism tests compare within one build configuration.

**Rendering target.** 8-bit indexed framebuffer plus a 256-entry palette and a light-level colormap table (light level × palette index to palette index). Conversion to 32-bit happens once at present time.

## 3. System architecture

The engine has five layers, and a layer may call only the layers below it; the renderer reaches pixels, on CPU or GPU, through one backend interface.

**Layers, top to bottom** (each may call only the layers below it)

| Layer | Contents |
| --- | --- |
| Game layer | Entities, AI, doors and lifts, input mapping, demo record and replay |
| Engine API (`engine.h`) | The only public header; game code never reaches below it |
| Engine modules | Six modules, listed below |
| Core | Math (float or 16.16 fixed), arena allocators, lookup tables, RNG, logging; no OS calls |
| Platform backends | SDL2 window and input, PPM file dump, and a null target for benchmarks |

**Engine modules**

| Module | Responsibilities |
| --- | --- |
| Map and portal graph | Sector and wall tables (SoA), portal transforms, PVS bitsets; graph built once at load |
| Sim and services | Fixed-step tick, demo replay, rewind keyframes, light and sound floods, AI sight |
| Voxel subsystem | Brick bitmask DDA, wall span columns and deformation, impostor cache |
| Renderer | Front-end portal traversal, command buffer (sorted draws), back-ends (CPU strips, CUDA) |
| Assets | Map and voxel-model loaders, palette, colormap, column-major mipped textures |
| Debug and benchmark | Automap, clip-buffer viewer, counters, frame-hash checks, headless benchmark runner |

**Boundaries that matter**

- `engine.h` is the only public header; modules talk through plain data tables, not each other's internals.
- Everything above Platform is C99 and never calls the OS, so the whole engine runs headless for tests and benchmarks.
- Platform presents an RGBA framebuffer and supplies input and timing; swapping SDL2 for the PPM or null target changes nothing above it.
- CUDA code lives only inside the renderer's back-end module (Section 10).

## 4. Data model

All world data is plain-old-data addressed by 32-bit indices, never pointers, so any array can be memcpy'd to a GPU buffer or serialized unchanged.

```c
typedef uint32_t id_t;           // index into a table; 0xFFFFFFFF = none

typedef struct { real_t m[2][2]; real_t t[2]; real_t dyaw; } Xform2; // portal transform

// --- Sectors and walls (SoA) ---
typedef struct {
    real_t  floor_h[N], ceil_h[N];         // per sector
    uint16_t floor_tex[N], ceil_tex[N];
    uint8_t light[N];
    id_t    first_wall[N]; uint16_t nwalls[N];
    id_t    vox_pocket[N];                 // optional voxel interior
} SectorTable;

typedef struct {
    Vec2    a[M], b[M];                    // endpoints
    id_t    back_sector[M];                // NONE = solid, else portal
    Xform2  portal_xf[M];                  // identity unless non-Euclidean
    uint16_t tex_up[M], tex_mid[M], tex_low[M];
    id_t    vcol[M];                       // optional volumetric column set
    uint8_t flags[M];
} WallTable;
```

**Volumetric wall columns.** A solid wall may carry a column set: for each horizontal sample along the wall, a short sorted list of vertical spans.

```c
typedef struct { uint16_t z0, z1; uint8_t material; uint8_t flags; } VoxSpan;
typedef struct { uint32_t first_span; uint8_t nspans; } VoxColumn;   // spans in one shared pool
```

A ray hitting the wall walks the column's spans and emits one draw segment per span. Edits (holes, cracks, windows) split, shrink, or merge spans in place.

**Voxel bricks.** Models and voxel pockets use 4x4x4 bricks with a 64-bit occupancy mask.

```c
typedef struct { uint64_t occ; uint32_t first_vox; } Brick;   // occ bit = z*16+y*4+x
typedef struct { id_t *brick_index; int bx, by, bz; Brick *bricks; uint8_t *voxels; } VoxModel;
```

Empty bricks cost one 64-bit compare. Inside an occupied brick, `ctz`/`popcount` locate the first solid voxel and its index into the packed voxel array.

**Camera and entities.**

```c
typedef struct { Vec2 pos; real_t z, yaw, pitch; id_t sector; Xform2 view_xf; } Camera;
typedef struct { Vec2 pos; real_t z, yaw; id_t sector, model; uint16_t anim, flags; } Entity;
```

Pitch uses y-shearing, as Doom ports do, to avoid true 3D rotation.

**Map format.** A small text format at first (sectors, walls, portal links, things) compiled to a binary pack at load. The binary pack is a flat blob of the tables above, so loading is a few `memcpy`s.

## 5. Frame pipeline

Every frame runs six stages in order; stage 2 only emits commands, and stage 4 is the only place pixels are written.

| Stage | What happens |
| --- | --- |
| 1. Tick the simulation | Fixed timestep: entities, doors, light and sound floods update; demo input is applied |
| 2. Front-end: traverse and clip | Camera sector, PVS skip, portal walk front to back with column clip arrays; writes no pixels |
| 3. Command buffer | Wall column, plane span, voxel job and sprite commands in flat arrays, binned by screen tile |
| 4. Back-end (one interface, two implementations) | **CPU:** thread pool over disjoint screen strips, SIMD span fills; the reference output. **CUDA (planned):** wall, plane and voxel kernels; commands uploaded per frame; output checked against the CPU frame |
| 5. Post and convert | Colormap lighting, dither fog, 8-bit indexed to RGBA |
| 6. Present | SDL2 window, PPM file, or null target; benchmark mode times the frame without presenting |

**Front-end.** Traversal starts in the camera's sector and processes walls front to back. A solid wall emits its columns and closes those columns in the clip arrays. A portal emits upper and lower step columns, narrows the clip window, and recurses into the back sector. A volumetric wall emits one command per span. Floors and ceilings emit plane spans from visplane runs. Sprites and voxel entities are collected, sorted far to near, and emitted last.

**Command buffer.** Commands are fixed-size, cache-line-friendly records (target 32 bytes), so they can be uploaded to a GPU as they are.

| Command | Key fields | Produced by |
| --- | --- | --- |
| `WALL_COL` | x, y0, y1, texture, u, v start, v step, light, depth, visit | Solid and portal walls, volumetric span segments |
| `PLANE_SPAN` | y, x0, x1, plane id, row distance, light | Visplane builder |
| `VOX_JOB` | entity, model, screen rect, mode (near or impostor) | Voxel entity pass |
| `SPRITE` | x0, x1, y0, y1, texture, depth, light, visit | Sprite pass |

**Binning and ordering.** After traversal, a counting sort bins commands by screen strip (CPU) or tile (CUDA). Because clip arrays keep wall columns and plane spans disjoint, they can be drawn in any order within a bin. Sprites and voxel jobs are drawn afterwards, far to near, and test against the per-column depth buffer.

**Visit index.** Each command records which portal visit produced it. With non-Euclidean portals (Section 8) the same sector can be drawn more than once, and the visit index keeps those draws ordered and separate.

**Replay.** The command buffer for any frame can be dumped and re-run through any back-end, which makes back-end bugs reproducible without rerunning the simulation.

## 6. Voxel subsystem

Voxels appear in three places, in this order of build priority: voxel entities, volumetric walls, then voxel pockets.

**Brick bitmask DDA.** The shared primitive. A ray steps through the brick grid with a 3D DDA; an empty brick (`occ == 0`) is skipped with one compare. In an occupied brick, a second DDA over 4x4x4 cells uses the bitmask: mask off the cells already passed, then `ctz` finds the next solid cell. Typical empty-space traversal touches one 64-bit word per brick.

**Voxel entities.** Rendered in two modes, chosen per entity by distance and screen size.

- *Near:* per-column ray-march in model space, clipped against the wall depth buffer and column clip arrays. Correct occlusion and lighting.
- *Far:* impostor sprite, drawn like an ordinary clipped billboard (see the cache below).

**Impostor cache.** Key = (model, animation frame, quantized view angle, quantized distance band). On a miss, the near-mode renderer draws the model into a small tile in an LRU atlas; later instances copy-draw that tile as a sprite. Target: 50 identical enemies cost a handful of renders. Track hit rate as a first-class metric.

**Volumetric walls.** For each ray hitting a wall with a column set, locate the column from the hit position along the wall, then emit one segment per span (top span, gap, bottom span, and so on). Segments feed the same command buffer as ordinary wall columns, so the back-end needs no special case.

**Deformation.** Destruction edits the span list of the affected columns (split, shrink, merge) and marks the wall dirty. Dirty flags invalidate only the PVS rows, light-cache entries, and impostors that depend on that wall. No remeshing is needed.

**Voxel pockets (stretch).** A sector may declare a voxel interior. Portals lead into it; inside, rays use brick DDA instead of flat floor/ceiling spans. Pockets exist only where wanted, so voxel cost stays local.

**Depth consistency.** Every drawn column writes a depth value to a per-column depth buffer, whether solid wall, span segment, or voxel hit. Sprites and voxel entities clip against it, so all three agree on what is in front.

## 7. Portal graph services

The portal graph (sectors as nodes, portals as edges) is built once at load and answers four different questions with the same traversal code.

| Service | Question answered | Mechanism | Cost control |
| --- | --- | --- | --- |
| Visibility (PVS) | Which sectors can possibly be seen from sector S? | Offline-at-load bitset per sector from portal flood-fill with conservative frustum tests | Rebuilt only for sectors touched by a dirty portal or wall |
| Light | How bright is each sector or wall from dynamic lights? | Flood from each light through portals, attenuating per hop; cache result per sector | Flood depth capped by the frame-time governor; only affected sectors re-flood |
| Sound | Where does a sound seem to come from, and how loud? | Shortest-path flood from the source; direction taken from the first portal on the path | Cap on path length; reuse the light flood's queue code |
| AI perception | Can enemy E see or hear the player? | Sight = PVS bit test plus a 2D line check; hearing = sound flood value at E's sector | Evaluated on a staggered schedule, not every tick |

**Why one graph.** Each service is a bounded breadth-first walk over the same adjacency arrays, so they share code, memory layout, and invalidation (a dirty portal marks the relevant PVS rows, light entries, and sound caches).

**Runtime use of PVS.** At the start of traversal, the renderer takes the camera sector's bitset and skips any sector outside it before clipping. Clip arrays still do the exact work; PVS only removes sectors that cannot contribute.

**Dynamic geometry.** Doors and lifts change portal openness. A closed portal clears its edge in the graph, and the PVS rows that depend on it are recomputed lazily on the next frame that needs them.

**Light model.** Sector light = ambient + sum of attenuated contributions from flooded lights, quantized to the 0-31 colormap range. Per-wall light is the sector value plus a fixed orientation bias, so no per-pixel lighting is needed.

## 8. Non-Euclidean portals

Each portal carries a 2D rigid transform (rotation plus translation, optionally uniform scale). When traversal recurses through a portal, it composes that transform into the current view transform, so the back sector is drawn as if it were placed elsewhere in the plane. Nothing else in the renderer changes.

**What it enables**

- Rooms larger on the inside than the outside.
- Loops that do not close, such as a corridor that returns to its start rotated 90 degrees.
- One sector reused many times to fake an endless hall.

**Engine rules that make it safe**

1. **Recursion cap.** Traversal depth is bounded (default 16) and also by a per-frame portal-visit budget, since a looping layout could otherwise recurse forever.
2. **PVS keyed by sector and transform.** In a Euclidean map each sector appears at most once per view. With transforms, the same sector can appear several times. Use PVS for the Euclidean subgraph only; at any portal with a non-identity transform, fall back to runtime clipping with the depth cap.
3. **Clip windows are transformed with the view.** Portal edges are clipped in the transformed frame, so column clip arrays stay valid.
4. **Sprites and entities draw per visit.** An entity visible through two copies of its sector is drawn once per visit, each with that visit's transform. The command buffer records the visit index so the back-end orders draws correctly.
5. **Movement and collision use the same transform.** When the player crosses a portal, position and yaw are mapped through it, so physics, AI sight lines, and sound floods remain consistent.
6. **Light and sound floods stay on the graph.** They follow portal edges and ignore the geometric transform, which keeps them cheap. This is a deliberate simplification and should be noted in the demo.

**Validation.** A test map set covers a closed loop, a doubled room, a self-containing sector, and a corridor that rotates by 90 degrees per cycle. Each has a golden frame in the replay suite.

## 9. Performance engineering

Optimizations are added in the order the profiler justifies them. Each row below becomes an entry in the feature log with a measured cost or gain.

| Area | Technique | Expected effect | How to verify |
| --- | --- | --- | --- |
| Memory | Arena allocation, SoA tables, no `malloc` in frame | Stable frame time, fewer cache misses | Allocation counter stays at 0 per frame |
| Framebuffer | Column-major 8-bit buffer, tiled transpose at present | Sequential writes for wall columns | Cycles per column, transpose tile size sweep |
| Textures | Column-major layout, per-distance mips (64, 32, 16) | Linear reads, far walls stay in L1 | L1/L2 miss counters via `perf` |
| Math | Reciprocal and per-row distance LUTs; divide per row, not per pixel | Divides out of the hot path | Instruction count in span loop |
| Spans | SIMD fills (SSE2, NEON behind `#ifdef`) for floor, ceiling, solid colour | Higher fill rate | Pixels per cycle |
| Voxels | Brick bitmask DDA with `ctz`/`popcount` | Cheap empty-space skipping | Bricks visited per ray |
| Culling | PVS bitsets, per-column occlusion early-out | Less wasted traversal | Sectors visited per frame |
| Overdraw | Front-to-back portal order, column clip arrays | Each pixel written about once | Overdraw ratio counter |
| Threading | Strip-parallel back-end over a small thread pool | Near-linear scaling on independent strips | Speedup vs thread count |
| Caching | Impostor atlas for voxel entities | Fewer voxel renders | Cache hit rate |
| Governor | Closed-loop control of resolution, voxel LOD, light flood depth | Holds a frame-time target | Frame-time histogram |

**Frame-time governor.** A simple controller reads last-frame cost (rolling average) and adjusts a small set of knobs in a fixed priority: impostor distance, voxel ray budget, light flood depth, then render scale. Hysteresis stops oscillation. The governor is disabled in benchmark mode so measurements are comparable.

**Fixed-point path.** The 16.16 build replaces `real_t` and a few macros. It is a later optimization and a determinism aid; it is not assumed to be faster on modern CPUs, so it must be measured.

**Yaw-shift frame reuse (experimental).** Under pure rotation, previous-frame columns approximately shift horizontally. The engine can reuse them and render only newly exposed edge columns, refreshing a few columns per frame to bound error. This is a stretch goal with an explicit error metric.

**Performance targets (to validate, not promises).** Define them after the first baseline: ms/frame at 640x400 on a named reference machine, cycles per pixel, and overdraw ratio. The v0 naive renderer stays in the repo as the baseline for every comparison.

## 10. CUDA backend (future support)

The command buffer is the seam: the front-end decides what to draw, and any back-end that implements one small interface can draw it. The CPU back-end is the reference; CUDA is a second implementation checked against it.

**Backend interface (plain C function table).**

```c
typedef struct RenderBackend {
    const char *name;
    int  (*init)(const BackendCfg *cfg);
    int  (*upload_assets)(const AssetPack *pack);     // textures, palette, colormap, voxel models
    void (*begin_frame)(const FrameParams *fp);
    void (*submit)(const CmdBuffer *cb);              // walls, planes, voxel jobs, sprites
    void (*end_frame)(void);                          // post pass + convert
    const uint32_t *(*framebuffer)(void);             // RGBA; may trigger a copy-back
    void (*shutdown)(void);
    BackendStats (*stats)(void);                      // per-stage timings for the harness
} RenderBackend;
```

The CUDA module is selected at runtime (`--backend=cuda`) and is compiled only when `ENGINE_BACKEND_CUDA=ON`. Its `.cu` files expose this table through one `extern "C"` header, so core code stays C99. Kernels are written in a C-like subset.

**Two integration modes.** Build A first; B is the research extension.

|  | Mode A: hybrid | Mode B: GPU-resident |
| --- | --- | --- |
| Traversal | CPU emits commands | One GPU thread per screen column walks portals along its ray |
| Per-frame upload | Command buffer (tens of KB to about 1 MB) | Camera and entity list only (hundreds of bytes) |
| World data | Stays on CPU | Uploaded once; tables are index-based PODs |
| Main cost | Launch and transfer latency; sequential traversal | Warp divergence from uneven portal depth |
| Likely strength | Easy to validate; reuses front-end | Scales with resolution and voxel load |
| Risk | CPU traversal may cap the speedup | Re-implementing traversal on GPU; more equivalence work |

**Mode A kernel plan.**

1. **Wall and span kernel.** Commands are binned per screen tile on the CPU or GPU. One block handles a tile; threads cover pixels in a column run. Because the clip arrays make a column's segments disjoint, commands can be drawn in any order.
2. **Plane kernel.** Thread per pixel; row distance from a constant-memory LUT; flat textures sampled through the read-only cache (`__ldg`).
3. **Voxel kernel.** Thread per column (or per pixel for near entities) running the brick DDA. The 64-bit occupancy mask maps to `__ffsll` and `__popcll`. The impostor atlas lives in GPU memory.
4. **Post and convert kernel.** Colormap lookup, dither fog, and 8-bit to RGBA conversion fused into one pass. The 256-entry palette and the light colormap (about 8 KB) sit in constant memory.

**Data movement.**

- Pinned host memory, double-buffered command buffers, and two streams so frame N+1 uploads while frame N renders.
- Textures stored column-major in global memory, uploaded once with `upload_assets`.
- Presentation: copy the RGBA frame back for SDL (about 1 MB at 640x400), or use CUDA-GL interop later. Benchmark mode times the render without presentation, plus a separate total including copy-back.

**Equivalence and determinism.** Integer and fixed-point arithmetic should match the CPU exactly. For float paths, compile with `-fmad=false` and compare against the CPU reference with a per-pixel tolerance and a differing-pixel count; the test fails above a set threshold.

**What to measure.** Use `cudaEvent` timers for upload, each kernel, and copy-back, and Nsight Systems / Nsight Compute for occupancy, memory throughput, and divergence. Report CPU (1 thread and N threads) against CUDA across resolutions (320x200 up to 1920x1080) and voxel loads.

**Expected outcome (a hypothesis to test).** At low resolution and light scenes, the CPU path should win because launch and transfer overhead dominate. The crossover should appear at higher resolutions or heavy voxel scenes. Publishing the crossover point, including where CUDA loses, is the most informative result.

**CUDA milestones.**

1. Conform the CPU back-end to the interface, with no CUDA involved yet.
2. Host glue, a solid-wall kernel, and convert kernel; pass equivalence on wall-only maps.
3. Textures, planes, and sprites.
4. Voxel kernel with brick DDA.
5. Overlap upload and compute with streams; profile and tune tile size.
6. Optional Mode B experiment on a subset of maps.

## 11. Determinism, replay, and benchmarking

The same input log must produce the same frames, and that property powers replay, rewind, regression tests, and every performance claim.

**Deterministic simulation.** Fixed timestep (for example 35 Hz, like Doom), a seeded RNG owned by the sim, no wall-clock reads or uninitialized memory in sim state, and sim state stored in arenas so it can be snapshotted by copying.

**Demo format.** A header (map id, build hash, RNG seed, tick rate) followed by one input record per tick. Playback feeds those records to the sim instead of the keyboard.

**Rewind.** Keyframe the sim state every N ticks (a straight arena copy) and keep the input log between keyframes. To rewind to tick T, restore the nearest earlier keyframe and re-simulate forward.

**Benchmark harness.** A headless binary that replays a demo through the PPM/null backend and reports per-frame data.

| Counter | Meaning |
| --- | --- |
| ms/frame, cycles/frame | Headline speed (via `rdtsc` or `clock_gettime`) |
| cycles/pixel | Speed independent of resolution |
| front-end vs back-end time | Where the frame goes |
| sectors visited, portals crossed | Traversal cost |
| columns drawn, overdraw ratio | Raster efficiency |
| command buffer size | Memory and bandwidth load |
| voxel bricks visited per ray | Voxel cost |
| impostor cache hit rate | Cache effectiveness |
| CPU vs CUDA time per stage | Backend comparison (Section 10) |

**Regression workflow.**

1. Run the benchmark demo set after every change; store results as CSV with the commit hash.
2. Compare frame hashes against golden files: any visual change must be intentional.
3. Fail CI if median frame time regresses by more than a set threshold (start at 5%).
4. Report median, p95, and p99 frame times, not only averages, and run enough frames to give stable medians.

**Backend equivalence.** The CPU back-end is the reference. CUDA output is checked against it per frame: exact match where arithmetic allows, otherwise a small per-pixel tolerance with a count of differing pixels.

## 12. Repository layout, build, and testing

The repo mirrors the module boundaries, so dependencies flow downward only and the GPU code is isolated.

```
engine/
  include/engine.h        # only public header
  src/
    core/                 # math, arenas, LUTs, log, rng
    map/                  # sector/wall tables, loader, portal graph, PVS
    sim/                  # fixed-timestep tick, entities, doors, demo/rewind
    services/             # light flood, sound flood, AI perception
    voxel/                # bricks, DDA, models, impostor cache, deformation
    render/
      frontend/           # traversal, clipping, command emission
      cmdbuf/             # command format, sort, tile binning
      backend_cpu/        # strip rasterizer, SIMD span fills
      backend_cuda/       # .cu kernels + host glue (optional)
      post/               # colormap, dither fog, present
    platform/             # sdl2, ppm, null
    debug/                # automap, clip viewer, counters
  tools/                  # map compiler, vox converter, bench runner
  tests/                  # unit, golden-frame, determinism
  demos/ maps/ docs/
```

**Build.** CMake or plain Make with a single option set: `ENGINE_BACKEND_CUDA=ON|OFF`, `ENGINE_FIXED_POINT=ON|OFF`, `ENGINE_SIMD=ON|OFF`, and a `Release`, `Profile`, or `Debug` mode. The core compiles as C99 with `-Wall -Wextra -Werror`; CUDA code lives only under `backend_cuda/` and never leaks into core headers.

**Testing.**

- **Unit tests** for math, arenas, portal graph, span editing, and brick DDA (against a brute-force voxel march).
- **Golden-frame tests**: replay a short demo, hash the framebuffer, compare to a stored hash.
- **Determinism tests**: run a demo twice, and run with a keyframe-and-resume, and require identical hashes.
- **Fuzzing** of the map loader and span-edit operations, with sanitizers (`-fsanitize=address,undefined`) in debug builds.
- **Backend equivalence tests** between CPU and CUDA (Section 10).

**Documentation deliverables.** README with the architecture diagram, a before/after performance table, a short demo video, and a `docs/` folder holding this document and the feature log.

## 13. Roadmap

Build in seven phases, and treat a phase as done only when its gate passes with a recorded measurement.

| Phase | What gets built | Gate (exit criterion) |
| --- | --- | --- |
| 1. Foundations and Wolf3D raycaster | Framebuffer, palette, PPM and SDL output, arenas, DDA grid raycaster, floors, colormap lighting | Baseline benchmark v0 recorded; golden frame hash checked in CI |
| 2. Sector and portal renderer | Map format and loader, automap, portal traversal with clip arrays, visplanes, sprites | Golden frames for test maps; no clip-buffer artifacts in the viewer |
| 3. Deferred back-end | Command buffer, backend interface, CPU strip rasterizer, thread pool, SIMD span fills | Frames identical to Phase 2 by hash; speedup vs thread count measured |
| 4. Voxels | Brick DDA, voxel entities, impostor cache, volumetric wall columns, span-edit deformation | Voxel benchmark scene runs; cache hit rate and cost per feature logged |
| 5. Graph services and non-Euclidean portals | PVS bitsets, light and sound floods, AI perception, portal transforms, rewind | Loop and doubled-room test maps pass; PVS gain measured |
| 6. CUDA backend | Interface conformance, wall and convert kernels, planes, voxels, streams, profiling | CPU/CUDA equivalence tests pass; crossover resolution published |
| 7. Polish and publish | README with diagrams, results table, demo video, cleaned docs; optional Mode B experiment | Fresh-clone build and benchmark reproduce the published numbers |

**How to use it**

- Phases 1 to 3 are the minimum portfolio piece: a working, benchmarked, documented renderer with a clean backend seam.
- Phases 4 to 6 are the differentiators; stop after any phase that leaves a complete, documented result rather than starting the next half-finished.
- Set dates only after Phase 1 gives a real baseline and a sense of your pace.
- Each phase adds a line to the feature log: what was added, what it cost or gained in ms/frame, and how it was measured.

## 14. Risks, open questions, and stretch goals

The largest risk is scope: finishing a smaller engine with measured results beats an ambitious one that stalls.

| Risk | Impact | Mitigation |
| --- | --- | --- |
| Scope creep across novelty features | High | Phase gates (Section 13); each feature needs a measured cost before the next starts |
| Clip-buffer and portal recursion bugs | High | Clip-buffer visualizer and automap from Phase 3; golden-frame tests |
| Non-Euclidean maps break PVS and sorting | Medium | PVS only on Euclidean subgraph; per-visit command ordering; depth and visit caps |
| CUDA backend slower than CPU at low resolution | Medium | Expected at 320x200; measure honestly, report crossover resolution, keep CPU as default |
| Float non-determinism across compilers | Medium | Compare hashes within one build config; optional fixed-point build |
| Voxel/wall depth inconsistencies | Medium | Single per-column depth buffer shared by all draw types |
| Yaw-shift reuse artifacts | Low | Experimental flag, explicit error metric, periodic full refresh |

**Open questions**

- Target resolution and reference machine for published numbers (suggest 640x400 plus a 1280x800 CUDA test).
- Float-only, or ship both float and fixed-point builds from the start?
- Texture and asset source: custom, or compatible with an existing WAD or `.vox` set?
- Thread pool: hand-written with pthreads, or OpenMP for the first pass?

**Stretch goals**

- Hybrid voxel pockets inside sectors.
- Dynamic 2.5D shadow casting from span data.
- Blue-noise dithered fog and transparency.
- Hot-reloadable game module.
- BSP builder as an optional alternative to portal traversal.
- Yaw-shift frame reuse.
