#!/usr/bin/env python3
"""
visualize.py  –  Cache Simulator Results Visualizer
====================================================
Reads CSV output from cache_sim and generates a comprehensive
multi-panel performance report as a high-resolution PNG.

Usage:
    python3 visualize.py                        # auto-discovers results/
    python3 visualize.py --csv results/foo.csv  # specific file
    python3 visualize.py --sweep                # cache-size sweep mode
    python3 visualize.py --all                  # generate all chart types

Requires: matplotlib, numpy  (pip install matplotlib numpy)
"""

import argparse
import csv
import os
import sys
import subprocess
import tempfile
from pathlib import Path
from typing import Optional

if sys.platform == "win32":
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    if hasattr(sys.stderr, "reconfigure"):
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")

import numpy as np
import matplotlib
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import matplotlib.gridspec as gridspec
from matplotlib.ticker import FuncFormatter

# ─────────────────────────────────────────────────────────────
#  Style constants  (NVIDIA-inspired dark theme)
# ─────────────────────────────────────────────────────────────
DARK_BG   = "#0d0d0d"
PANEL_BG  = "#151515"
GRID_CLR  = "#2a2a2a"
TEXT_CLR  = "#e8e8e8"
DIM_CLR   = "#666666"
ACCENT    = "#76b900"      # NVIDIA green
ACCENT2   = "#00b4d8"      # cyan
ACCENT3   = "#f77f00"      # orange
ACCENT4   = "#e63946"      # red

POLICY_COLORS = {
    "LRU":    ACCENT,
    "FIFO":   ACCENT2,
    "RANDOM": ACCENT3,
}
LEVEL_COLORS  = {"L1": ACCENT, "L2": ACCENT2}

matplotlib.rcParams.update({
    "figure.facecolor":  DARK_BG,
    "axes.facecolor":    PANEL_BG,
    "axes.edgecolor":    GRID_CLR,
    "axes.labelcolor":   TEXT_CLR,
    "axes.titlecolor":   TEXT_CLR,
    "text.color":        TEXT_CLR,
    "xtick.color":       DIM_CLR,
    "ytick.color":       DIM_CLR,
    "grid.color":        GRID_CLR,
    "grid.linewidth":    0.5,
    "legend.facecolor":  "#1e1e1e",
    "legend.edgecolor":  GRID_CLR,
    "font.family":       "monospace",
    "axes.spines.top":   False,
    "axes.spines.right": False,
})

# ─────────────────────────────────────────────────────────────
#  CSV loading
# ─────────────────────────────────────────────────────────────
def load_csv(path: str) -> list[dict]:
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        rows = []
        for row in reader:
            cast = {}
            for k, v in row.items():
                try:
                    cast[k] = float(v)
                except ValueError:
                    cast[k] = v
            rows.append(cast)
    return rows


def find_latest_csv(results_dir: str = "results") -> Optional[str]:
    """Return the most recently modified CSV in results/."""
    p = Path(results_dir)
    if not p.exists():
        return None
    csvs = sorted(p.glob("*.csv"), key=lambda f: f.stat().st_mtime, reverse=True)
    return str(csvs[0]) if csvs else None


# ─────────────────────────────────────────────────────────────
#  Chart helpers
# ─────────────────────────────────────────────────────────────
def pct_fmt(x, _):
    return f"{x:.1f}%"

def comma_fmt(x, _):
    return f"{int(x):,}"


def bar_group(ax, labels, values_dict, ylabel, title, ylim=None,
              fmt=None, value_labels=True):
    """Grouped bar chart."""
    n_groups = len(labels)
    n_bars   = len(values_dict)
    width    = 0.7 / n_bars
    x        = np.arange(n_groups)

    for i, (name, vals) in enumerate(values_dict.items()):
        offset = (i - n_bars / 2 + 0.5) * width
        bars = ax.bar(x + offset, vals, width,
                      color=POLICY_COLORS.get(name, ACCENT),
                      alpha=0.88, label=name,
                      zorder=3)
        if value_labels:
            for bar, v in zip(bars, vals):
                label = (fmt(v) if fmt else f"{v:.2f}")
                ax.text(bar.get_x() + bar.get_width() / 2,
                        bar.get_height() + (ylim[1] * 0.01 if ylim else 0.5),
                        label, ha="center", va="bottom",
                        fontsize=7.5, color=TEXT_CLR)

    ax.set_xticks(x)
    ax.set_xticklabels(labels, fontsize=9)
    ax.set_ylabel(ylabel, fontsize=9)
    ax.set_title(title, fontsize=11, fontweight="bold", pad=8)
    ax.grid(axis="y", zorder=0)
    ax.legend(fontsize=8)
    if ylim:
        ax.set_ylim(*ylim)


