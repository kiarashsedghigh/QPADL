/*
 * Batched Dilithium verification on GPU — reference implementation.
 *
 * Each CUDA thread runs one full crypto_sign_verify. We time N concurrent
 * verifications and report aggregate throughput.
 *
 * The reference code is compiled unchanged except for __host__ __device__
 * qualifiers added by patch.py. Per-thread local memory usage is high
 * (16-56 KB depending on Dilithium mode) because polyvecl mat[K] lives on
 * the "stack" (really per-thread local memory, backed by global). Expected.
 */

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cuda_runtime.h>

/* params.h gives us CRYPTO_PUBLICKEYBYTES, CRYPTO_SECRETKEYBYTES, CRYPTO_BYTES.
 * sign.h gives us the function prototypes. We deliberately do NOT include
 * api.h — it redeclares the same functions under their fully namespaced names
 * without the __host__ __device__ qualifier, which triggers warning #20040.
 *
 * NOTE: no `extern "C"` wrapper here. The patched sign.cu / *.cu files are
 * compiled as C++ (all .cu files are), so their function definitions get
 * C++ mangling. Declaring the prototypes as extern "C" here would leave the
 * call sites looking for an unmangled symbol name that no object exports,
 * producing an nvlink error like:
 *   Undefined reference to 'pqcrystals_dilithium2_ref_verify'
 * Keeping the include in C++ linkage matches the definitions. */
#include "params.h"
#include "sign.h"

#ifndef BATCH_SIZE
#define BATCH_SIZE 4096
#endif
#ifndef THREADS_PER_BLOCK
#define THREADS_PER_BLOCK 32
#endif
#ifndef REPEATS
#define REPEATS 1000
#endif

/* Per-thread CUDA call-stack limit, in KB. The runtime reserves
 * PER_THREAD_STACK_KB × max_resident_threads × num_SMs of device memory the
 * first time you push this up, so on a 40-SM GPU with 1536 resident threads/SM
 * a value of 256 KB tries to reserve ~15 GB and OOMs at cudaDeviceSetLimit.
 * The reference verify path fits comfortably in 32 KB per thread. Override
 * with -DPER_THREAD_STACK_KB=<n> if a verify ever hits an illegal address. */
#ifndef PER_THREAD_STACK_KB
#define PER_THREAD_STACK_KB 32
#endif

#define MSGLEN 59

#define CUDA_CHECK(x) do {                                                  \
    cudaError_t _e = (x);                                                   \
    if (_e != cudaSuccess) {                                                \
        fprintf(stderr, "CUDA error %s:%d: %s\n",                           \
                __FILE__, __LINE__, cudaGetErrorString(_e));                \
        exit(1);                                                            \
    }                                                                       \
} while (0)

/* One thread = one verify. All threads share the same (pk, sig, msg) here,
 * but the pointers are per-index so real workloads with distinct inputs
 * change only the setup, not the kernel. */
__global__ void verify_kernel(const uint8_t * __restrict__ sigs,
                              const uint8_t * __restrict__ msgs,
                              size_t                       msglen,
                              const uint8_t * __restrict__ pks,
                              int *           __restrict__ results,
                              int                          n)
{
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= n) return;

    const uint8_t *sig = sigs + (size_t)idx * CRYPTO_BYTES;
    const uint8_t *msg = msgs + (size_t)idx * msglen;
    const uint8_t *pk  = pks  + (size_t)idx * CRYPTO_PUBLICKEYBYTES;

    results[idx] = crypto_sign_verify(sig, CRYPTO_BYTES,
                                      msg, msglen,
                                      /*ctx*/ NULL, /*ctxlen*/ 0,
                                      pk);
}

