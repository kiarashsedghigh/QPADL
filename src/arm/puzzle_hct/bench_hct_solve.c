/*
 * HCT PoW solve on ARM. Actually does the work — no paper stand-in.
 *
 * Per paper §5.1.2, the client solves an n_l-leaf hash-cash tree:
 *   for each leaf i > n_l:  find n_x s.t. h_κ(n_s || i || 0 || 0 || n_x)
 *                            has κ leading zero bits
 *   for each internal i:    solve h_κ(n_s || i || h_{2i} || h_{2i+1} || n_x)
 * The root nonce is Ψ.
 *
 * Total nodes to solve for n_l = 2: 3 (two leaves + root).
 * Expected hashes per node: 2^κ.  Expected total: 3·2^κ.
 *
 * Reports mean ± 95% CI across `SAMPLES` independent solves (each with a
 * fresh random n_s so nonce searches are independent). At larger κ this
 * runs for seconds — set SAMPLES=3 or reduce ITERS via -DKAPPA=<n> to
 * stay quick.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include "../common/bench.h"

#ifndef HCT_LOG_N_LEAVES
#define HCT_LOG_N_LEAVES 1              /* n_l = 2 per paper §7.1 */
#endif
#ifndef HCT_KAPPA_BITS
#define HCT_KAPPA_BITS 14
#endif
#ifndef SAMPLES
#define SAMPLES 5                       /* 5 independent solves for CI */
#endif

#define N_LEAVES        (1 << HCT_LOG_N_LEAVES)
#define N_NODES         (2 * N_LEAVES - 1)    /* full binary tree */

/* Return 1 iff first κ bits of h are all zero. */
static inline int has_kappa_zeros(const uint8_t *h, int kappa)
{
    int full = kappa / 8;
    int tail = kappa % 8;
    for (int i = 0; i < full; ++i) if (h[i] != 0) return 0;
    if (tail) {
        uint8_t mask = (uint8_t)(0xff << (8 - tail));
        if (h[full] & mask) return 0;
    }
    return 1;
}

/* Solve one node: find n_x such that SHA-256(n_s || idx || h_l || h_r || n_x)
 * has κ leading zeros. Writes the winning hash to `out`, returns trial count. */
static uint64_t solve_one_node(const uint8_t n_s[32], uint32_t idx,
                               const uint8_t h_l[32], const uint8_t h_r[32],
                               uint8_t out[32], int kappa)
{
    /* Input layout: n_s(32) | idx(4) | h_l(32) | h_r(32) | nonce(8) = 108 B */
    uint8_t buf[32 + 4 + 32 + 32 + 8];
    memcpy(buf, n_s, 32);
    buf[32] = (uint8_t)(idx >> 24);
    buf[33] = (uint8_t)(idx >> 16);
    buf[34] = (uint8_t)(idx >>  8);
    buf[35] = (uint8_t)(idx      );
    memcpy(buf + 36, h_l, 32);
    memcpy(buf + 68, h_r, 32);
    uint64_t nonce = 0;
    uint64_t trials = 0;
    for (;;) {
        memcpy(buf + 100, &nonce, 8);
        SHA256(buf, sizeof buf, out);
        trials++;
        if (has_kappa_zeros(out, kappa)) return trials;
        nonce++;
    }
}

/* Solve full HCT (leaves + root) with a fresh salt n_s. Returns Ψ (root nonce
 * search's trial count is what the last call returned; total_trials is set
 * for reporting only — the sink for DCE is Ψ + total_trials). */
static uint64_t solve_hct(int kappa, uint64_t *out_total_trials)
{
    uint8_t n_s[32];
    RAND_bytes(n_s, sizeof n_s);
    uint8_t zeros[32] = {0};
    uint8_t leaf_hashes[N_LEAVES][32];
    uint64_t total = 0;

    /* Leaves — idx starts at N_LEAVES (i > n_l per paper's convention). */
    for (int i = 0; i < N_LEAVES; ++i)
        total += solve_one_node(n_s, (uint32_t)(N_LEAVES + i),
                                zeros, zeros, leaf_hashes[i], kappa);

    /* Root (only one internal for n_l=2). For larger trees this would be a
     * bottom-up loop; kept minimal here since paper fixes n_l = 2. */
    uint8_t root_hash[32];
    total += solve_one_node(n_s, 1, leaf_hashes[0], leaf_hashes[1],
                            root_hash, kappa);

    if (out_total_trials) *out_total_trials = total;
    /* Return low 64 bits of root hash as Ψ proxy (real Ψ is the nonce). */
    uint64_t psi;
    memcpy(&psi, root_hash, 8);
    return psi;
}

int main(void)
{
    printf("== HCT.PoW.Solve (client, ARM) — n_l=%d, κ=%d, samples=%d\n",
           N_LEAVES, HCT_KAPPA_BITS, SAMPLES);
    uint64_t sink = 0, trials_sum = 0;

    /* Each iter is one full HCT solve. iters_per_sample = 1 so each sample
     * is a single, independent solve. Total wall = SAMPLES * expected. */
    BENCH_CI("HCT solve", SAMPLES, 1, 0, {
        uint64_t t = 0;
        sink += solve_hct(HCT_KAPPA_BITS, &t);
        trials_sum += t;
    });
    printf("        avg trials/solve = %.0f  (expected ≈ %.0f)\n",
           (double)trials_sum / (double)SAMPLES,
           (double)N_NODES * (double)((uint64_t)1 << HCT_KAPPA_BITS));
    SINK(sink);
    return 0;
}
