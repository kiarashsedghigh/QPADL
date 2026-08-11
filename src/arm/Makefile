# -----------------------------------------------------------------------
# Top-level driver for the ARM client-op benchmarks.
#
# Each subdir (hash, aes, puzzle_hct, puzzle_lbp, pir, ml_dsa) has its
# own Makefile and can be built/run independently. This file just fans
# out to them.
# -----------------------------------------------------------------------

SUBDIRS_NO_SETUP = hash aes puzzle_hct puzzle_lbp pir
SUBDIRS_WITH_SETUP = ml_dsa
SUBDIRS = $(SUBDIRS_NO_SETUP) $(SUBDIRS_WITH_SETUP)

all: setup
	@for d in $(SUBDIRS); do $(MAKE) -C $$d all || exit $$?; done

setup:
	@for d in $(SUBDIRS_WITH_SETUP); do \
	  if [ ! -f $$d/src/setup.stamp ]; then $(MAKE) -C $$d setup; fi; \
	done

run: all
	@bash run_all.sh

clean:
	@for d in $(SUBDIRS); do $(MAKE) -s -C $$d clean; done

distclean:
	@for d in $(SUBDIRS); do $(MAKE) -s -C $$d clean; done
	@for d in $(SUBDIRS_WITH_SETUP); do \
	  $(MAKE) -s -C $$d distclean 2>/dev/null || true; \
	done

.PHONY: all setup run clean distclean
