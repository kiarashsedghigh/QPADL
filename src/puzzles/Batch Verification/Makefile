# --------------------------------------------------------------------------
# Batched Dilithium verify on GPU (reference implementation).
#
# Usage:
#   make setup           # clone repo, copy ref sources, patch .c AND .h files
#   make MODE=2          # build for Dilithium2 (also: MODE=3, MODE=5)
#   ./benchmark_verify
# --------------------------------------------------------------------------

NVCC     ?= nvcc
MODE     ?= 2
ARCH     ?= native

# -UDILITHIUM_RANDOMIZED_SIGNING makes signing deterministic (no randombytes
# on the sign path), so repeat runs verify the same signature.
# -rdc=true (relocatable device code) is required because the kernel in
# benchmark_verify.cu calls crypto_sign_verify defined in sign.cu — a
# different translation unit. Without RDC, device symbols don't cross TUs
# and ptxas fails with "Unresolved extern function". nvcc's link step
# handles the device link (nvlink + cudadevrt) automatically when RDC is on.
NVCCFLAGS = -O3 -std=c++14 -rdc=true \
            -DDILITHIUM_MODE=$(MODE) -UDILITHIUM_RANDOMIZED_SIGNING \
            -arch=$(ARCH) -I./src

# Ref .c files whose functions we want callable from both host and device.
REF_SRCS  = fips202.c ntt.c packing.c poly.c polyvec.c \
            reduce.c rounding.c sign.c symmetric-shake.c

# Ref headers that declare those functions or symbols. Patched in place so
# the declarations match the __host__ __device__ definitions; without this,
# nvcc sees host-only prototypes at kernel call sites and refuses.
# randombytes.h is intentionally NOT patched — randombytes stays host-only.
REF_HDRS  = sign.h poly.h polyvec.h ntt.h packing.h reduce.h rounding.h \
            fips202.h symmetric.h

PATCHED   = $(REF_SRCS:%.c=src/%.cu)
OBJ       = $(PATCHED:.cu=.o) src/randombytes.o benchmark_verify.o

# -------------------- build --------------------
benchmark_verify: $(OBJ)
	$(NVCC) $(NVCCFLAGS) -o $@ $^

# Depending on Makefile means editing NVCCFLAGS (or anything else here)
# forces a rebuild of every object. Prevents the "stale .o without -rdc"
# footgun that happens when make thinks objects are up-to-date but they
# were built with an older set of flags.
benchmark_verify.o: benchmark_verify.cu src/patched.stamp Makefile
	$(NVCC) $(NVCCFLAGS) -c $< -o $@

src/%.o: src/%.cu src/patched.stamp Makefile
	$(NVCC) $(NVCCFLAGS) -c $< -o $@

src/randombytes.o: src/randombytes.c Makefile
	$(NVCC) $(NVCCFLAGS) -c $< -o $@

# -------------------- setup --------------------
setup:
	@if [ ! -d dilithium-repo ]; then \
	  git clone --depth 1 https://github.com/pq-crystals/dilithium.git dilithium-repo; \
	fi
	@mkdir -p src
	@cp dilithium-repo/ref/*.h src/
	@cp dilithium-repo/ref/*.c src/
	@echo "Patching sources ..."
	@cd src && python3 ../patch.py $(REF_SRCS) $(REF_HDRS)
	@# randombytes.h stays host-only (no __host__ __device__), but it still
	@# needs `extern "C"` guards. randombytes.c is compiled by nvcc as C
	@# (no name mangling), while sign.cu etc. are C++ (mangled). Without
	@# these guards, C++ TUs generate a mangled call to `randombytes` that
	@# nothing defines, producing an ld error like
	@#   undefined reference to `randombytes(unsigned char*, unsigned long)'
	@python3 -c "import pathlib; p=pathlib.Path('src/randombytes.h'); t=p.read_text();\
	  guard_open='\n#ifdef __cplusplus\nextern \"C\" {\n#endif\n';\
	  guard_close='\n#ifdef __cplusplus\n}\n#endif\n';\
	  s=t.find('#define RANDOMBYTES_H');\
	  e=t.rfind('#endif');\
	  assert s>=0 and e>s, 'unexpected randombytes.h layout';\
	  nl=t.find('\n', s)+1;\
	  patched=t[:nl]+guard_open+t[nl:e]+guard_close+t[e:];\
	  p.write_text(patched);\
	  print('  randombytes.h            wrapped in extern \"C\"')"
	@rm -f $(addprefix src/, $(REF_SRCS))
	@touch src/patched.stamp
	@echo "Setup complete."

src/patched.stamp:
	@echo "Run 'make setup' first." && false

clean:
	rm -f benchmark_verify *.o src/*.o

distclean: clean
	rm -rf src dilithium-repo

.PHONY: setup clean distclean