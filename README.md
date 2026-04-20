# Monte Carlo Path Tracer with BVH Acceleration

A physically-based Monte Carlo path tracer implemented in C++ and CUDA, featuring two BVH acceleration structures (SAH-BVH and LBVH) with comparative benchmarking across CPU and GPU.

## Features

- **Path Tracing**: Monte Carlo integration of the rendering equation with cosine-weighted importance sampling
- **Materials**: Lambertian diffuse, specular mirror (with optional roughness), and dielectric glass (Schlick's Fresnel)
- **Russian Roulette**: Unbiased path termination for bounded recursion
- **BVH Acceleration**:
  - CPU SAH-BVH with 12-bin binned Surface Area Heuristic
  - GPU LBVH via Morton codes, Thrust radix sort, and Karras parallel tree construction
  - Iterative stack-based traversal (64-entry stack) on both CPU and GPU
- **Dual Renderers**: OpenMP CPU renderer and CUDA GPU renderer (16x16 thread blocks, cuRAND XORWOW)
- **Blinn-Phong Mode**: Direct illumination with hard shadows for fast preview
- **Modular Scene Registry**: String-label → builder-function factory; new scenes added by writing one file and registering one label
- **Scenes**:
  - `cornell_box` — Cornell Box (14 primitives)
  - `spheres` — test scene (4 spheres)
  - `ocean` — procedural, animated "Infinite Ocean at Golden Hour" (raymarched Gerstner waves, Fresnel water/sky blend, Rayleigh+Mie sky, sun arc, ACES filmic tone mapping)

## Requirements

- CMake 3.18+
- CUDA Toolkit 12.x
- C++17 compiler with OpenMP support
- GLM (auto-downloaded via FetchContent if not found)
- Python 3 + matplotlib (optional, for benchmark plots)

## Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

## Usage

```bash
./build/pathtracer --scene <label> [options]
```

`--scene` is **required**. Passing an unknown label prints the list of registered scenes and exits with a non-zero status.

| Option | Default | Description |
|--------|---------|-------------|
| `--scene <label>` | *(required)* | Scene label: `cornell_box`, `spheres`, or `ocean` |
| `--no-accel` |  | Disables BVH; uses brute-force ray–scene intersection |
| `--bvh <type>` | `sah` | BVH: `sah`, `lbvh`, or `none` (equivalent to `--no-accel`) |
| `--spp <int>` | 64 | Samples per pixel |
| `--width <int>` | 800 | Image width |
| `--height <int>` | 600 | Image height |
| `--frames <int>` | 1 | Number of animation frames to render (each advances time `t`) |
| `--depth <int>` | 10 | Max bounce depth (path tracing) |
| `--cpu` |  | Use CPU renderer (OpenMP) |
| `--gpu` | (default) | Use GPU renderer (CUDA) |
| `--phong` |  | Blinn-Phong shading only (geometry scenes) |
| `--output <prefix>` | `frame` | Output prefix (`<prefix>.ppm` for single frames, `<prefix>_XXXX.ppm` for sequences) |
| `--help` |  | Print usage and available scene labels |

By default, acceleration is **on** with SAH-BVH. Pass `--no-accel` to force brute-force.

When `--frames > 1`, a `timing.csv` is written with columns: `frame, accel_mode, render_time_ms, rays_cast`. This allows wall-clock comparison between `--no-accel` and BVH-accelerated runs for the same scene.

> **Note on `ocean` and BVH:** The ocean scene is fragment-shader-style — every pixel is evaluated via raymarching a procedural Gerstner height field, **not** by intersecting rays against sphere/triangle primitives. The scene holds zero primitives, so there is nothing for a BVH to accelerate. As a result:
>
> - `--bvh sah`, `--bvh lbvh`, and `--no-accel` all produce the **same output and the same performance** when `--scene ocean`.
> - No BVH is built when rendering the ocean scene (the build step is skipped entirely in `main.cpp`).
> - If the scene is later extended with supplementary BVH-visible geometry (e.g. a ground plane or floating debris), `--bvh` / `--no-accel` will apply to that geometry only — never to the ocean surface itself.
>
> For this reason, the `--bvh` flag is deliberately omitted from the ocean examples below.

### Examples

#### Existing geometry scenes

```bash
# Cornell Box — GPU path tracing, default SAH-BVH, 64 SPP
./build/pathtracer --scene cornell_box --spp 64 --output cornell

# Cornell Box — GPU with LBVH explicitly, 128 SPP
./build/pathtracer --scene cornell_box --bvh lbvh --spp 128 --output cornell_lbvh

# Cornell Box — brute-force (no acceleration) for comparison
./build/pathtracer --scene cornell_box --no-accel --spp 64 --output cornell_brute

# CPU Phong shading with SAH-BVH on the simple sphere scene
./build/pathtracer --cpu --scene spheres --bvh sah --phong --output spheres_phong

# High-resolution Cornell Box render with default SAH
./build/pathtracer --scene cornell_box --width 1600 --height 1200 --spp 128
```

#### Ocean scene (animated)

```bash
# Single-frame preview (GPU)
./build/pathtracer --scene ocean --spp 4 --width 800 --height 600 --output ocean

# 60-frame animation at 64 spp (golden-hour sun arc + wave motion)
./build/pathtracer --scene ocean --frames 60 --spp 64 --output ocean_frame

# Same run on CPU (slower, useful for machines without CUDA)
./build/pathtracer --cpu --scene ocean --frames 60 --spp 1 --output ocean_frame

# Ocean with --no-accel (produces an image identical to the accelerated
# version because the ocean bypasses BVH)
./build/pathtracer --scene ocean --no-accel --frames 1 --output ocean_noaccel
```

`--frames 60` produces `ocean_frame_0000.ppm` … `ocean_frame_0059.ppm` plus `timing.csv`.

#### Acceleration-structure comparison harness

Render the same scene twice — once accelerated, once brute-force — and compare `timing.csv`:

```bash
# Accelerated (SAH by default)
./build/pathtracer --scene cornell_box --frames 10 --spp 32 --output cornell_sah
mv timing.csv timing_sah.csv

# Brute-force
./build/pathtracer --scene cornell_box --no-accel --frames 10 --spp 32 --output cornell_brute
mv timing.csv timing_brute.csv
```

### Adding a new scene

Adding a scene requires only two steps:

1. Create `src/scenes/scene_<label>.cpp` implementing `void build_scene_<label>(Scene& scene, const RenderConfig& cfg)`.
2. Add a line to `register_all_scenes()` in `src/scenes/scene_registry.cpp` and list the new `.cpp` in `CMakeLists.txt`.

## Benchmarking

Run the full benchmark suite (device x BVH x scene x resolution x SPP):

```bash
cd benchmark
bash run_benchmarks.sh
```

Results are saved to `benchmark/results/<timestamp>/benchmark_results.csv`.

Generate comparison plots:

```bash
python3 benchmark/plot_benchmarks.py benchmark/results/<timestamp>/benchmark_results.csv
```

### Key Results (Cornell Box, 800x600, 16 SPP)

| Configuration | Render Time | Throughput | Speedup vs Brute-Force |
|---------------|------------|------------|----------------------|
| CPU, no BVH | 5,276.8 ms | 1.5 Mrays/s | 1.0x |
| CPU, SAH | 1,277.1 ms | 6.0 Mrays/s | 4.1x |
| GPU, no BVH | 40.4 ms | 190.3 Mrays/s | 1.0x |
| GPU, SAH | 16.0 ms | 479.1 Mrays/s | 2.5x |
| GPU, LBVH | 13.7 ms | 560.8 Mrays/s | 2.9x |

## Project Structure

```
├── CMakeLists.txt
├── include/
│   ├── camera/          # Camera ray generation
│   ├── core/            # Scene, lights, common types
│   ├── geometry/        # Ray, sphere, triangle, AABB, BVH nodes, SAH/LBVH builders
│   ├── material/        # Lambertian, specular, dielectric materials
│   ├── renderer/        # CPU/GPU renderers, render config
│   └── utils/           # Image I/O, timer, CUDA utilities
├── src/
│   ├── main.cpp         # CLI entry point + frame loop + timing.csv
│   ├── camera/          # Camera implementation
│   ├── core/            # Scene struct + helpers
│   ├── geometry/        # SAH-BVH (CPU), LBVH (CUDA)
│   ├── renderer/        # CPU renderer, GPU renderer, path tracer kernels
│   ├── scenes/          # Scene registry + per-scene builders (cornell_box, spheres, ocean)
│   └── utils/           # Image writer, timer
├── benchmark/
│   ├── run_benchmarks.sh    # Automated benchmark suite
│   ├── plot_benchmarks.py   # Visualization script
│   └── results/             # Benchmark outputs (CSV, logs, PPMs)
└── project_report/
    └── main.tex             # LaTeX project report
```

## References

- Kajiya, "The Rendering Equation" (SIGGRAPH 1986)
- Karras, "Maximizing Parallelism in the Construction of BVHs, Octrees, and k-d Trees" (HPG 2012)
- Wald, "On Fast Construction of SAH-based Bounding Volume Hierarchies" (RT 2007)
- Moller & Trumbore, "Fast, Minimum Storage Ray-Triangle Intersection" (JGT 1997)
