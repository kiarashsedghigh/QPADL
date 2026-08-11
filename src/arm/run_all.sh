#!/usr/bin/env bash
# Run every client-op bench under src/arm/ one-by-one and print a header
# before each so the output is scannable.

set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"

banner() {
    echo
    echo "============================================================"
    echo "  $1"
    echo "============================================================"
}

# List: (label, dir, binary or make target)
run_one() {
    local label="$1" dir="$2" cmd="$3"
    banner "$label"
    ( cd "$HERE/$dir" && eval "$cmd" ) \
        || echo "  !! $label FAILED"
}

# hashes
run_one "SHA-256"                    hash        ./bench_sha256
run_one "SHA3-256"                   hash        ./bench_sha3_256

# symmetric
run_one "AES-256-CTR"                aes         ./bench_aes256

# puzzles
run_one "HCT verify"                 puzzle_hct  ./bench_hct_verify
run_one "LBP verify (dim=64)"        puzzle_lbp  ./bench_lbp_verify

# PIR client ops
run_one "PIR-ENS (Chor) client"      pir         ./bench_pir_ens
run_one "PIR-FTR (Goldberg) client"  pir         ./bench_pir_ftr
run_one "PIR-OOP (CIP) client"       pir         ./bench_pir_oop

# ML-DSA (requires make setup once)
if [ -x "$HERE/ml_dsa/bench_ml_dsa_verify" ]; then
    run_one "ML-DSA-2 verify"        ml_dsa      ./bench_ml_dsa_verify
else
    echo
    echo "-- ML-DSA bench not built. Run: (cd ml_dsa && make setup && make) --"
fi

echo
echo "-- done --"
