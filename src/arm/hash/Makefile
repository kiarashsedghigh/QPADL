CC       ?= gcc
include ../common/arch.mk
CFLAGS    = -O3 $(ARCH_FLAGS) -std=gnu11 -Wall -Wextra -I../common
LDFLAGS   = -lcrypto -lm

BINS = bench_sha256 bench_sha3_256

all: $(BINS)

bench_sha256: bench_sha256.c ../common/bench.h
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

bench_sha3_256: bench_sha3_256.c ../common/bench.h
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

run: all
	./bench_sha256
	./bench_sha3_256

clean:
	rm -f $(BINS)

.PHONY: all run clean
