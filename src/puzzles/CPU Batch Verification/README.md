# Batched Dilithium Verify — CPU (OpenMP)

Mirror of `../Batch Verification/` (GPU) so results are directly comparable.
Same batch size, same repeats, same throughput definition. Everything else
about the reference code is unchanged — no patching, no qualifiers, no rewrite.

## Build

```
make setup           # clones pq-crystals/dilithium into ./dilithium-repo
                     # (or reuses ../Batch\ Verification/dilithium-repo if present)
make MODE=2          # Dilithium2 (also MODE=3, MODE=5)
./benchmark_verify
```

## Tuning knobs

| knob | how | default |
|---|---|---|
| batch size | `-DBATCH_SIZE=<n>` in CFLAGS or edit `benchmark_verify.c` | 4096 |
| repeats | `-DREPEATS=<n>` | 5 |
| threads | `OMP_NUM_THREADS=<n> ./benchmark_verify` | cores |
| pinning | `OMP_PROC_BIND=close OMP_PLACES=cores ./benchmark_verify` | free |

## What the output means

- `throughput` — total verifies (`BATCH_SIZE × REPEATS`) divided by wall-clock
  seconds across the whole timed region. Directly comparable to the GPU
  benchmark's `verify/s`.
- `amortized / verify` — per-single-verify wall time, same definition.
- `RSS peak` — peak resident-set size from `getrusage(ru_maxrss)`, in bytes.
  This is the CPU equivalent of the GPU `used total` line: input buffers +
  per-thread stack + libc/runtime overhead. Thread stacks are pthread-default
  (8 MB each on glibc unless capped with `OMP_STACKSIZE`), so `OMP_NUM_THREADS`
  moves this number more than anything else.

## Compare against the GPU benchmark

Same batch, same mode, side by side:

```
# GPU
cd "../Batch Verification" && make MODE=2 && ./benchmark_verify

# CPU
cd "../CPU Batch Verification" && make MODE=2 && ./benchmark_verify
```

Look at `throughput` and per-verify amortized time. Vary `OMP_NUM_THREADS` on
the CPU side to see the scaling curve.
