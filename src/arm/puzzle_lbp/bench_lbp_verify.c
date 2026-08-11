/*
 * LBP proof-check on ARM (single-threaded client bench).
 * Uses the same proof_lbp.h shipped under puzzles/CPU Batch Proof Check/,
 * which is pure integer arithmetic (no hash calls, no external deps).
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "proof_lbp.h"
#include "../common/bench.h"

#ifndef ITERS
#define ITERS 500000
#endif

int main(void)
{
    uint8_t payload[PROOF_PAYLOAD_BYTES];
    for (size_t i = 0; i < PROOF_PAYLOAD_BYTES; ++i)
        payload[i] = (uint8_t)(i * 131u + 7u);

    printf("== LBP verify (client, ARM) — n=%d, payload=%d B, iters=%d\n",
           LBP_N, (int)PROOF_PAYLOAD_BYTES, ITERS);
    int sink = 0;
    BENCH_CI("LBP verify", 10, ITERS / 10, PROOF_PAYLOAD_BYTES, {
        sink += proof_verify_one(payload, (uint32_t)__i);
    });
    SINK(sink);
    return 0;
}
