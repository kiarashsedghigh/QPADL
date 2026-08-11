#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Sweep batched Dilithium verify + HCT + LBP proof-checks across batch
# sizes (and TPB for GPU, plus κ for HCT / dim for LBP) at REPEATS=500 on
# both CPU and GPU. Emit CSVs to ./results/ and, if matplotlib is
# available, render the comparison PDFs automatically at the end.
#
# Modes:
#   ./run_sweep.sh                # ML-DSA verify: cpu + gpu, PDF at end
#   ./run_sweep.sh cpu | gpu | all
#   ./run_sweep.sh hct            # HCT: cpu + gpu for κ ∈ {14,18,20,23}
#   ./run_sweep.sh hct-cpu | hct-gpu
#   ./run_sweep.sh lbp            # LBP: cpu + gpu for n ∈ {40,60,80,100}
#   ./run_sweep.sh lbp-cpu | lbp-gpu
#   ./run_sweep.sh everything     # ML-DSA + HCT + LBP, both sides, all PDFs
#
# Env overrides:
#   MODE=3 REPEATS=100 OMP_NUM_THREADS=16 ./run_sweep.sh
#   SKIP_REPORT=1 ./run_sweep.sh  # sweep only, don't render PDFs
#
# Sibling benchmarks are `make setup`-ed on demand if not already.
# ---------------------------------------------------------------------------

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
GPU_DIR="$HERE/../Batch Verification"
CPU_DIR="$HERE/../CPU Batch Verification"
PROOF_GPU_DIR="$HERE/../Batch Proof Check"
PROOF_CPU_DIR="$HERE/../CPU Batch Proof Check"
RESULTS="$HERE/results"
mkdir -p "$RESULTS"

MODE="${MODE:-2}"
REPEATS="${REPEATS:-500}"
#BATCHES=(128 512 1024 2048 4096 8192)
BATCHES=(128 512 1024 2048 4096 8192 16384 32768)
TPB_LIST=(32)
# HCT κ sweep matches paper Table 5: {14, 18, 20, 23}. Verify cost is
# O(log n_l) hashes and only weakly depends on κ (the leading-zeros loop
# scans κ/8 bytes), but we sweep it anyway so plots track the paper's axis.
KAPPA_LIST=(14 18 20 23)
LOG_N="${LOG_N:-1}"    # n_l = 2 leaves per paper §7.1

DIM_LIST=(48 62 69 79)

log() { printf '\033[36m[%s]\033[0m %s\n' "$(date +%H:%M:%S)" "$*"; }

ensure_setup() {
    local dir="$1"
    if [ ! -d "$dir/src" ]; then
        log "running setup in ${dir##*/}"
        (cd "$dir" && make -s setup >/dev/null 2>&1)
    fi
}

# -------- CPU sweep --------------------------------------------------------
run_cpu() {
    ensure_setup "$CPU_DIR"
    local out="$RESULTS/cpu_mode${MODE}_repeats${REPEATS}.csv"
    echo "batch,threads,repeats,ms_per_batch,per_verify_us,throughput_vps,failures" > "$out"

    local threads="${OMP_NUM_THREADS:-$(nproc)}"
    log "CPU sweep — threads=$threads, mode=$MODE, repeats=$REPEATS"

    for b in "${BATCHES[@]}"; do
        log "  cpu batch=$b (building)"
        (cd "$CPU_DIR" && make -s clean >/dev/null && \
            make -s MODE="$MODE" \
                CFLAGS="-O3 -std=gnu11 -Wall -Wextra -fopenmp \
                    -DDILITHIUM_MODE=$MODE -UDILITHIUM_RANDOMIZED_SIGNING \
                    -I./src -DBATCH_SIZE=$b -DREPEATS=$REPEATS" \
                >/dev/null 2>&1) || { log "  cpu batch=$b BUILD FAILED"; continue; }

        log "  cpu batch=$b (running)"
        local logf; logf="$(cd "$CPU_DIR" && OMP_NUM_THREADS="$threads" \
            OMP_PROC_BIND=close OMP_PLACES=cores ./benchmark_verify 2>&1)" \
            || { log "  cpu batch=$b RUN FAILED"; continue; }

        local ms us tp fl
        ms=$(awk '/time \/ batch:/       {print $4}' <<<"$logf" | head -1)
        us=$(awk '/amortized \/ verify:/ {print $4}' <<<"$logf" | head -1)
        tp=$(awk '/throughput:/          {print $2}' <<<"$logf" | head -1)
        fl=$(awk '/failures:/            {print $2}' <<<"$logf" | head -1)
        echo "$b,$threads,$REPEATS,$ms,$us,$tp,$fl" >> "$out"
    done
    log "cpu results → $out"
}

