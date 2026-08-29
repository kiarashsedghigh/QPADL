/*
 * Hashcash Tree (HCT) client-puzzle benchmark.
 *
 *   Alviano, M. (2023). "Hashcash tree, a data structure to mitigate
 *   denial-of-service attacks." Algorithms 16, 46.
 *
 * Configuration (per the task):
 *   - number of leaves  n_l = 2   (full binary tree: 2 leaves + 1 root = 3 nodes)
 *   - hash              SHA-256    (via OpenSSL)
 *   - proof-of-work     find a nonce so the digest has kappa leading zero bits
 *   - nonce search      "ctr mode appended": a 64-bit counter is appended to
 *                       the hash input and incremented until kappa zeros appear
 *   - security levels   kappa in {14, 18, 20, 23}
 *
 * Two benchmarks are reported for each kappa, averaged over X iterations with
 * mean / std-dev / 95% CI:
 *
 *   1. client-side computation  -> solve the whole tree (2 leaves + root)
 *   2. server-side verification -> recompute the 3 node hashes and check that
 *                                  each carries kappa leading zeros
 *
 * Per-iteration randomness: a fresh random 32-byte salt n_s is drawn each
 * iteration, so every nonce search / verification is independent.
 *
 * Node hash input layout (108 bytes, fixed):
 *   n_s(32) | idx(4, big-endian) | h_left(32) | h_right(32) | ctr(8)
 * Leaves use h_left = h_right = 0. For n_l = 2 the leaves have idx {2,3} and
 * the root has idx 1 (matches src/arm/puzzle_hct).
 *
 * Usage:  ./bench_hct [iterations]        (default 20)
 *         ./bench_hct --iters=50
 */
#include <cstdint>
#include <cstring>
#include <vector>

#include <openssl/sha.h>
#include <openssl/rand.h>

#include "bench_common.h"

static const int   KAPPAS[]   = {14, 18, 20, 23};
static const int   N_KAPPAS   = (int)(sizeof(KAPPAS) / sizeof(KAPPAS[0]));
static const int   N_LEAVES   = 2;                 /* n_l = 2 */
static const int   N_NODES    = 2 * N_LEAVES - 1;  /* full binary tree = 3  */

#define NS_LEN     32
#define HASH_LEN   32
#define IDX_LEN     4
#define CTR_LEN     8
#define BUF_LEN    (NS_LEN + IDX_LEN + 2 * HASH_LEN + CTR_LEN)  /* 108 */

/* Assemble a node's hash input into `buf`. `ctr` is written little-endian in
 * the trailing 8 bytes (the "appended counter"). */
static inline void build_input(uint8_t *buf, const uint8_t n_s[NS_LEN],
                               uint32_t idx, const uint8_t h_l[HASH_LEN],
                               const uint8_t h_r[HASH_LEN], uint64_t ctr)
{
    std::memcpy(buf, n_s, NS_LEN);
    buf[NS_LEN + 0] = (uint8_t)(idx >> 24);
    buf[NS_LEN + 1] = (uint8_t)(idx >> 16);
    buf[NS_LEN + 2] = (uint8_t)(idx >> 8);
    buf[NS_LEN + 3] = (uint8_t)(idx);
    std::memcpy(buf + NS_LEN + IDX_LEN, h_l, HASH_LEN);
    std::memcpy(buf + NS_LEN + IDX_LEN + HASH_LEN, h_r, HASH_LEN);
    std::memcpy(buf + NS_LEN + IDX_LEN + 2 * HASH_LEN, &ctr, CTR_LEN);
}

/* True iff the first `kappa` bits of `h` are all zero. */
static inline bool has_kappa_zeros(const uint8_t *h, int kappa)
{
    int full = kappa / 8, tail = kappa % 8;
    for (int i = 0; i < full; ++i)
        if (h[i] != 0) return false;
    if (tail) {
        uint8_t mask = (uint8_t)(0xff << (8 - tail));
        if (h[full] & mask) return false;
    }
    return true;
}

/* Count how many of the leading `kappa` bits are non-zero (assume-valid
 * accumulator used by the verifier so its work is content-independent). */
static inline int kappa_nonzero(const uint8_t *h, int kappa)
{
    int full = kappa / 8, tail = kappa % 8, nz = 0;
    for (int i = 0; i < full; ++i) nz += (h[i] != 0);
    if (tail) {
        uint8_t mask = (uint8_t)(0xff << (8 - tail));
        nz += ((h[full] & mask) != 0);
    }
    return nz;
}

/* Solve one node: increment the appended counter until the digest has kappa
 * leading zeros. Writes the winning digest to `out`, returns the trial count. */
static uint64_t solve_node(const uint8_t n_s[NS_LEN], uint32_t idx,
                           const uint8_t h_l[HASH_LEN], const uint8_t h_r[HASH_LEN],
                           uint8_t out[HASH_LEN], int kappa)
{
    uint8_t buf[BUF_LEN];
    build_input(buf, n_s, idx, h_l, h_r, 0);
    uint64_t ctr = 0, trials = 0;
    for (;;) {
        std::memcpy(buf + NS_LEN + IDX_LEN + 2 * HASH_LEN, &ctr, CTR_LEN);
        SHA256(buf, BUF_LEN, out);
        ++trials;
        if (has_kappa_zeros(out, kappa)) return trials;
        ++ctr;
    }
}

/* CLIENT: solve the full 2-leaf hashcash tree with salt n_s. Leaf nonces and
 * the root nonce (Psi) are found; returns a value derived from the root digest
 * so the compiler cannot elide the work. `out_trials` (optional) receives the
 * total number of hash evaluations. */
