/*
 * QPADL-ENS client bench (Chor-PIR, GF(2), ℓ non-colluding servers).
 *
 * Client-side work per query round:
 *   1. Query build: for each of ℓ servers, produce an r-bit selection
 *      vector s_i ∈ {0,1}^r. Sum (XOR) equals the standard basis
 *      vector e_θ.  Implemented as: (ℓ-1) random vectors + one XOR'd
 *      completer. Random draws are PRG-expanded from a small seed
 *      (32 B), which is the on-wire query cost per server.
 *   2. Block reconstruct: XOR the ℓ b-bit block responses.
 *
 * The AVX implementation used _mm256_xor_si256; the portable code below
 * is a plain uint64_t XOR loop that gcc -O3 auto-vectorizes to NEON
 * (ARMv8 has 128-bit veorq_u64). Nothing x86-specific.
 *
 * Sizes come from paper §7 (Table 5): r ∈ {2^12, 2^14, 2^16, 2^18}
 * (DB rows) and b = 3 KB per block. ℓ = 3 non-colluding PSDs.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include "../common/bench.h"

#ifndef ELL
#define ELL 3                           /* non-colluding PSDs */
#endif
#ifndef R_LOG2
#define R_LOG2 14                       /* r = 2^R_LOG2 DB rows */
#endif
#ifndef B_BYTES
#define B_BYTES (3 * 1024)              /* 3 KB block per paper */
#endif
#ifndef ITERS
#define ITERS 200
#endif

#define R_ROWS  (1UL << R_LOG2)
#define R_BYTES (R_ROWS / 8)            /* bit-vector packed */

/* PRG-expand a 32-byte seed into `out_bytes` output bytes using AES-256-CTR.
 * Context is cached across calls — creating/freeing an EVP_CIPHER_CTX per
 * call adds ~5 µs of malloc/free that dwarfs the actual crypto at small r
 * and breaks the paper's ~constant-vs-r trend for the ENS client. */
static void prg_expand(const uint8_t seed[32], uint8_t *out, size_t out_bytes)
{
    static const uint8_t iv[16] = {0};
    static EVP_CIPHER_CTX *ctx = NULL;
    if (!ctx) ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_256_ctr(), NULL, seed, iv);
    int outl = 0;
    memset(out, 0, out_bytes);          /* CTR: XOR keystream into 0 = keystream */
    EVP_EncryptUpdate(ctx, out, &outl, out, (int)out_bytes);
}

/* Portable 8-byte-wise XOR — gcc/clang -O3 auto-vectorizes to NEON. */
static inline void xor_bytes(uint8_t *__restrict__ dst,
                             const uint8_t *__restrict__ src,
                             size_t n)
{
    size_t i = 0;
    uint64_t *d64 = (uint64_t *)dst;
    const uint64_t *s64 = (const uint64_t *)src;
    for (; i + 8 <= n; i += 8) *d64++ ^= *s64++;
    for (; i < n; ++i) dst[i] ^= src[i];
}

int main(void)
{
    printf("== QPADL-ENS client (Chor-PIR, ARM) — ℓ=%d, r=2^%d, b=%d B, iters=%d\n",
           ELL, R_LOG2, B_BYTES, ITERS);

    /* Preallocate ℓ query vectors (r-bit each) and one response scratch. */
    uint8_t *q[ELL];
    for (int i = 0; i < ELL; ++i) q[i] = (uint8_t *)malloc(R_BYTES);
    uint8_t *resp = (uint8_t *)malloc(B_BYTES);
    uint8_t seeds[ELL][32];
    RAND_bytes(seeds[0], sizeof(seeds));

    /* --- query build --- */
    /* Build ℓ-1 pseudo-random vectors, then set the last so ⊕ = e_θ. */
    BENCH_CI("PIR-ENS query build", 10, ITERS / 10, ELL * R_BYTES, {
        for (int i = 0; i < ELL - 1; ++i) prg_expand(seeds[i], q[i], R_BYTES);
        /* completer q[ℓ-1] = e_θ ⊕ q[0] ⊕ ... ⊕ q[ℓ-2] */
        memset(q[ELL - 1], 0, R_BYTES);
        long theta = (__i * 2654435761u) % R_ROWS;   /* pseudo-random θ */
        q[ELL - 1][theta / 8] ^= (uint8_t)(1u << (theta % 8));
        for (int i = 0; i < ELL - 1; ++i) xor_bytes(q[ELL - 1], q[i], R_BYTES);
        SINK(q[ELL - 1][0]);
    });

    /* --- block reconstruct: XOR ℓ b-byte responses --- */
    uint8_t *responses = (uint8_t *)malloc((size_t)ELL * B_BYTES);
    RAND_bytes(responses, (size_t)ELL * B_BYTES);
    BENCH_CI("PIR-ENS block reconstruct", 10, ITERS * 10, ELL * B_BYTES, {
        memcpy(resp, responses, B_BYTES);
        for (int i = 1; i < ELL; ++i)
            xor_bytes(resp, responses + (size_t)i * B_BYTES, B_BYTES);
        SINK(resp[0]);
    });

    free(resp);
    free(responses);
    for (int i = 0; i < ELL; ++i) free(q[i]);
    return 0;
}
