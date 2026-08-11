#!/usr/bin/env python3
"""
Turn LBP proof-check CSVs (from run_sweep.sh lbp) into a single compact PDF.

Layout mirrors make_proof_report.py: one landscape page per lattice
dimension n ∈ {40, 60, 80, 100}, each holding a 2×4 grid of mini-tables
(one per batch size). Rows CPU + GPU-<tpb>, columns MemComm / Comp / E2E.

Usage:
    ./make_lbp_report.py                      # reads results/lbp_*_repeats500.csv
    ./make_lbp_report.py --repeats 100
    ./make_lbp_report.py --dim 60             # single-dimension PDF
"""

import argparse
import csv
import math
import sys
from pathlib import Path

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.backends.backend_pdf import PdfPages
except ImportError:
    sys.exit("matplotlib is required: pip install matplotlib")


PROOF_TYPE_NAME = "LBP"


def fmt_batch(n):
    n = int(n)
    return f"{n // 1024}K" if (n >= 1024 and n % 1024 == 0) else str(n)


def fmt_ms(x):
    try:    return f"{float(x):.3f}"
    except: return str(x)


def load_csv(path):
    p = Path(path)
    if not p.exists():
        return []
    with open(p) as f:
        return list(csv.DictReader(f))


def cpu_ms_by_batch(rows):
    out = {}
    for r in rows:
        try:
            out[int(r["batch"])] = float(r["ms_per_batch"])
        except (KeyError, ValueError):
            continue
    return out


def gpu_by_batch_tpb(rows):
    out = {}
    for r in rows:
        try:
            b   = int(r["batch"])
            t   = int(r["tpb"])
            h2d = float(r["ms_h2d"])
            ker = float(r["ms_kernel"])
            d2h = float(r["ms_d2h"])
        except (KeyError, ValueError):
            continue
        out[(b, t)] = (h2d + d2h, ker, h2d + ker + d2h)
    return out


def draw_mini_table(ax, batch, cpu_ms, gpu_map, tpb_values):
    ax.axis("off")
    ax.set_title(f"batch = {fmt_batch(batch)}",
                 fontsize=10, weight="bold", pad=4, loc="left")

    headers = ["Config", "MemComm", "Comp", "E2E"]
    rows = []
    if cpu_ms is None:
        rows.append(["CPU", "—", "—", "—"])
    else:
        rows.append(["CPU", fmt_ms(0), fmt_ms(cpu_ms), fmt_ms(cpu_ms)])
    for t in tpb_values:
        vals = gpu_map.get((batch, t))
        if vals is None:
            rows.append([f"GPU-{t}", "—", "—", "—"])
        else:
            mem, comp, e2e = vals
            rows.append([f"GPU-{t}", fmt_ms(mem), fmt_ms(comp), fmt_ms(e2e)])

    best_e2e_idx = None
    best_e2e_val = math.inf
    for i, t in enumerate(tpb_values, start=1):
        vals = gpu_map.get((batch, t))
        if vals is not None and vals[2] < best_e2e_val:
            best_e2e_val = vals[2]
            best_e2e_idx = i + 1

    tbl = ax.table(cellText=rows, colLabels=headers, loc="center",
                   cellLoc="center", colLoc="center")
    tbl.auto_set_font_size(False)
    tbl.set_fontsize(8)
    tbl.scale(1.0, 1.25)

    for c in range(len(headers)):
        cell = tbl[(0, c)]
        cell.set_facecolor("#334155")
        cell.set_text_props(color="white", weight="bold")

    for i in range(1, len(rows) + 1):
        for c in range(len(headers)):
            cell = tbl[(i, c)]
            if i == 1:
                cell.set_facecolor("#fef3c7")
            else:
                cell.set_facecolor("#f8fafc" if i % 2 else "#ffffff")

    if best_e2e_idx is not None:
        cell = tbl[(best_e2e_idx, 3)]
        cell.set_facecolor("#bbf7d0")
        cell.set_text_props(weight="bold")


