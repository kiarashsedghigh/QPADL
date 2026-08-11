# Benchmark Sweep — GPU vs CPU

Automates sweeping the sibling benchmarks and rendering comparison PDFs.
Three independent tracks:

- **ML-DSA verify** — `../Batch Verification/` (GPU), `../CPU Batch Verification/` (CPU)
- **HCT proof-check** — `../Batch Proof Check/` (GPU), `../CPU Batch Proof Check/` (CPU)
- **LBP proof-check** — same dirs as HCT, built with `PROOF=lbp` to swap
  in `proof_lbp.h` (SVP-challenge lattice format,
  https://www.latticechallenge.org/svp-challenge/)

## What it sweeps

| Axis | Values |
|---|---|
| `BATCH_SIZE` | 128, 512, 1024, 2048, 4096, 8192, 16384, 32768 |
| `REPEATS` | 500 (`REPEATS=<n>` to override) |
| `THREADS_PER_BLOCK` (GPU) | 32, 64, 128, 256 |
| `MODE` (ML-DSA only) | 2 (`MODE=3` or `5` also supported) |
| `HCT_KAPPA_BITS` (HCT only) | 14, 18, 20, 23 — paper Table 5 |
| `HCT_LOG_N_LEAVES` (HCT only) | 1 (n_l = 2 leaves; `LOG_N=<n>` to override) |
| `LBP_N` (LBP only) | 48, 62, 69, 79 — SVP-challenge hardness tiers |

## Usage — one shot

```bash
pip install matplotlib          # one-time, for PDF rendering

./run_sweep.sh                  # ML-DSA verify (cpu + gpu) → 1 PDF
./run_sweep.sh hct              # HCT proof-check (cpu + gpu, all 4 κ) → 1 PDF
./run_sweep.sh lbp              # LBP proof-check (cpu + gpu, all 4 dims) → 1 PDF
./run_sweep.sh everything       # ML-DSA + HCT + LBP, both sides, all PDFs

# Selective:
./run_sweep.sh cpu | gpu | all
./run_sweep.sh hct-cpu | hct-gpu
./run_sweep.sh lbp-cpu | lbp-gpu

# Env overrides:
MODE=3 REPEATS=100 OMP_NUM_THREADS=16 ./run_sweep.sh
SKIP_REPORT=1 ./run_sweep.sh    # CSVs only, don't render PDFs
```

Skips GPU cleanly if `nvcc`/`nvidia-smi` are absent. Runs `make setup` on
demand in each sibling if it hasn't been done yet.

## Outputs (all in `./results/`)

### ML-DSA sweep

| File | Shape |
|---|---|
| `cpu_mode2_repeats500.csv` | one row per batch |
| `gpu_mode2_repeats500.csv` | one row per (batch, tpb); H2D / kernel / D2H / E2E |
| `report_mode2_repeats500.pdf` | one table per batch, CPU + GPU-TPB rows |

### HCT proof-check sweep

| File | Shape |
|---|---|
| `hct_cpu_kappa{14,18,20,23}_repeats500.csv` | one row per batch |
| `hct_gpu_kappa{14,18,20,23}_repeats500.csv` | one row per (batch, tpb) |
| `hct_report_repeats500.pdf` | one PDF, 4 landscape pages (one per κ) |

Each page holds a 2×4 grid of mini-tables — 8 batch sizes at once, rows =
CPU + GPU-`<tpb>`, columns = MemComm / Comp / E2E. Best GPU E2E per batch
tinted green. `--kappa <n>` on `make_hct_report.py` writes a per-κ PDF
instead.

### LBP proof-check sweep

Same shape, but the sweep dimension is lattice size `n` (SVP-challenge
hardness tier):

| File | Shape |
|---|---|
| `lbp_cpu_dim{48,62,69,79}_repeats500.csv` | one row per batch |
| `lbp_gpu_dim{48,62,69,79}_repeats500.csv` | one row per (batch, tpb) |
| `lbp_report_repeats500.pdf` | one PDF, 4 pages (one per dim) |

## Runtime

Build-time dominated (each config is a fresh compile so the `-D` values
land in the binary).

- CPU sweep: ~2 min build + a few min run.
- GPU sweep: 8 batches × 4 TPB = 32 configs → ~5-15 min build + run.
- HCT sweep: multiply by 4 κ values. Runtime per verify is tiny, so
  hct-cpu ≈ 4 × few-min build; hct-gpu ≈ 20 min.
- `everything` mode is the sum.

If you only want a few points, edit `BATCHES` / `TPB_LIST` / `KAPPA_LIST` at
the top of `run_sweep.sh`.

## Rendering PDFs standalone

Both reporters run independently of the sweep, so you can iterate on
formatting without re-running the benchmark:

```
./make_report.py --mode 2 --repeats 500
./make_hct_report.py --repeats 500                # all κ
./make_hct_report.py --kappa 20 --repeats 500     # single-κ
```

## Direct plotting from the CSVs

If you want your own plots, the CSVs are pandas- / matplotlib- / gnuplot-
ready. Two useful comparisons:

1. Per-verify time vs batch size (log-x). Watch ramp-up → plateau on both.
2. GPU throughput vs TPB at fixed batch. Pick the TPB winner near the
   plateau; use that for the "final" number.

## HCT ↔ LBP swap

Both proof types live side by side in the same two dirs, gated by a
build-time flag: `#ifdef PROOF_LBP` picks `proof_lbp.h`, otherwise
`proof_hct.h`. The Makefile exposes `PROOF=lbp` and `DIM=<n>`:

```
cd ../Batch\ Proof\ Check
make                       # HCT, defaults
make PROOF=lbp DIM=80      # LBP with lattice dimension n=80
```

The sweep driver uses this via `./run_sweep.sh lbp` (see above).
