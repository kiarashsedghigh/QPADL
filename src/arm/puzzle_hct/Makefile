CC       ?= gcc
KAPPA    ?= 20
LOG_N    ?= 1

include ../common/arch.mk
CFLAGS    = -O3 $(ARCH_FLAGS) -std=gnu11 -Wall -Wextra -I../common \
            -DHCT_KAPPA_BITS=$(KAPPA) -DHCT_LOG_N_LEAVES=$(LOG_N)
LDFLAGS   = -lcrypto -lm

BINS = bench_hct_verify bench_hct_solve

all: $(BINS)

bench_hct_verify: bench_hct_verify.c proof_hct.h ../common/bench.h
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

bench_hct_solve: bench_hct_solve.c ../common/bench.h
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

run: all
	./bench_hct_verify
	./bench_hct_solve

# Sweep the paper's κ values. Each sample at large κ takes seconds — SAMPLES=3.
run-solve-sweep:
	@for k in 14 18 20 23; do \
	  $(MAKE) -s clean >/dev/null; \
	  $(MAKE) -s KAPPA=$$k CFLAGS="$(CFLAGS) -DKAPPA=$$k -DSAMPLES=3" bench_hct_solve; \
	  ./bench_hct_solve; \
	done

clean:
	rm -f $(BINS)

.PHONY: all run run-solve-sweep clean