static uint64_t solve_tree(const uint8_t n_s[NS_LEN], int kappa,
                           uint64_t *out_trials, uint64_t leaf_ctr_out[N_LEAVES],
                           uint64_t *root_ctr_out)
{
    const uint8_t zero[HASH_LEN] = {0};
    uint8_t leaf_h[N_LEAVES][HASH_LEN];
    uint64_t total = 0, ctr;

    for (int i = 0; i < N_LEAVES; ++i) {
        /* leaves carry idx = N_LEAVES + i = {2, 3} */
        uint64_t t = solve_node(n_s, (uint32_t)(N_LEAVES + i),
                                zero, zero, leaf_h[i], kappa);
        /* recover the winning counter value = t - 1 (search started at 0) */
        ctr = t - 1;
        if (leaf_ctr_out) leaf_ctr_out[i] = ctr;
        total += t;
    }

    uint8_t root_h[HASH_LEN];
    uint64_t tr = solve_node(n_s, 1, leaf_h[0], leaf_h[1], root_h, kappa);
    if (root_ctr_out) *root_ctr_out = tr - 1;
    total += tr;

    if (out_trials) *out_trials = total;
    uint64_t psi;
    std::memcpy(&psi, root_h, sizeof psi);
    return psi;
}

/* SERVER: recompute the 3 node digests from the proof and (assume-valid) count
 * their leading non-zero bits. Real acceptance is `return == 0`; the count is
 * accumulated so the compiler keeps every hash. Work is exactly N_NODES = 3
 * SHA-256 evaluations, independent of the payload contents.
 *
 * Proof layout: n_s(32) | leaf0_ctr(8) | leaf1_ctr(8) | root_ctr(8) = 56 B. */
static int verify_tree(const uint8_t *proof, int kappa)
{
    const uint8_t *n_s      = proof;
    uint64_t leaf_ctr[N_LEAVES], root_ctr;
    std::memcpy(&leaf_ctr[0], proof + NS_LEN + 0, 8);
    std::memcpy(&leaf_ctr[1], proof + NS_LEN + 8, 8);
    std::memcpy(&root_ctr,    proof + NS_LEN + 16, 8);

    const uint8_t zero[HASH_LEN] = {0};
    uint8_t buf[BUF_LEN], leaf_h[N_LEAVES][HASH_LEN], root_h[HASH_LEN];
    int nz = 0;

    for (int i = 0; i < N_LEAVES; ++i) {
        build_input(buf, n_s, (uint32_t)(N_LEAVES + i), zero, zero, leaf_ctr[i]);
        SHA256(buf, BUF_LEN, leaf_h[i]);
        nz += kappa_nonzero(leaf_h[i], kappa);
    }
    build_input(buf, n_s, 1, leaf_h[0], leaf_h[1], root_ctr);
    SHA256(buf, BUF_LEN, root_h);
    nz += kappa_nonzero(root_h, kappa);
    return nz;
}

int main(int argc, char **argv)
{
    int X = parse_iters(argc, argv, 20);

    std::printf("================================================================\n");
    std::printf(" Hashcash Tree (Alviano 2023) benchmark — SHA-256, n_l=%d (%d nodes)\n",
                N_LEAVES, N_NODES);
    std::printf(" nonce search: appended 64-bit counter (ctr mode)\n");
    std::printf(" iterations per level (X): %d   (fresh random salt n_s each iter)\n", X);
    std::printf("================================================================\n\n");

    /* ---------- 1. Client-side computation (solve) ---------- */
    std::printf("[1] CLIENT-SIDE COMPUTATION  (solve the %d-node tree)\n", N_NODES);
    for (int k = 0; k < N_KAPPAS; ++k) {
        int kappa = KAPPAS[k];
        std::vector<double> times;
        times.reserve(X);
        uint64_t last_trials = 0;
        for (int it = 0; it < X; ++it) {
            uint8_t n_s[NS_LEN];
            RAND_bytes(n_s, NS_LEN);
            uint64_t trials = 0;
            auto op = [&]() { g_sink += solve_tree(n_s, kappa, &trials, nullptr, nullptr); };
            /* One solve is heavy enough to time directly (min_s reached in a
             * single rep for larger kappa; small kappa auto-repeats). No
             * separate warm-up: a solve is expensive, so per_call_us's own
             * first call serves that role and `trials` is valid afterwards. */
            times.push_back(per_call_us(op, 0.02));
            last_trials = trials;
        }
        Stats s = summarize(times);
        char tail[96];
        std::snprintf(tail, sizeof tail, "  (~%.0f hashes, last=%llu)",
                      (double)N_NODES * (double)((uint64_t)1 << kappa),
                      (unsigned long long)last_trials);
        char lbl[16];
        std::snprintf(lbl, sizeof lbl, "kappa=%d", kappa);
        print_row(lbl, s, tail);
    }

    /* ---------- 2. Server-side verification ---------- */
    std::printf("\n[2] SERVER-SIDE VERIFICATION  (recompute %d hashes, check kappa zeros)\n",
                N_NODES);
    for (int k = 0; k < N_KAPPAS; ++k) {
        int kappa = KAPPAS[k];
        std::vector<double> times;
        times.reserve(X);
        for (int it = 0; it < X; ++it) {
            uint8_t proof[NS_LEN + 3 * 8];
            RAND_bytes(proof, sizeof proof);        /* fresh random salt+nonces */
            auto op = [&]() { g_sink += (uint64_t)verify_tree(proof, kappa); };
            op();                                   /* warm-up */
            times.push_back(per_call_us(op, 0.02));
        }
        Stats s = summarize(times);
        char lbl[16];
        std::snprintf(lbl, sizeof lbl, "kappa=%d", kappa);
        print_row(lbl, s, "");
    }

    std::printf("\n(sink=%llu)\n", (unsigned long long)g_sink);
    return 0;
}
