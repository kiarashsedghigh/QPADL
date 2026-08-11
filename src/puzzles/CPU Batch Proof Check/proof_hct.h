/*
 * QPADL HCT (hashcash tree) verify — CPU-side, same contract as the GPU
 * sibling under ../Batch Proof Check/proof_hct.h. Kept as a plain header
 * (no CUDA qualifiers) so the CPU benchmark stays a stock C11 build.
 *
 * See the GPU sibling's header for the design rationale.
 */
#ifndef PROOF_HCT_H
#define PROOF_HCT_H

#include <stdint.h>
#include <stddef.h>
#include "fips202.h"

#ifndef HCT_LOG_N_LEAVES
#define HCT_LOG_N_LEAVES 1
#endif
#ifndef HCT_KAPPA_BITS
#define HCT_KAPPA_BITS   20
#endif
#define HCT_HASH_LEN     32
#define HCT_N_S_LEN      32
#define HCT_NONCE_LEN    32

#define PROOF_PAYLOAD_BYTES \
    (HCT_N_S_LEN + HCT_LOG_N_LEAVES * (HCT_NONCE_LEN + HCT_HASH_LEN))

#define PROOF_TYPE_NAME "HCT"

static inline int
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
