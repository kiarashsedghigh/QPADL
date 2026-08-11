/*
 * ARM-flavored HCT verify. Same algorithm as
 * ../../puzzles/CPU Batch Proof Check/proof_hct.h, but the hash is
 * SHA-256 via OpenSSL (matches paper §7.1: "HCT uses SHA-256"), which
 * ARMv8-A crypto extensions accelerate in hardware.
 *
 * Interface: PROOF_PAYLOAD_BYTES, PROOF_TYPE_NAME, proof_verify_one.
 */
#ifndef PROOF_HCT_H
#define PROOF_HCT_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <openssl/sha.h>

#ifndef HCT_LOG_N_LEAVES
#define HCT_LOG_N_LEAVES 1          /* n_l = 2 per paper §7.1 */
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
    uint8_t h_cur[HCT_HASH_LEN] = {0};

    uint32_t idx = leaf_idx;
    int nz_total = 0;

    for (int lvl = 0; lvl < HCT_LOG_N_LEAVES; ++lvl) {
        int off = 0;
        memcpy(buf + off, n_s, HCT_N_S_LEN); off += HCT_N_S_LEN;
        buf[off++] = (uint8_t)(idx >> 24);
        buf[off++] = (uint8_t)(idx >> 16);
        buf[off++] = (uint8_t)(idx >>  8);
        buf[off++] = (uint8_t)(idx      );

        const uint8_t *sib = siblings + (size_t)lvl * HCT_HASH_LEN;
        if (idx & 1u) {
            memcpy(buf + off, sib,   HCT_HASH_LEN); off += HCT_HASH_LEN;
            memcpy(buf + off, h_cur, HCT_HASH_LEN); off += HCT_HASH_LEN;
        } else {
            memcpy(buf + off, h_cur, HCT_HASH_LEN); off += HCT_HASH_LEN;
            memcpy(buf + off, sib,   HCT_HASH_LEN); off += HCT_HASH_LEN;
        }
        memcpy(buf + off, nonces + (size_t)lvl * HCT_NONCE_LEN, HCT_NONCE_LEN);
        off += HCT_NONCE_LEN;

        SHA256(buf, (size_t)off, h_cur);

        /* κ-leading-zeros as a branchless byte-count. Assume-valid: sum
         * non-zero bytes into a sink instead of failing. */
        int full_bytes = HCT_KAPPA_BITS / 8;
        int tail_bits  = HCT_KAPPA_BITS % 8;
        for (int j = 0; j < full_bytes; ++j) nz_total += (h_cur[j] != 0);
        if (tail_bits) {
            uint8_t mask = (uint8_t)(0xff << (8 - tail_bits));
            nz_total += ((h_cur[full_bytes] & mask) != 0);
        }
        idx >>= 1;
    }
    return nz_total;
}

#endif /* PROOF_HCT_H */
