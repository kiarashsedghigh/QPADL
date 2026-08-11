/*
 * ARM benchmark harness — shared timing/reporting macro.
 *
 * All benches under src/arm/ include this and use BENCH(...) to time a
 * body block. Prints a fixed-column line so the run_all.sh output can be
 * grep'd or piped downstream.
 *
 * No x86 intrinsics anywhere in this tree. -O3 with gcc/clang auto-
 * vectorizes hot loops to NEON on ARMv8.
 */
#ifndef ARM_BENCH_H
#define ARM_BENCH_H

#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <math.h>

/* Monotonic wall-clock, seconds. */
static inline double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* Time `body` executed `iters` times after a small warmup, print a one-
 * line summary. `bytes_per_iter` = 0 skips the MB/s column.
 *
 *   op=<name>  us/op=<...>  ops/s=<...>  [MB/s=<...>]
 */
#define BENCH(name, iters, bytes_per_iter, body) do {                    \
    for (long __i = 0; __i < 32; ++__i) { body; }                        \
    double __t0 = now_s();                                               \
    for (long __i = 0; __i < (long)(iters); ++__i) { body; }             \
    double __t1 = now_s();                                               \
    double __s = __t1 - __t0;                                            \
    double __us = 1e6 * __s / (double)(iters);                           \
    double __ops = (double)(iters) / __s;                                \
    printf("op=%-28s us/op=%10.3f  ops/s=%12.0f", (name), __us, __ops);  \
    if ((bytes_per_iter) > 0) {                                          \
        double __mb = (double)(iters) * (bytes_per_iter) / 1e6 / __s;    \
        printf("  MB/s=%9.2f", __mb);                                    \
    }                                                                    \
    printf("\n");                                                        \
    fflush(stdout);                                                      \
} while (0)

/* Force the compiler to keep a computation live (no DCE). Portable
 * volatile-write sink; works on gcc/clang/nvcc host. */
#define SINK(x) do { __asm__ __volatile__("" : : "r"(x) : "memory"); } while (0)

/* BENCH_CI: sample-based timing with 95% confidence interval.
 *
 * Runs `samples` independent measurements, each timing `iters_per_sample`
 * body executions. Reports mean, 95% CI half-width (Student-t for M ≤ 10,
 * z=1.96 above), ops/s and MB/s. Format:
 *
 *   op=<name>  us/op=<mean>  ci95=<half-width>  ops/s=<v>  [MB/s=<v>]
 *
 * For slow ops (each execution is already tens of ms — e.g., HCT solve),
 * pass iters_per_sample=1 so each sample is one execution. */
#define BENCH_CI(name, samples, iters_per_sample, bytes_per_iter, body) do {   \
    double _times[samples];                                                    \
    for (long __i = 0; __i < 3; ++__i) { body; }                               \
    for (int _s = 0; _s < (samples); ++_s) {                                   \
        double _t0 = now_s();                                                  \
        for (long __i = 0; __i < (long)(iters_per_sample); ++__i) { body; }    \
        double _t1 = now_s();                                                  \
        _times[_s] = 1e6 * (_t1 - _t0) / (double)(iters_per_sample);           \
    }                                                                          \
    double _sum = 0;                                                           \
    for (int _s = 0; _s < (samples); ++_s) _sum += _times[_s];                 \
    double _mean = _sum / (double)(samples);                                   \
    double _var = 0;                                                           \
    for (int _s = 0; _s < (samples); ++_s) {                                   \
        double _d = _times[_s] - _mean; _var += _d * _d;                       \
    }                                                                          \
    double _stddev = ((samples) > 1) ? sqrt(_var / (double)((samples) - 1)) : 0; \
    /* Student-t 97.5th percentile for df = samples-1, small-M table */        \
    static const double _tv[] = { 0, 0, 12.71, 4.30, 3.18, 2.78, 2.57,          \
                                  2.45, 2.36, 2.31, 2.26 };                    \
    double _z = ((samples) >= 2 && (samples) <= 10) ? _tv[(samples)] : 1.96;   \
    double _ci = _z * _stddev / sqrt((double)(samples));                       \
    double _ci_pct = (_mean > 0) ? (100.0 * _ci / _mean) : 0.0;                \
    double _ops = 1e6 / _mean;                                                 \
    printf("op=%-28s us/op=%12.3f  ci95=%11.3f  ci95_pct=%7.3f  ops/s=%12.0f", \
           (name), _mean, _ci, _ci_pct, _ops);                                 \
    if ((bytes_per_iter) > 0) {                                                \
        double _mb = (double)(bytes_per_iter) / _mean;                         \
        printf("  MB/s=%9.2f", _mb);                                           \
    }                                                                          \
    printf("\n");                                                              \
    fflush(stdout);                                                            \
} while (0)

#endif /* ARM_BENCH_H */
