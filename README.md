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
- **Scenes**: Cornell Box (14 primitives) and test scene (4 spheres)

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
./build/pathtracer [options]
```

| Option | Default | Description |
|--------|---------|-------------|
| `--width <int>` | 800 | Image width |
| `--height <int>` | 600 | Image height |
| `--spp <int>` | 64 | Samples per pixel |
| `--depth <int>` | 10 | Max bounce depth |
| `--cpu` | | Use CPU renderer (OpenMP) |
| `--gpu` | (default) | Use GPU renderer (CUDA) |
| `--phong` | | Blinn-Phong shading only |
| `--scene <name>` | cornell | Scene: `cornell` or `test` |
| `--bvh <type>` | none | BVH: `none`, `sah`, or `lbvh` |
| `--output <file>` | output.ppm | Output PPM file |

### Examples

```bash
# Cornell Box, GPU path tracing with LBVH, 64 SPP
./build/pathtracer --gpu --scene cornell --bvh lbvh --spp 64 --output cornell.ppm

# CPU Phong shading with SAH-BVH
./build/pathtracer --cpu --scene cornell --bvh sah --phong --output phong.ppm

# High-resolution render
./build/pathtracer --gpu --bvh sah --width 1600 --height 1200 --spp 128
```

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
│   ├── main.cpp         # CLI entry point
│   ├── camera/          # Camera implementation
│   ├── core/            # Scene factory (Cornell Box, test)
│   ├── geometry/        # SAH-BVH (CPU), LBVH (CUDA)
│   ├── renderer/        # CPU renderer, GPU renderer, path tracer kernels
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
