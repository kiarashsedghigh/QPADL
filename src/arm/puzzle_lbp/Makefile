CC       ?= gcc
DIM      ?= 64

include ../common/arch.mk
CFLAGS    = -O3 $(ARCH_FLAGS) -std=gnu11 -Wall -Wextra -I../common -DLBP_N=$(DIM)

bench_lbp_verify: bench_lbp_verify.c proof_lbp.h ../common/bench.h
	$(CC) $(CFLAGS) $< -o $@ -lm

run: bench_lbp_verify
	./bench_lbp_verify

# Sweep the paper's dimensions:
run-sweep: bench_lbp_verify
	@for d in 48 62 69 79; do \
	  $(MAKE) -s clean && $(MAKE) -s DIM=$$d bench_lbp_verify && ./bench_lbp_verify; \
	done

clean:
	rm -f bench_lbp_verify

.PHONY: all run run-sweep clean
all: bench_lbp_verify