def horizontal_bar(ax, labels, values, colors, title, xlabel):
    y = np.arange(len(labels))
    bars = ax.barh(y, values, color=colors, alpha=0.88, zorder=3)
    ax.set_yticks(y)
    ax.set_yticklabels(labels, fontsize=9)
    ax.set_xlabel(xlabel, fontsize=9)
    ax.set_title(title, fontsize=11, fontweight="bold", pad=8)
    ax.grid(axis="x", zorder=0)
    for bar, v in zip(bars, values):
        ax.text(v + max(values) * 0.01, bar.get_y() + bar.get_height() / 2,
                f"{v:,.0f}", va="center", fontsize=8.5, color=TEXT_CLR)


# ─────────────────────────────────────────────────────────────
#  Main report figure
# ─────────────────────────────────────────────────────────────
def generate_report(data: list[dict], output_path: str, title_suffix: str = ""):
    policies = [r["policy"] for r in data]

    # Extract metrics
    l1_hit   = [r["l1_hit_rate"]   for r in data]
    l2_hit   = [r["l2_hit_rate"]   for r in data]
    l1_miss  = [r["l1_miss_rate"]  for r in data]
    l2_miss  = [r["l2_miss_rate"]  for r in data]
    amat     = [r["amat"]          for r in data]
    cycles   = [r["total_cycles"]  for r in data]
    stall    = [r["stall_cycles"]  for r in data]
    l1_rh    = [r["l1_read_hits"]  for r in data]
    l1_rm    = [r["l1_read_misses"]for r in data]
    l1_wh    = [r["l1_write_hits"] for r in data]
    l1_wm    = [r["l1_write_misses"]for r in data]
    runtime  = [r.get("runtime_ms", 0) for r in data]

    fig = plt.figure(figsize=(18, 14), dpi=130)
    fig.patch.set_facecolor(DARK_BG)

    # Title
    fig.text(0.5, 0.97,
             f"Cache Simulator  ─  Performance Report{title_suffix}",
             ha="center", va="top", fontsize=16, fontweight="bold",
             color=ACCENT, fontfamily="monospace")
    fig.text(0.5, 0.945,
             "CPU  →  L1 Cache  →  L2 Cache  →  Main Memory  |  C++17",
             ha="center", va="top", fontsize=9, color=DIM_CLR)

    gs = gridspec.GridSpec(3, 3, figure=fig,
                           left=0.06, right=0.97,
                           top=0.92,  bottom=0.06,
                           hspace=0.52, wspace=0.38)

    # ── 1. L1 & L2 Hit Rate grouped bar ─────────────────────
    ax1 = fig.add_subplot(gs[0, 0])
    bar_group(ax1, policies,
              {"L1": l1_hit, "L2": l2_hit},
              "Hit Rate (%)", "L1 vs L2 Hit Rate",
              ylim=(0, 115), fmt=lambda v: f"{v:.1f}%")
    ax1.yaxis.set_major_formatter(FuncFormatter(pct_fmt))

    # ── 2. AMAT per policy ───────────────────────────────────
    ax2 = fig.add_subplot(gs[0, 1])
    colors = [POLICY_COLORS.get(p, ACCENT) for p in policies]
    bars   = ax2.bar(policies, amat, color=colors, alpha=0.88, zorder=3)
    for bar, v in zip(bars, amat):
        ax2.text(bar.get_x() + bar.get_width() / 2,
                 bar.get_height() + 0.3,
                 f"{v:.2f}", ha="center", va="bottom",
                 fontsize=9, color=TEXT_CLR, fontweight="bold")
    ax2.set_ylabel("AMAT (cycles)", fontsize=9)
    ax2.set_title("Average Memory Access Time", fontsize=11,
                  fontweight="bold", pad=8)
    ax2.grid(axis="y", zorder=0)
    ax2.set_ylim(0, max(amat) * 1.2)

    # ── 3. Total vs Stall cycles stacked bar ─────────────────
    ax3 = fig.add_subplot(gs[0, 2])
    x    = np.arange(len(policies))
    useful = [c - s for c, s in zip(cycles, stall)]
    ax3.bar(x, useful, color=ACCENT,  alpha=0.85, label="Useful",  zorder=3)
    ax3.bar(x, stall,  bottom=useful, color=ACCENT4, alpha=0.85,
            label="Stall", zorder=3)
    ax3.set_xticks(x)
    ax3.set_xticklabels(policies, fontsize=9)
    ax3.set_ylabel("Cycles", fontsize=9)
    ax3.set_title("Total vs Stall Cycles", fontsize=11,
                  fontweight="bold", pad=8)
    ax3.yaxis.set_major_formatter(FuncFormatter(comma_fmt))
    ax3.grid(axis="y", zorder=0)
    ax3.legend(fontsize=8)

    # ── 4. L1 Read vs Write breakdown ───────────────────────
    ax4 = fig.add_subplot(gs[1, 0])
    w = 0.35
    x = np.arange(len(policies))
    ax4.bar(x - w/2, l1_rh, w, color=ACCENT,  alpha=0.88, label="Read Hits",   zorder=3)
    ax4.bar(x - w/2, l1_rm, w, bottom=l1_rh,  color=ACCENT4, alpha=0.75,
            label="Read Misses",  zorder=3)
    ax4.bar(x + w/2, l1_wh, w, color=ACCENT2, alpha=0.88, label="Write Hits",  zorder=3)
    ax4.bar(x + w/2, l1_wm, w, bottom=l1_wh,  color=ACCENT3, alpha=0.75,
            label="Write Misses", zorder=3)
    ax4.set_xticks(x)
    ax4.set_xticklabels(policies, fontsize=9)
    ax4.set_ylabel("Accesses", fontsize=9)
    ax4.set_title("L1 Read / Write Breakdown", fontsize=11,
                  fontweight="bold", pad=8)
    ax4.yaxis.set_major_formatter(FuncFormatter(comma_fmt))
    ax4.grid(axis="y", zorder=0)
    ax4.legend(fontsize=7, ncol=2)

    # ── 5. Miss Rate comparison (both levels) ────────────────
    ax5 = fig.add_subplot(gs[1, 1])
    bar_group(ax5, policies,
              {"L1": l1_miss, "L2": l2_miss},
              "Miss Rate (%)", "L1 vs L2 Miss Rate",
              fmt=lambda v: f"{v:.1f}%")
    ax5.yaxis.set_major_formatter(FuncFormatter(pct_fmt))

    # ── 6. Radar / Spider chart ──────────────────────────────
    ax6 = fig.add_subplot(gs[1, 2], polar=True)
    categories = ["L1\nHit%", "L2\nHit%", "Low\nAMAT", "Low\nStall%", "Throughput"]
    N = len(categories)
    angles = np.linspace(0, 2 * np.pi, N, endpoint=False).tolist()
    angles += angles[:1]

    max_cycles = max(cycles) if cycles else 1
    max_amat   = max(amat)   if amat   else 1

    for i, row in enumerate(data):
        vals = [
            row["l1_hit_rate"] / 100,
            row["l2_hit_rate"] / 100,
            1 - row["amat"] / max_amat,
            1 - row["stall_cycles"] / max_cycles,
            1 - row["total_cycles"] / max_cycles,
        ]
        vals += vals[:1]
        pol  = row["policy"]
        col  = POLICY_COLORS.get(pol, ACCENT)
        ax6.plot(angles, vals, color=col, linewidth=2, label=pol)
        ax6.fill(angles, vals, color=col, alpha=0.12)

    ax6.set_xticks(angles[:-1])
    ax6.set_xticklabels(categories, fontsize=8, color=TEXT_CLR)
    ax6.set_yticklabels([])
    ax6.set_facecolor(PANEL_BG)
    ax6.grid(color=GRID_CLR, linewidth=0.6)
    ax6.spines["polar"].set_color(GRID_CLR)
    ax6.set_title("Policy Radar Chart\n(higher = better)", fontsize=11,
                  fontweight="bold", pad=16)
    ax6.legend(loc="upper right", bbox_to_anchor=(1.32, 1.1), fontsize=8)

    # ── 7. Horizontal: Total Cycles ──────────────────────────
    ax7 = fig.add_subplot(gs[2, 0])
    horizontal_bar(ax7, policies, cycles,
                   [POLICY_COLORS.get(p, ACCENT) for p in policies],
                   "Total Execution Cycles", "Cycles")
    ax7.xaxis.set_major_formatter(FuncFormatter(comma_fmt))

    # ── 8. Runtime (benchmark) or AMAT breakdown ─────────────
    ax8 = fig.add_subplot(gs[2, 1])
    if any(r > 0 for r in runtime):
        horizontal_bar(ax8, policies, runtime,
                       [POLICY_COLORS.get(p, ACCENT) for p in policies],
                       "Wall-Clock Runtime", "Milliseconds")
        for spine in ax8.spines.values():
            spine.set_edgecolor(ACCENT2)
    else:
        # Show AMAT breakdown waterfall
        l1_contrib  = [4.0] * len(policies)          # L1 hit latency (fixed)
        l2_contrib  = [a - 4.0 for a in amat]
        ax8.bar(policies, l1_contrib, color=ACCENT,  alpha=0.9, label="L1 contrib", zorder=3)
        ax8.bar(policies, l2_contrib, bottom=l1_contrib,
                color=ACCENT2, alpha=0.9, label="L2+Mem contrib", zorder=3)
        ax8.set_ylabel("AMAT (cycles)", fontsize=9)
        ax8.set_title("AMAT Stack Breakdown", fontsize=11,
                      fontweight="bold", pad=8)
        ax8.grid(axis="y", zorder=0)
        ax8.legend(fontsize=8)

    # ── 9. Summary scorecard table ───────────────────────────
    ax9 = fig.add_subplot(gs[2, 2])
    ax9.axis("off")
    col_labels = ["Metric", *policies]
    rows_data  = [
        ["L1 Hit Rate",  *[f"{v:.2f}%" for v in l1_hit]],
        ["L2 Hit Rate",  *[f"{v:.2f}%" for v in l2_hit]],
        ["AMAT (cyc)",   *[f"{v:.2f}"  for v in amat]],
        ["Total Cycles", *[f"{int(v):,}" for v in cycles]],
        ["Stall Cycles", *[f"{int(v):,}" for v in stall]],
    ]
    if any(r > 0 for r in runtime):
        rows_data.append(["Runtime (ms)", *[f"{v:.2f}" for v in runtime]])

    table = ax9.table(cellText=rows_data, colLabels=col_labels,
                      loc="center", cellLoc="center")
    table.auto_set_font_size(False)
    table.set_fontsize(9)
    table.scale(1, 1.6)

    # Style table
    for (row, col), cell in table.get_celld().items():
        cell.set_facecolor(PANEL_BG if row > 0 else "#1e1e1e")
        cell.set_edgecolor(GRID_CLR)
        cell.set_text_props(color=TEXT_CLR)
        if row == 0:
            cell.set_text_props(color=ACCENT, fontweight="bold")
        if col == 0 and row > 0:
            cell.set_text_props(color=DIM_CLR)

    ax9.set_title("Summary Scorecard", fontsize=11, fontweight="bold", pad=8)

    plt.savefig(output_path, dpi=130, bbox_inches="tight",
                facecolor=DARK_BG, edgecolor="none")
    plt.close()
    print(f"  ✔  Report saved → {output_path}")


