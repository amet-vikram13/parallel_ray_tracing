#!/usr/bin/env python3
"""
Benchmark Visualization for Monte Carlo Path Tracer
Reads benchmark_results.csv and produces comparison plots.

Usage:
    python3 plot_benchmarks.py <path/to/benchmark_results.csv>

Outputs PNG plots alongside the CSV file.
"""

import sys
import os
import csv
from collections import defaultdict

# ---------------------------------------------------------------------------
# Try importing matplotlib; fall back to a text-only summary if unavailable.
# ---------------------------------------------------------------------------
try:
    import matplotlib
    matplotlib.use("Agg")  # non-interactive backend
    import matplotlib.pyplot as plt
    import matplotlib.ticker as ticker
    HAS_MPL = True
except ImportError:
    HAS_MPL = False
    print("[warn] matplotlib not found — generating text summary only.")
    print("       Install with:  pip3 install matplotlib\n")


# ---------------------------------------------------------------------------
# Colour & style constants
# ---------------------------------------------------------------------------
BVH_COLORS = {"none": "#e74c3c", "sah": "#2ecc71", "lbvh": "#3498db"}
BVH_LABELS = {"none": "Brute-force", "sah": "SAH-BVH (CPU)", "lbvh": "LBVH (GPU)"}
DEVICE_MARKERS = {"cpu": "o", "gpu": "s"}


def safe_float(v):
    try:
        return float(v)
    except (ValueError, TypeError):
        return None


# ---------------------------------------------------------------------------
# Load CSV
# ---------------------------------------------------------------------------
def load_csv(path):
    rows = []
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        for r in reader:
            r["width"] = int(r["width"])
            r["height"] = int(r["height"])
            r["spp"] = int(r["spp"])
            r["render_ms"] = safe_float(r["render_ms"])
            r["build_ms"] = safe_float(r["build_ms"])
            r["mrays_per_sec"] = safe_float(r["mrays_per_sec"])
            r["fps"] = safe_float(r["fps"])
            r["pixels"] = r["width"] * r["height"]
            rows.append(r)
    return rows


# ---------------------------------------------------------------------------
# Filter helpers
# ---------------------------------------------------------------------------
def filt(rows, **kw):
    out = []
    for r in rows:
        if all(r.get(k) == v for k, v in kw.items()):
            out.append(r)
    return out


# ---------------------------------------------------------------------------
# 1. Grouped bar chart: Render time — Device x BVH (pathtracing, fixed res/spp)
# ---------------------------------------------------------------------------
def plot_render_time_comparison(rows, out_dir):
    sub = [r for r in rows if r["mode"] == "pathtracing"
           and r["width"] == 800 and r["height"] == 600 and r["spp"] == 16]
    if not sub:
        return

    # Group by (scene, device)
    groups = defaultdict(dict)
    for r in sub:
        key = f"{r['scene']} / {r['device'].upper()}"
        groups[key][r["bvh"]] = r["render_ms"]

    fig, ax = plt.subplots(figsize=(10, 5))
    labels = list(groups.keys())
    x = range(len(labels))
    bar_w = 0.25

    for i, bvh in enumerate(["none", "sah", "lbvh"]):
        vals = [groups[l].get(bvh) or 0 for l in labels]
        bars = ax.bar([xi + i * bar_w for xi in x], vals, bar_w,
                      label=BVH_LABELS[bvh], color=BVH_COLORS[bvh])
        for bar, v in zip(bars, vals):
            if v:
                ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height(),
                        f"{v:.0f}", ha="center", va="bottom", fontsize=7)

    ax.set_ylabel("Render Time (ms)")
    ax.set_title("Render Time Comparison — Path Tracing (800x600, 16 SPP)")
    ax.set_xticks([xi + bar_w for xi in x])
    ax.set_xticklabels(labels)
    ax.legend()
    ax.grid(axis="y", alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, "01_render_time_comparison.png"), dpi=150)
    plt.close(fig)
    print("  -> 01_render_time_comparison.png")


