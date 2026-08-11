CC       ?= gcc
include ../common/arch.mk
CFLAGS    = -O3 $(ARCH_FLAGS) -std=gnu11 -Wall -Wextra -I../common
LDFLAGS   = -lcrypto -lm

bench_aes256: bench_aes256.c ../common/bench.h
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

run: bench_aes256
	./bench_aes256

clean:
	rm -f bench_aes256

.PHONY: all run clean
all: bench_aes256
