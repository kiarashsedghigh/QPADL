CC       ?= gcc
R_LOG2   ?= 14
B_BYTES  ?= 3072

include ../common/arch.mk
CFLAGS    = -O3 $(ARCH_FLAGS) -std=gnu11 -Wall -Wextra -I../common \
            -DR_LOG2=$(R_LOG2) -DB_BYTES=$(B_BYTES)
LDFLAGS   = -lcrypto -lm

BINS = bench_pir_ens bench_pir_ftr bench_pir_oop

all: $(BINS)

bench_pir_ens: bench_pir_ens.c ../common/bench.h
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

bench_pir_ftr: bench_pir_ftr.c ../common/bench.h
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

bench_pir_oop: bench_pir_oop.c ../common/bench.h
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

run: all
	./bench_pir_ens
	./bench_pir_ftr
	./bench_pir_oop

# Sweep DB-size axis: r ∈ {2^12, 2^14, 2^16, 2^18} per paper Fig 2
run-sweep:
	@for r in 12 14 16 18; do \
	  echo "--- r = 2^$$r ---"; \
	  $(MAKE) -s clean && $(MAKE) -s R_LOG2=$$r all && \
	  ./bench_pir_ens && ./bench_pir_ftr && ./bench_pir_oop; \
	done

clean:
	rm -f $(BINS)

.PHONY: all run run-sweep clean