# ─────────────────────────────────────────────────────────────
#  Cache-size sweep  (calls cache_sim internally)
# ─────────────────────────────────────────────────────────────
def run_sweep(trace: str, output_path: str,
              binary: str = "./cache_sim",
              base_cfg: str = "configs/default.cfg"):
    """
    Sweep L1 size from 4KB → 512KB and plot hit rate vs size
    for all three replacement policies.
    """
    import re

    l1_sizes = [4096, 8192, 16384, 32768, 65536, 131072, 262144, 524288]

    results = {p: {"sizes": [], "l1_hit": [], "l2_hit": [], "amat": []}
               for p in ("LRU", "FIFO", "RANDOM")}

    # Parse the base config and override L1_SIZE each iteration
    with open(base_cfg) as f:
        base_text = f.read()

    with tempfile.TemporaryDirectory() as tmpdir:
        for size in l1_sizes:
            cfg_path = os.path.join(tmpdir, f"cfg_{size}.cfg")
            text = re.sub(r"^L1_SIZE\s*=.*$", f"L1_SIZE = {size}",
                          base_text, flags=re.MULTILINE)
            text = re.sub(r"^(BENCHMARK|COMPARE)\s*=.*$", "",
                          text, flags=re.MULTILINE)
            with open(cfg_path, "w") as f:
                f.write(text)

            csv_path = os.path.join(tmpdir, f"out_{size}.csv")
            ret = subprocess.run(
                [binary, cfg_path, trace, "--compare"],
                capture_output=True, text=True)
            if ret.returncode != 0:
                print(f"  ✘ cache_sim failed for L1_SIZE={size}", file=sys.stderr)
                continue

            # Find the CSV that was just written
            import glob
            csvs = glob.glob("results/compare_results.csv")
            if not csvs:
                continue
            data = load_csv(csvs[0])
            for row in data:
                p = row["policy"]
                if p in results:
                    results[p]["sizes"].append(size / 1024)
                    results[p]["l1_hit"].append(row["l1_hit_rate"])
                    results[p]["l2_hit"].append(row["l2_hit_rate"])
                    results[p]["amat"].append(row["amat"])

    # Plot
    fig, axes = plt.subplots(1, 3, figsize=(18, 6), dpi=130)
    fig.patch.set_facecolor(DARK_BG)
    fig.suptitle("Cache Size Sweep  ─  L1 Size vs Performance",
                 fontsize=15, fontweight="bold", color=ACCENT, y=1.02)

    metrics = [("l1_hit", "L1 Hit Rate (%)", "L1 Hit Rate vs Cache Size"),
               ("amat",   "AMAT (cycles)",   "AMAT vs Cache Size"),
               ("l2_hit", "L2 Hit Rate (%)", "L2 Hit Rate vs Cache Size")]

    for ax, (key, ylabel, title) in zip(axes, metrics):
        for p, d in results.items():
            if d["sizes"]:
                ax.plot(d["sizes"], d[key],
                        marker="o", linewidth=2.2, markersize=6,
                        color=POLICY_COLORS[p], label=p, zorder=3)
        ax.set_xlabel("L1 Cache Size (KB)", fontsize=9)
        ax.set_ylabel(ylabel, fontsize=9)
        ax.set_title(title, fontsize=11, fontweight="bold", pad=8)
        ax.set_xscale("log", base=2)
        ax.grid(True, which="both", zorder=0)
        ax.legend(fontsize=8)

    plt.tight_layout()
    plt.savefig(output_path, dpi=130, bbox_inches="tight",
                facecolor=DARK_BG, edgecolor="none")
    plt.close()
    print(f"  ✔  Sweep chart saved → {output_path}")


