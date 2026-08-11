/*
 * QPADL HCT (hashcash tree) verify — the "proof check" per service-access
 * request that the SAS server runs alongside ML-DSA verify (paper §5.1.2).
 *
 * Concrete parameters from the paper's evaluation (§7.1):
 *   n_l  = 2      leaves          → log₂(n_l) = 1 hash per verify
 *   κ   ∈ {14, 18, 20, 23}        leading zero bits per hash
 *   hash = SHA-256                (we use SHA3-256 via fips202; same order-
 *                                  of-magnitude cost per byte, and it's
 *                                  already linked for ML-DSA. Swap in
 *                                  crypto/hash SHA-256 here if you want
 *                                  bit-exact hash timing.)
 *
 * The user asked us to assume puzzles are valid — we do the hash + the
 * κ-leading-zeros for-loop, but we don't reject; we accumulate the
 * non-zero byte count and return it so the caller can OR it into the
 * result word (keeps the compiler from dead-code-eliminating the hash).
 *
 * TO SWAP HCT → LBP LATER: create proof_lbp.h with the same interface
 * (PROOF_PAYLOAD_BYTES + proof_verify_one), and change the #include in
 * benchmark_proof.cu. Nothing else needs to move.
 */
#ifndef PROOF_HCT_H
#define PROOF_HCT_H

#include <stdint.h>
#include <stddef.h>
#include "fips202.h"                       /* sha3_256 (patched HD-callable) */

/* Cross-compile marker so the same header works in .c (host) and .cu
 * (host + device). */
#ifdef __CUDACC__
#  define PROOF_HD __host__ __device__
#else
#  define PROOF_HD
#endif

/* -------- HCT parameters ------------------------------------------------- */
/* Fixed by the paper's HCT evaluation. Override with -DHCT_LOG_N_LEAVES=<n>
 * if you want a different tree depth (e.g., LBP-comparable variants). */
#ifndef HCT_LOG_N_LEAVES
#define HCT_LOG_N_LEAVES 1                 /* n_l = 2 → 1 hash per verify */
#endif
/* κ sweep values from paper Table 5: {14, 18, 20, 23}. Verify-side cost is
 * O(log n_l) hashes independent of κ; κ only changes how many prefix bits
 * the for-loop scans. */
#ifndef HCT_KAPPA_BITS
#define HCT_KAPPA_BITS   20
#endif
#define HCT_HASH_LEN     32                /* SHA3-256 output */
#define HCT_N_S_LEN      32                /* λ = 256-bit salt */
#define HCT_NONCE_LEN    32

/* Payload per verify: n_s || nonces[log n] || siblings[log n]. */
#define PROOF_PAYLOAD_BYTES \
    (HCT_N_S_LEN + HCT_LOG_N_LEAVES * (HCT_NONCE_LEN + HCT_HASH_LEN))

/* Human-readable tag for log lines. */
#define PROOF_TYPE_NAME "HCT"

/* One leaf→root walk. Returns non-zero-byte count across all κ-prefix
 * checks — a value the caller must fold into its output. */
PROOF_HD static int
proof_verify_one(const uint8_t *payload, uint32_t leaf_idx)
{
    const uint8_t *n_s      = payload;
    const uint8_t *nonces   = payload + HCT_N_S_LEN;
    const uint8_t *siblings = nonces  + HCT_LOG_N_LEAVES * HCT_NONCE_LEN;

    uint8_t buf[HCT_N_S_LEN + 4 + 2 * HCT_HASH_LEN + HCT_NONCE_LEN];
    uint8_t h_cur[HCT_HASH_LEN];
    for (int j = 0; j < HCT_HASH_LEN; ++j) h_cur[j] = 0;

    uint32_t idx = leaf_idx;
    int nz_total = 0;

    for (int lvl = 0; lvl < HCT_LOG_N_LEAVES; ++lvl) {
        int off = 0;
        for (int j = 0; j < HCT_N_S_LEN; ++j) buf[off++] = n_s[j];
        buf[off++] = (uint8_t)(idx >> 24);
        buf[off++] = (uint8_t)(idx >> 16);
        buf[off++] = (uint8_t)(idx >>  8);
        buf[off++] = (uint8_t)(idx      );

        const uint8_t *sib = siblings + (size_t)lvl * HCT_HASH_LEN;
        if (idx & 1u) {
            for (int j = 0; j < HCT_HASH_LEN; ++j) buf[off++] = sib[j];
            for (int j = 0; j < HCT_HASH_LEN; ++j) buf[off++] = h_cur[j];
        } else {
            for (int j = 0; j < HCT_HASH_LEN; ++j) buf[off++] = h_cur[j];
            for (int j = 0; j < HCT_HASH_LEN; ++j) buf[off++] = sib[j];
        }
        const uint8_t *nonce = nonces + (size_t)lvl * HCT_NONCE_LEN;
        for (int j = 0; j < HCT_NONCE_LEN; ++j) buf[off++] = nonce[j];

        sha3_256(h_cur, buf, (size_t)off);

        /* κ-leading-zeros check as a for-loop. Puzzles are assumed valid,
         * so instead of failing on non-zero we count them so the loop and
         * the hash above can't be optimized out. Bit-level check with a
         * partial-byte fixup at the tail; matches the paper's use of κ as
         * a bit-count (not byte-count). */
        int full_bytes  = HCT_KAPPA_BITS / 8;
        int tail_bits   = HCT_KAPPA_BITS % 8;
        for (int j = 0; j < full_bytes; ++j) {
            if (h_cur[j] != 0) nz_total++;
        }
        if (tail_bits) {
            uint8_t mask = (uint8_t)(0xff << (8 - tail_bits));
            if ((h_cur[full_bytes] & mask) != 0) nz_total++;
        }

        idx >>= 1;
    }
    return nz_total;
}

#endif /* PROOF_HCT_H */
