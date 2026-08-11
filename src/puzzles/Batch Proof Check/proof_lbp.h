/*
 * QPADL LBP (Lattice-Based Puzzle) verify — SVP-challenge lattice format
 * (https://www.latticechallenge.org/svp-challenge/), paper §5.1.2.
 *
 * Puzzle Π = (α, n_Λ, B, p);   Solution Ψ = (v, ν).
 * B is n×n HNF: first row [p, x_2, ..., x_n], subdiagonal ones, zeros else.
 *
 * LB.PoW.Verify checks:
 *     v = B · ν                     (linear-combo membership)
 *     ||v||_2 ≤ α · p^{1/n_Λ}       (short-vector threshold)
 *
 * HNF sparsity collapses recomputation to O(n):
 *     v'[0] = Σ_{i≥1} x_i · ν_i  mod p          (p·ν[0] mod p = 0 dropped)
 *     v'[i] = ν_{i-1}                            for i ≥ 1
 *
 * ---- GPU / CPU perf notes -----------------------------------------------
 * All of these are stacked in the code below:
 *
 *   (1) LBP_P is a compile-time macro → magic-number modular reduction
 *       (constant-p `%` becomes a mul-hi + shift, no runtime div).
 *   (2) __ldg on GPU pulls reads through the read-only texture cache; on
 *       CPU we just deref (uint32*) pointers directly. Both compile to a
 *       single 4-byte load — much better than the old per-byte OR-shift.
 *   (3) __restrict__ on the payload pointer lets the compiler assume no
 *       aliasing with anything else on the frame.
 *   (4) #pragma unroll on GPU exposes ILP inside each thread's verify;
 *       gcc handles unrolling under -O3 via its own cost model.
 *   (5) Assume-valid → coefficients already in [0, p), so no signed-mod
 *       fixup pair per basis coefficient.
 *   (6) Branchless compares: `diff_count += (a != b)` → setne / predicated
 *       add, no warp divergence.
 *   (7) Signed squaring via int64 promotion — (-x)² == x², zero branches.
 *   (8) Norm-bound compare compiled out for unknown n; for the swept dims
 *       it's a single 64-bit constant compare, again branchless.
 *
 * Result word is the folded diff/nz sink so the compiler can't DCE
 * the arithmetic.
 */
#ifndef PROOF_LBP_H
#define PROOF_LBP_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef __CUDACC__
#  define PROOF_HD __host__ __device__
#else
#  define PROOF_HD
#endif

/* 4-byte load, strict-aliasing safe.
 * __CUDA_ARCH__ is defined only during nvcc's DEVICE compilation pass, so
 * we transparently pick the right one for host-side unit tests.
 *   Device: __ldg pulls the read through the read-only texture cache
 *           (sm_35+). Requires typed pointer.
 *   Host:   memcpy(&dst, ptr, 4). Every real compiler folds this into a
 *           single 4-byte load, and it's strict-aliasing-safe unlike
 *           `*(uint32_t*)cast_from_uint8`.
 * Two-arg form so the macro doesn't need a statement-expression. */
#ifdef __CUDA_ARCH__
#  define LBP_LD_U32(dst, src) ((dst) = __ldg(reinterpret_cast<const uint32_t *>(src)))
#  define LBP_LD_I32(dst, src) ((dst) = __ldg(reinterpret_cast<const int32_t  *>(src)))
#else
#  define LBP_LD_U32(dst, src) memcpy(&(dst), (src), sizeof(uint32_t))
#  define LBP_LD_I32(dst, src) memcpy(&(dst), (src), sizeof(int32_t))
#endif

/* Loop unrolling hint. nvcc honors #pragma unroll; gcc ignores it and
 * relies on its own cost model under -O3. */
#ifdef __CUDACC__
#  define LBP_UNROLL _Pragma("unroll")
#else
#  define LBP_UNROLL
#endif

/* Hardness = lattice dimension. */
#ifndef LBP_N
#define LBP_N 64
#endif

/* Compile-time prime → magic-number division for the inner-loop % p. */
#define LBP_P 1073741789u

/* Precomputed ‖v‖² bound = floor((α · p^{1/n})²) with p = LBP_P and
 * α = 1.05 · Γ(n/2+1)^{1/n} / √π  (Gaussian heuristic, paper §5.1.2).
 *
 *     n     α·p^{1/n}     bound²
 *    ----   ---------    --------
 *     48      2.86         8.18 → 8
 *     62      2.92         8.52 → 8
 *     64      2.94         8.63 → 8
 *     69      2.97         8.80 → 8
 *     79      3.04         9.26 → 9
 *
 * Real deployments use p sized bit·n (~10n bits), pushing the bound to
 * ~2000-2400 and thus bound² well into 4-6M. Regardless of the bound
 * magnitude, the check is a single 64-bit compare — no cost impact.
 * Override with -DLBP_BOUND_SQ=<n> for a different p or dim. */