# ---------------------------------------------------------------------------
# 2. BVH Build Time comparison (bar chart)
# ---------------------------------------------------------------------------
def plot_build_time(rows, out_dir):
    sub = [r for r in rows if r["mode"] == "pathtracing"
           and r["bvh"] != "none"
           and r["width"] == 800 and r["height"] == 600 and r["spp"] == 16]
    if not sub:
        return

    groups = defaultdict(dict)
    for r in sub:
        key = f"{r['scene']} / {r['device'].upper()}"
        groups[key][r["bvh"]] = r["build_ms"]

    fig, ax = plt.subplots(figsize=(8, 4))
    labels = list(groups.keys())
    x = range(len(labels))
    bar_w = 0.35

    for i, bvh in enumerate(["sah", "lbvh"]):
        vals = [groups[l].get(bvh) or 0 for l in labels]
        bars = ax.bar([xi + i * bar_w for xi in x], vals, bar_w,
                      label=BVH_LABELS[bvh], color=BVH_COLORS[bvh])
        for bar, v in zip(bars, vals):
            if v:
                ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height(),
                        f"{v:.3f}", ha="center", va="bottom", fontsize=8)

    ax.set_ylabel("Build Time (ms)")
    ax.set_title("BVH Construction Time — SAH vs LBVH")
    ax.set_xticks([xi + bar_w / 2 for xi in x])
    ax.set_xticklabels(labels)
    ax.legend()
    ax.grid(axis="y", alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, "02_build_time_comparison.png"), dpi=150)
    plt.close(fig)
    print("  -> 02_build_time_comparison.png")


# ---------------------------------------------------------------------------
# 3. Throughput (Mrays/s) — Resolution sweep
# ---------------------------------------------------------------------------
def plot_resolution_sweep(rows, out_dir):
    sub = [r for r in rows if r["mode"] == "pathtracing"
           and r["device"] == "gpu" and r["scene"] == "cornell"
           and r["spp"] == 16 and r["mrays_per_sec"] is not None]
    if not sub:
        return

    fig, ax = plt.subplots(figsize=(8, 5))
    for bvh in ["none", "sah", "lbvh"]:
        pts = sorted([r for r in sub if r["bvh"] == bvh], key=lambda r: r["pixels"])
        if not pts:
            continue
        xs = [r["pixels"] for r in pts]
        ys = [r["mrays_per_sec"] for r in pts]
        xlabels = [f"{r['width']}x{r['height']}" for r in pts]
        ax.plot(xs, ys, marker="o", label=BVH_LABELS[bvh], color=BVH_COLORS[bvh], linewidth=2)

    ax.set_xlabel("Resolution (pixels)")
    ax.set_ylabel("Throughput (Mrays/sec)")
    ax.set_title("GPU Throughput vs Resolution (cornell, 16 SPP)")
    ax.legend()
    ax.grid(alpha=0.3)
    ax.xaxis.set_major_formatter(ticker.FuncFormatter(lambda x, _: f"{x / 1e6:.1f}M"))
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, "03_resolution_sweep.png"), dpi=150)
    plt.close(fig)
    print("  -> 03_resolution_sweep.png")


