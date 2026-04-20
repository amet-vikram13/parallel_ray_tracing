# Monte Carlo Path Tracer — Scene Extension & Modularization

## Project Context

This project implements a Monte Carlo Path Tracer in C++ and CUDA, organized into three incremental phases:

1. **Phong Shading** — Direct illumination with ambient, diffuse (Lambertian), and specular (Blinn-Phong) components, plus hard shadow rays.
2. **Monte Carlo Path Tracing** — Global illumination via recursive/iterative random walks. Supports Lambertian, specular (mirror), and dielectric (glass) materials with Russian Roulette termination and cosine-weighted importance sampling.
3. **Acceleration Structures** — Two BVH implementations: CPU-built SAH-BVH and GPU-built LBVH (Linear BVH with Morton codes).

The renderer runs on both CPU (parallelized via OpenMP) and GPU (CUDA kernels). All geometry types (spheres, triangles) share a unified intersection interface. The BVH is stored in a flat array suitable for both CPU stack-based and GPU stack-based traversal.

---

## Objective

Add a **modular scene system** with a new complex procedural scene to stress-test the acceleration structures. The system must support:

- Selecting any scene by passing a **label via command-line argument**.
- Toggling acceleration structures **on or off** via a separate command-line flag, enabling direct performance comparison.
- Easy addition of **future scenes** with minimal changes to existing code.

---

## Task 1 — Scene Registry / Modularization

Refactor the codebase so that scenes are self-contained, registerable units. The goal is that adding a new scene in the future requires **only two steps**: writing a new scene file and registering a label.

### Requirements

- Create a `SceneRegistry` (or equivalent factory/map pattern) that maps string labels to scene-builder functions.
- Each scene builder populates a shared `Scene` struct (geometry list, camera, lights, material table, any per-frame animation callback).
- The renderer's `main.cpp` (or equivalent entry point) should call the registry lookup and fail gracefully with a printed list of available scene labels if an unknown label is passed.
- Existing scenes (e.g., `cornell_box`, `spheres`) must be registered in the same system without behavioral change.

### Command-Line Interface

```
./renderer --scene <label> [--no-accel] [--spp <N>] [--width <W>] [--height <H>] [--frames <F>]
```

| Flag | Description |
|---|---|
| `--scene <label>` | Required. Selects the scene to render. |
| `--no-accel` | Disables BVH; uses brute-force ray–scene intersection. |
| `--spp <N>` | Samples per pixel (default: 64). |
| `--width / --height` | Output resolution. |
| `--frames <F>` | Number of animation frames to render (default: 1). For animated scenes, each frame advances an internal time parameter `t`. |

---

## Task 2 — New Scene: "Infinite Ocean at Golden Hour"

Implement a procedural, animated ocean scene as a new registered scene with label `ocean`. This scene is **fragment-shader-style**: every pixel's color is determined by a raymarching + procedural evaluation pass rather than rasterizing discrete geometry. It is inspired by the Shadertoy paradigm where per-pixel time-varying evaluation drives the image.

### High-Level Description

A real-time, procedurally generated open ocean rendered from a low camera hovering just above the water surface. The mood is vast and cinematic — like watching the ocean from the deck of a ship at golden hour.

### Required Visual Features

#### 1. Animated Ocean Surface (Gerstner Waves)

- Construct the water surface by **summing multiple Gerstner wave octaves** (minimum 4, ideally 6–8), each with distinct amplitude, frequency, direction, and phase speed.
- Vary drag/steepness per octave to produce organic, non-repeating swell patterns.
- The surface position and normal must be **analytically computed** (not finite-difference approximated) so normals are smooth and correct for lighting.
- Surface height and normal must be a function of `(x, z, t)` where `t` is the per-frame time parameter passed from the renderer.

#### 2. Raymarching Integration

- Cast a primary ray per pixel from the camera. Intersect the ray with the animated water surface using **iterative raymarching** (the surface is not a closed-form primitive).
- March through two depth planes: the **surface plane** and a **subsurface scattering approximation plane** at a fixed depth below.
- Use the analytically computed surface normal at the hit point for all subsequent shading.

#### 3. Fresnel Water Shading

- Implement a **Schlick Fresnel approximation** to blend between:
  - **Subsurface color**: deep teal/navy tones representing transmitted light absorption at depth.
  - **Sky reflection color**: mirror-like reflection of the sky at grazing angles.
- The Fresnel term should use the angle between the view ray and surface normal.

#### 4. Sky & Atmospheric Scattering

- Implement a **fast atmospheric scattering approximation** (no full volumetric integration required):
  - **Rayleigh term**: wavelength-dependent scattering producing cerulean blue at zenith.
  - **Mie term**: forward-scattering haze near the horizon, producing warm amber/orange tints at low sun angles.
