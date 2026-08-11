/*
 * QPADL-OOP client bench (CIP-PIR, online-offline preprocessing).
 *
 * Client-side work per online query (paper §5.1.1 (iii), §5.1.3):
 *   1. PRG-expand ℓ seeds S_i (32 B each) into r-bit sub-queries q_i,
 *      each of length (t-1)·bw bits — the chunk-permutation pattern.
 *      Only one chunk of size b/n is queried online; the rest of the
 *      DB has been precomputed offline into A_i values (paper Alg 3).
 *   2. XOR each PRG-expanded chunk selection with the flip pattern to
 *      form the actual per-server query q_i.
 *   3. Block reconstruct: XOR ℓ responses + XOR precomputed A_i.
 *
 * Bench captures the online path only, which is the SU-visible latency.
 * Offline preprocessing is per-day amortized (paper §5.1.3) and out of
 * scope for a per-request client bench.
 *
 * Portable: PRG via AES-256-CTR (ARMv8 crypto ext); XOR via 8-byte-wise
 * loop (auto-vectorized to NEON).
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include "../common/bench.h"

#ifndef ELL
#define ELL 3
#endif
#ifndef R_LOG2
#define R_LOG2 14
#endif
#ifndef B_BYTES
#define B_BYTES (3 * 1024)
#endif
#ifndef N_CHUNKS
#define N_CHUNKS 8                      /* B split into n chunks; online reads 1 */
#endif
#ifndef ITERS
#define ITERS 200
#endif

#define R_ROWS  (1UL << R_LOG2)
#define R_BYTES (R_ROWS / 8)

/* Ctx cached across calls — see bench_pir_ens.c for the rationale. */
static void prg_expand(const uint8_t seed[32], uint8_t *out, size_t out_bytes)
{
    static const uint8_t iv[16] = {0};
    static EVP_CIPHER_CTX *ctx = NULL;
    if (!ctx) ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_256_ctr(), NULL, seed, iv);
    memset(out, 0, out_bytes);
    int outl = 0;
    EVP_EncryptUpdate(ctx, out, &outl, out, (int)out_bytes);
}

static inline void xor_bytes(uint8_t *__restrict__ dst,
                             const uint8_t *__restrict__ src, size_t n)
{
    size_t i = 0;
    uint64_t *d = (uint64_t *)dst;
    const uint64_t *s = (const uint64_t *)src;
    for (; i + 8 <= n; i += 8) *d++ ^= *s++;
    for (; i < n; ++i) dst[i] ^= src[i];
}

int main(void)
{
    printf("== QPADL-OOP client (CIP-PIR, ARM) — ℓ=%d, r=2^%d, b=%d B, chunks=%d, iters=%d\n",
           ELL, R_LOG2, B_BYTES, N_CHUNKS, ITERS);

    size_t chunk_bytes = B_BYTES / N_CHUNKS;
    uint8_t seeds[ELL][32];
    RAND_bytes(seeds[0], sizeof seeds);
    uint8_t *q[ELL];
    for (int i = 0; i < ELL; ++i) q[i] = (uint8_t *)malloc(R_BYTES);
    uint8_t flip_pattern[R_BYTES];      /* θ-bit set, one-hot */

    /* --- query build: online path only --- */
    /* Each server sees a randomly-permuted chunk order; client PRG-
     * expands and XORs a small flip pattern to encode θ. */
    BENCH_CI("PIR-OOP query build", 10, ITERS / 10, ELL * R_BYTES, {
        long theta = (__i * 2654435761u) % R_ROWS;
        memset(flip_pattern, 0, R_BYTES);
        flip_pattern[theta / 8] ^= (uint8_t)(1u << (theta % 8));
        for (int i = 0; i < ELL; ++i) {
            prg_expand(seeds[i], q[i], R_BYTES);
            xor_bytes(q[i], flip_pattern, R_BYTES);
        }
        SINK(q[0][0]);
    });

    /* --- block reconstruct: XOR ℓ online responses ⊕ ℓ precomputed A_i --- */
    /* All responses/A_i are chunk-sized (b/n bytes) — that's the online win. */
    uint8_t *online_resps = (uint8_t *)malloc((size_t)ELL * chunk_bytes);
    uint8_t *A_i          = (uint8_t *)malloc((size_t)ELL * chunk_bytes);
    RAND_bytes(online_resps, (size_t)ELL * chunk_bytes);
    RAND_bytes(A_i,          (size_t)ELL * chunk_bytes);
    uint8_t *out = (uint8_t *)malloc(chunk_bytes);

    BENCH_CI("PIR-OOP block reconstruct", 10, ITERS * 10, 2 * ELL * chunk_bytes, {
        memcpy(out, online_resps, chunk_bytes);
        for (int i = 1; i < ELL; ++i)
            xor_bytes(out, online_resps + (size_t)i * chunk_bytes, chunk_bytes);
        for (int i = 0; i < ELL; ++i)
            xor_bytes(out, A_i + (size_t)i * chunk_bytes, chunk_bytes);
        SINK(out[0]);
    });

    for (int i = 0; i < ELL; ++i) free(q[i]);
    free(online_resps); free(A_i); free(out);
    return 0;
}