int main(void)
{
    /* -------- 1. host-side: generate one keypair + one signature -------- */
    uint8_t pk[CRYPTO_PUBLICKEYBYTES];
    uint8_t sk[CRYPTO_SECRETKEYBYTES];
    uint8_t sig[CRYPTO_BYTES];
    uint8_t msg[MSGLEN];
    size_t  siglen = 0;

    for (size_t i = 0; i < MSGLEN; ++i) msg[i] = (uint8_t)i;

    crypto_sign_keypair(pk, sk);
    crypto_sign_signature(sig, &siglen, msg, MSGLEN, NULL, 0, sk);

    if (crypto_sign_verify(sig, siglen, msg, MSGLEN, NULL, 0, pk) != 0) {
        fprintf(stderr, "Host self-verify failed — build is broken.\n");
        return 1;
    }

    printf("Dilithium mode:        %d\n", DILITHIUM_MODE);
    printf("  pk size:             %d bytes\n", CRYPTO_PUBLICKEYBYTES);
    printf("  sig size:            %zu bytes\n", siglen);
    printf("  host self-verify:    OK\n\n");

    /* -------- 2. build BATCH_SIZE copies on host, then push to device --- */
    size_t bytes_sigs = (size_t)BATCH_SIZE * CRYPTO_BYTES;
    size_t bytes_msgs = (size_t)BATCH_SIZE * MSGLEN;
    size_t bytes_pks  = (size_t)BATCH_SIZE * CRYPTO_PUBLICKEYBYTES;

    uint8_t *h_sigs = (uint8_t *)malloc(bytes_sigs);
    uint8_t *h_msgs = (uint8_t *)malloc(bytes_msgs);
    uint8_t *h_pks  = (uint8_t *)malloc(bytes_pks);
    for (int i = 0; i < BATCH_SIZE; ++i) {
        memcpy(h_sigs + (size_t)i * CRYPTO_BYTES,          sig, CRYPTO_BYTES);
        memcpy(h_msgs + (size_t)i * MSGLEN,                msg, MSGLEN);
        memcpy(h_pks  + (size_t)i * CRYPTO_PUBLICKEYBYTES, pk,  CRYPTO_PUBLICKEYBYTES);
    }

    /* Snapshot free/total device memory before any of our allocs so we can
     * report the delta the benchmark actually consumes. */
    size_t mem_free_before = 0, mem_total = 0;
    CUDA_CHECK(cudaMemGetInfo(&mem_free_before, &mem_total));

    uint8_t *d_sigs, *d_msgs, *d_pks;
    int     *d_results;
    CUDA_CHECK(cudaMalloc(&d_sigs,    bytes_sigs));
    CUDA_CHECK(cudaMalloc(&d_msgs,    bytes_msgs));
    CUDA_CHECK(cudaMalloc(&d_pks,     bytes_pks));
    CUDA_CHECK(cudaMalloc(&d_results, (size_t)BATCH_SIZE * sizeof(int)));

    CUDA_CHECK(cudaMemcpy(d_sigs, h_sigs, bytes_sigs, cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(d_msgs, h_msgs, bytes_msgs, cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(d_pks,  h_pks,  bytes_pks,  cudaMemcpyHostToDevice));

    /* Ask nvcc how much local memory it already allocated for verify_kernel.
     * That's per-thread storage the runtime reserves at launch regardless of
     * cudaLimitStackSize — separate accounting, but useful to print alongside
     * so users can see where the memory is going. */
    cudaFuncAttributes fattr;
    CUDA_CHECK(cudaFuncGetAttributes(&fattr, verify_kernel));

    /* Bump the per-thread call-stack limit. The runtime reserves
     *   stack × max_resident_threads_per_SM × num_SMs
     * of device memory globally, so overshoot on a big GPU = OOM at
     * cudaDeviceSetLimit itself. Try the configured value first, then step
     * down through progressively smaller fallbacks so this works across
     * consumer and datacenter cards without a rebuild. */
    int dev; cudaGetDevice(&dev);
    cudaDeviceProp prop; cudaGetDeviceProperties(&prop, dev);
    size_t max_resident = (size_t)prop.maxThreadsPerMultiProcessor
                        * (size_t)prop.multiProcessorCount;

    const size_t stack_candidates_kb[] = {
        PER_THREAD_STACK_KB, 32, 16, 8, 4
    };
    size_t chosen_stack_kb = 0;
    for (size_t i = 0; i < sizeof(stack_candidates_kb) / sizeof(stack_candidates_kb[0]); ++i) {
        size_t bytes = stack_candidates_kb[i] * 1024;
        cudaError_t e = cudaDeviceSetLimit(cudaLimitStackSize, bytes);
        if (e == cudaSuccess) {
            chosen_stack_kb = stack_candidates_kb[i];
            break;
        }
        /* Clear the sticky error before the next attempt. */
        cudaGetLastError();
        fprintf(stderr,
                "  cudaLimitStackSize=%zu KB rejected (would reserve ~%.1f GB); "
                "trying smaller...\n",
                stack_candidates_kb[i],
                (double)bytes * (double)max_resident / (1024.0 * 1024.0 * 1024.0));
    }
    if (!chosen_stack_kb) {
        fprintf(stderr, "Could not set any per-thread stack limit — GPU is too small.\n");
        return 1;
    }

    /* Print a bit of context */
    size_t stack_lim; cudaDeviceGetLimit(&stack_lim, cudaLimitStackSize);
    printf("Device:                %s (sm_%d%d)\n", prop.name, prop.major, prop.minor);
    printf("  SMs × threads/SM:    %d × %d = %zu resident threads\n",
           prop.multiProcessorCount, prop.maxThreadsPerMultiProcessor, max_resident);
    printf("  per-thread stack:    %zu KB  (reserves ~%.2f GB globally)\n",
           stack_lim / 1024,
           (double)stack_lim * (double)max_resident / (1024.0 * 1024.0 * 1024.0));
    printf("  kernel local mem:    %zu B / thread  (nvcc-allocated, separate)\n",
           fattr.localSizeBytes);
    printf("  kernel registers:    %d\n", fattr.numRegs);
    printf("  batch size:          %d\n", BATCH_SIZE);
    printf("  threads/block:       %d\n\n", THREADS_PER_BLOCK);

    /* Per-buffer breakdown, plus the driver-reported total. cudaMemGetInfo
     * includes context overhead + the runtime's per-thread stack reserve
     * for the max resident threads, so `used total` is typically much
     * larger than the sum of our explicit cudaMalloc sizes. Sample it after
     * the warmup kernel below to make sure the stack reserve is realized. */
    printf("== GPU memory ==\n");
    printf("  d_sigs:              %.2f MB\n", bytes_sigs             / (1024.0 * 1024.0));
    printf("  d_msgs:              %.2f MB\n", bytes_msgs             / (1024.0 * 1024.0));
    printf("  d_pks:               %.2f MB\n", bytes_pks              / (1024.0 * 1024.0));
    printf("  d_results:           %.2f MB\n",
           (BATCH_SIZE * sizeof(int)) / (1024.0 * 1024.0));
    printf("  input buffers sum:   %.2f MB\n",
           (bytes_sigs + bytes_msgs + bytes_pks + BATCH_SIZE * sizeof(int))
               / (1024.0 * 1024.0));

    int blocks = (BATCH_SIZE + THREADS_PER_BLOCK - 1) / THREADS_PER_BLOCK;

    /* -------- 3. warmup + timing ---------------------------------------- */
    verify_kernel<<<blocks, THREADS_PER_BLOCK>>>(d_sigs, d_msgs, MSGLEN,
                                                 d_pks, d_results, BATCH_SIZE);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());

    /* Now that the runtime has committed the per-thread stack reserve for
     * the kernel we're timing, sample free memory again. Difference from
     * the pre-alloc snapshot = full peak GPU footprint of the run
     * (input buffers + local-memory reserve). */
    size_t mem_free_after = 0;
    CUDA_CHECK(cudaMemGetInfo(&mem_free_after, &mem_total));
    size_t used_total = mem_free_before - mem_free_after;
    printf("  used total:          %.2f MB  (of %.0f MB device)\n",
           used_total / (1024.0 * 1024.0),
           mem_total  / (1024.0 * 1024.0));
    printf("  local/stack + ctx:   %.2f MB  (used total - input buffers)\n\n",
           (used_total
             - (bytes_sigs + bytes_msgs + bytes_pks + BATCH_SIZE * sizeof(int)))
               / (1024.0 * 1024.0));

    /* Allocate host result buffer once so we can time D2H inside the loop
     * without paying malloc every iteration. Pinned memory would speed
     * PCIe transfers noticeably; kept pageable here so the numbers reflect
     * the naive path most callers use. */
    int *h_results = (int *)malloc((size_t)BATCH_SIZE * sizeof(int));

    /* Four events per iteration so each phase is timed independently:
     *   h2d_start -> h2d_end          input copy
     *   h2d_end   -> kernel_end       verify_kernel
     *   kernel_end -> d2h_end         result copy
     * Per-iteration cudaEventSynchronize on d2h_end serializes the phases
     * so their times don't overlap in the measurement (which is exactly
     * what we want — you asked for separate copy vs compute numbers,
     * not overlapped). Real code that pipelines with streams will beat
     * the sum reported here. */
    cudaEvent_t ev_h2d_start, ev_h2d_end, ev_kernel_end, ev_d2h_end;
    cudaEventCreate(&ev_h2d_start);
    cudaEventCreate(&ev_h2d_end);
    cudaEventCreate(&ev_kernel_end);
    cudaEventCreate(&ev_d2h_end);

    float ms_h2d_total    = 0.0f;
    float ms_kernel_total = 0.0f;
    float ms_d2h_total    = 0.0f;

    for (int r = 0; r < REPEATS; ++r) {
        cudaEventRecord(ev_h2d_start);
        CUDA_CHECK(cudaMemcpyAsync(d_sigs, h_sigs, bytes_sigs, cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaMemcpyAsync(d_msgs, h_msgs, bytes_msgs, cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaMemcpyAsync(d_pks,  h_pks,  bytes_pks,  cudaMemcpyHostToDevice));
        cudaEventRecord(ev_h2d_end);

        verify_kernel<<<blocks, THREADS_PER_BLOCK>>>(d_sigs, d_msgs, MSGLEN,
                                                     d_pks, d_results, BATCH_SIZE);
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

    /* -------- 4. correctness: every verify must return 0 ----------------- */
    /* h_results holds the last iteration's D2H (see loop above). */
    int failures = 0;
    for (int i = 0; i < BATCH_SIZE; ++i) if (h_results[i] != 0) ++failures;

    /* Report copy and compute as independent numbers, plus their sum and
     * per-verify amortizations for each so you can see where the cost sits
     * across batch sizes. Throughput lines below use each phase's total
     * time — the kernel line is the "compute-only" ceiling; the E2E line
     * is the "real workload" figure comparable to the CPU benchmark. */
    printf("== GPU batched verify — phase breakdown ==\n");
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

    printf("  failures:            %d / %d\n", failures, BATCH_SIZE);

    /* -------- 5. cleanup ------------------------------------------------- */
    cudaFree(d_sigs); cudaFree(d_msgs); cudaFree(d_pks); cudaFree(d_results);
    free(h_sigs); free(h_msgs); free(h_pks); free(h_results);
    cudaEventDestroy(ev_h2d_start);
    cudaEventDestroy(ev_h2d_end);
    cudaEventDestroy(ev_kernel_end);
    cudaEventDestroy(ev_d2h_end);
    return failures ? 1 : 0;
}