# -------- GPU sweep --------------------------------------------------------
have_gpu() { command -v nvcc >/dev/null 2>&1 && command -v nvidia-smi >/dev/null 2>&1; }

run_gpu() {
    if ! have_gpu; then
        log "GPU sweep skipped (no nvcc / no nvidia-smi)"
        return
    fi
    ensure_setup "$GPU_DIR"
    local out="$RESULTS/gpu_mode${MODE}_repeats${REPEATS}.csv"
    echo "batch,tpb,repeats,ms_h2d,ms_kernel,ms_d2h,ms_e2e,us_e2e_per_verify,throughput_e2e_vps,us_kernel_per_verify,throughput_kernel_vps,failures" > "$out"

    log "GPU sweep — mode=$MODE, repeats=$REPEATS"

    for tpb in "${TPB_LIST[@]}"; do
        for b in "${BATCHES[@]}"; do
            log "  gpu batch=$b tpb=$tpb (building)"
            (cd "$GPU_DIR" && make -s clean >/dev/null && \
                make -s MODE="$MODE" \
                    NVCCFLAGS="-O3 -std=c++14 -rdc=true \
                        -DDILITHIUM_MODE=$MODE -UDILITHIUM_RANDOMIZED_SIGNING \
                        -arch=native -I./src \
                        -DBATCH_SIZE=$b -DREPEATS=$REPEATS \
                        -DTHREADS_PER_BLOCK=$tpb" \
                    >/dev/null 2>&1) || { log "  gpu batch=$b tpb=$tpb BUILD FAILED"; continue; }

            log "  gpu batch=$b tpb=$tpb (running)"
            local logf; logf="$(cd "$GPU_DIR" && ./benchmark_verify 2>&1)" \
                || { log "  gpu batch=$b tpb=$tpb RUN FAILED"; continue; }

            # Pull the phase-breakdown numbers. Each phase has its own block
            # with "time / batch:", "per verify:", "throughput:" lines; we
            # scan in order H2D → kernel → D2H → E2E.
            local ms_h2d ms_kernel ms_d2h ms_e2e
            mapfile -t ms_all < <(awk '/time \/ batch:/ {print $4}' <<<"$logf")
            ms_h2d="${ms_all[0]:-NA}"
            ms_kernel="${ms_all[1]:-NA}"
            ms_d2h="${ms_all[2]:-NA}"
            ms_e2e="${ms_all[3]:-NA}"

            mapfile -t us_all < <(awk '/per verify:/ {print $3}' <<<"$logf")
            local us_kernel="${us_all[1]:-NA}"
            local us_e2e="${us_all[3]:-NA}"

            mapfile -t tp_all < <(awk '/throughput:/ {print $2}' <<<"$logf")
            local tp_kernel="${tp_all[1]:-NA}"
            local tp_e2e="${tp_all[3]:-NA}"

            local fl; fl=$(awk '/failures:/ {print $2}' <<<"$logf" | head -1)
            echo "$b,$tpb,$REPEATS,$ms_h2d,$ms_kernel,$ms_d2h,$ms_e2e,$us_e2e,$tp_e2e,$us_kernel,$tp_kernel,$fl" >> "$out"
        done
    done
    log "gpu results → $out"
}

