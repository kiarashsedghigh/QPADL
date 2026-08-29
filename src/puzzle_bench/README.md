# Client-Puzzle Benchmarks — Hashcash Tree & SVP

Two independent C++ micro-benchmarks for the DoS-mitigation client puzzles used
in QPADL. Each program takes the **number of iterations** as its argument and
reports **mean, sample standard deviation, and 95% confidence interval** (CI)
over those iterations. Every iteration draws **fresh random inputs**.

| Program      | Puzzle | Reference | What is benchmarked |
|--------------|--------|-----------|---------------------|
| `bench_hct`  | Hashcash Tree | Alviano, M. (2023). *Hashcash tree, a data structure to mitigate denial-of-service attacks.* Algorithms 16, 46. | **client-side computation** (solve) **and** **server-side verification** |
| `bench_svp`  | SVP | <https://www.latticechallenge.org/svp-challenge/> | **server-side verification only** (client solution assumed correct/trivial) |

## Security levels

The four levels are paired: `kappa` drives the hashcash tree, dimension drives SVP.

| Level | HCT `kappa` (leading zero bits) | SVP dimension |
|-------|--------------------------------|---------------|
| 1 | 14 | 48 |
| 2 | 18 | 62 |
| 3 | 20 | 69 |
| 4 | 23 | 79 |

## Build & run

```sh
make                 # builds bench_hct (needs OpenSSL/libssl-dev) and bench_svp
./bench_hct 50       # 50 iterations per kappa level
./bench_svp 50       # 50 iterations per dimension
make run ITERS=50    # build both and run with 50 iterations
```

`libssl-dev` (OpenSSL) is required for `bench_hct` (SHA-256). `bench_svp` has no
external dependencies.

## Hashcash Tree details (`bench_hct`)

* Fixed **`n_l = 2` leaves** ⇒ full binary tree of **3 nodes** (2 leaves + root).
* Hash = **SHA-256** (paper §7.1).
* **Proof of work / "ctr mode appended":** each node hashes the fixed input
  `n_s || idx || h_left || h_right || ctr` where `ctr` is a 64-bit counter
  **appended** to the input and incremented until the digest has `kappa` leading
  zero bits. Leaves use `h_left = h_right = 0`; for `n_l = 2` the leaf indices
  are `{2,3}` and the root index is `1`. Expected work ≈ `3 · 2^kappa` hashes.
* **Client-side computation** = solve all 3 nodes (leaf nonces + root nonce Ψ).
* **Server-side verification** = recompute the 3 node digests and check the
  `kappa`-zero condition (fixed 3-SHA-256 cost).
* Fresh random 32-byte salt `n_s` each iteration.

## SVP details (`bench_svp`)

* Security level = lattice **dimension** `d ∈ {48, 62, 69, 79}`.
* Instance = random rank-`d` integer basis `B` (challenge-style q-ary,
  lower-triangular, prime `q = 8191`). A solution is submitted as an integer
  coefficient vector `x`, with lattice vector `v = x · B`.
* **Verification** recomputes `v = x · B` (a `d × d` matrix-vector product,
  the dominant `O(d²)` cost), checks `v` matches, that `v ≠ 0`, and that
  `‖v‖² ≤ target²`. Norm is accumulated in `__int128` for exactness.
* Client solving is **not** benchmarked — a correct (trivial) short solution is
  assumed, per the task.
* Fresh random basis **and** solution each iteration.

## Notes on the statistics

* Reported times are per single operation (one full tree solve; one verify).
* Fast operations (verifications) are internally repeated per iteration until
  the timed span clears a floor, then divided out, so results are above clock
  resolution. Fresh randomness is guaranteed at the *iteration* level, which is
  what the variance/CI reflect.
* The CI half-width uses Student-t for small sample counts (`X ≤ 20`) and
  `z = 1.96` otherwise, matching `src/arm/common/bench.h`. Proof-of-work solve
  times are inherently high-variance (geometric trial counts), so use a larger
  `X` (e.g. 30–100) for tight client-side intervals.
