# --------------------------------------------------------------------------
# Batched puzzle proof-check on GPU. Currently HCT (via proof_hct.h); swap
# the include in benchmark_proof.cu to run LBP once that header exists.
#
# Only depends on fips202.{c,h} from the pq-crystals dilithium ref (SHA3-256).
# We reuse the sibling ../Batch\ Verification/ setup so we don't clone twice.
#
# Usage:
#   make setup           # copy + patch fips202 sources from sibling
#   make                 # build (defaults HCT_LOG_N_LEAVES=1, HCT_KAPPA_BITS=20)
#   make KAPPA=23        # sweep kappa
#   ./benchmark_proof
# --------------------------------------------------------------------------

NVCC     ?= nvcc
ARCH     ?= native
# HCT knobs
KAPPA    ?= 20
LOG_N    ?= 1
# LBP knobs — SVP-challenge lattice dimension (hardness tier)
DIM      ?= 64
# Choose the puzzle: hct (default) or lbp
PROOF    ?= hct

ifeq ($(PROOF),lbp)
  PROOF_FLAGS  = -DPROOF_LBP -DLBP_N=$(DIM)
  # LBP does not use fips202 — the kernel is pure integer arithmetic on
  # the sparse HNF basis. No `make setup` needed for LBP builds.
  OBJ          = benchmark_proof.o
  STAMP_DEP    =
else
  PROOF_FLAGS  = -DHCT_KAPPA_BITS=$(KAPPA) -DHCT_LOG_N_LEAVES=$(LOG_N)
  # -rdc=true because fips202's sha3_256 is in its own TU.
  OBJ          = src/fips202.o benchmark_proof.o
  STAMP_DEP    = src/setup.stamp
endif

NVCCFLAGS = -O3 -std=c++14 -rdc=true \
            -arch=$(ARCH) -I./src \
            $(PROOF_FLAGS)

benchmark_proof: $(OBJ)
	$(NVCC) $(NVCCFLAGS) -o $@ $^

benchmark_proof.o: benchmark_proof.cu proof_hct.h proof_lbp.h $(STAMP_DEP) Makefile
	$(NVCC) $(NVCCFLAGS) -c $< -o $@

src/%.o: src/%.cu src/setup.stamp Makefile
	$(NVCC) $(NVCCFLAGS) -c $< -o $@

# -------------------- setup --------------------
# Reuse the sibling ML-DSA benchmark's patched fips202 (has __host__ __device__
# on sha3_256 already). If that sibling isn't set up, do a stripped-down setup
# ourselves.
SIBLING = ../Batch\ Verification

setup:
	@mkdir -p src
	@if [ -f $(SIBLING)/src/fips202.cu ] && [ -f $(SIBLING)/src/fips202.h ]; then \
	  echo "Reusing patched fips202 from $(SIBLING)/src/"; \
	  cp $(SIBLING)/src/fips202.cu src/fips202.cu; \
	  cp $(SIBLING)/src/fips202.h  src/fips202.h; \
	else \
	  echo "$(SIBLING) not set up — cloning + patching fips202 in-place..."; \
	  if [ ! -d $(SIBLING)/dilithium-repo ]; then \
	    git clone --depth 1 https://github.com/pq-crystals/dilithium.git $(SIBLING)/dilithium-repo; \
	  fi; \
	  cp $(SIBLING)/dilithium-repo/ref/fips202.c src/fips202.c; \
	  cp $(SIBLING)/dilithium-repo/ref/fips202.h src/fips202.h; \
	  cd src && python3 ../$(SIBLING)/patch.py fips202.c fips202.h; \
	  rm -f fips202.c; \
	fi
	@touch src/setup.stamp
	@echo "Setup complete."

src/setup.stamp:
	@echo "Run 'make setup' first." && false

clean:
	rm -f benchmark_proof *.o src/*.o

distclean: clean
	rm -rf src

.PHONY: setup clean distclean
