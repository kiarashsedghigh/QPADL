/*
 * ML-DSA-<MODE> verify on ARM (client-side, single-thread).
 * Client verifies the PSD's signature on the retrieved puzzle
 * (paper Alg 1, step 23: `1 = ML-DSA.Verify(PK_PSD, Π, σ_Π)`).
 *
 * Uses the pq-crystals dilithium ref implementation, which is pure C and
 * portable to ARM without changes. The Makefile clones the ref repo on
 * `make setup`.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "params.h"
#include "sign.h"
#include "../common/bench.h"

#ifndef ITERS
#define ITERS 5000
#endif
#define MSGLEN 59

int main(void)
{
    uint8_t pk[CRYPTO_PUBLICKEYBYTES];
    uint8_t sk[CRYPTO_SECRETKEYBYTES];
    uint8_t sig[CRYPTO_BYTES];
    uint8_t msg[MSGLEN];
    size_t  siglen = 0;
    for (int i = 0; i < MSGLEN; ++i) msg[i] = (uint8_t)i;

    crypto_sign_keypair(pk, sk);
    crypto_sign_signature(sig, &siglen, msg, MSGLEN, NULL, 0, sk);
    if (crypto_sign_verify(sig, siglen, msg, MSGLEN, NULL, 0, pk) != 0) {
        fprintf(stderr, "self-verify failed\n"); return 1;
    }

    printf("== ML-DSA-%d verify (client, ARM) — sig=%zu B, pk=%d B, iters=%d\n",
           DILITHIUM_MODE, siglen, CRYPTO_PUBLICKEYBYTES, ITERS);
    int sink = 0;
    BENCH_CI("ML-DSA verify", 10, ITERS / 10,
             (int)siglen + CRYPTO_PUBLICKEYBYTES + MSGLEN, {
        sink += crypto_sign_verify(sig, siglen, msg, MSGLEN, NULL, 0, pk);
    });
    SINK(sink);
    return 0;
}
