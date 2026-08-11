/*
 * SHA-256 throughput on ARM. Uses OpenSSL's EVP_Digest, which auto-picks
 * ARMv8-A Crypto Extensions (SHA-256 fixed-function unit) at runtime when
 * available — no build-time flags needed.
 *
 * Client uses SHA-256 for HCT hash-cash chain and for commitment/pseudonym
 * derivation in the PoL phase (H, H' in paper §5.1.1).
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/sha.h>
#include "../common/bench.h"

#ifndef MSG_LEN
#define MSG_LEN 64          /* typical PoL commitment input size */
#endif
#ifndef ITERS
#define ITERS  1000000
#endif

int main(void)
{
    uint8_t msg[MSG_LEN];
    uint8_t out[SHA256_DIGEST_LENGTH];
    for (int i = 0; i < MSG_LEN; ++i) msg[i] = (uint8_t)(i * 131u + 7u);

    printf("== SHA-256 (client-side, ARM) — msg=%d B, samples=10 x iters=%d\n",
           MSG_LEN, ITERS / 10);
    BENCH_CI("SHA-256", 10, ITERS / 10, MSG_LEN, {
        SHA256(msg, MSG_LEN, out);
        SINK(out[0]);
    });
    return 0;
}
