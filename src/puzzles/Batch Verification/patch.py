#!/usr/bin/env python3
"""
Patch Dilithium reference sources for CUDA compilation.

- .c files: rewritten to sibling .cu files.
- .h files: rewritten in place (still .h).

Two things get rewritten in each file:

 1. Function definitions AND prototypes at file scope get
    `__host__ __device__` prepended. Skipping this for headers is
    what caused the mismatch that made kernels see host-only
    prototypes at their call sites. Functions in HOST_ONLY are
    left alone (they transitively call randombytes(), so they must
    stay host-only).

 2. File-scope `[static] const TYPE NAME[...]` arrays get a
    conditional `__device__` qualifier via the `__ROMQ` macro
    defined at the top of the patched file. During device
    compilation `__CUDA_ARCH__` is set and `__ROMQ` expands to
    `__device__`; during host compilation `__ROMQ` is empty.
    Covers KeccakF_RoundConstants (fips202.c) and zetas (ntt.c).
"""

import re
import sys
from pathlib import Path

HOST_ONLY = {
    "crypto_sign_keypair",
    "crypto_sign_signature",
    "crypto_sign_signature_internal",
    "crypto_sign",
    "crypto_sign_open",
    "randombytes",
}

TYPES = "|".join([
    r"unsigned\s+int", r"unsigned\s+char", r"unsigned\s+long",
    "void", "int", "size_t",
    "uint8_t", "uint16_t", "uint32_t", "uint64_t",
    "int8_t", "int16_t", "int32_t", "int64_t",
    "poly", "polyvecl", "polyveck", "keccak_state",
])

# Function definitions and prototypes at file scope (column 0).
# We don't distinguish between them — the regex ends at `(`, which
# is the same in both. Everything after `(` (parameter list, `{...}`
# body, or trailing `;`) is left as-is.
FUNC_DEF_RE = re.compile(
    r"^(?P<mods>(?:static\s+)?(?:inline\s+)?)"
    r"(?P<type>(?:const\s+)?(?:" + TYPES + r")\s*\*?\s*)"
                                           r"(?P<name>[A-Za-z_][A-Za-z0-9_]*)"
                                           r"\s*\(",
    re.MULTILINE,
    )

# File-scope const arrays: [static] const TYPE NAME[
CONST_TYPES = "|".join([
    r"unsigned\s+(?:int|char|long|short)",
    "uint8_t", "uint16_t", "uint32_t", "uint64_t",
    "int8_t", "int16_t", "int32_t", "int64_t",
    "int", "char", "long", "short",
])
GLOBAL_CONST_RE = re.compile(
    r"^(?P<static>static\s+)?"
    r"const\s+"
    r"(?P<type>" + CONST_TYPES + r")"
                                 r"\s+"
                                 r"(?P<name>[A-Za-z_][A-Za-z0-9_]*)"
                                 r"\s*\[",
    re.MULTILINE,
    )

ROMQ_PROLOGUE = """\
/* --- inserted by patch.py: conditional device qualifier for constants --- */
#ifndef __ROMQ
#  ifdef __CUDA_ARCH__
#    define __ROMQ __device__
#  else
#    define __ROMQ
#  endif
#endif
/* ------------------------------------------------------------------------ */

"""

def patch(text):
    n_funcs = 0
    def repl_func(m):
        nonlocal n_funcs
        name = m.group("name")
        if name in HOST_ONLY:
            return m.group(0)
        n_funcs += 1
        return f"__host__ __device__ {m.group('mods')}{m.group('type')}{name}("
    text = FUNC_DEF_RE.sub(repl_func, text)

    n_consts = 0
    def repl_const(m):
        nonlocal n_consts
        n_consts += 1
        static = m.group("static") or ""
        return f"__ROMQ {static}const {m.group('type')} {m.group('name')}["
    text = GLOBAL_CONST_RE.sub(repl_const, text)

    if n_consts > 0:
        text = ROMQ_PROLOGUE + text
    return text, n_funcs, n_consts

def main():
    if len(sys.argv) < 2:
        print("Usage: patch.py <files.c|.h> ...", file=sys.stderr)
        sys.exit(1)
    for path in sys.argv[1:]:
        p = Path(path)
        text = p.read_text()
        out, nf, nc = patch(text)
        # .c -> .cu (rename), .h -> .h (in place)
        if p.suffix == ".c":
            outp = p.with_suffix(".cu")
        else:
            outp = p
        outp.write_text(out)
        print(f"  {p.name:24s} -> {outp.name:24s}  ({nf} funcs, {nc} consts)")

if __name__ == "__main__":
    main()