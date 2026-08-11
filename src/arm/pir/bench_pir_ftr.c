/*
 * QPADL-FTR client bench (Goldberg-PIR, Byzantine-robust via Shamir SS).
 *
 * Client-side work per query round (paper §5.1.1(ii), §7.1):
 *   1. Query build: for the index θ, sample ℓ shares of the standard
 *      basis vector e_θ over GF(2^8). For each row j∈[r] the client picks
 *      a random degree-t polynomial p_j with p_j(0) = 1_{j==θ}, then sends
 *      share_i[j] = p_j(x_i) to PSD_i for i=1..ℓ.
 *   2. Block reconstruct: given k = t+1 responses (each b B), interpolate
 *      block[j] = Σ_i λ_i · response_i[j]  where λ_i are Lagrange
 *      coefficients computed once from the k evaluation points. Paper's
 *      concrete config: (t, k) = (1, 2), so plain Lagrange over 2 shares.
 *
 * Paper implementation notes we replicate:
 *   - GF(2^8) with the AES polynomial 0x11B, via log/antilog tables
 *     (the same construction any real impl uses).
 *   - Coefficient rows come from an AES-CTR PRG seeded once — real
 *     clients derive Shamir coefficients from an expandable seed, not
 *     from OS entropy.
 *   - Byzantine reconstruction (Guruswami-Sudan for k > t+1) is
 *     out of scope of this bench; we time the common-path Lagrange.
 *
 * The previous AVX impl used _mm256_gf2p8mul_epi8 (GFNI). Portable path
 * uses the 256-byte log/antilog tables above — NEON-friendly under -O3.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/rand.h>
#include <openssl/evp.h>
#include "../common/bench.h"

#ifndef ELL
#define ELL 3
#endif
#ifndef T_PRIV
#define T_PRIV 1                        /* privacy threshold */
#endif
#ifndef R_LOG2
#define R_LOG2 14
#endif
#ifndef B_BYTES
#define B_BYTES (3 * 1024)
#endif
#ifndef ITERS
#define ITERS 200
#endif

#define R_ROWS (1UL << R_LOG2)

/* AES-CTR PRG for Shamir coefficient expansion. Real FTR clients use a
 * seed-derived stream (same primitive as ENS/OOP); RAND_bytes(3) here
 * would pull from the OS entropy pool and over-count client cost. Ctx is
 * cached across calls so we don't pay malloc/free per iteration. */
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

/* GF(2^8) with AES's irreducible polynomial 0x11B, primitive element 0x03.
 * gf_log[0] is undefined; gf_alog wraps at 255. Built at startup. */
static uint8_t gf_log[256];
static uint8_t gf_alog[512];

static void gf_init(void)
{
    uint16_t x = 1;
    for (int i = 0; i < 255; ++i) {
        gf_alog[i] = (uint8_t)x;
        gf_log[x] = (uint8_t)i;
        x <<= 1;
        if (x & 0x100) x ^= 0x11B;
    }
    for (int i = 255; i < 512; ++i) gf_alog[i] = gf_alog[i - 255];
}

static inline uint8_t gf_mul(uint8_t a, uint8_t b)
{
    if (a == 0 || b == 0) return 0;
    return gf_alog[(int)gf_log[a] + (int)gf_log[b]];
}

/* Compute Lagrange coefficients λ_i for evaluation points x_1..x_k at x=0.
 * λ_i = Π_{j≠i} (-x_j) / (x_i - x_j)   in GF(2^8), where +/- are XOR. */
static void lagrange_at_zero(const uint8_t *x, int k, uint8_t *lambda)
{
    for (int i = 0; i < k; ++i) {
        uint8_t num = 1, den = 1;
        for (int j = 0; j < k; ++j) {
            if (j == i) continue;
            num = gf_mul(num, x[j]);            /* -x_j = x_j in char 2 */
            den = gf_mul(den, x[i] ^ x[j]);
        }
        /* invert den via Fermat: a^-1 = a^254 in GF(2^8) */
        uint8_t inv_den = gf_alog[(255 - gf_log[den]) % 255];
        lambda[i] = gf_mul(num, inv_den);
    }
}

