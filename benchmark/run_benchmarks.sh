#!/usr/bin/env bash
# =============================================================================
# Benchmark suite for Monte Carlo Path Tracer
# Runs all combinations of device / BVH / scene / mode and logs results.
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${PROJECT_DIR}/build"
BIN="${BUILD_DIR}/pathtracer"

# ── Output root ──────────────────────────────────────────────────────────────
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
OUT_ROOT="${SCRIPT_DIR}/results/${TIMESTAMP}"

# ── Configurable parameters ─────────────────────────────────────────────────
SCENES=("cornell" "test")
DEVICES=("cpu" "gpu")
BVH_TYPES=("none" "sah" "lbvh")
MODES=("pathtracing" "phong")

# Resolution / SPP sweeps for throughput benchmarks
RESOLUTIONS=("400x300" "800x600" "1200x900" "1600x1200")
SPP_VALUES=(1 4 16 64)

# Fixed defaults used when sweeping the other variable
DEFAULT_RES="800x600"
DEFAULT_SPP=16
DEFAULT_DEPTH=10
DEFAULT_SCENE="cornell"

# ── Helpers ──────────────────────────────────────────────────────────────────
build_project() {
    echo "=== Building project ==="
    mkdir -p "$BUILD_DIR"
    cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -5
    cmake --build "$BUILD_DIR" -j "$(nproc 2>/dev/null || sysctl -n hw.ncpu)" 2>&1 | tail -5
    echo ""
}

run_single() {
    local device="$1" bvh="$2" scene="$3" mode="$4" width="$5" height="$6" spp="$7"
    local tag="${device}_${bvh}_${scene}_${mode}_${width}x${height}_spp${spp}"
    local out_dir="${OUT_ROOT}/${device}/${bvh}/${scene}"
    mkdir -p "$out_dir"

    local mode_flag=""
    [[ "$mode" == "phong" ]] && mode_flag="--phong"

    local device_flag="--gpu"
    [[ "$device" == "cpu" ]] && device_flag="--cpu"

    local log_file="${out_dir}/${tag}.log"
    local ppm_file="${out_dir}/${tag}.ppm"

    echo -n "  Running ${tag} ... "

    # Run and capture stdout+stderr
    "$BIN" \
        "$device_flag" \
        --bvh "$bvh" \
        --scene "$scene" \
        $mode_flag \
        --width "$width" --height "$height" \
        --spp "$spp" \
        --depth "$DEFAULT_DEPTH" \
        --output "$ppm_file" \
        > "$log_file" 2>&1

    # Extract key metrics (portable — no grep -P)
    local render_time build_time mrays fps
    render_time=$(sed -n 's/.*\[Render time\] \([0-9.]*\) ms.*/\1/p' "$log_file" | head -1)
    render_time="${render_time:-N/A}"
    build_time=$(sed -n 's/.*Total build time: *\([0-9.]*\) ms.*/\1/p' "$log_file" | head -1)
    [ -z "$build_time" ] && build_time=$(sed -n 's/.*Build time: *\([0-9.]*\) ms.*/\1/p' "$log_file" | head -1)
    build_time="${build_time:-0}"
    mrays=$(sed -n 's/.*Throughput: *\([0-9.]*\) Mrays.*/\1/p' "$log_file" | head -1)
    mrays="${mrays:-N/A}"
    fps=$(sed -n 's/.*Frames\/sec: *\([0-9.]*\).*/\1/p' "$log_file" | head -1)
    fps="${fps:-N/A}"

    echo "render=${render_time}ms  build=${build_time}ms  Mrays/s=${mrays}  FPS=${fps}"

    # Append to master CSV
    echo "${device},${bvh},${scene},${mode},${width},${height},${spp},${render_time},${build_time},${mrays},${fps}" \
        >> "${OUT_ROOT}/benchmark_results.csv"
}

parse_res() {
    # "800x600" -> width height
    echo "${1%%x*} ${1##*x}"
}

# ── Main ─────────────────────────────────────────────────────────────────────
echo "============================================================"
echo " Path Tracer Benchmark Suite"
echo " Results → ${OUT_ROOT}"
echo "============================================================"
echo ""

build_project

mkdir -p "$OUT_ROOT"
echo "device,bvh,scene,mode,width,height,spp,render_ms,build_ms,mrays_per_sec,fps" \
    > "${OUT_ROOT}/benchmark_results.csv"

# ── 1. Full matrix: device x bvh x scene (path tracing, fixed res+spp) ──────
echo "=== Benchmark 1: Device x BVH x Scene (pathtracing, ${DEFAULT_RES}, spp=${DEFAULT_SPP}) ==="
read -r W H <<< "$(parse_res "$DEFAULT_RES")"
for scene in "${SCENES[@]}"; do
    for device in "${DEVICES[@]}"; do
        for bvh in "${BVH_TYPES[@]}"; do
            run_single "$device" "$bvh" "$scene" "pathtracing" "$W" "$H" "$DEFAULT_SPP"
        done
    done
done
echo ""

# ── 2. Phong mode comparison ────────────────────────────────────────────────
echo "=== Benchmark 2: Phong shading (${DEFAULT_RES}) ==="
for scene in "${SCENES[@]}"; do
    for device in "${DEVICES[@]}"; do
        for bvh in "${BVH_TYPES[@]}"; do
            run_single "$device" "$bvh" "$scene" "phong" "$W" "$H" 1
        done
    done
done
echo ""

# ── 3. Resolution sweep (GPU, cornell, pathtracing, fixed spp) ──────────────
echo "=== Benchmark 3: Resolution sweep (GPU, cornell, spp=${DEFAULT_SPP}) ==="
for bvh in "${BVH_TYPES[@]}"; do
    for res in "${RESOLUTIONS[@]}"; do
        read -r RW RH <<< "$(parse_res "$res")"
        run_single "gpu" "$bvh" "$DEFAULT_SCENE" "pathtracing" "$RW" "$RH" "$DEFAULT_SPP"
    done
done
echo ""

# ── 4. SPP sweep (GPU, cornell, pathtracing, fixed res) ─────────────────────
echo "=== Benchmark 4: SPP sweep (GPU, cornell, ${DEFAULT_RES}) ==="
read -r W H <<< "$(parse_res "$DEFAULT_RES")"
for bvh in "${BVH_TYPES[@]}"; do
    for spp in "${SPP_VALUES[@]}"; do
        run_single "gpu" "$bvh" "$DEFAULT_SCENE" "pathtracing" "$W" "$H" "$spp"
    done
done
echo ""

echo "============================================================"
echo " All benchmarks complete."
echo " CSV   → ${OUT_ROOT}/benchmark_results.csv"
echo " PPMs  → ${OUT_ROOT}/<device>/<bvh>/<scene>/"
echo " Logs  → same directory as PPMs"
echo ""
echo " Generate plots:"
echo "   python3 ${SCRIPT_DIR}/plot_benchmarks.py ${OUT_ROOT}/benchmark_results.csv"
echo "============================================================"