# ---------------------------------------------------------------------------
# 4. Render time vs SPP sweep
# ---------------------------------------------------------------------------
def plot_spp_sweep(rows, out_dir):
    sub = [r for r in rows if r["mode"] == "pathtracing"
           and r["device"] == "gpu" and r["scene"] == "cornell"
           and r["width"] == 800 and r["height"] == 600
           and r["render_ms"] is not None]
    if not sub:
        return

    fig, ax = plt.subplots(figsize=(8, 5))
    for bvh in ["none", "sah", "lbvh"]:
        pts = sorted([r for r in sub if r["bvh"] == bvh], key=lambda r: r["spp"])
        if not pts:
            continue
        xs = [r["spp"] for r in pts]
        ys = [r["render_ms"] for r in pts]
        ax.plot(xs, ys, marker="s", label=BVH_LABELS[bvh], color=BVH_COLORS[bvh], linewidth=2)

    ax.set_xlabel("Samples Per Pixel (SPP)")
    ax.set_ylabel("Render Time (ms)")
    ax.set_title("GPU Render Time vs SPP (cornell, 800x600)")
    ax.legend()
    ax.grid(alpha=0.3)
    ax.set_xscale("log", base=2)
    ax.xaxis.set_major_formatter(ticker.ScalarFormatter())
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, "04_spp_sweep.png"), dpi=150)
    plt.close(fig)
    print("  -> 04_spp_sweep.png")


# ---------------------------------------------------------------------------
# 5. Speedup chart: BVH render time / brute-force render time
# ---------------------------------------------------------------------------
def plot_speedup(rows, out_dir):
    sub = [r for r in rows if r["mode"] == "pathtracing"
           and r["width"] == 800 and r["height"] == 600 and r["spp"] == 16
           and r["render_ms"] is not None]
    if not sub:
        return

    # Build baseline lookup: (scene, device) -> brute-force render_ms
    baselines = {}
    for r in sub:
        if r["bvh"] == "none":
            baselines[(r["scene"], r["device"])] = r["render_ms"]

    # Build speedup lookup: label -> {bvh -> speedup}
    speedups = defaultdict(dict)
    for r in sub:
        if r["bvh"] == "none" or not r["render_ms"]:
            continue
        key = (r["scene"], r["device"])
        base = baselines.get(key)
        if not base or base == 0:
            continue
        label = f"{r['scene']} / {r['device'].upper()}"
        speedups[label][r["bvh"]] = base / r["render_ms"]

    labels = sorted(speedups.keys())
    if not labels:
        return

    fig, ax = plt.subplots(figsize=(8, 4))
    x = range(len(labels))
    bar_w = 0.35
    sah_vals = [speedups[l].get("sah", 0) for l in labels]
    lbvh_vals = [speedups[l].get("lbvh", 0) for l in labels]

    ax.bar([xi for xi in x], sah_vals, bar_w,
           label=BVH_LABELS["sah"], color=BVH_COLORS["sah"])
    ax.bar([xi + bar_w for xi in x], lbvh_vals, bar_w,
           label=BVH_LABELS["lbvh"], color=BVH_COLORS["lbvh"])

    ax.axhline(y=1.0, color="gray", linestyle="--", linewidth=0.8, label="Baseline (brute-force)")
    ax.set_ylabel("Speedup (x)")
    ax.set_title("BVH Speedup over Brute-Force (800x600, 16 SPP)")
    ax.set_xticks([xi + bar_w / 2 for xi in x])
    ax.set_xticklabels(labels)
    ax.legend()
    ax.grid(axis="y", alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, "05_speedup.png"), dpi=150)
    plt.close(fig)
    print("  -> 05_speedup.png")


# ---------------------------------------------------------------------------
# 6. FPS comparison (phong mode — single-frame latency)
# ---------------------------------------------------------------------------
def plot_phong_fps(rows, out_dir):
    sub = [r for r in rows if r["mode"] == "phong" and r["fps"] is not None]
    if not sub:
        return

    groups = defaultdict(dict)
    for r in sub:
        key = f"{r['scene']} / {r['device'].upper()}"
        groups[key][r["bvh"]] = r["fps"]

    fig, ax = plt.subplots(figsize=(10, 5))
    labels = list(groups.keys())
    x = range(len(labels))
    bar_w = 0.25

    for i, bvh in enumerate(["none", "sah", "lbvh"]):
        vals = [groups[l].get(bvh) or 0 for l in labels]
        bars = ax.bar([xi + i * bar_w for xi in x], vals, bar_w,
                      label=BVH_LABELS[bvh], color=BVH_COLORS[bvh])
        for bar, v in zip(bars, vals):
            if v:
                ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height(),
                        f"{v:.1f}", ha="center", va="bottom", fontsize=7)

    ax.set_ylabel("Frames/sec")
    ax.set_title("Phong Shading FPS Comparison")
    ax.set_xticks([xi + bar_w for xi in x])
    ax.set_xticklabels(labels)
    ax.legend()
    ax.grid(axis="y", alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, "06_phong_fps.png"), dpi=150)
    plt.close(fig)
    print("  -> 06_phong_fps.png")