- The sun traces a **slow arc** across the sky as `t` advances, cycling through dawn → noon → dusk lighting.
- Render a **specular solar disc** that blooms sharply on both the sky dome and the water surface (a tight Phong-style specular lobe centered on the sun direction).

#### 5. Tone Mapping

- Apply **ACES filmic tone mapping** to the final HDR color before writing to the output buffer.
- The ACES curve provides cinematic contrast and prevents highlight blow-out on the solar specular.

#### 6. Camera

- Default camera: positioned 1–2 units above the water surface (`y ≈ 1.5`), looking toward the horizon.
- Implement **optional mouse/keyboard-driven yaw** for interactive previews if the renderer has a display mode; for offline frame sequences, the camera slowly rotates (yaw advances with `t`) to pan across the horizon.

---

## Task 3 — Acceleration Structure Comparison Harness

When `--frames F` is used with `--scene ocean` (or any scene), the renderer should output:

- Frame images named `frame_XXXX.ppm` (or `.png` if lodepng/stb is available).
- A **timing log** `timing.csv` with columns: `frame, accel_mode, render_time_ms, rays_cast`.

This allows direct wall-clock comparison between `--no-accel` and BVH-accelerated renders for the same scene.

> **Note:** Because the ocean scene uses raymarching rather than discrete BVH-intersectable primitives, the `--no-accel` flag's effect in that scene will be on any supplementary geometry (e.g., a ground plane, decorative floating objects). Document this clearly in a comment at the top of the ocean scene file.

---

## Architecture Guidelines

### File Layout (suggested, adapt to existing conventions)

```
src/
  scenes/
    scene_registry.h        # SceneRegistry class / factory map
    scene_registry.cpp
    scene_cornell_box.cpp   # Existing scene, re-registered
    scene_spheres.cpp       # Existing scene, re-registered
    scene_ocean.cpp         # New scene (this task)
  renderer/
    main.cpp                # CLI parsing, registry lookup, render loop
    ...existing files...
```

### Scene Interface Contract

Each scene file must implement a function matching the signature:

```cpp
void build_scene_<label>(Scene& scene, const RenderConfig& cfg);
```

And register itself in the registry (e.g., via a static initializer or an explicit registration call in `scene_registry.cpp`).

The `Scene` struct must include:
- Geometry list (triangles/spheres).
- Material table.
- Camera parameters.
- Light list.
- An optional `animate(Scene&, float t)` callback pointer for per-frame updates.
- An optional `shade_pixel(Ray, float t) -> vec3` callback pointer for fragment-shader-style scenes like `ocean` that bypass geometry intersection entirely.

### Coding Standards

- No global mutable state outside the `Scene` struct.
- CUDA kernels must accept the `shade_pixel` function pointer (or equivalent device function) via a dispatch mechanism (e.g., a `scene_mode` enum in the kernel launch parameters).
- All new `.cu` / `.cpp` files must compile without warnings under `-Wall -Wextra` and `nvcc` defaults.
- Each scene file should begin with a block comment describing: scene label, visual features, expected render time at 512×512 / 64spp, and any known limitations.

---

## Acceptance Criteria

- [ ] `./renderer --scene ocean --frames 60 --spp 64` produces 60 animated frames showing visible wave motion and sky color shift.
- [ ] `./renderer --scene ocean --no-accel --frames 1` runs without error and produces an identical image to the accelerated version (since ocean uses raymarching, not BVH geometry).
- [ ] `./renderer --scene cornell_box` still produces the correct Cornell Box image (no regression).
- [ ] `./renderer --scene unknown_label` prints the list of valid labels and exits with a non-zero code.
- [ ] `timing.csv` is written whenever `--frames` > 1.
- [ ] The ocean scene visually shows: animated waves, Fresnel water-sky blend, warm/cool atmospheric gradient, and a visible solar disc with specular highlight on the water.

---

## References & Inspiration

- Gerstner wave formulation: Tessendorf, J. (2001). *Simulating Ocean Water.*
- ACES filmic tone mapping: Stephen Hill's ACES fit (`https://github.com/TheRealMJP/BakingLab`).
- Atmospheric scattering approximation: Íñigo Quílez, Shadertoy `Atmospheric Scattering` demos.
- Shadertoy paradigm (per-pixel time-varying evaluation):
  ```glsl
  vec2 uv = fragCoord / iResolution.xy;
  vec3 col = 0.5 + 0.5 * cos(iTime + uv.xyx + vec3(0, 2, 4));
  fragColor = vec4(col, 1.0);
  ```
  The ocean scene generalizes this pattern: `uv` drives ray direction, `iTime` drives wave phase and sun position, and the output is a physically-motivated color rather than a raw cosine.
