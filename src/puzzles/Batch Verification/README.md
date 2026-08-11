# Dilithium reference verify on GPU (batched)

Runs the pq-crystals **reference** implementation of `crypto_sign_verify`
unchanged, on the GPU, with one CUDA thread per signature.

## Files

- `patch.py` — regex-based patcher that prepends `__host__ __device__`
  to every function definition in the ref sources, except the
  keygen/sign/`randombytes` functions (which stay host-only).
- `benchmark_verify.cu` — host driver + kernel.
- `Makefile` — clones the repo, patches, builds.

## Build & run

```bash
# 1. Fetch and patch the reference sources into ./src
make setup

# 2. Compile — MODE is 2 (default), 3, or 5
make MODE=2

# 3. Run
./benchmark_verify
```

If `nvcc` complains about `-arch=native`, pass your GPU arch explicitly:

```bash
make MODE=2 ARCH=sm_86    # Ampere (RTX 30xx / A100)
make MODE=2 ARCH=sm_89    # Ada    (RTX 40xx / L40)
make MODE=2 ARCH=sm_90    # Hopper (H100)
```

Batch size, threads per block, and repeat count are compile-time constants
in `benchmark_verify.cu` — edit and rebuild to sweep them:

```bash
nvcc ... -DBATCH_SIZE=16384 -DTHREADS_PER_BLOCK=64 ...
```

## Expected output (rough)

```
Dilithium mode:        2
  pk size:             1312 bytes
  sig size:            2420 bytes
  host self-verify:    OK

Device:                NVIDIA GeForce RTX 4090 (sm_89)
  per-thread stack:    256 KB
  batch size:          4096
  threads/block:       32

== GPU batched verify ==
  repeats:             5
  time / batch:        X.XXX ms
  amortized / verify:  X.XX us
  failures:            0 / 4096
```

## Caveats — read before writing anything down

1. **Reference is not optimized.** The pq-crystals README says explicitly
   that benchmarking ref numbers isn't representative. The interesting
   comparison points are (a) AVX2 CPU verify (~30-50 us on a modern x86),
   (b) a hand-tuned GPU verify that uses shared memory + cooperative NTT
   (Wan et al., "High-Throughput GPU Implementation of Dilithium",
   arXiv:2211.12265, get ~10⁶ verify/s for Dilithium2 on an A100).
   Expect this batched-reference number to sit well below that.

2. **Local memory dominates.** A single `polyvecl mat[K]` is 16 KB
   (Dilithium2) → 56 KB (Dilithium5) per thread. This lives in per-thread
   local memory (global-memory-backed, L1-cached). The kernel is
   memory-latency bound, not compute bound. Use `nsys` / `ncu` to confirm
   before drawing conclusions.

3. **Occupancy will be low.** Because each thread carves out that much
   register pressure + local memory, the scheduler won't be able to hide
   latency the way it would for a lean kernel. This is fundamental to the
   "run reference unchanged" choice.

4. **Same input, N times.** The benchmark verifies BATCH_SIZE copies of
   *the same* (pk, sig, msg) — fine for cycle-counting but does not stress
   memory bandwidth realistically. For a real workload measurement,
   generate BATCH_SIZE distinct keypairs+signatures on the host first.

5. **Deterministic signing enforced.** Built with
   `-UDILITHIUM_RANDOMIZED_SIGNING` so repeat runs produce the same sig.
