# --------------------------------------------------------------------------
# Batched puzzle proof-check on CPU (OpenMP). Currently HCT via proof_hct.h.
#
# Only needs fips202.{c,h} from the pq-crystals dilithium ref (SHA3-256).
# Reuses ../CPU\ Batch\ Verification/src/fips202.* if that sibling is set up.
#
# Usage:
#   make setup
#   make                     # defaults HCT_LOG_N_LEAVES=1, HCT_KAPPA_BITS=20
#   make KAPPA=23
#   OMP_NUM_THREADS=16 ./benchmark_proof
# --------------------------------------------------------------------------

CC       ?= gcc
KAPPA    ?= 20
LOG_N    ?= 1
DIM      ?= 64
PROOF    ?= hct

ifeq ($(PROOF),lbp)
  PROOF_FLAGS = -DPROOF_LBP -DLBP_N=$(DIM)
  OBJ         = benchmark_proof.o
  STAMP_DEP   =
else
  PROOF_FLAGS = -DHCT_KAPPA_BITS=$(KAPPA) -DHCT_LOG_N_LEAVES=$(LOG_N)
  OBJ         = src/fips202.o benchmark_proof.o
  STAMP_DEP   = src/setup.stamp
endif

CFLAGS    = -O3 -std=gnu11 -Wall -Wextra -fopenmp -I./src $(PROOF_FLAGS)
LDFLAGS   = -fopenmp

benchmark_proof: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

benchmark_proof.o: benchmark_proof.c proof_hct.h proof_lbp.h $(STAMP_DEP) Makefile
	$(CC) $(CFLAGS) -c $< -o $@

src/%.o: src/%.c src/setup.stamp Makefile
	$(CC) $(CFLAGS) -c $< -o $@

# -------------------- setup --------------------
SIBLING = ../CPU\ Batch\ Verification

setup:
	@mkdir -p src
	@if [ -f $(SIBLING)/src/fips202.c ] && [ -f $(SIBLING)/src/fips202.h ]; then \
	  echo "Reusing fips202 from $(SIBLING)/src/"; \
	  cp $(SIBLING)/src/fips202.c src/fips202.c; \
	  cp $(SIBLING)/src/fips202.h src/fips202.h; \
	elif [ -d $(SIBLING)/dilithium-repo ]; then \
	  echo "Copying fips202 from $(SIBLING)/dilithium-repo/ref/"; \
	  cp $(SIBLING)/dilithium-repo/ref/fips202.c src/; \
	  cp $(SIBLING)/dilithium-repo/ref/fips202.h src/; \
	elif [ -d ../Batch\ Verification/dilithium-repo ]; then \
	  echo "Copying fips202 from ../Batch Verification/dilithium-repo/ref/"; \
	  cp ../Batch\ Verification/dilithium-repo/ref/fips202.c src/; \
	  cp ../Batch\ Verification/dilithium-repo/ref/fips202.h src/; \
	else \
	  echo "No sibling dilithium-repo — cloning..."; \
	  git clone --depth 1 https://github.com/pq-crystals/dilithium.git dilithium-repo; \
	  cp dilithium-repo/ref/fips202.c src/; \
	  cp dilithium-repo/ref/fips202.h src/; \
	fi
	@touch src/setup.stamp
	@echo "Setup complete."

src/setup.stamp:
	@echo "Run 'make setup' first." && false

clean:
	rm -f benchmark_proof *.o src/*.o

distclean: clean
	rm -rf src dilithium-repo

.PHONY: setup clean distclean
