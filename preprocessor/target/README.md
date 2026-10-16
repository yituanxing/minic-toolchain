# MiniPP native RISC-V target predefines

The **production** target builtin macro engine is now C code:
`riscv_linux_predefines.c`, compiled directly into the canonical
`minic-cpp` executable; the three independently verified ABI baselines
are stored in `riscv_linux_profiles.inc`. This code implements the exact
certified Linux 6.6.143 / GCC 13.3.0 option contexts. It does **not**
call Python, GNU GCC, gzip or any other subprocess to produce the macros.

The old rule implementation is preserved only at
`tests/preprocessor/oracle_riscv_linux_predefines.py` as a **read-only
historical test oracle**, not product source. A focused regression checks
that every one of the nine certified macro contexts is **byte-identical**
between native C and the historical algorithm, and that unsupported
options fail closed.

## Scope

- RV64 LP64 C: GNU11 Linux `rv64imac_zicsr_zifencei_zihintpause`,
  `-mcmodel=medany`, plus verified `-O2`, `-fPIC`, `-fpic`,
  `-fpie`, `-fno-builtin-wcslen` variations.
- RV64 LP64 assembler-with-cpp: `rv64imafdcv_zicsr_zifencei_zihintpause`.
- RV32 ILP32 compat assembler-with-cpp: `rv32g`.
- Arbitrary architectures, flag combinations and general GCC-target builtin
  synthesis are not certified. Neither is RV32 ELF32 **linking**.

The frozen reference dataset in
`tests/preprocessor/profiles/rv64-linux-6.6.143-gcc13.json.gz.b64`
is the independent compatibility oracle. This immutable historical data
does **not** mean that GCC generates macros at runtime.

## Commands

Build once:

```sh
make -j4 MODE=release CFLAGS=-Werror build/release/bin/minic-cpp
bash tests/preprocessor/run-target-predefines-native.sh \
  build/release/bin/minic-cpp
```

The same native `minic-cpp` can produce its own target predefines:

```sh
build/release/bin/minic-cpp --dump-target-predefines \
  --language c --output /tmp/riscv-target.h -- \
  -std=gnu11 -fshort-wchar -funsigned-char -fno-PIE \
  -mabi=lp64 -march=rv64imac_zicsr_zifencei_zihintpause \
  -mcmodel=medany
```

In Linux integration, the existing
`tests/preprocessor/build_reference_predefines.sh` dispatches to
native C under `MINIPP_PREDEFINES_NATIVE_RULES=1` and passes the
generated target-macro header to native MiniPP via `-include`.
`MINIPP_NATIVE_CPP` can point at an already-built product binary.
The ordinary GNU reference route remains a separate differential oracle.

## Acceptance

Focused C-vs-Python byte identity is a **necessary but insufficient**
qualification. The previously accepted full Linux 2064 C + 34 S + four
generated-header runtime with QEMU P1 was certified with the Python
product generator before the C port. The native C path must independently
prove the exact seven-shard Linux producer, complete union, and QEMU
P1 12/12 on a known source SHA before that runtime status is transferred
to the C implementation. A previous successful run is not evidence that a
new binary was used.

RV32 compat-vDSO ELF32 output by MiniLD is independent and has its own
cached four-object differential gate and runtime integration acceptance.