# ─────────────────────────────────────────────────────────────
#  CLI
# ─────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(
        description="Cache Simulator  –  Results Visualizer",
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--csv",    metavar="PATH",
                        help="CSV file to visualize (default: latest in results/)")
    parser.add_argument("--out",    metavar="PATH", default="results/report.png",
                        help="Output image path (default: results/report.png)")
    parser.add_argument("--sweep",  action="store_true",
                        help="Run L1 cache-size sweep and plot")
    parser.add_argument("--trace",  metavar="PATH", default="traces/large.trace",
                        help="Trace file for sweep mode")
    parser.add_argument("--all",    action="store_true",
                        help="Generate all available charts")
    args = parser.parse_args()

    os.makedirs("results", exist_ok=True)

    if args.sweep or args.all:
        print("  Running cache-size sweep (this calls cache_sim multiple times)...")
        run_sweep(args.trace, "results/sweep.png")

    # Main report
    csv_path = args.csv or find_latest_csv()
    if not csv_path:
        print("No CSV found. Run cache_sim first, or pass --csv <path>.",
              file=sys.stderr)
        sys.exit(1)

    print(f"  Loading: {csv_path}")
    data = load_csv(csv_path)
    if not data:
        print("CSV is empty.", file=sys.stderr)
        sys.exit(1)

    # Determine title suffix from filename
    suffix = ""
    if "benchmark" in csv_path:
        suffix = "  [Benchmark Mode]"
    elif "compare" in csv_path:
        suffix = "  [Compare Mode]"

    generate_report(data, args.out, suffix)

    if args.all:
        # Also generate individual charts per CSV in results/
        for csv_file in Path("results").glob("*.csv"):
            data = load_csv(str(csv_file))
            if data:
                out = str(csv_file).replace(".csv", "_chart.png")
                generate_report(data, out,
                                f"  [{csv_file.stem.replace('_', ' ').title()}]")


if __name__ == "__main__":
    main()