# -------- Proof-check CPU sweep (HCT) -------------------------------------
# One CSV per (side, kappa). Batch is the row dimension inside.
run_hct_cpu() {
    ensure_setup "$PROOF_CPU_DIR"
    for k in "${KAPPA_LIST[@]}"; do
        local out="$RESULTS/hct_cpu_kappa${k}_repeats${REPEATS}.csv"
        echo "batch,threads,repeats,kappa,ms_per_batch,per_verify_us,throughput_vps" > "$out"
        local threads="${OMP_NUM_THREADS:-$(nproc)}"
        log "HCT-CPU sweep — κ=$k, threads=$threads, repeats=$REPEATS"
        for b in "${BATCHES[@]}"; do
            log "  hct-cpu batch=$b κ=$k (building)"
            (cd "$PROOF_CPU_DIR" && make -s clean >/dev/null && \
                make -s KAPPA="$k" LOG_N="$LOG_N" \
                    CFLAGS="-O3 -std=gnu11 -Wall -Wextra -fopenmp -I./src \
                        -DHCT_KAPPA_BITS=$k -DHCT_LOG_N_LEAVES=$LOG_N \
                        -DBATCH_SIZE=$b -DREPEATS=$REPEATS" \
                    >/dev/null 2>&1) || { log "  hct-cpu BUILD FAILED"; continue; }
            local logf; logf="$(cd "$PROOF_CPU_DIR" && OMP_NUM_THREADS="$threads" \
                OMP_PROC_BIND=close OMP_PLACES=cores ./benchmark_proof 2>&1)" \
                || { log "  hct-cpu RUN FAILED"; continue; }
            local ms us tp
            ms=$(awk '/time \/ batch:/       {print $4}' <<<"$logf" | head -1)
            us=$(awk '/amortized \/ verify:/ {print $4}' <<<"$logf" | head -1)
            tp=$(awk '/throughput:/          {print $2}' <<<"$logf" | head -1)
            echo "$b,$threads,$REPEATS,$k,$ms,$us,$tp" >> "$out"
        done
        log "hct-cpu results → $out"
    done
}

# -------- HCT proof-check GPU sweep ---------------------------------------
run_hct_gpu() {
    if ! have_gpu; then
        log "HCT-GPU sweep skipped (no nvcc / no nvidia-smi)"
        return
    fi
    ensure_setup "$PROOF_GPU_DIR"
    for k in "${KAPPA_LIST[@]}"; do
        local out="$RESULTS/hct_gpu_kappa${k}_repeats${REPEATS}.csv"
        echo "batch,tpb,repeats,kappa,ms_h2d,ms_kernel,ms_d2h,ms_e2e,us_e2e_per_verify,throughput_e2e_vps,us_kernel_per_verify,throughput_kernel_vps" > "$out"
        log "HCT-GPU sweep — κ=$k, repeats=$REPEATS"
        for tpb in "${TPB_LIST[@]}"; do
            for b in "${BATCHES[@]}"; do
                log "  hct-gpu batch=$b tpb=$tpb κ=$k (building)"
                (cd "$PROOF_GPU_DIR" && make -s clean >/dev/null && \
                    make -s KAPPA="$k" LOG_N="$LOG_N" \
                        NVCCFLAGS="-O3 -std=c++14 -rdc=true -arch=native -I./src \
                            -DHCT_KAPPA_BITS=$k -DHCT_LOG_N_LEAVES=$LOG_N \
                            -DBATCH_SIZE=$b -DREPEATS=$REPEATS -DTHREADS_PER_BLOCK=$tpb" \
                        >/dev/null 2>&1) || { log "  hct-gpu BUILD FAILED"; continue; }
                local logf; logf="$(cd "$PROOF_GPU_DIR" && ./benchmark_proof 2>&1)" \
                    || { log "  hct-gpu RUN FAILED"; continue; }
                local ms_h2d ms_kernel ms_d2h ms_e2e us_kernel us_e2e tp_kernel tp_e2e
                mapfile -t ms_all < <(awk '/time \/ batch:/ {print $4}' <<<"$logf")
                ms_h2d="${ms_all[0]:-NA}"; ms_kernel="${ms_all[1]:-NA}"
                ms_d2h="${ms_all[2]:-NA}"; ms_e2e="${ms_all[3]:-NA}"
                mapfile -t us_all < <(awk '/per verify:/ {print $3}' <<<"$logf")
                us_kernel="${us_all[1]:-NA}"; us_e2e="${us_all[3]:-NA}"
                mapfile -t tp_all < <(awk '/throughput:/ {print $2}' <<<"$logf")
                tp_kernel="${tp_all[1]:-NA}"; tp_e2e="${tp_all[3]:-NA}"
                echo "$b,$tpb,$REPEATS,$k,$ms_h2d,$ms_kernel,$ms_d2h,$ms_e2e,$us_e2e,$tp_e2e,$us_kernel,$tp_kernel" >> "$out"
            done
        done
        log "hct-gpu results → $out"
    done
}

