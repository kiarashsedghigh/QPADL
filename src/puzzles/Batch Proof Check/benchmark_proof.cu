/*
 * Batched proof-check on GPU — one puzzle verify per thread, no ML-DSA.
 * Mirrors ../Batch Verification/benchmark_verify.cu structurally so the
 * timing conventions (H2D / kernel / D2H) and sweep harness are shared.
 *
 * The proof implementation is behind a single #include below; swap it for
 * proof_lbp.h once LBP verify lands and everything else stays put.
 */

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cuda_runtime.h>

/* ---- pick the puzzle type ----------------------------------------------- */
/* Build with -DPROOF_LBP to run the lattice-based puzzle; default is HCT.
 * Both headers export the same interface — PROOF_PAYLOAD_BYTES,
 * PROOF_TYPE_NAME, and PROOF_HD int proof_verify_one(payload, leaf) — so
 * everything downstream of this include is proof-agnostic. */
#ifdef PROOF_LBP
#  include "proof_lbp.h"
#else
#  include "proof_hct.h"
#endif

#ifndef BATCH_SIZE
#define BATCH_SIZE 4096
#endif
#ifndef THREADS_PER_BLOCK
#define THREADS_PER_BLOCK 32
#endif
#ifndef REPEATS
#define REPEATS 500
#endif
#ifndef PER_THREAD_STACK_KB
#define PER_THREAD_STACK_KB 32
#endif

#define CUDA_CHECK(x) do {                                                  \
    cudaError_t _e = (x);                                                   \
    if (_e != cudaSuccess) {                                                \
        fprintf(stderr, "CUDA error %s:%d: %s\n",                           \
                __FILE__, __LINE__, cudaGetErrorString(_e));                \
        exit(1);                                                            \
    }                                                                       \
} while (0)

/* One thread = one puzzle verify. Result = non-zero-byte count so the
 * compiler can't dead-code-eliminate the work. HCT uses leaf_idx to pick
 * which path to walk; LBP ignores it. Pass thread id and let the proof
 * header do what it wants. */
__global__ void proof_kernel(const uint8_t * __restrict__ payloads,
                             int *           __restrict__ results,
                             int                          n)
{
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= n) return;

    const uint8_t *pl = payloads + (size_t)idx * PROOF_PAYLOAD_BYTES;
    results[idx] = proof_verify_one(pl, (uint32_t)idx);
}

