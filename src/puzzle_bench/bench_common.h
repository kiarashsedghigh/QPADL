/*
 * Shared benchmarking helpers for the client-puzzle micro-benchmarks
 * (Hashcash Tree and SVP). Header-only, no external dependencies beyond
 * the C++ standard library.
 *
 * Provides:
 *   - a monotonic per-call timer with automatic inner-repeat calibration
 *     (so sub-microsecond operations are still timed above clock noise),
 *   - a Welford-free summary (mean, sample std-dev, 95% CI) matching the
 *     Student-t convention used elsewhere in this repo (src/arm/common/bench.h),
 *   - a volatile sink to defeat dead-code elimination,
 *   - adaptive time formatting (ns / us / ms / s).
 *
 * Statistics model
 * ----------------
 * Each benchmark runs X *outer* iterations. Every outer iteration draws
 * fresh random inputs (a random hash salt for HCT, a random basis+solution
 * for SVP) and produces ONE timing measurement. Mean / std-dev / 95% CI are
 * computed across those X measurements, so the reported spread reflects the
 * cross-input variance the user asked for.
 *
 * For very fast operations (e.g. a single verify), one call is below the
 * clock resolution, so per_call_us() internally repeats the SAME (already
 * randomized) input until the measured span exceeds a floor and divides out
 * the repeat count. The fresh-randomness-per-iteration guarantee is at the
 * outer-iteration level, which is what matters for the variance estimate.
 */
#ifndef PUZZLE_BENCH_COMMON_H
#define PUZZLE_BENCH_COMMON_H

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

/* Volatile sink — read/written across TU boundaries would need extern; a
 * single definition per program is fine since each bench is one TU. */
static volatile uint64_t g_sink = 0;

/* Monotonic seconds. */
static inline double now_s()
{
    return std::chrono::duration<double>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

/*
 * Time one call of `f`, in microseconds. If a single call is faster than
 * `min_s` seconds, repeat it (doubling) until the measured span clears the
 * floor, then divide by the repeat count. Returns per-call microseconds.
 *
 * `f` must be idempotent w.r.t. timing (same work each call); callers keep
 * the randomized input fixed for the duration of one measurement.
 */
template <class F>
static inline double per_call_us(F &&f, double min_s)
{
    long reps = 1;
    for (;;) {
        double t0 = now_s();
        for (long i = 0; i < reps; ++i) f();
        double t1 = now_s();
        double sec = t1 - t0;
        if (sec >= min_s || reps >= (1L << 40))
            return 1e6 * sec / (double)reps;
        reps *= 2;
    }
}

/* Student-t 97.5th percentile for df = n-1 (small-sample table), else z=1.96. */
static inline double t_crit(int n)
{
    static const double t[] = {
        0.0,    0.0,    12.706, 4.303, 3.182, 2.776, 2.571, 2.447,
        2.365,  2.306,  2.262,  2.228, 2.201, 2.179, 2.160, 2.145,
        2.131,  2.120,  2.110,  2.101, 2.093
    };
    if (n < 2) return 0.0;
    int df = n - 1;
    if (df >= 1 && df <= 20) return t[df];
    return 1.96;
}

struct Stats {
    double mean;    /* microseconds */
    double sd;      /* sample std-dev, microseconds */
    double ci;      /* 95% CI half-width, microseconds */
    int    n;
};

static inline Stats summarize(const std::vector<double> &xs)
{
    Stats s{0, 0, 0, (int)xs.size()};
    if (xs.empty()) return s;
    double sum = 0;
    for (double x : xs) sum += x;
    s.mean = sum / (double)xs.size();
    if (xs.size() >= 2) {
        double var = 0;
        for (double x : xs) { double d = x - s.mean; var += d * d; }
        s.sd = std::sqrt(var / (double)(xs.size() - 1));
        s.ci = t_crit((int)xs.size()) * s.sd / std::sqrt((double)xs.size());
    }
    return s;
}

/* Adaptive unit formatting for a microsecond value. */
static inline std::string fmt_us(double us)
{
    char b[64];
    double mag = std::fabs(us);          /* choose unit by magnitude, keep sign */
    if (mag < 1.0)           std::snprintf(b, sizeof b, "%8.3f ns", us * 1e3);
    else if (mag < 1e3)      std::snprintf(b, sizeof b, "%8.3f us", us);
    else if (mag < 1e6)      std::snprintf(b, sizeof b, "%8.3f ms", us / 1e3);
    else                     std::snprintf(b, sizeof b, "%8.3f s ", us / 1e6);
    return std::string(b);
}

/* Print one summary row for a labelled security level. */
static inline void print_row(const char *label, const Stats &s, const char *tail)
{
    std::printf("  %-10s  mean=%s  sd=%s  ci95=\xC2\xB1%s  95%%CI=[%s, %s]%s\n",
                label,
                fmt_us(s.mean).c_str(),
                fmt_us(s.sd).c_str(),
                fmt_us(s.ci).c_str(),
                fmt_us(s.mean - s.ci).c_str(),
                fmt_us(s.mean + s.ci).c_str(),
                tail ? tail : "");
    std::fflush(stdout);
}

/* Parse iteration count from argv (positional or --iters=N); default `def`. */
static inline int parse_iters(int argc, char **argv, int def)
{
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (std::strncmp(a, "--iters=", 8) == 0) {
            int v = std::atoi(a + 8);
            if (v > 0) return v;
        } else if (a[0] != '-') {
            int v = std::atoi(a);
            if (v > 0) return v;
        }
    }
    return def;
}

#endif /* PUZZLE_BENCH_COMMON_H */
