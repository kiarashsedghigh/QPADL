# -----------------------------------------------------------------------
# Shared arch-detection helper. Include from each bench Makefile:
#
#   include ../common/arch.mk
#   CFLAGS := -O3 $(ARCH_FLAGS) -std=gnu11 -Wall -Wextra -I../common ...
#
# On aarch64 (RPi 4 / Cortex-A72 / A76 / Neoverse / M-series) this expands
# to `-mcpu=native`, which auto-selects the full ISA the host CPU exposes:
#     * NEON (always on ARMv8)
#     * ARMv8-A Crypto Extensions (AES / SHA-1 / SHA-2 / PMULL)  ← on A72
#     * CRC32
# so gcc emits hardware AES/SHA instructions where applicable, and -O3
# auto-vectorizes plain C to NEON.
#
# On armv7l (older 32-bit RPi) we settle for -march=native + explicit NEON.
# On x86_64 the block is a no-op so the same Makefiles keep working on
# your build/test host.
# -----------------------------------------------------------------------
ARCH := $(shell uname -m)

ifeq ($(ARCH),aarch64)
  # -mcpu=native: detect host CPU features. On RPi 4 this is equivalent to
  # -mcpu=cortex-a72+crypto, which enables AES/SHA/PMULL crypto ext.
  ARCH_FLAGS := -mcpu=native
else ifneq (,$(filter armv7l armv6l,$(ARCH)))
  ARCH_FLAGS := -march=native -mfpu=neon
else
  ARCH_FLAGS :=
endif
