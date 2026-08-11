#!/usr/bin/env bash
# ============================================================================
# QPADL client summary on ARM — one page, itemized per phase.
#
#   Phase 1 · PoL              hash × 3, LRS.Verify [paper], ProxVerif [paper]
#   Phase 2 · Spectrum Query   PIR client work — ENS / FTR / OOP side by side
#   Phase 3 · Service Request  ML-DSA verify, puzzle solve (HCT κ=20 measured
#                              + LBP [paper]), token concat
#
# No κ sweep, no puzzle sweep — one number per row. Pick κ via -DKAPPA and
# LBP dim via env if needed.
# ============================================================================
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"

# Phase 3 shows ALL four security levels — no need to re-run per κ.
KAPPAS=(14 18 20)             # HCT κ tiers to measure locally; κ=23 skipped
                              # (~hours on ARM). Paper reference below still
                              # shows all four for context.
KAPPAS_PAPER=(14 18 20 23)    # what to print in the HCT paper-reference block
LBP_DIMS=(48 62 69 79)        # LBP SVP-challenge dim tiers
TOR_HOPS=3

# Phase 2 sweeps DB size — the PIR query is an r-bit vector, so r must
# match the DB size we're pricing. Paper Fig 2 x-axis (§7.2.2):
#   8 / 32 / 128 / 512 MB → r ∈ {2^12, 2^14, 2^16, 2^18} at b = 3 KB.
R_LOG2S=(12 14 16 18)
B_BYTES=3072                  # DB row size in bytes (paper: 3 KB)
db_size_str() {   # human-readable DB size for a given R_LOG2
    awk -v r=$1 -v b=$B_BYTES 'BEGIN{
        n = (2^r) * b;
        if      (n >= 1073741824) printf "%.0f GB", n/1073741824;
        else if (n >= 1048576)    printf "%.0f MB", n/1048576;
        else                      printf "%.0f KB", n/1024;
    }'
}

# ---- paper-provided constants (µs) ------------------------------------------
PAPER_POL_LRS_US=33340        # Table 5 "PoL User"
PAPER_PROXVERIF_US=5000       # §7.2.1 "1-10 ms" midpoint
PAPER_MLKEM_ENCAPS_US=13.4
PAPER_MLKEM_DECAPS_US=9
PAPER_AES256_LAYER_US=8
declare -A LBP_SOLVE_US=( [48]=133080 [62]=259150 [69]=881410 [79]=2931450 )
declare -A HCT_SOLVE_PAPER=( [14]=38940 [18]=66360 [20]=316550 [23]=6251370 )

# ---- ARM scaling for paper-only, CPU-bound ops ------------------------------
# Paper measured on Intel i9-11900K @ 3.5 GHz. RPi 4 Cortex-A72 @ 1.5 GHz is
# ~2.3× slower on clock alone, less aggressive OoO, smaller caches, no AVX.
# Empirical multipliers for the workloads we care about:
#   LBP solve  — bignum lattice reduction (NTL / LLL / BKZ). Cache-heavy,
#                integer-multiply-heavy. Slowdown grows mildly with n.
#                Central estimate: ~6×.
#   LRS verify — STARK-SoK + Rescue-Prime hashes. No hw accel for either
#                on Cortex-A72. Central estimate: ~5×.
# Override at run time:
#   ARM_SCALE_LBP=8 ARM_SCALE_LRS=6 ./client_summary.sh
ARM_SCALE_LBP="${ARM_SCALE_LBP:-6.0}"
ARM_SCALE_LRS="${ARM_SCALE_LRS:-5.0}"

# ---- ensure builds ----------------------------------------------------------
[ -x "$HERE/ml_dsa/bench_ml_dsa_verify" ] || \
    (cd "$HERE/ml_dsa" && make -s setup >/dev/null && make -s >/dev/null)
(cd "$HERE" && make -s all >/dev/null) || { echo "build failed" >&2; exit 1; }

# ---- pull us/op + ci95_pct from a bench line --------------------------------
run() {
    local out
    out="$(cd "$HERE/$1" && ./"$2" 2>/dev/null | grep '^op=' | grep -F "$3" | head -1)"
    local us pct
    us=$(awk  -F 'us/op='     '{print $2}' <<<"$out" | awk '{print $1}')
    pct=$(awk -F 'ci95_pct='  '{print $2}' <<<"$out" | awk '{print $1}')
    [ -z "$pct" ] && pct="0"
    echo "$us $pct"
}