# -------- LBP proof-check CPU sweep ---------------------------------------
run_lbp_cpu() {
    # No `make setup` needed for LBP (no fips202 dependency).
    for d in "${DIM_LIST[@]}"; do
        local out="$RESULTS/lbp_cpu_dim${d}_repeats${REPEATS}.csv"
        echo "batch,threads,repeats,dim,ms_per_batch,per_verify_us,throughput_vps" > "$out"
        local threads="${OMP_NUM_THREADS:-$(nproc)}"
        log "LBP-CPU sweep — n=$d, threads=$threads, repeats=$REPEATS"
        for b in "${BATCHES[@]}"; do
            log "  lbp-cpu batch=$b n=$d (building)"
            (cd "$PROOF_CPU_DIR" && make -s clean >/dev/null && \
                make -s PROOF=lbp DIM="$d" \
                    CFLAGS="-O3 -std=gnu11 -Wall -Wextra -fopenmp -I./src \
                        -DPROOF_LBP -DLBP_N=$d \
                        -DBATCH_SIZE=$b -DREPEATS=$REPEATS" \
                    >/dev/null 2>&1) || { log "  lbp-cpu BUILD FAILED"; continue; }
            local logf; logf="$(cd "$PROOF_CPU_DIR" && OMP_NUM_THREADS="$threads" \
                OMP_PROC_BIND=close OMP_PLACES=cores ./benchmark_proof 2>&1)" \
                || { log "  lbp-cpu RUN FAILED"; continue; }
            local ms us tp
            ms=$(awk '/time \/ batch:/       {print $4}' <<<"$logf" | head -1)
            us=$(awk '/amortized \/ verify:/ {print $4}' <<<"$logf" | head -1)
            tp=$(awk '/throughput:/          {print $2}' <<<"$logf" | head -1)
            echo "$b,$threads,$REPEATS,$d,$ms,$us,$tp" >> "$out"
        done
        log "lbp-cpu results → $out"
    done
}

# -------- LBP proof-check GPU sweep ---------------------------------------
run_lbp_gpu() {
    if ! have_gpu; then
        log "LBP-GPU sweep skipped (no nvcc / no nvidia-smi)"
        return
    fi
    for d in "${DIM_LIST[@]}"; do
        local out="$RESULTS/lbp_gpu_dim${d}_repeats${REPEATS}.csv"
        echo "batch,tpb,repeats,dim,ms_h2d,ms_kernel,ms_d2h,ms_e2e,us_e2e_per_verify,throughput_e2e_vps,us_kernel_per_verify,throughput_kernel_vps" > "$out"
        log "LBP-GPU sweep — n=$d, repeats=$REPEATS"
        for tpb in "${TPB_LIST[@]}"; do
            for b in "${BATCHES[@]}"; do
                log "  lbp-gpu batch=$b tpb=$tpb n=$d (building)"
                (cd "$PROOF_GPU_DIR" && make -s clean >/dev/null && \
                    make -s PROOF=lbp DIM="$d" \
                        NVCCFLAGS="-O3 -std=c++14 -rdc=true -arch=native -I./src \
                            -DPROOF_LBP -DLBP_N=$d \
                            -DBATCH_SIZE=$b -DREPEATS=$REPEATS -DTHREADS_PER_BLOCK=$tpb" \
                        >/dev/null 2>&1) || { log "  lbp-gpu BUILD FAILED"; continue; }
                local logf; logf="$(cd "$PROOF_GPU_DIR" && ./benchmark_proof 2>&1)" \
                    || { log "  lbp-gpu RUN FAILED"; continue; }
                local ms_h2d ms_kernel ms_d2h ms_e2e us_kernel us_e2e tp_kernel tp_e2e
                mapfile -t ms_all < <(awk '/time \/ batch:/ {print $4}' <<<"$logf")
                ms_h2d="${ms_all[0]:-NA}"; ms_kernel="${ms_all[1]:-NA}"
                ms_d2h="${ms_all[2]:-NA}"; ms_e2e="${ms_all[3]:-NA}"
                mapfile -t us_all < <(awk '/per verify:/ {print $3}' <<<"$logf")
                us_kernel="${us_all[1]:-NA}"; us_e2e="${us_all[3]:-NA}"
                mapfile -t tp_all < <(awk '/throughput:/ {print $2}' <<<"$logf")
                tp_kernel="${tp_all[1]:-NA}"; tp_e2e="${tp_all[3]:-NA}"
                echo "$b,$tpb,$REPEATS,$d,$ms_h2d,$ms_kernel,$ms_d2h,$ms_e2e,$us_e2e,$tp_e2e,$us_kernel,$tp_kernel" >> "$out"
            done
        done
        log "lbp-gpu results → $out"
    done
}