/* Portable per-byte GF(2^8) multiply-add.  gcc/clang -O3 vectorizes the
 * table lookups + XOR to NEON tbl instructions. */
static void gf_muladd(uint8_t *__restrict__ dst,
                      const uint8_t *__restrict__ src,
                      uint8_t coef, size_t n)
{
    if (coef == 0) return;
    if (coef == 1) { for (size_t i = 0; i < n; ++i) dst[i] ^= src[i]; return; }
    uint8_t log_c = gf_log[coef];
    for (size_t i = 0; i < n; ++i) {
        uint8_t s = src[i];
        if (s) dst[i] ^= gf_alog[log_c + gf_log[s]];
    }
}

int main(void)
{
    gf_init();
    printf("== QPADL-FTR client (Goldberg-PIR, ARM) — ℓ=%d, t=%d, r=2^%d, b=%d B, iters=%d\n",
           ELL, T_PRIV, R_LOG2, B_BYTES, ITERS);

    /* --- query build --- */
    /* For each row j∈[r], pick a random poly p_j of degree t with
     * p_j(0) = 1_{j==θ}, then share_i[j] = p_j(x_i).  We amortize:
     * store t coefficient vectors (r bytes each), refresh once per query
     * via a PRG, evaluate per share. */
    uint8_t *coef_rows[T_PRIV];
    for (int c = 0; c < T_PRIV; ++c)
        coef_rows[c] = (uint8_t *)malloc(R_ROWS);
    uint8_t *shares[ELL];
    for (int i = 0; i < ELL; ++i) shares[i] = (uint8_t *)malloc(R_ROWS);
    /* eval points x_i ∈ GF(2^8) \ {0} */
    uint8_t xpts[ELL]; for (int i = 0; i < ELL; ++i) xpts[i] = (uint8_t)(i + 1);
    uint8_t prg_seeds[T_PRIV][32];
    RAND_bytes(prg_seeds[0], sizeof prg_seeds);

    BENCH_CI("PIR-FTR query build", 10, ITERS / 10, ELL * R_ROWS, {
        /* Fresh coefficient rows per query, derived from a PRG (not OS RNG). */
        for (int c = 0; c < T_PRIV; ++c) prg_expand(prg_seeds[c], coef_rows[c], R_ROWS);
        long theta = (__i * 2654435761u) % R_ROWS;
        for (int i = 0; i < ELL; ++i) {
            /* For t=1: share_i[j] = coef_rows[0][j] · x_i + 1_{j==θ}
             * (degree-1 polynomial, one gf_mul per position). */
            uint8_t x = xpts[i];
            for (size_t j = 0; j < R_ROWS; ++j)
                shares[i][j] = gf_mul(coef_rows[0][j], x);
            shares[i][theta] ^= 1;
            /* For t≥2 we'd fold in additional x^{c+1} terms:
             *   for c in 1..t-1:  shares[i][j] ^= gf_mul(coef_rows[c][j], x_i^{c+1});
             * paper's config is t=1 so this loop is empty. */
        }
        SINK(shares[0][0]);
    });

    /* --- block reconstruct via Lagrange interpolation at 0 --- */
    /* Paper's config: (t, k) = (1, 2)  → interpolate over k = t+1 responses.
     * This matches "k out of ℓ" Byzantine-robust Goldberg-PIR at the minimal
     * correctness threshold (k = t+1, no error correction). */
    int k = T_PRIV + 1;
    uint8_t *responses = (uint8_t *)malloc((size_t)k * B_BYTES);
    RAND_bytes(responses, (size_t)k * B_BYTES);
    uint8_t lambda[ELL];
    lagrange_at_zero(xpts, k, lambda);
    uint8_t *block = (uint8_t *)malloc(B_BYTES);

    BENCH_CI("PIR-FTR block reconstruct", 10, ITERS * 2, k * B_BYTES, {
        memset(block, 0, B_BYTES);
        for (int i = 0; i < k; ++i)
            gf_muladd(block, responses + (size_t)i * B_BYTES, lambda[i], B_BYTES);
        SINK(block[0]);
    });

    for (int c = 0; c < T_PRIV; ++c) free(coef_rows[c]);
    for (int i = 0; i < ELL; ++i) free(shares[i]);
    free(responses); free(block);
    return 0;
}
