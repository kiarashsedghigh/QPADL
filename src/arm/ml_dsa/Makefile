# ARM ML-DSA verify bench. Uses the pq-crystals dilithium ref implementation
# (pure C, no x86 intrinsics — portable to ARM as-is).

CC       ?= gcc
MODE     ?= 2

include ../common/arch.mk
CFLAGS    = -O3 $(ARCH_FLAGS) -std=gnu11 -Wall -Wextra -I./src -I../common \
            -DDILITHIUM_MODE=$(MODE) -UDILITHIUM_RANDOMIZED_SIGNING

REF_SRCS  = fips202.c ntt.c packing.c poly.c polyvec.c \
            reduce.c rounding.c sign.c symmetric-shake.c randombytes.c
REF_OBJ   = $(REF_SRCS:%.c=src/%.o)

bench_ml_dsa_verify: bench_ml_dsa_verify.c $(REF_OBJ) ../common/bench.h src/setup.stamp
	$(CC) $(CFLAGS) bench_ml_dsa_verify.c $(REF_OBJ) -o $@ -lm

src/%.o: src/%.c src/setup.stamp Makefile
	$(CC) $(CFLAGS) -c $< -o $@

# Reuse the sibling ../../puzzles/CPU\ Batch\ Verification/dilithium-repo if it
# was already cloned, otherwise clone fresh.
setup:
	@mkdir -p src
	@if [ -d ../../puzzles/CPU\ Batch\ Verification/dilithium-repo ]; then \
	  cp ../../puzzles/CPU\ Batch\ Verification/dilithium-repo/ref/*.c src/; \
	  cp ../../puzzles/CPU\ Batch\ Verification/dilithium-repo/ref/*.h src/; \
	elif [ -d ../../puzzles/Batch\ Verification/dilithium-repo ]; then \
	  cp ../../puzzles/Batch\ Verification/dilithium-repo/ref/*.c src/; \
	  cp ../../puzzles/Batch\ Verification/dilithium-repo/ref/*.h src/; \
	else \
	  git clone --depth 1 https://github.com/pq-crystals/dilithium.git dilithium-repo && \
	  cp dilithium-repo/ref/*.c src/ && cp dilithium-repo/ref/*.h src/; \
	fi
	@touch src/setup.stamp

src/setup.stamp:
	@echo "Run 'make setup' first." && false

run: bench_ml_dsa_verify
	./bench_ml_dsa_verify

clean:
	rm -f bench_ml_dsa_verify src/*.o

distclean: clean
	rm -rf src dilithium-repo

.PHONY: all run setup clean distclean
all: bench_ml_dsa_verify