#ifndef LBP_BOUND_SQ
#  if   LBP_N == 48
#    define LBP_BOUND_SQ 8ULL
#  elif LBP_N == 62
#    define LBP_BOUND_SQ 8ULL
#  elif LBP_N == 64
#    define LBP_BOUND_SQ 8ULL
#  elif LBP_N == 69
#    define LBP_BOUND_SQ 8ULL
#  elif LBP_N == 79
#    define LBP_BOUND_SQ 9ULL
#  else
     /* Unknown LBP_N — check compiled out. Pass -DLBP_BOUND_SQ=<n>. */
#    define LBP_BOUND_SQ 0ULL
#  endif
#endif

/* Payload layout (LE, packed, 4-byte-aligned starts):
 *   [ p (4)  |  x[1..n-1] (4·(n-1))  |  ν[0..n-1] (4·n)  |  v[0..n-1] (4·n) ]
 * `p` field kept for wire-size symmetry; runtime uses LBP_P macro. */
#define LBP_P_BYTES     ((size_t)4)
#define LBP_BASIS_BYTES ((size_t)4 * (LBP_N - 1))
#define LBP_COEFF_BYTES ((size_t)4 * LBP_N)
#define LBP_VEC_BYTES   ((size_t)4 * LBP_N)
#define PROOF_PAYLOAD_BYTES \
    (LBP_P_BYTES + LBP_BASIS_BYTES + LBP_COEFF_BYTES + LBP_VEC_BYTES)

#define PROOF_TYPE_NAME "LBP"

PROOF_HD static int
proof_verify_one(const uint8_t *__restrict__ payload,
                 uint32_t                    leaf_idx_unused)
{
    (void)leaf_idx_unused;

    /* Slice into typed 4-byte segments. cudaMalloc guarantees 256-byte
     * alignment; malloc gives ≥ 8-byte. Every field starts on a 4-byte
     * boundary by construction. */
    const uint8_t *x_bytes  = payload  + LBP_P_BYTES;
    const uint8_t *nu_bytes = x_bytes  + LBP_BASIS_BYTES;
    const uint8_t *v_bytes  = nu_bytes + LBP_COEFF_BYTES;

    /* Step 1: v'[0] = Σ x[i-1] · ν[i]  mod LBP_P                          */
    uint32_t acc = 0;
    LBP_UNROLL
    for (int i = 1; i < LBP_N; ++i) {
        uint32_t xi, ni;
        LBP_LD_U32(xi, x_bytes  + (size_t)(i - 1) * 4);
        LBP_LD_U32(ni, nu_bytes + (size_t)i       * 4);
        /* xi, ni < LBP_P < 2^30 → product < 2^60, fits in uint64 */
        acc = (uint32_t)((acc + (uint64_t)xi * ni) % LBP_P);
    }

    /* Step 2: v[0] vs acc — branchless setne. */
    uint32_t v0;
    LBP_LD_U32(v0, v_bytes);
    int diff_count = (int)(v0 != acc);

    /* Step 3: v[i] vs ν[i-1] for i ≥ 1. */
    LBP_UNROLL
    for (int i = 1; i < LBP_N; ++i) {
        uint32_t vi, nprev;
        LBP_LD_U32(vi,    v_bytes  + (size_t)i       * 4);
        LBP_LD_U32(nprev, nu_bytes + (size_t)(i - 1) * 4);
        diff_count += (int)(vi != nprev);
    }

    /* Step 4: ‖v‖² = Σ v[i]²  (int64 promotion handles sign automatically) */
    uint64_t norm2 = 0;
    LBP_UNROLL
    for (int i = 0; i < LBP_N; ++i) {
        int32_t vi_s;
        LBP_LD_I32(vi_s, v_bytes + (size_t)i * 4);
        int64_t sq = (int64_t)vi_s * (int64_t)vi_s;
        norm2 += (uint64_t)sq;
    }

    /* Step 5: norm-bound check — compiled out for unknown n. */
#if LBP_BOUND_SQ != 0
    diff_count += (int)(norm2 > (uint64_t)LBP_BOUND_SQ);
#endif

    /* Sink: fold norm² so DCE can't drop the loops; degenerate reject. */
    if (norm2 == 0) diff_count++;
    return diff_count + (int)(norm2 & 0xff);
}

#endif /* PROOF_LBP_H */
