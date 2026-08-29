/*
 * SVP (Shortest Vector Problem) client-puzzle — SERVER-SIDE VERIFICATION only.
 *
 *   Puzzle family: https://www.latticechallenge.org/svp-challenge/
 *
 * Per the task, the client side is NOT benchmarked: we assume the client has
 * already produced a correct (trivial) solution, and we measure only the
 * server's cost to verify it, over X iterations with mean / std-dev / 95% CI.
 *
 * Security levels are the lattice dimensions:  d in {48, 62, 69, 79}.
 *
 * Verification model
 * ------------------
 * The puzzle is stated by a random rank-d integer lattice basis B (a d x d
 * matrix). A solution is a short lattice vector, submitted as an integer
 * coefficient vector x with v = x * B (the standard succinct proof: the server
 * is handed the coefficients, not asked to solve lattice membership). The
 * server accepts iff:
 *      (a) v = x * B                      (v really is a lattice vector)
 *      (b) v != 0                         (non-trivial)
 *      (c) ||v||^2 <= target^2           (short enough)
 *
 * The verifier therefore recomputes v = x * B (a d x d matrix-vector product),
 * checks equality, and computes ||v||^2. That is the dominant O(d^2) work and
 * is what we time. It runs assume-valid (accumulates mismatches / norm into a
 * sink) so its cost is independent of whether the particular instance passes.
 *
 * Per-iteration randomness: a fresh random basis B AND a fresh random solution
 * (x, v) are drawn each iteration.
 *
 * Precision: basis entries are ~13-bit and coefficients are small, so every
 * v-entry fits comfortably in int64; ||v||^2 is accumulated in __int128 to be
 * safe. This keeps the verifier dependency-free (no NTL/GMP) while remaining
 * exact for these dimensions.
 *
 * Usage:  ./bench_svp [iterations]        (default 20)
 *         ./bench_svp --iters=50
 */
#include <cstdint>
#include <random>
#include <vector>

#include "bench_common.h"

static const int DIMS[]  = {48, 62, 69, 79};
static const int N_DIMS  = (int)(sizeof(DIMS) / sizeof(DIMS[0]));

/* Prime near 2^13 used for the q-ary basis (Goldstein–Mayer / challenge style). */
static const int64_t Q = 8191;
/* Coefficient magnitude bound for the (trivial) planted solution. */
static const int64_t COEFF_BOUND = 3;

/* One SVP instance: basis B (row-major d x d), coefficient vector x (length d),
 * lattice vector v = x * B (length d), and an accepting norm bound target^2. */
struct SvpInstance {
    int                  d;
    std::vector<int64_t> B;       /* d*d */
    std::vector<int64_t> x;       /* d   */
    std::vector<int64_t> v;       /* d   */
    __int128             target2; /* accepting bound on ||v||^2 */
};

/* Build a fresh random instance with a genuinely-valid short solution.
 *
 * Basis (challenge-style, lower-triangular q-ary):
 *   B[0][0] = Q,  B[0][j>0] = 0
 *   B[i][i] = 1,  B[i][0]   = rand in [0, Q),  else 0        (i >= 1)
 * This spans a rank-d lattice. A random small coefficient vector x then yields
 * a genuine (and, for small x, short) lattice vector v = x * B.
 */
static void make_instance(SvpInstance &inst, int d, std::mt19937_64 &rng)
{
    inst.d = d;
    inst.B.assign((size_t)d * d, 0);
    inst.x.assign(d, 0);
    inst.v.assign(d, 0);

    std::uniform_int_distribution<int64_t> qd(0, Q - 1);
    std::uniform_int_distribution<int64_t> cd(-COEFF_BOUND, COEFF_BOUND);

    auto Bref = [&](int i, int j) -> int64_t & { return inst.B[(size_t)i * d + j]; };
    Bref(0, 0) = Q;
    for (int i = 1; i < d; ++i) {
        Bref(i, i) = 1;
        Bref(i, 0) = qd(rng);
    }

    /* Random non-zero small coefficient vector. */
    bool nonzero = false;
    for (int i = 0; i < d; ++i) {
        inst.x[i] = cd(rng);
        nonzero = nonzero || (inst.x[i] != 0);
    }
    if (!nonzero) inst.x[0] = 1;

    /* v = x * B */
    for (int j = 0; j < d; ++j) {
        int64_t acc = 0;
        for (int i = 0; i < d; ++i) acc += inst.x[i] * Bref(i, j);
        inst.v[j] = acc;
    }

    /* Accepting bound: set target^2 = ||v||^2 so this valid solution passes.
     * (Verification WORK is identical whatever the bound; this just keeps the
     * assume-valid accept path honest.) */
    __int128 n2 = 0;
    for (int j = 0; j < d; ++j)
        n2 += (__int128)inst.v[j] * inst.v[j];
    inst.target2 = n2;
}

/* SERVER verify: recompute v' = x * B, count coordinate mismatches, compute
 * ||v'||^2 and compare to target^2, and require v' != 0. Assume-valid: fold
 * everything into an accumulator so the O(d^2) product is never elided.
 * Real acceptance == (returned value folds to "no mismatch, in-bound, non-zero"). */
static uint64_t verify(const SvpInstance &inst)
{
    const int d = inst.d;
    const int64_t *B = inst.B.data();
    const int64_t *x = inst.x.data();
    const int64_t *v = inst.v.data();

    uint64_t mismatch = 0;
    __int128  norm2   = 0;

    for (int j = 0; j < d; ++j) {
        int64_t acc = 0;
        for (int i = 0; i < d; ++i)
            acc += x[i] * B[(size_t)i * d + j];
        mismatch += (uint64_t)(acc != v[j]);
        norm2    += (__int128)acc * acc;
    }

    uint64_t bad_norm = (uint64_t)(norm2 > inst.target2);
    uint64_t is_zero  = (uint64_t)(norm2 == 0);
    /* fold to a single value; low bits of norm2 keep the accumulator live */
    return mismatch + bad_norm + is_zero + (uint64_t)(norm2 & 0xff);
}

int main(int argc, char **argv)
{
    int X = parse_iters(argc, argv, 20);

    std::printf("================================================================\n");
    std::printf(" SVP puzzle (latticechallenge.org) — SERVER-SIDE VERIFICATION only\n");
    std::printf(" client solve assumed correct/trivial (not benchmarked)\n");
    std::printf(" verify = recompute v = x*B (O(d^2)) + norm-bound check\n");
    std::printf(" iterations per level (X): %d   (fresh random basis+solution each iter)\n", X);
    std::printf("================================================================\n\n");

    std::random_device rd;
    std::mt19937_64 rng(((uint64_t)rd() << 32) ^ rd());

    std::printf("[*] SERVER-SIDE VERIFICATION\n");
    for (int di = 0; di < N_DIMS; ++di) {
        int d = DIMS[di];
        std::vector<double> times;
        times.reserve(X);
        for (int it = 0; it < X; ++it) {
            SvpInstance inst;
            make_instance(inst, d, rng);       /* fresh basis + solution */
            auto op = [&]() { g_sink += verify(inst); };
            op();                              /* warm-up */
            times.push_back(per_call_us(op, 0.02));
        }
        Stats s = summarize(times);
        char lbl[16];
        std::snprintf(lbl, sizeof lbl, "dim=%d", d);
        print_row(lbl, s, "");
    }

    std::printf("\n(sink=%llu)\n", (unsigned long long)g_sink);
    return 0;
}
