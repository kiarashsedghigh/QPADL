/*
 * SHA3-256 throughput on ARM. Uses OpenSSL EVP_Digest with EVP_sha3_256().
 * ARMv8 has no fixed-function SHA-3 unit (only SHA-1/SHA-2), so this is a
 * software Keccak implementation from OpenSSL, still hand-tuned for ARM.
 *
 * Client uses SHA3-256 as an alternative to SHA-256 for HCT/commitments
 * when the paper's parameter set selects it (see paper §5.1.1 note on H').
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <openssl/evp.h>
#include "../common/bench.h"

#ifndef MSG_LEN
#define MSG_LEN 64
#endif
#ifndef ITERS
#define ITERS  500000
#endif

int main(void)
{
    uint8_t msg[MSG_LEN];
    uint8_t out[32];
    for (int i = 0; i < MSG_LEN; ++i) msg[i] = (uint8_t)(i * 131u + 7u);

    /* Reuse a single EVP_MD_CTX across iterations so the malloc + hash-
     * type lookup happens once. */
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    const EVP_MD *md = EVP_sha3_256();

    printf("== SHA3-256 (client-side, ARM) — msg=%d B, samples=10 x iters=%d\n",
           MSG_LEN, ITERS / 10);
    BENCH_CI("SHA3-256", 10, ITERS / 10, MSG_LEN, {
        unsigned int len = 0;
        EVP_DigestInit_ex(ctx, md, NULL);
        EVP_DigestUpdate(ctx, msg, MSG_LEN);
        EVP_DigestFinal_ex(ctx, out, &len);
        SINK(out[0]);
    });

    EVP_MD_CTX_free(ctx);
    return 0;
}
