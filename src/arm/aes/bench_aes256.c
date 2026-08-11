/*
 * AES-256-CTR throughput on ARM. OpenSSL auto-selects ARMv8-A AES crypto
 * extensions when the CPU supports them (all Cortex-A72+ do).
 *
 * Client uses AES-256 as the symmetric layer of the PQ-Tor onion
 * (paper §2.2.1 / §7.1: "PQ-Tor uses AES-256 for layered encryption,
 * ML-KEM for per-hop encapsulation").
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include "../common/bench.h"

#ifndef BLOCK_LEN
#define BLOCK_LEN 512       /* per-hop payload; PQ-Tor cells are ~512 B */
#endif
#ifndef ITERS
#define ITERS  500000
#endif

int main(void)
{
    uint8_t key[32], iv[16], in[BLOCK_LEN], out[BLOCK_LEN + 16];
    RAND_bytes(key, sizeof key);
    RAND_bytes(iv, sizeof iv);
    for (int i = 0; i < BLOCK_LEN; ++i) in[i] = (uint8_t)(i * 131u + 7u);

    printf("== AES-256-CTR (PQ-Tor symmetric layer, ARM) — block=%d B, iters=%d\n",
           BLOCK_LEN, ITERS);

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    BENCH_CI("AES-256-CTR encrypt", 10, ITERS / 10, BLOCK_LEN, {
        int outl = 0;
        EVP_EncryptInit_ex(ctx, EVP_aes_256_ctr(), NULL, key, iv);
        EVP_EncryptUpdate(ctx, out, &outl, in, BLOCK_LEN);
        SINK(out[0]);
    });
    EVP_CIPHER_CTX_free(ctx);

    return 0;
}