int main(void)
{
    printf("Puzzle proof-check:    %s\n", PROOF_TYPE_NAME);
#ifdef PROOF_LBP
    printf("  LBP_N (dim):         %d\n", LBP_N);
    printf("  LBP_P:               %u\n", (unsigned)LBP_P);
#else
    printf("  HCT_LOG_N_LEAVES:    %d  (n_l = %d)\n",
           HCT_LOG_N_LEAVES, 1 << HCT_LOG_N_LEAVES);
    printf("  HCT_KAPPA_BITS:      %d\n", HCT_KAPPA_BITS);
#endif
    printf("  payload per verify:  %d B\n\n", (int)PROOF_PAYLOAD_BYTES);

    /* -------- 1. build BATCH_SIZE payloads on host ---------------------- */
    size_t bytes_pl = (size_t)BATCH_SIZE * PROOF_PAYLOAD_BYTES;
    uint8_t *h_pl = (uint8_t *)malloc(bytes_pl);
    if (!h_pl) { fprintf(stderr, "malloc failed\n"); return 1; }
    /* Puzzles assumed valid, so contents don't affect verify semantics;
     * a deterministic fill lets us copy exactly the per-verify byte count. */
    for (size_t i = 0; i < bytes_pl; ++i) h_pl[i] = (uint8_t)(i * 131u + 7u);

    size_t mem_free_before = 0, mem_total = 0;
    CUDA_CHECK(cudaMemGetInfo(&mem_free_before, &mem_total));

    uint8_t *d_pl;
    int     *d_results;
    CUDA_CHECK(cudaMalloc(&d_pl,      bytes_pl));
    CUDA_CHECK(cudaMalloc(&d_results, (size_t)BATCH_SIZE * sizeof(int)));
    CUDA_CHECK(cudaMemcpy(d_pl, h_pl, bytes_pl, cudaMemcpyHostToDevice));

    /* -------- 2. GPU context + stack ------------------------------------ */
    cudaFuncAttributes fattr;
    CUDA_CHECK(cudaFuncGetAttributes(&fattr, proof_kernel));

    int dev; cudaGetDevice(&dev);
    cudaDeviceProp prop; cudaGetDeviceProperties(&prop, dev);
    size_t max_resident = (size_t)prop.maxThreadsPerMultiProcessor
                        * (size_t)prop.multiProcessorCount;
    const size_t stack_candidates_kb[] = { PER_THREAD_STACK_KB, 32, 16, 8, 4 };
    size_t chosen_stack_kb = 0;
    for (size_t i = 0; i < sizeof(stack_candidates_kb)/sizeof(stack_candidates_kb[0]); ++i) {
        if (cudaDeviceSetLimit(cudaLimitStackSize, stack_candidates_kb[i] * 1024) == cudaSuccess) {
            chosen_stack_kb = stack_candidates_kb[i];
            break;
        }
        cudaGetLastError();
    }
    if (!chosen_stack_kb) { fprintf(stderr, "no stack fits\n"); return 1; }

    size_t stack_lim; cudaDeviceGetLimit(&stack_lim, cudaLimitStackSize);
    printf("Device:                %s (sm_%d%d)\n", prop.name, prop.major, prop.minor);
    printf("  SMs × threads/SM:    %d × %d = %zu resident threads\n",
           prop.multiProcessorCount, prop.maxThreadsPerMultiProcessor, max_resident);
    printf("  per-thread stack:    %zu KB\n", stack_lim / 1024);
    printf("  kernel local mem:    %zu B / thread\n", fattr.localSizeBytes);
    printf("  kernel registers:    %d\n", fattr.numRegs);
    printf("  batch size:          %d\n", BATCH_SIZE);
    printf("  threads/block:       %d\n\n", THREADS_PER_BLOCK);

    printf("== GPU memory ==\n");
    printf("  d_payloads:          %.2f MB  (%d B/verify)\n",
           bytes_pl / (1024.0 * 1024.0), (int)PROOF_PAYLOAD_BYTES);
    printf("  d_results:           %.2f MB\n",
           (BATCH_SIZE * sizeof(int)) / (1024.0 * 1024.0));
    printf("  input buffers sum:   %.2f MB\n\n",
           (bytes_pl + BATCH_SIZE * sizeof(int)) / (1024.0 * 1024.0));

    int blocks = (BATCH_SIZE + THREADS_PER_BLOCK - 1) / THREADS_PER_BLOCK;

    /* -------- 3. warmup + timing ---------------------------------------- */
    proof_kernel<<<blocks, THREADS_PER_BLOCK>>>(d_pl, d_results, BATCH_SIZE);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());

    int *h_results = (int *)malloc((size_t)BATCH_SIZE * sizeof(int));

    cudaEvent_t ev_h2d_start, ev_h2d_end, ev_kernel_end, ev_d2h_end;
    cudaEventCreate(&ev_h2d_start);
    cudaEventCreate(&ev_h2d_end);
    cudaEventCreate(&ev_kernel_end);
    cudaEventCreate(&ev_d2h_end);

    float ms_h2d_total = 0.0f, ms_kernel_total = 0.0f, ms_d2h_total = 0.0f;
    for (int r = 0; r < REPEATS; ++r) {
        cudaEventRecord(ev_h2d_start);
        CUDA_CHECK(cudaMemcpyAsync(d_pl, h_pl, bytes_pl, cudaMemcpyHostToDevice));
        cudaEventRecord(ev_h2d_end);

        proof_kernel<<<blocks, THREADS_PER_BLOCK>>>(d_pl, d_results, BATCH_SIZE);
        cudaEventRecord(ev_kernel_end);

        CUDA_CHECK(cudaMemcpyAsync(h_results, d_results,
                                   (size_t)BATCH_SIZE * sizeof(int),
                                   cudaMemcpyDeviceToHost));
        cudaEventRecord(ev_d2h_end);
        cudaEventSynchronize(ev_d2h_end);

        float ms;
        cudaEventElapsedTime(&ms, ev_h2d_start,  ev_h2d_end);    ms_h2d_total    += ms;
        cudaEventElapsedTime(&ms, ev_h2d_end,    ev_kernel_end); ms_kernel_total += ms;
        cudaEventElapsedTime(&ms, ev_kernel_end, ev_d2h_end);    ms_d2h_total    += ms;
    }

    float ms_h2d_avg    = ms_h2d_total    / REPEATS;
    float ms_kernel_avg = ms_kernel_total / REPEATS;
    float ms_d2h_avg    = ms_d2h_total    / REPEATS;
    float ms_e2e_avg    = ms_h2d_avg + ms_kernel_avg + ms_d2h_avg;

    /* Sanity: the hash for-loop should produce non-zero counts on random
     * data; if the sum is 0 the compiler probably dead-code-eliminated
     * something. We don't fail the run but we flag it in the output. */
    long long sink = 0;
    for (int i = 0; i < BATCH_SIZE; ++i) sink += h_results[i];

    printf("== GPU %s proof-check — phase breakdown ==\n", PROOF_TYPE_NAME);
    printf("  repeats:             %d\n", REPEATS);
    printf("  batch size:          %d\n\n", BATCH_SIZE);

    printf("  [H2D copy]\n");
    printf("    time / batch:      %.3f ms\n", ms_h2d_avg);
    printf("    per verify:        %.2f us\n", ms_h2d_avg    * 1000.0f / BATCH_SIZE);
    printf("    throughput:        %.0f verify/s\n", BATCH_SIZE * 1000.0f / ms_h2d_avg);

    printf("  [kernel / compute]\n");
    printf("    time / batch:      %.3f ms\n", ms_kernel_avg);
    printf("    per verify:        %.2f us\n", ms_kernel_avg * 1000.0f / BATCH_SIZE);
    printf("    throughput:        %.0f verify/s\n", BATCH_SIZE * 1000.0f / ms_kernel_avg);

    printf("  [D2H copy]\n");
    printf("    time / batch:      %.3f ms\n", ms_d2h_avg);
    printf("    per verify:        %.2f us\n", ms_d2h_avg    * 1000.0f / BATCH_SIZE);
    printf("    throughput:        %.0f verify/s\n", BATCH_SIZE * 1000.0f / ms_d2h_avg);

    printf("  [end-to-end (H2D + kernel + D2H, serialized)]\n");
    printf("    time / batch:      %.3f ms\n", ms_e2e_avg);
    printf("    per verify:        %.2f us\n", ms_e2e_avg    * 1000.0f / BATCH_SIZE);
    printf("    throughput:        %.0f verify/s\n", BATCH_SIZE * 1000.0f / ms_e2e_avg);

    printf("  sink (nz-count sum): %lld  %s\n",
           sink,
           sink == 0 ? "(WARNING: dead-code elimination?)" : "");

    cudaFree(d_pl); cudaFree(d_results);
    free(h_pl); free(h_results);
    cudaEventDestroy(ev_h2d_start);
    cudaEventDestroy(ev_h2d_end);
    cudaEventDestroy(ev_kernel_end);
    cudaEventDestroy(ev_d2h_end);
    return 0;
}
