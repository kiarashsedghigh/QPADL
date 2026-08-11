/*
 * Batched puzzle proof-check on CPU (OpenMP). Mirrors the GPU sibling.
 * Currently HCT via proof_hct.h; swap the include below for LBP later.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <omp.h>
#include <sys/resource.h>
#include <unistd.h>

/* Build with -DPROOF_LBP to run the lattice puzzle; default is HCT.
 * Both headers export the same interface. */
#ifdef PROOF_LBP
#  include "proof_lbp.h"
#else
#  include "proof_hct.h"
#endif

#ifndef BATCH_SIZE
#define BATCH_SIZE 4096
#endif
#ifndef REPEATS
#define REPEATS 500
#endif

static size_t peak_rss_bytes(void)
{
    struct rusage r;
    getrusage(RUSAGE_SELF, &r);
    return (size_t)r.ru_maxrss * 1024;
}
static size_t current_rss_bytes(void)
{
    FILE *f = fopen("/proc/self/statm", "r");
    if (!f) return 0;
    long size_pages = 0, rss_pages = 0;
    if (fscanf(f, "%ld %ld", &size_pages, &rss_pages) != 2) { fclose(f); return 0; }
    fclose(f);
    return (size_t)rss_pages * (size_t)sysconf(_SC_PAGESIZE);
}
static double now_s(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
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

    size_t bytes_pl = (size_t)BATCH_SIZE * PROOF_PAYLOAD_BYTES;
    uint8_t *h_pl    = (uint8_t *)malloc(bytes_pl);
    int     *results = (int *)malloc((size_t)BATCH_SIZE * sizeof(int));
    if (!h_pl || !results) { fprintf(stderr, "malloc failed\n"); return 1; }
    for (size_t i = 0; i < bytes_pl; ++i) h_pl[i] = (uint8_t)(i * 131u + 7u);

    int num_threads = omp_get_max_threads();
    printf("Host:                  OpenMP\n");
    printf("  cores available:     %d\n", omp_get_num_procs());
    printf("  threads (max):       %d\n", num_threads);
    printf("  batch size:          %d\n", BATCH_SIZE);
    printf("  repeats:             %d\n\n", REPEATS);

    size_t rss_before = current_rss_bytes();
    printf("== Host memory ==\n");
    printf("  h_payloads:          %.2f MB  (%d B/verify)\n",
           bytes_pl / (1024.0 * 1024.0), (int)PROOF_PAYLOAD_BYTES);
    printf("  results:             %.2f MB\n",
           (BATCH_SIZE * sizeof(int)) / (1024.0 * 1024.0));
    printf("  RSS pre-warmup:      %.2f MB\n\n", rss_before / (1024.0 * 1024.0));

    /* Warmup */
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < BATCH_SIZE; ++i) {
        const uint8_t *pl = h_pl + (size_t)i * PROOF_PAYLOAD_BYTES;
        results[i] = proof_verify_one(pl, (uint32_t)i);
    }

    double t0 = now_s();
    for (int r = 0; r < REPEATS; ++r) {
        #pragma omp parallel for schedule(static)
        for (int i = 0; i < BATCH_SIZE; ++i) {
            const uint8_t *pl = h_pl + (size_t)i * PROOF_PAYLOAD_BYTES;
            results[i] = proof_verify_one(pl, (uint32_t)i);
        }
    }
    double t1 = now_s();

    double s_total       = t1 - t0;
    double ms_per_batch  = (s_total * 1000.0) / REPEATS;
    double us_per_verify = (ms_per_batch * 1000.0) / BATCH_SIZE;
    double throughput    = (double)BATCH_SIZE * (double)REPEATS / s_total;

    long long sink = 0;
    for (int i = 0; i < BATCH_SIZE; ++i) sink += results[i];

    size_t rss_after = current_rss_bytes();
    size_t rss_peak  = peak_rss_bytes();

    printf("== CPU %s proof-check ==\n", PROOF_TYPE_NAME);
    printf("  time / batch:        %.3f ms\n", ms_per_batch);
    printf("  amortized / verify:  %.2f us\n", us_per_verify);
    printf("  throughput:          %.0f verify/s\n", throughput);
    printf("  sink (nz-count sum): %lld  %s\n\n",
           sink, sink == 0 ? "(WARNING: DCE?)" : "");
    printf("== Host memory (post-run) ==\n");
    printf("  RSS post-run:        %.2f MB\n", rss_after / (1024.0 * 1024.0));
    printf("  RSS peak:            %.2f MB  (ru_maxrss)\n",
           rss_peak / (1024.0 * 1024.0));

    free(h_pl); free(results);
    return 0;
}
