# Batch Proof Check — CPU (HCT, OpenMP)

Mirror of `../Batch Proof Check/` (GPU). Same parameters, same result
shape, so the sweep + reporter can pair them side by side.

See the GPU sibling's README for the design and parameter rationale.

## Build + run

```
make setup             # reuses ../CPU Batch Verification/ fips202 if present
make                   # defaults HCT_KAPPA_BITS=20, HCT_LOG_N_LEAVES=1
OMP_NUM_THREADS=$(nproc) ./benchmark_proof
```

## Swap HCT → LBP

Same as the GPU sibling: create `proof_lbp.h` with the same interface,
change the include in `benchmark_proof.c`.
