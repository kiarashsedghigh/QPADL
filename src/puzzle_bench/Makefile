# Client-puzzle micro-benchmarks: Hashcash Tree (solve + verify) and
# SVP (verify only). Two independent C++ programs; each takes the iteration
# count as its argument.
#
#   make            # build both
#   make bench_hct  # HCT only  (needs OpenSSL: libssl-dev)
#   make bench_svp  # SVP only  (no external deps)
#   ./bench_hct 50
#   ./bench_svp 50
#   make run        # build both and run with ITERS iterations (default 20)

CXX      ?= g++
CXXFLAGS ?= -O3 -march=native -std=c++17 -Wall -Wextra
# SHA256()/RAND_bytes() are the OpenSSL one-shot APIs; silence 3.x deprecation.
HCT_CXXFLAGS := $(CXXFLAGS) -Wno-deprecated-declarations
HCT_LDLIBS   := -lcrypto

ITERS ?= 20

.PHONY: all run clean
all: bench_hct bench_svp

bench_hct: bench_hct.cpp bench_common.h
	$(CXX) $(HCT_CXXFLAGS) -o $@ bench_hct.cpp $(HCT_LDLIBS)

bench_svp: bench_svp.cpp bench_common.h
	$(CXX) $(CXXFLAGS) -o $@ bench_svp.cpp

run: all
	./bench_hct $(ITERS)
	@echo
	./bench_svp $(ITERS)

clean:
	rm -f bench_hct bench_svp
