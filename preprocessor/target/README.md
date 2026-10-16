# MiniPP RISC-V target predefines

`riscv_linux_predefines.py` is the canonical source-owned target builtin macro
producer for the certified **Linux 6.6.143 / RISC-V GCC 13.3.0** compilation
profile. It is not a CI patch, is not a GCC subprocess wrapper, and is never
allowed to guess an unsupported target-flag combination.

## Architecture and ABI

The current supported profiles are:
- RV64 LP64 C: GNU11, Linux `rv64imac_zicsr_zifencei_zihintpause`,
  `-mcmodel=medany`, with explicit tested `-O2` and PIC/PIE variations.
- RV64 LP64 assembler-with-cpp:
  `rv64imafdcv_zicsr_zifencei_zihintpause`.
- RV32 ILP32 compat assembler-with-cpp: `rv32g`.

Production uses a fixed GCC-13-compatible ABI macro baseline and derives
flag-dependent macros without invoking GCC. The immutable capture at
`tests/preprocessor/profiles/rv64-linux-6.6.143-gcc13.json.gz.b64` serves
as a pinned compatibility dataset and independent per-context oracle. The
target rules are not yet a general-purpose builtin generator for arbitrary
architectures or GCC versions, nor are they embedded in the C MiniPP binary.

## Usage and acceptance

```sh
python3 preprocessor/target/riscv_linux_predefines.py --self-check

MINIPP_PREDEFINES_NATIVE_RULES=1 \
MINIPP_PREDEFINES_LANG=c \
MINIPP_LINUX_SRC=/path/to/linux-6.6.143 \
MINIPP_PREDEFINES_OUT=/tmp/riscv-target.h \
  bash tests/preprocessor/build_reference_predefines.sh \
    -std=gnu11 -fshort-wchar -funsigned-char -fno-PIE \
    -mabi=lp64 -march=rv64imac_zicsr_zifencei_zihintpause \
    -mcmodel=medany
```

The generated header is fed to the actual `minic-cpp` executable through
`-include`. For Linux integration, each authentic GNU `-E` result is
checked **byte-for-byte** against native MiniPP output, but GNU reference
output is never passed to MiniC or MiniAS as the accepted product input.

The acceptance gate is seven fresh C shards (2064 objects), 34 real
`.S` owners, four regenerated-header C owners, the certified MiniAR/MiniLD/
MiniObjcopy boundaries, and final QEMU P1 12/12, with immutable source and
artifact hashes. A byte-identity or standalone macro self-check is not, by
itself, runtime certification.

RV32 compat-vDSO ELF32 linking by MiniLD remains a separately scoped
deferred capability; this macro work does not claim that link edge is native.