# ---------------------------------------------------------------------------
# Text summary (always printed, even without matplotlib)
# ---------------------------------------------------------------------------
def print_summary(rows):
    print("\n" + "=" * 70)
    print(" BENCHMARK SUMMARY")
    print("=" * 70)

    # Path tracing, 800x600, 16 spp
    sub = [r for r in rows if r["mode"] == "pathtracing"
           and r["width"] == 800 and r["height"] == 600 and r["spp"] == 16]

    if sub:
        print(f"\n{'Device':<6} {'BVH':<10} {'Scene':<10} {'Render(ms)':>12} "
              f"{'Build(ms)':>10} {'Mrays/s':>10} {'FPS':>8}")
        print("-" * 70)
        for r in sorted(sub, key=lambda x: (x["scene"], x["device"], x["bvh"])):
            render = f"{r['render_ms']:.1f}" if r["render_ms"] else "N/A"
            build = f"{r['build_ms']:.3f}" if r["build_ms"] else "N/A"
            mrays = f"{r['mrays_per_sec']:.2f}" if r["mrays_per_sec"] else "N/A"
            fps = f"{r['fps']:.2f}" if r["fps"] else "N/A"
            print(f"{r['device'].upper():<6} {r['bvh']:<10} {r['scene']:<10} "
                  f"{render:>12} {build:>10} {mrays:>10} {fps:>8}")

    # Speedups
    baselines = {}
    for r in sub:
        if r["bvh"] == "none" and r["render_ms"]:
            baselines[(r["scene"], r["device"])] = r["render_ms"]

    accel = [r for r in sub if r["bvh"] != "none" and r["render_ms"]]
    if accel and baselines:
        print(f"\n{'Device':<6} {'BVH':<10} {'Scene':<10} {'Speedup':>10}")
        print("-" * 40)
        for r in sorted(accel, key=lambda x: (x["scene"], x["device"], x["bvh"])):
            base = baselines.get((r["scene"], r["device"]))
            if base and base > 0:
                sp = base / r["render_ms"]
                print(f"{r['device'].upper():<6} {r['bvh']:<10} {r['scene']:<10} {sp:>9.2f}x")

    print("")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <benchmark_results.csv>")
        sys.exit(1)

    csv_path = sys.argv[1]
    if not os.path.isfile(csv_path):
        print(f"Error: {csv_path} not found")
        sys.exit(1)

    out_dir = os.path.dirname(os.path.abspath(csv_path))
    plots_dir = os.path.join(out_dir, "plots")
    os.makedirs(plots_dir, exist_ok=True)

    print(f"Loading {csv_path} ...")
    rows = load_csv(csv_path)
    print(f"  {len(rows)} data points loaded.\n")

    print_summary(rows)

    if HAS_MPL:
        print("Generating plots ...")
        plot_render_time_comparison(rows, plots_dir)
        plot_build_time(rows, plots_dir)
        plot_resolution_sweep(rows, plots_dir)
        plot_spp_sweep(rows, plots_dir)
        plot_speedup(rows, plots_dir)
        plot_phong_fps(rows, plots_dir)
        print(f"\nAll plots saved to: {plots_dir}/")
    else:
        print("Skipping plot generation (install matplotlib).")


if __name__ == "__main__":
    main()
