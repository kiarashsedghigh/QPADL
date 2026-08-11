# ARM client-op benchmarks

Portable-C benchmarks for the client-side operations of QPADL, targeted at
ARM devices (e.g., Raspberry Pi 4 / ARM Cortex-A72 as in paper §7.1). No
x86 intrinsics anywhere — `-O3` with gcc/clang auto-vectorizes hot loops
to NEON on ARMv8.

Each operation is a separate binary in its own subdir, wired to its own
`Makefile` and script so you can run one at a time.

## Layout

| Dir             | Bench binary                              | What it times                                              |
|-----------------|-------------------------------------------|------------------------------------------------------------|
| `hash/`         | `bench_sha256`, `bench_sha3_256`          | commitment / pseudonym derivation (paper §5.1.1, §7.1)     |
| `aes/`          | `bench_aes256`                            | PQ-Tor per-hop symmetric layer (§2.2.1, §7.1)              |
| `puzzle_hct/`   | `bench_hct_verify`                        | HCT PoW verify (§5.1.2, Table 5)                           |
| `puzzle_lbp/`   | `bench_lbp_verify`                        | LBP PoW verify (§5.1.2, Table 5)                           |
| `pir/`          | `bench_pir_ens` (Chor / ENS)              | client query build + block reconstruct                     |
|                 | `bench_pir_ftr` (Goldberg / FTR)          | Shamir-share build + Lagrange interpolate                  |
|                 | `bench_pir_oop` (CIP / OOP)               | online path only (offline is per-day amortized)            |
| `ml_dsa/`       | `bench_ml_dsa_verify`                     | client verify of PSD signature on puzzle (Alg 1 step 23)   |

## Dependencies

- `libcrypto` from OpenSSL — SHA-256/SHA3, AES-256, RNG. On ARMv8 crypto
  extensions OpenSSL uses hardware SHA-256/AES automatically.
- `gcc` / `clang` with `-O3`. NEON is baseline on ARMv8, no `-mfpu=neon`
  needed.

Install on Debian/Ubuntu ARM:
```
sudo apt install build-essential libssl-dev git
```

## Hardware acceleration flags (auto-enabled)

Every bench Makefile now includes `common/arch.mk`, which detects the host
architecture and adds the right SIMD/crypto-ext flag:

| host (`uname -m`) | flag added | what it enables |
|---|---|---|
| `aarch64` (RPi 4 Cortex-A72, Neoverse, M-series) | `-mcpu=native` | NEON + AES + SHA-1/SHA-2 + PMULL + CRC32 (whatever the host CPU exposes) |
| `armv7l` (older 32-bit RPi) | `-march=native -mfpu=neon` | 32-bit NEON |
| `x86_64` / anything else | (nothing) | plain `-O3` |

On RPi 4, `-mcpu=native` expands to roughly `-mcpu=cortex-a72+crypto`, so:
- gcc emits ARMv8 `AESE`/`AESMC`/`SHA256H` instructions where the source
  code allows (mostly in libcrypto).
- `-O3` auto-vectorizes plain C loops (XOR loops in ENS/OOP, GF(2⁸) table
  lookups in FTR, the LBP muladd) to NEON `veorq_u64` / `tbl` /
  `mla`. No source changes needed.

Confirm on your box:
```bash
cat /proc/cpuinfo | grep -m1 Features    # expect: aes pmull sha1 sha2 crc32
openssl speed -evp aes-256-ctr           # should hit 500-1000 MB/s
openssl speed -evp sha256                # should hit 400-700 MB/s
```

## Build + run

```bash
cd src/arm
make setup              # clones dilithium ref for ml_dsa (one-time)
make                    # builds every bench
./run_all.sh            # runs every bench, one line per op
./client_summary.sh     # per-(κ, puzzle) total client compute time
```

`client_summary.sh` runs the ARM benches once, plugs in the values we
can't measure locally (LRS.Verify, PQ-Tor transport, ProxVerif, puzzle
solve) from the paper (§7.1, §7.2, Table 5), and prints a per-op
breakdown + total for every combination of κ ∈ {14, 18, 20, 23} and
puzzle ∈ {HCT, LBP}. Change PIR variant via `PIR_VARIANT=ENS|FTR|OOP`.

## Per-op targeting

```bash
# Just SHA-256
(cd hash && make bench_sha256 && ./bench_sha256)

# Just LBP verify at a specific dim
(cd puzzle_lbp && make DIM=79 && ./bench_lbp_verify)

# LBP dim sweep
(cd puzzle_lbp && make run-sweep)

# PIR ENS/FTR/OOP
(cd pir && make run)

# PIR DB-size sweep (r ∈ {2^12, 2^14, 2^16, 2^18})
(cd pir && make run-sweep)

# ML-DSA at a different NIST level
(cd ml_dsa && make MODE=3 && ./bench_ml_dsa_verify)
```

## Output format

Every bench prints a uniform line via the `BENCH()` macro in
`common/bench.h`:

```
op=<name>  us/op=<per-call-microseconds>  ops/s=<throughput>  [MB/s=<bandwidth>]
```

Grep-friendly for downstream reporting.

## What's not (yet) in here

- **LRS.Sign / LRS.Verify** — the paper uses ethSTARK / Rescue-Prime,
  which is a large dependency; the client PoL cost of ~33 ms is dominated
  by these, and porting to ARM requires the ethSTARK reference build.
- **ML-KEM keygen/encaps/decaps** — trivial to add; drop in the pq-crystals
  kyber ref repo alongside dilithium and mirror `ml_dsa/Makefile`.

Both are mechanical to add — same shape as `ml_dsa/`. Ping if you want them
next.