echo "== running micro-benches (non-PIR) =="
read US_SHA256 CI_SHA256 <<<"$(run hash        bench_sha256        'SHA-256')"
read US_MLDSA  CI_MLDSA  <<<"$(run ml_dsa      bench_ml_dsa_verify 'ML-DSA verify')"

# --- PIR sweep across DB sizes -----------------------------------------------
# For each R_LOG2 we rebuild the 3 PIR benches with the matching -DR_LOG2 so
# the query bit-vector length actually matches the DB size we're pricing.
declare -A PIR_Q_US PIR_Q_CI PIR_R_US PIR_R_CI    # keyed by "<variant>_<R>"
echo "== running PIR sweep over DB sizes: r ∈ {2^${R_LOG2S[*]}} =="
for R in "${R_LOG2S[@]}"; do
    printf "   DB=%s (r=2^%d) ... " "$(db_size_str $R)" "$R"
    (cd "$HERE/pir" && make -s clean >/dev/null && \
        make -s R_LOG2=$R B_BYTES=$B_BYTES all >/dev/null 2>&1) \
        || { echo "build failed"; continue; }
    for V in ens ftr oop; do
        Vu=$(echo $V | tr 'a-z' 'A-Z')
        Q_OUT="$(cd "$HERE/pir" && ./bench_pir_$V 2>/dev/null | grep '^op=' | grep -F "PIR-$Vu query build" | head -1)"
        R_OUT="$(cd "$HERE/pir" && ./bench_pir_$V 2>/dev/null | grep '^op=' | grep -F "PIR-$Vu block reconstruct" | head -1)"
        PIR_Q_US[${Vu}_$R]=$(awk -F 'us/op='    '{print $2}' <<<"$Q_OUT" | awk '{print $1}')
        PIR_Q_CI[${Vu}_$R]=$(awk -F 'ci95_pct=' '{print $2}' <<<"$Q_OUT" | awk '{print $1}')
        PIR_R_US[${Vu}_$R]=$(awk -F 'us/op='    '{print $2}' <<<"$R_OUT" | awk '{print $1}')
        PIR_R_CI[${Vu}_$R]=$(awk -F 'ci95_pct=' '{print $2}' <<<"$R_OUT" | awk '{print $1}')
    done
    printf "done\n"
done

# HCT solve for every κ (measured). κ=23 is slow; use fewer samples there.
# Total wall time on a modest ARM: ~1-2 minutes for the full sweep.
declare -A US_HCT_S CI_HCT_S
echo "== running HCT.PoW.Solve for κ ∈ {${KAPPAS[*]}} =="
for K in "${KAPPAS[@]}"; do
    printf "   κ=%d ... " "$K"
    S=5; [ "$K" -ge 22 ] && S=3
    (cd "$HERE/puzzle_hct" && make -s clean >/dev/null && \
        make -s KAPPA=$K \
          CFLAGS="-O3 -std=gnu11 -Wall -Wextra -I../common -DHCT_KAPPA_BITS=$K -DHCT_LOG_N_LEAVES=1 -DSAMPLES=$S" \
          bench_hct_solve >/dev/null 2>&1) || { echo "build failed"; continue; }
    OUT="$(cd "$HERE/puzzle_hct" && ./bench_hct_solve 2>/dev/null | grep '^op=' | head -1)"
    US_HCT_S[$K]=$(awk  -F 'us/op='    '{print $2}' <<<"$OUT" | awk '{print $1}')
    CI_HCT_S[$K]=$(awk  -F 'ci95_pct=' '{print $2}' <<<"$OUT" | awk '{print $1}')
    printf "%.3f ms ± %.1f%%\n" \
        "$(awk -v x=${US_HCT_S[$K]} 'BEGIN{print x/1000}')" "${CI_HCT_S[$K]:-0}"
done

