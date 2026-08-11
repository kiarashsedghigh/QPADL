/*
 * HCT proof-check on ARM (single-threaded client bench).
 * Times one proof_verify_one call over a random-byte payload.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "proof_hct.h"
#include "../common/bench.h"

#ifndef ITERS
#define ITERS 500000
#endif

int main(void)
{
    uint8_t payload[PROOF_PAYLOAD_BYTES];
    for (size_t i = 0; i < PROOF_PAYLOAD_BYTES; ++i)
        payload[i] = (uint8_t)(i * 131u + 7u);

    printf("== HCT verify (client, ARM) — log2(n_l)=%d, κ=%d, payload=%d B, iters=%d\n",
           HCT_LOG_N_LEAVES, HCT_KAPPA_BITS, (int)PROOF_PAYLOAD_BYTES, ITERS);
    int sink = 0;
    BENCH_CI("HCT verify", 10, ITERS / 10, PROOF_PAYLOAD_BYTES, {
        sink += proof_verify_one(payload, (uint32_t)__i);
    });
    SINK(sink);
    return 0;
}
