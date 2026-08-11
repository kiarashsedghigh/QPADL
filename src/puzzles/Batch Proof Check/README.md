# Batch Proof Check — GPU (HCT)

One thread = one HCT PoW verify. No ML-DSA in this benchmark — that lives in
`../Batch Verification/` and is measured separately so the tables don't get
huge and the two costs stay independently readable.

## Parameters (paper §7.1, Table 5)

- `n_l = 2` leaves (fixed → `log₂(n_l) = 1` hash per verify)
- `κ ∈ {14, 18, 20, 23}` leading-zero bits (server-side verify cost is
  ~constant in κ; only the leading-zeros for-loop scans a different width)
- Hash: SHA3-256 via patched `fips202.cu` (paper uses SHA-256 — same
  order-of-magnitude cost per byte; swap the header if you need bit-exact)

Overridable at build time via the Makefile:

```
make KAPPA=23 LOG_N=1      # HCT_KAPPA_BITS=23, HCT_LOG_N_LEAVES=1
```

## Swapping HCT → LBP

`benchmark_proof.cu` has exactly one `#include "proof_hct.h"`. When LBP
lands, create `proof_lbp.h` with the same interface (`PROOF_PAYLOAD_BYTES`,
`PROOF_TYPE_NAME`, `PROOF_HD int proof_verify_one(payload, leaf)`) and
change the include. Nothing else moves.

## Build + run

```
make setup           # reuses ../Batch Verification/'s patched fips202 if present
make                 # defaults: HCT_KAPPA_BITS=20, HCT_LOG_N_LEAVES=1
./benchmark_proof
```

## Output shape

Same H2D / kernel / D2H / E2E phase breakdown as `../Batch Verification/`,
so the sweep harness (`../Benchmark Sweep/run_sweep.sh proof-gpu`) scrapes
it identically.