# -------- reporter invocation ---------------------------------------------
# Renders PDFs at the end. Silent no-op if SKIP_REPORT=1 or matplotlib is
# missing. Kept a separate step so failures here don't wipe out the CSVs.
render_ml_report() {
    [ "${SKIP_REPORT:-0}" = "1" ] && return
    if ! python3 -c "import matplotlib" >/dev/null 2>&1; then
        log "PDF report skipped (matplotlib not installed — pip install matplotlib)"
        return
    fi
    log "rendering ML-DSA report PDF..."
    (cd "$HERE" && ./make_report.py --mode "$MODE" --repeats "$REPEATS") || \
        log "make_report.py failed (CSVs still in $RESULTS)"
}

render_hct_reports() {
    [ "${SKIP_REPORT:-0}" = "1" ] && return
    if ! python3 -c "import matplotlib" >/dev/null 2>&1; then
        log "HCT PDF skipped (matplotlib not installed)"
        return
    fi
    log "rendering HCT PDF..."
    (cd "$HERE" && ./make_hct_report.py --repeats "$REPEATS") || \
        log "make_hct_report.py failed (CSVs still in $RESULTS)"
}

render_lbp_reports() {
    [ "${SKIP_REPORT:-0}" = "1" ] && return
    if ! python3 -c "import matplotlib" >/dev/null 2>&1; then
        log "LBP PDF skipped (matplotlib not installed)"
        return
    fi
    log "rendering LBP PDF..."
    (cd "$HERE" && ./make_lbp_report.py --repeats "$REPEATS") || \
        log "make_lbp_report.py failed (CSVs still in $RESULTS)"
}

case "${1:-all}" in
    cpu)         run_cpu; render_ml_report ;;
    gpu)         run_gpu; render_ml_report ;;
    all)         run_cpu; run_gpu; render_ml_report ;;
    hct-cpu)     run_hct_cpu; render_hct_reports ;;
    hct-gpu)     run_hct_gpu; render_hct_reports ;;
    hct)         run_hct_cpu; run_hct_gpu; render_hct_reports ;;
    lbp-cpu)     run_lbp_cpu; render_lbp_reports ;;
    lbp-gpu)     run_lbp_gpu; render_lbp_reports ;;
    lbp)         run_lbp_cpu; run_lbp_gpu; render_lbp_reports ;;
    everything)  run_cpu; run_gpu; run_hct_cpu; run_hct_gpu
                 run_lbp_cpu; run_lbp_gpu
                 render_ml_report; render_hct_reports; render_lbp_reports ;;
    *)   echo "Usage: $0 [cpu|gpu|all|hct-cpu|hct-gpu|hct|lbp-cpu|lbp-gpu|lbp|everything]" >&2; exit 1 ;;
esac

log "done."