TOR_SEND=$(awk -v h=$TOR_HOPS -v k=$PAPER_MLKEM_ENCAPS_US -v a=$PAPER_AES256_LAYER_US \
    'BEGIN{printf "%.1f", h*(k+a)}')
TOR_RECV=$(awk -v h=$TOR_HOPS -v k=$PAPER_MLKEM_DECAPS_US -v a=$PAPER_AES256_LAYER_US \
    'BEGIN{printf "%.1f", h*(k+a)}')

# ---- formatters -------------------------------------------------------------
# "12345.678 µs ± 8.04%"  or  "12345.678 µs [paper]"  or  "0.000 µs"
fmt() { awk -v v="$1" -v c="$2" 'BEGIN{
    if (c+0 > 0 && v+0 > 0) printf "%12.3f µs  ± %5.2f%%", v, c;
    else if (v+0 == 0)      printf "%12.3f µs         ", v;
    else                    printf "%12.3f µs  [paper]", v; }'; }
row()  { printf "   %-38s %s\n" "$1" "$(fmt "$2" "${3:-0}")"; }
tot()  { printf "   %-38s %12.3f µs   (%.3f ms)\n" "$1" "$2" "$(awk -v x=$2 'BEGIN{print x/1000}')"; }
hr()   { printf "   ---------------------------------------- ---------------------------\n"; }
add()  { awk -v a="$1" -v b="$2" 'BEGIN{printf "%.3f", a+b}'; }

echo
echo "================================================================"
echo "  QPADL client summary (ARM)"
echo "  PQ-Tor $TOR_HOPS-hop; HCT κ ∈ {${KAPPAS[*]}} (measured);"
echo "                       LBP n ∈ {${LBP_DIMS[*]}} (paper)"
echo "================================================================"

# --- Phase 1 · PoL -----------------------------------------------------------
echo
echo "── Phase 1 · PoL     (all values in µs) ───────────────────────"
row "PRG for nym_TW = PRG(sk_c, TW)"          "$US_SHA256"       "$CI_SHA256"
row "SHA-256 commitment C_TW"                 "$US_SHA256"       "$CI_SHA256"
row "SHA-256 recompute e_ID (PoL verify)"     "$US_SHA256"       "$CI_SHA256"
LRS_ARM=$(awk -v p=$PAPER_POL_LRS_US -v s=$ARM_SCALE_LRS 'BEGIN{printf "%.3f", p*s}')
row "LRS.Verify (n_AP=2^13, est ×${ARM_SCALE_LRS} ARM)"  "$LRS_ARM" "0"
row "ProxVerif (RSS+RTT fusion)     [paper]"  "$PAPER_PROXVERIF_US" "0"
hr
P1=$(awk -v a="$US_SHA256" -v b="$LRS_ARM" -v c="$PAPER_PROXVERIF_US" \
    'BEGIN{printf "%.3f", 3*a + b + c}')
tot "Phase 1 total"                            "$P1"

# --- Phase 2 · Spectrum Query — sweep over DB sizes -------------------------
echo
echo "── Phase 2 · Spectrum Query (per DB size, all PIR variants) ───"
echo "   Block size b = ${B_BYTES} B (paper §7.1);"
echo "   PQ-Tor per-hop: send=${TOR_SEND} µs, recv=${TOR_RECV} µs   [paper]"
echo

# Column header: DB size for each R_LOG2 in R_LOG2S
COL_W=13
printf "   %-32s" "Metric (all values in µs)"
for R in "${R_LOG2S[@]}"; do printf " %${COL_W}s" "$(db_size_str $R)"; done
echo
printf "   %-32s" ""
for R in "${R_LOG2S[@]}"; do printf " %${COL_W}s" "(r=2^$R)"; done
echo
printf "   %-32s" "--------------------------------"
for _ in "${R_LOG2S[@]}"; do printf " %${COL_W}s" "-------------"; done
echo

# Compact cell formatter for the grid
cell() { awk -v v="$1" -v c="$2" -v w=$COL_W 'BEGIN{
    if (c+0>0) printf " %*s", w, sprintf("%.2f±%.1f%%", v, c);
    else       printf " %*.2f", w, v; }'; }

