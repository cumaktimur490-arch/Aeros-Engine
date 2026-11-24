#!/usr/bin/env python3
"""
Aeros Engine — Post-Processing & Verification Helper (Python)

Reads the C++17 simulation outputs (*_timeseries.csv and *_summary.json)
produced by aeros/src/phys_sim.cpp (Stage 1 & Stage 2) and prints a formatted
post-processing summary plus an optional lightweight SVG timeline chart.

Note: All physics simulation, force integration, DFT spectral analysis, and
regime classification happen strictly inside the C++17 engine. This script is
solely a cross-language post-processing / visualization helper.
"""

import argparse
import csv
import json
import math
import os
import sys
from pathlib import Path


def load_timeseries(csv_path: Path):
    rows = []
    with csv_path.open("r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            parsed = {}
            for k, v in row.items():
                try:
                    parsed[k] = float(v)
                except (ValueError, TypeError):
                    parsed[k] = v
            rows.append(parsed)
    return rows


def write_svg_chart(rows, out_svg: Path):
    if len(rows) < 2:
        return
    w, h = 920, 380
    pad_l, pad_r, pad_t, pad_b = 64, 24, 34, 44
    pw = w - pad_l - pad_r
    ph = h - pad_t - pad_b

    steps = [r["step"] for r in rows]
    cds = [r["cd"] for r in rows]
    cls = [r["cl"] for r in rows]
    s_min, s_max = min(steps), max(steps)
    if s_max <= s_min:
        s_max = s_min + 1.0

    vals = cds + cls
    v_min, v_max = min(vals), max(vals)
    span = max(1e-3, v_max - v_min)
    v_min -= 0.1 * span
    v_max += 0.1 * span

    def map_x(s):
        return pad_l + ((s - s_min) / (s_max - s_min)) * pw

    def map_y(v):
        return pad_t + ph - ((v - v_min) / (v_max - v_min)) * ph

    cd_pts = " ".join(f"{map_x(s):.1f},{map_y(v):.1f}" for s, v in zip(steps, cds))
    cl_pts = " ".join(f"{map_x(s):.1f},{map_y(v):.1f}" for s, v in zip(steps, cls))
    zero_y = map_y(0.0) if v_min <= 0.0 <= v_max else None

    lines = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">',
        '<rect width="100%" height="100%" fill="#0f141c"/>',
        f'<text x="{pad_l}" y="22" fill="#e6edf3" font-family="sans-serif" font-size="14" font-weight="bold">Aeros Engine C++17 Pre-Simulation Force History (Cd &amp; Cl)</text>',
        f'<rect x="{pad_l}" y="{pad_t}" width="{pw}" height="{ph}" fill="#161d27" stroke="#2d3748"/>',
    ]
    if zero_y is not None:
        lines.append(f'<line x1="{pad_l}" y1="{zero_y:.1f}" x2="{pad_l + pw}" y2="{zero_y:.1f}" stroke="#4a5568" stroke-dasharray="4 4"/>')
    lines.append(f'<polyline fill="none" stroke="#38bdf8" stroke-width="2" points="{cd_pts}"/>')
    lines.append(f'<polyline fill="none" stroke="#f97316" stroke-width="2" points="{cl_pts}"/>')
    lines.append(f'<text x="{pad_l + 10}" y="{pad_t + 18}" fill="#38bdf8" font-family="sans-serif" font-size="12">Cd</text>')
    lines.append(f'<text x="{pad_l + 42}" y="{pad_t + 18}" fill="#f97316" font-family="sans-serif" font-size="12">Cl</text>')
    lines.append(f'<text x="{pad_l}" y="{h - 14}" fill="#94a3b8" font-family="sans-serif" font-size="11">Step {int(s_min)} .. {int(s_max)} | Range [{v_min:.3f}, {v_max:.3f}]</text>')
    lines.append("</svg>\n")
    out_svg.write_text("\n".join(lines), encoding="utf-8")


def main():
    ap = argparse.ArgumentParser(description="Inspect C++17 Aeros-Engine pre-simulation outputs.")
    ap.add_argument("summary_json", help="Path to *_summary.json exported by phys_sim.cpp")
    ap.add_argument("--csv", help="Optional path to *_timeseries.csv (auto-inferred if omitted)")
    ap.add_argument("--svg", help="Optional output SVG path for Cd/Cl force timeline")
    args = ap.parse_args()

    json_path = Path(args.summary_json)
    if not json_path.is_file():
        print(f"ERROR: summary JSON not found: {json_path}", file=sys.stderr)
        return 2

    summary = json.loads(json_path.read_text(encoding="utf-8"))
    csv_path = Path(args.csv) if args.csv else Path(str(json_path).replace("_summary.json", "_timeseries.csv"))

    print(f"=== Aeros Post-Simulation Summary ({json_path.name}) ===")
    print(f"Regime:      {summary.get('regime')} ({summary.get('regime_reason')})")
    print(f"Grid:        {summary.get('grid')} | Fluid cells: {summary.get('fluid_cells')}")
    print(f"Operators:   {summary.get('collision')} + {summary.get('turbulence')} | BFL={summary.get('curved_bfl')}")
    print(f"Performance: {summary.get('steps_simulated')} steps in {summary.get('wall_time_s', 0):.2f}s ({summary.get('mlups', 0):.2f} MLUPS)")
    print(f"Forces:      Cd={summary.get('cd_mean', 0):.4f} +/- {summary.get('cd_std', 0):.4f}, Cl={summary.get('cl_mean', 0):.4f} +/- {summary.get('cl_std', 0):.4f}, L/D={summary.get('ld_mean', 0):.2f}")
    print(f"Wake:        1L={summary.get('wake_deficit_1L', 0)*100:.1f}%, 2L={summary.get('wake_deficit_2L', 0)*100:.1f}%, 4L={summary.get('wake_deficit_4L', 0)*100:.1f}%")
    print(f"Health:      mass_drift={summary.get('mass_drift_rel', 0):.3e}, diverged={summary.get('had_divergence')}")

    if csv_path.is_file():
        rows = load_timeseries(csv_path)
        print(f"Timeseries:  {len(rows)} rows loaded from {csv_path.name}")
        if args.svg:
            out_svg = Path(args.svg)
            write_svg_chart(rows, out_svg)
            print(f"SVG chart:   wrote {out_svg}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
