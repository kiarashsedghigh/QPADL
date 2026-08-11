# --------------------------------------------------------------------------
# Batched Dilithium verify on CPU (reference implementation, OpenMP-parallel).
#
# Mirrors the GPU benchmark in ../Batch Verification/ so the two are directly
# comparable — same batch size, same repeats, same reported quantities.
#
# Usage:
#   make setup           # clone reference repo, copy .c/.h sources
#   make MODE=2          # build for Dilithium2 (also: MODE=3, MODE=5)
#   ./benchmark_verify
#   OMP_NUM_THREADS=8 ./benchmark_verify   # cap threads
# --------------------------------------------------------------------------

CC       ?= gcc
MODE     ?= 2

# -UDILITHIUM_RANDOMIZED_SIGNING makes signing deterministic so repeat runs
# verify the same signature. -fopenmp turns on OpenMP for the batch loop.
#
# -std=gnu11 (not c11): the upstream randombytes.c calls syscall() without
# including <unistd.h>, and glibc only exposes syscall() when a "non-strict"
# feature set is active (__USE_MISC). Strict -std=c11 hides it and gcc
# treats the missing prototype as an error, breaking the build. gnu11 is
# c11 + GNU extensions, matching what the reference Makefile assumes.
CFLAGS    = -O3 -std=gnu11 -Wall -Wextra -fopenmp \
            -DDILITHIUM_MODE=$(MODE) -UDILITHIUM_RANDOMIZED_SIGNING \
            -I./src
LDFLAGS   = -fopenmp

# Ref .c files. Compiled unchanged — no patching, no host/device qualifiers.
REF_SRCS  = fips202.c ntt.c packing.c poly.c polyvec.c \
            reduce.c rounding.c sign.c symmetric-shake.c randombytes.c

OBJ       = $(REF_SRCS:%.c=src/%.o) benchmark_verify.o

# -------------------- build --------------------
benchmark_verify: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

benchmark_verify.o: benchmark_verify.c src/setup.stamp Makefile
	$(CC) $(CFLAGS) -c $< -o $@

src/%.o: src/%.c src/setup.stamp Makefile
	$(CC) $(CFLAGS) -c $< -o $@

# -------------------- setup --------------------
# Reuses the sibling directory's clone of dilithium-repo if it exists so the
# CPU and GPU benchmarks don't each pay the clone cost.
DILITHIUM_REPO_CANDIDATES = dilithium-repo ../Batch\ Verification/dilithium-repo

setup:
	@repo=""; \
	 for c in $(DILITHIUM_REPO_CANDIDATES); do \
	   if [ -d "$$c" ]; then repo="$$c"; break; fi; \
	 done; \
	 if [ -z "$$repo" ]; then \
	   git clone --depth 1 https://github.com/pq-crystals/dilithium.git dilithium-repo; \
	   repo="dilithium-repo"; \
	 else \
	   echo "Reusing $$repo"; \
	 fi; \
	 mkdir -p src; \
	 cp "$$repo"/ref/*.h src/; \
	 cp "$$repo"/ref/*.c src/
	@touch src/setup.stamp
	@echo "Setup complete."

src/setup.stamp:
	@echo "Run 'make setup' first." && false

clean:
	rm -f benchmark_verify *.o src/*.o

distclean: clean
	rm -rf src dilithium-repo

.PHONY: setup clean distclean
