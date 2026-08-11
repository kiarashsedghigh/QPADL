/*
 * QPADL LBP verify — CPU sibling. Kept in lock-step with the GPU header
 * (../Batch Proof Check/proof_lbp.h) so both benchmarks measure the same
 * work. See that file for the full paper reference and per-optimization
 * rationale.
 *
 * CPU-side optimizations that carry over:
 *   - compile-time LBP_P → gcc emits magic-number modular reduction
 *   - direct (uint32_t*) loads instead of per-byte OR-shift
 *   - assume-valid → no signed % / sign fixup
 *   - branchless compares
 *   - signed squaring via int64 promotion
 *   - compile-time ‖v‖² bound compare
 */
#ifndef PROOF_LBP_H
#define PROOF_LBP_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifndef LBP_N
#define LBP_N 64
#endif

#define LBP_P 1073741789u

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
#    define LBP_BOUND_SQ 0ULL
#  endif
#endif

#define LBP_P_BYTES     ((size_t)4)
#define LBP_BASIS_BYTES ((size_t)4 * (LBP_N - 1))
#define LBP_COEFF_BYTES ((size_t)4 * LBP_N)
#define LBP_VEC_BYTES   ((size_t)4 * LBP_N)
#define PROOF_PAYLOAD_BYTES \
    (LBP_P_BYTES + LBP_BASIS_BYTES + LBP_COEFF_BYTES + LBP_VEC_BYTES)

#define PROOF_TYPE_NAME "LBP"

static inline int
proof_verify_one(const uint8_t *__restrict__ payload,
                 uint32_t                    leaf_idx_unused)
{
    (void)leaf_idx_unused;

    const uint8_t *x_bytes  = payload  + LBP_P_BYTES;
    const uint8_t *nu_bytes = x_bytes  + LBP_BASIS_BYTES;
    const uint8_t *v_bytes  = nu_bytes + LBP_COEFF_BYTES;

    /* Step 1: v'[0] = Σ x[i-1] · ν[i]  mod LBP_P */
    uint32_t acc = 0;
    for (int i = 1; i < LBP_N; ++i) {
        uint32_t xi, ni;
        memcpy(&xi, x_bytes  + (size_t)(i - 1) * 4, sizeof(uint32_t));
        memcpy(&ni, nu_bytes + (size_t)i       * 4, sizeof(uint32_t));
        acc = (uint32_t)((acc + (uint64_t)xi * ni) % LBP_P);
    }

    /* Step 2: v[0] vs acc — branchless. */
    uint32_t v0;
    memcpy(&v0, v_bytes, sizeof(uint32_t));
    int diff_count = (int)(v0 != acc);

    /* Step 3: v[i] vs ν[i-1] for i ≥ 1. */
    for (int i = 1; i < LBP_N; ++i) {
        uint32_t vi, nprev;
        memcpy(&vi,    v_bytes  + (size_t)i       * 4, sizeof(uint32_t));
        memcpy(&nprev, nu_bytes + (size_t)(i - 1) * 4, sizeof(uint32_t));
        diff_count += (int)(vi != nprev);
    }

    /* Step 4: ‖v‖² — signed int64 promotion for correct squaring. */
    uint64_t norm2 = 0;
    for (int i = 0; i < LBP_N; ++i) {
        int32_t vi_s;
        memcpy(&vi_s, v_bytes + (size_t)i * 4, sizeof(int32_t));
        int64_t sq = (int64_t)vi_s * (int64_t)vi_s;
        norm2 += (uint64_t)sq;
    }

    /* Step 5: norm-bound compare — compiled out for unknown n. */
#if LBP_BOUND_SQ != 0
    diff_count += (int)(norm2 > (uint64_t)LBP_BOUND_SQ);
#endif

    if (norm2 == 0) diff_count++;
    return diff_count + (int)(norm2 & 0xff);
}

#endif /* PROOF_LBP_H */
