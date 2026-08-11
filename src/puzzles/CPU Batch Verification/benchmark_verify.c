/*
 * Batched Dilithium verification on CPU — reference implementation,
 * OpenMP-parallelized.
 *
 * Mirrors ../Batch Verification/benchmark_verify.cu so the two benchmarks
 * report directly comparable numbers: same BATCH_SIZE, same REPEATS,
 * same MSGLEN, same "throughput = verifies / second" definition.
 *
 * Parallelism model: one OMP thread per verify at a time, dispatched by a
 * `#pragma omp parallel for`. Thread count follows OMP_NUM_THREADS (defaults
 * to the number of available cores). No per-verify shared state — each
 * crypto_sign_verify call has its own stack frame in its worker thread.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <omp.h>
#include <sys/resource.h>
#include <unistd.h>

#include "params.h"
#include "sign.h"

#ifndef BATCH_SIZE
#define BATCH_SIZE 4096
#endif
#ifndef REPEATS
#define REPEATS 1000
#endif

#define MSGLEN 59

/* Peak resident-set size in bytes, sampled from getrusage. On Linux
 * ru_maxrss is reported in KB — normalize to bytes. */
static size_t peak_rss_bytes(void)
{
    struct rusage r;
    getrusage(RUSAGE_SELF, &r);
    return (size_t)r.ru_maxrss * 1024;
}

/* Current resident-set size in bytes, parsed from /proc/self/statm. Second
 * field is resident pages. Falls back to 0 if the file isn't available. */
static size_t current_rss_bytes(void)
{
    FILE *f = fopen("/proc/self/statm", "r");
    if (!f) return 0;
    long size_pages = 0, rss_pages = 0;
    if (fscanf(f, "%ld %ld", &size_pages, &rss_pages) != 2) {
        fclose(f);
        return 0;
    }
    fclose(f);
    return (size_t)rss_pages * (size_t)sysconf(_SC_PAGESIZE);
}

/* Monotonic wall-clock, seconds. */
static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
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

    /* -------- 2. build BATCH_SIZE copies in host memory ------------------ */
    size_t bytes_sigs = (size_t)BATCH_SIZE * CRYPTO_BYTES;
    size_t bytes_msgs = (size_t)BATCH_SIZE * MSGLEN;
    size_t bytes_pks  = (size_t)BATCH_SIZE * CRYPTO_PUBLICKEYBYTES;

    uint8_t *h_sigs = (uint8_t *)malloc(bytes_sigs);
    uint8_t *h_msgs = (uint8_t *)malloc(bytes_msgs);
    uint8_t *h_pks  = (uint8_t *)malloc(bytes_pks);
    int     *results = (int *)malloc((size_t)BATCH_SIZE * sizeof(int));
    if (!h_sigs || !h_msgs || !h_pks || !results) {
        fprintf(stderr, "malloc failed\n");
        return 1;
    }
    for (int i = 0; i < BATCH_SIZE; ++i) {
        memcpy(h_sigs + (size_t)i * CRYPTO_BYTES,          sig, CRYPTO_BYTES);
        memcpy(h_msgs + (size_t)i * MSGLEN,                msg, MSGLEN);
        memcpy(h_pks  + (size_t)i * CRYPTO_PUBLICKEYBYTES, pk,  CRYPTO_PUBLICKEYBYTES);
    }

    int num_threads = omp_get_max_threads();
    printf("Host:                  OpenMP\n");
    printf("  cores available:     %d\n", omp_get_num_procs());
    printf("  threads (max):       %d\n", num_threads);
    printf("  batch size:          %d\n", BATCH_SIZE);
    printf("  repeats:             %d\n\n", REPEATS);

    /* -------- 3. buffer sizing + resident-set snapshot ------------------- */
    size_t rss_before = current_rss_bytes();
    printf("== Host memory ==\n");
    printf("  h_sigs:              %.2f MB\n", bytes_sigs / (1024.0 * 1024.0));
    printf("  h_msgs:              %.2f MB\n", bytes_msgs / (1024.0 * 1024.0));
    printf("  h_pks:               %.2f MB\n", bytes_pks  / (1024.0 * 1024.0));
    printf("  results:             %.2f MB\n",
           (BATCH_SIZE * sizeof(int)) / (1024.0 * 1024.0));
    printf("  input buffers sum:   %.2f MB\n",
           (bytes_sigs + bytes_msgs + bytes_pks + BATCH_SIZE * sizeof(int))
               / (1024.0 * 1024.0));
    printf("  RSS pre-warmup:      %.2f MB\n\n", rss_before / (1024.0 * 1024.0));

    /* -------- 4. warmup + timing ---------------------------------------- */
    /* Warmup: fault in pages, JIT thread pool, warm the caches. */
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < BATCH_SIZE; ++i) {
        const uint8_t *s = h_sigs + (size_t)i * CRYPTO_BYTES;
        const uint8_t *m = h_msgs + (size_t)i * MSGLEN;
        const uint8_t *p = h_pks  + (size_t)i * CRYPTO_PUBLICKEYBYTES;
        results[i] = crypto_sign_verify(s, CRYPTO_BYTES, m, MSGLEN,
                                        NULL, 0, p);
    }

    double t0 = now_s();
    for (int r = 0; r < REPEATS; ++r) {
        #pragma omp parallel for schedule(static)
        for (int i = 0; i < BATCH_SIZE; ++i) {
            const uint8_t *s = h_sigs + (size_t)i * CRYPTO_BYTES;
            const uint8_t *m = h_msgs + (size_t)i * MSGLEN;
            const uint8_t *p = h_pks  + (size_t)i * CRYPTO_PUBLICKEYBYTES;
            results[i] = crypto_sign_verify(s, CRYPTO_BYTES, m, MSGLEN,
                                            NULL, 0, p);
        }
    }
    double t1 = now_s();

    double s_total       = t1 - t0;
    double ms_per_batch  = (s_total * 1000.0) / REPEATS;
    double us_per_verify = (ms_per_batch * 1000.0) / BATCH_SIZE;
    double throughput    = (double)BATCH_SIZE * (double)REPEATS / s_total;

    /* -------- 5. correctness: every verify must return 0 ----------------- */
    int failures = 0;
    for (int i = 0; i < BATCH_SIZE; ++i) if (results[i] != 0) ++failures;

    size_t rss_after = current_rss_bytes();
    size_t rss_peak  = peak_rss_bytes();

    printf("== CPU batched verify ==\n");
    printf("  time / batch:        %.3f ms\n", ms_per_batch);
    printf("  amortized / verify:  %.2f us\n", us_per_verify);
    printf("  throughput:          %.0f verify/s\n", throughput);
    printf("  failures:            %d / %d\n\n", failures, BATCH_SIZE);
    printf("== Host memory (post-run) ==\n");
    printf("  RSS post-run:        %.2f MB\n", rss_after / (1024.0 * 1024.0));
    printf("  RSS peak:            %.2f MB  (ru_maxrss)\n",
           rss_peak / (1024.0 * 1024.0));

    /* -------- 6. cleanup ------------------------------------------------- */
    free(h_sigs); free(h_msgs); free(h_pks); free(results);
    return failures ? 1 : 0;
}
