#!/usr/bin/env python3
"""
Turn the CSVs written by run_sweep.sh into a comparison PDF.

Usage:
    ./make_report.py                          # reads results/{cpu,gpu}_mode2_repeats500.csv
    ./make_report.py --mode 3
    ./make_report.py --repeats 100
    ./make_report.py --cpu path.csv --gpu path.csv --out report.pdf

Output: one table per batch size. Each table has one row per configuration
(CPU, then GPU-<tpb> for every TPB swept) and three timing columns:

    MemoryComm    ms spent moving data host<->device this batch
                  (CPU: 0, GPU: H2D + D2H)
    Computation   ms of actual verify work
                  (CPU: total wall time, GPU: kernel time)
    E2E           MemoryComm + Computation

All numbers are per-batch (already averaged across REPEATS by the sweep).

No pandas dependency — csv module + matplotlib only.
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


# ---------- formatting helpers ----------------------------------------------

def fmt_batch(n):
    """4096 -> '4K', 32768 -> '32K', 128 -> '128'."""
    n = int(n)
    if n >= 1024 and n % 1024 == 0:
        return f"{n // 1024}K"
    return str(n)


def fmt_ms(x):
    """3.14159 -> '3.142', 0 -> '0.000'."""
    try:
        return f"{float(x):.3f}"
    except (TypeError, ValueError):
        return str(x)


# ---------- CSV loading ------------------------------------------------------

def load_csv(path):
    if not path or not Path(path).exists():
        return []
    with open(path) as f:
        return list(csv.DictReader(f))


def cpu_by_batch(rows):
    """batch -> ms_per_batch (Computation for CPU)."""
    out = {}
    for r in rows:
        try:
            out[int(r["batch"])] = float(r["ms_per_batch"])
        except (KeyError, ValueError):
            continue
    return out


def gpu_by_batch_tpb(rows):
    """(batch, tpb) -> (memory_ms, compute_ms, e2e_ms) where
       memory = H2D + D2H, compute = kernel, e2e = sum."""
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


# ---------- one table --------------------------------------------------------

def draw_table(ax, batch, cpu_ms, gpu_map, tpb_values):
    """batch: int; cpu_ms: float or None; gpu_map: {(batch,tpb) -> (mem,comp,e2e)}."""
    ax.axis("off")
    ax.set_title(f"batch size = {fmt_batch(batch)}",
                 fontsize=12, weight="bold", pad=10, loc="left")

    headers = ["Config", "MemoryComm (ms)", "Computation (ms)", "E2E (ms)"]
    rows = []

    # CPU row (MemoryComm = 0 by construction; the data already lives in host RAM)
    if cpu_ms is None:
        rows.append(["CPU", "—", "—", "—"])
    else:
        rows.append(["CPU", fmt_ms(0), fmt_ms(cpu_ms), fmt_ms(cpu_ms)])

    # GPU rows, one per TPB
    for t in tpb_values:
        vals = gpu_map.get((batch, t))
        if vals is None:
            rows.append([f"GPU-{t}", "—", "—", "—"])
        else:
            mem, comp, e2e = vals
            rows.append([f"GPU-{t}", fmt_ms(mem), fmt_ms(comp), fmt_ms(e2e)])

    # Highlight the best (lowest) E2E across GPU rows only — CPU comparison
    # lives in the visual difference between the two blocks.
    best_e2e_idx = None
    best_e2e_val = math.inf
    for i, t in enumerate(tpb_values, start=1):  # +1 because row 0 is CPU
        vals = gpu_map.get((batch, t))
        if vals is not None and vals[2] < best_e2e_val:
            best_e2e_val = vals[2]
            best_e2e_idx = i + 1  # +1 more for the header row

    tbl = ax.table(cellText=rows, colLabels=headers, loc="center",
                   cellLoc="center", colLoc="center")
    tbl.auto_set_font_size(False)
    tbl.set_fontsize(10)
    tbl.scale(1.0, 1.6)

    # Header styling
    for c in range(len(headers)):
        cell = tbl[(0, c)]
        cell.set_facecolor("#334155")
        cell.set_text_props(color="white", weight="bold")

    # Zebra + section separator
    for i in range(1, len(rows) + 1):
        for c in range(len(headers)):
            cell = tbl[(i, c)]
            # CPU row (row 1) gets a distinct tint so the CPU vs GPU boundary
            # is obvious without needing an extra separator column.
            if i == 1:
                cell.set_facecolor("#fef3c7")   # amber-50
            else:
                cell.set_facecolor("#f8fafc" if i % 2 else "#ffffff")

    # Star the best GPU E2E
    if best_e2e_idx is not None:
        cell = tbl[(best_e2e_idx, 3)]
        cell.set_facecolor("#bbf7d0")           # green-200
        cell.set_text_props(weight="bold")


# ---------- pages ------------------------------------------------------------

TABLES_PER_PAGE = 2   # 2 tables stacked per portrait letter page

def render_pdf(pdf_path, batches, cpu_ms_by_batch, gpu_map, tpb_values, mode, repeats):
    """Render one PDF with TABLES_PER_PAGE tables per page. First page carries
    a header line naming the run so the file is self-describing."""
    with PdfPages(pdf_path) as pdf:
        for page_start in range(0, len(batches), TABLES_PER_PAGE):
            page_batches = batches[page_start:page_start + TABLES_PER_PAGE]
            fig, axes = plt.subplots(TABLES_PER_PAGE, 1, figsize=(8.5, 11))
            if TABLES_PER_PAGE == 1:
                axes = [axes]

            # Top-of-page heading only on page 1
            if page_start == 0:
                fig.suptitle(
                    f"Dilithium mode {mode} — CPU vs GPU per batch (repeats={repeats})",
                    fontsize=14, weight="bold", y=0.985)

            for i, ax in enumerate(axes):
                if i < len(page_batches):
                    b = page_batches[i]
                    draw_table(ax, b, cpu_ms_by_batch.get(b), gpu_map, tpb_values)
                else:
                    ax.axis("off")  # blank slot on the last page

            fig.text(0.5, 0.02,
                     "MemoryComm = H2D + D2H (0 for CPU).  "
                     "Computation = verify work.  "
                     "E2E = MemoryComm + Computation.  "
                     "Green = best GPU E2E for the batch.",
                     ha="center", fontsize=8, style="italic", color="#475569")

            pdf.savefig(fig, bbox_inches="tight")
            plt.close(fig)


# ---------- main -------------------------------------------------------------

def main():
    here = Path(__file__).resolve().parent
    ap = argparse.ArgumentParser()
    ap.add_argument("--mode", default="2")
    ap.add_argument("--repeats", default="500")
    ap.add_argument("--cpu", default=None, help="CPU CSV path")
    ap.add_argument("--gpu", default=None, help="GPU CSV path")
    ap.add_argument("--out", default=None, help="Output PDF path")
    args = ap.parse_args()

    cpu_path = args.cpu or here / "results" / f"cpu_mode{args.mode}_repeats{args.repeats}.csv"
    gpu_path = args.gpu or here / "results" / f"gpu_mode{args.mode}_repeats{args.repeats}.csv"
    out_path = args.out or here / "results" / f"report_mode{args.mode}_repeats{args.repeats}.pdf"

    cpu_rows = load_csv(cpu_path)
    gpu_rows = load_csv(gpu_path)
    if not cpu_rows and not gpu_rows:
        sys.exit(f"No CSVs found. Looked at:\n  {cpu_path}\n  {gpu_path}\n"
                 f"Run ./run_sweep.sh first, or pass --cpu / --gpu.")

    cpu_ms_by_batch = cpu_by_batch(cpu_rows)
    gpu_map         = gpu_by_batch_tpb(gpu_rows)

    all_batches = sorted({*cpu_ms_by_batch,
                          *{b for (b, _) in gpu_map}})
    tpb_values  = sorted({t for (_, t) in gpu_map})

    Path(out_path).parent.mkdir(parents=True, exist_ok=True)
    render_pdf(out_path, all_batches, cpu_ms_by_batch, gpu_map, tpb_values,
               args.mode, args.repeats)
    print(f"wrote {out_path}")


if __name__ == "__main__":
    main()