# Per-metric rows
for label_and_bucket in \
    "PIR-ENS query build:PIR_Q_US:ENS:PIR_Q_CI" \
    "PIR-ENS block reconstruct:PIR_R_US:ENS:PIR_R_CI" \
    "PIR-FTR query build:PIR_Q_US:FTR:PIR_Q_CI" \
    "PIR-FTR block reconstruct:PIR_R_US:FTR:PIR_R_CI" \
    "PIR-OOP query build:PIR_Q_US:OOP:PIR_Q_CI" \
    "PIR-OOP block reconstruct:PIR_R_US:OOP:PIR_R_CI"; do
    IFS=: read -r label USMAP V CIMAP <<< "$label_and_bucket"
    printf "   %-32s" "$label"
    for R in "${R_LOG2S[@]}"; do
        key="${V}_$R"
        # bash dynamic assoc-array deref
        eval "us=\${$USMAP[$key]:-0}"; eval "ci=\${$CIMAP[$key]:-0}"
        cell "$us" "$ci"
    done
    echo
done

# Constants row (PQ-Tor per-hop total, batch across DB sizes)
TOR_TOTAL=$(awk -v s=$TOR_SEND -v r=$TOR_RECV 'BEGIN{printf "%.2f", s+r}')
printf "   %-32s" "PQ-Tor send+recv  [paper]"
for _ in "${R_LOG2S[@]}"; do printf " %${COL_W}.2f" "$TOR_TOTAL"; done
echo

printf "   %-32s" "--------------------------------"
for _ in "${R_LOG2S[@]}"; do printf " %${COL_W}s" "-------------"; done
echo

# Per-variant Phase-2 totals per DB size
for V in ENS FTR OOP; do
    printf "   %-32s" "Phase 2 total, $V   (µs)"
    for R in "${R_LOG2S[@]}"; do
        Q=${PIR_Q_US[${V}_$R]:-0}
        RE=${PIR_R_US[${V}_$R]:-0}
        TOT=$(awk -v q="$Q" -v r="$RE" -v s="$TOR_SEND" -v v="$TOR_RECV" \
                  'BEGIN{printf "%.2f", q+r+s+v}')
        printf " %${COL_W}s" "$TOT"
    done
    echo
done
for V in ENS FTR OOP; do
    printf "   %-32s" "Phase 2 total, $V   (ms)"
    for R in "${R_LOG2S[@]}"; do
        Q=${PIR_Q_US[${V}_$R]:-0}
        RE=${PIR_R_US[${V}_$R]:-0}
        TOT_MS=$(awk -v q="$Q" -v r="$RE" -v s="$TOR_SEND" -v v="$TOR_RECV" \
                     'BEGIN{printf "%.4f", (q+r+s+v)/1000}')
        printf " %${COL_W}s" "$TOT_MS"
    done
    echo
done

# --- Phase 3 · Service Request — all security levels enumerated -------------
echo
echo "── Phase 3 · Service Request   (all values in µs) ────────────"
row "ML-DSA.Verify (puzzle σ)"                "$US_MLDSA"    "$CI_MLDSA"
row "Token = (Π, σ, Ψ)  (concat)"             "0"            "0"
echo
echo "   Puzzle solve — HCT (measured on this box):"
for K in "${KAPPAS[@]}"; do
    row "  HCT.PoW.Solve @κ=$K"                "${US_HCT_S[$K]:-0}" "${CI_HCT_S[$K]:-0}"
done
echo
echo "   Puzzle solve — HCT paper reference (Table 5):"
for K in "${KAPPAS_PAPER[@]}"; do
    row "  HCT.PoW.Solve @κ=$K   [paper]"      "${HCT_SOLVE_PAPER[$K]}" "0"
done
echo
echo "   Puzzle solve — LBP (Intel-i9 paper × ${ARM_SCALE_LBP} = ARM estimate):"
for D in "${LBP_DIMS[@]}"; do
    paper_us="${LBP_SOLVE_US[$D]}"
    arm_us=$(awk -v p="$paper_us" -v s="$ARM_SCALE_LBP" 'BEGIN{printf "%.0f", p*s}')
    printf "     LBP.PoW.Solve @n=%-3d  [ARM est]  %14.3f µs   (paper i9: %10d µs)\n" \
        "$D" "$arm_us" "$paper_us"
done