GRID_ROWS = 2
GRID_COLS = 4
TABLES_PER_PAGE = GRID_ROWS * GRID_COLS

def render_all_dim_pdf(pdf_path, dim_data, tpb_values_all, repeats):
    with PdfPages(pdf_path) as pdf:
        for dim, batches, cpu_map, gpu_map in dim_data:
            for page_start in range(0, len(batches), TABLES_PER_PAGE):
                page_batches = batches[page_start:page_start + TABLES_PER_PAGE]
                fig, axes = plt.subplots(GRID_ROWS, GRID_COLS,
                                         figsize=(14, 8.5),
                                         gridspec_kw={"hspace": 0.55,
                                                      "wspace": 0.20})
                fig.suptitle(
                    f"{PROOF_TYPE_NAME} proof-check — CPU vs GPU  "
                    f"(SVP-challenge dim n={dim}, repeats={repeats})",
                    fontsize=13, weight="bold", y=0.97)

                flat = axes.flatten()
                for i, ax in enumerate(flat):
                    if i < len(page_batches):
                        b = page_batches[i]
                        draw_mini_table(ax, b, cpu_map.get(b),
                                        gpu_map, tpb_values_all)
                    else:
                        ax.axis("off")

                fig.text(0.5, 0.02,
                         "MemComm = H2D + D2H (0 for CPU).  "
                         "Comp = lattice muladd + norm.  "
                         "E2E = MemComm + Comp.  "
                         "Green = best GPU E2E for the batch.",
                         ha="center", fontsize=8, style="italic",
                         color="#475569")

                fig.subplots_adjust(left=0.03, right=0.99,
                                    top=0.90, bottom=0.06)
                pdf.savefig(fig)
                plt.close(fig)


def main():
    here = Path(__file__).resolve().parent
    ap = argparse.ArgumentParser()
    ap.add_argument("--repeats", default="500")
    ap.add_argument("--dim", type=int, default=None,
                    help="single dimension to render (default: all present)")
    ap.add_argument("--results-dir", default=None)
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    rdir = Path(args.results_dir) if args.results_dir else here / "results"
    if not rdir.exists():
        sys.exit(f"No results dir at {rdir}. Run ./run_sweep.sh lbp first.")

    if args.dim is not None:
        dims = [args.dim]
    else:
        found = set()
        for p in rdir.glob(f"lbp_*_dim*_repeats{args.repeats}.csv"):
            try:
                stem = p.stem
                d_tok = [t for t in stem.split("_") if t.startswith("dim")][0]
                found.add(int(d_tok[len("dim"):]))
            except (IndexError, ValueError):
                pass
        dims = sorted(found)

    if not dims:
        sys.exit(f"No LBP CSVs found in {rdir} for repeats={args.repeats}.")

    dim_data = []
    tpb_set = set()
    for d in dims:
        cpu_rows = load_csv(rdir / f"lbp_cpu_dim{d}_repeats{args.repeats}.csv")
        gpu_rows = load_csv(rdir / f"lbp_gpu_dim{d}_repeats{args.repeats}.csv")
        if not cpu_rows and not gpu_rows:
            print(f"  skip n={d}: no rows for either side")
            continue
        cpu_map = cpu_ms_by_batch(cpu_rows)
        gpu_map = gpu_by_batch_tpb(gpu_rows)
        batches = sorted({*cpu_map, *{b for (b, _) in gpu_map}})
        dim_data.append((d, batches, cpu_map, gpu_map))
        tpb_set.update(t for (_, t) in gpu_map)

    if not dim_data:
        sys.exit("No data collected.")

    tpb_values_all = sorted(tpb_set)

    if args.out:
        out = Path(args.out)
    elif args.dim is not None:
        out = rdir / f"lbp_report_dim{args.dim}_repeats{args.repeats}.pdf"
    else:
        out = rdir / f"lbp_report_repeats{args.repeats}.pdf"

    render_all_dim_pdf(out, dim_data, tpb_values_all, args.repeats)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
