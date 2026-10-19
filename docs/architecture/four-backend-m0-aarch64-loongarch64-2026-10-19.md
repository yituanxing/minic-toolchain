# Four-backend M0: scoped pre-implementation audit and target registry

Baseline: `main@df16824e859364bfde082bca7cd3dbbe0a8037cd` (merged PR #524).
Branch: `feature/four-backend-aarch64-loongarch64-m0`.

This is **not** a second whole-compiler audit: the two-backend merge
already passed fresh RV64 Linux 2064/QEMU P1 and x86-64 full-BusyBox
581-MiniC-owner executable smoke. The only useful additional audit now
is at the point a third/fourth ISA touches shared code.

## Source-checked interfaces and new-ISA boundaries

| Interface | Existing owner | M0 action | Rule going forward |
| --- | --- | --- | --- |
| C semantic target selection | `src/target/target_info.{h,c}` | Give AArch64 and LoongArch64 separate target identities / LP64 integer models | `char` signedness and other C types must match their *own* ABI |
| C object sizes and layout | `src/target/data_layout.{h,c}` | Register separate AAPCS64 and LoongArch lp64d descriptors | Equal byte sizes do **not** imply interchangeable call/return ABI |
| Shared parse/normalize/Core IR | `src/compiler/compiler.c` | Preserve the existing common path; reject unimplemented machine emitters explicitly | Never fall through into x86-64 or RV64 code emission |
| x86 and RV64 backends | `src/target/{x86_64,riscv64}/` | No modifications in M0 | Actual new ISA owns registers, stack frames, call ABI, instructions, ELF relocations |
| GNU cross-toolchain oracle | Existing `tests/compiler/c0` and external project probes | Reuse current preprocessed C fixtures and command structure | Cross GCC/AS, native GCC oracle and QEMU prove each *separate* gate |

### Specific targeted findings

- **F1:** The current compiler explicitly lists only RV64/x86 targets, then
  dispatches the fallback case to the **x86 emitter**. Registering two
  new target enum values without a strong guard would therefore risk an
  architecture-mismatched assembly output. M0 must **fail closed**
  *before parsing or emitting* for the two new targets.
- **F2:** The x86 parser has target-specific `_Float{32,64,128}` and
  `__builtin_va_list` handling. This should **not** be rebranded as generic
  merely to make the new targets parse Linux headers; each new ABI must
  prove its own va_list shape. More targets help reveal genuine sharing.
- **F3:** RV64 currently has its own layout stage while x86-64 handles
  layout within its own backend. Do **not** introduce a Machine IR,
  global register allocator, universal ABI classifier or emitter vtable
  without having a *working third* backend expose a concrete duplication.
- **F4:** LoongArch ELF `lp64d` has signed plain `char`; AAPCS64 LP64
  has unsigned plain `char`. Both use 8-byte pointers, 8-byte `long`,
  and 16-byte `long double`, but the instruction set, register bank and
  aggregate calling rules differ. The M0 type model may record widths;
  M0 does not claim any machine-code or linking capability.
- **F5:** Test names need scoped verdicts (source translation, GNU assembly,
  full link, QEMU runtime, project integration) and exact input/target/
  toolchain provenance. Never treat a recognized target string or a
  successfully generated `.s` as an executable passing.

### Primary ABI sources

- Arm AAPCS64 (Linux LP64 C arithmetic/char mapping and 128-bit long double):
  https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst
- LoongArch ELF ABI (lp64d, default signed char, type sizes/alignments):
  https://loongson.github.io/LoongArch-Documentation/LoongArch-ELF-ABI-EN.html
- LoongArch GCC ABI mode `-mabi=lp64d`:
  https://gcc.gnu.org/onlinedocs/gcc/LoongArch-Options.html

## Minimum implementation ladder (shared tests, separate ABI)

- **M0 — target identity and negative gate (this branch):** The compiler
  can name all four target models, but only RV64 and x86-64 can emit machine
  code; AArch64 and LoongArch64 must fail with a precise missing-backend
  message and leave **no output**. Existing RV64 default and native x86
  target tests must still pass.
- **M1 — real target-specific executable:** Each new target independently
  emits correct ELF assembly for `return 42` and small integer arithmetic;
  GNU AS + GNU linker + QEMU runs it and checks GCC reference exit 42.
  Claim target runtime only after both toolchains and QEMU actually run.
- **M2 — scalar ABI:** 32/64-bit integers, pointers, local variables,
  six-plus calls and returns; independent native GNU ↔ MiniC cross-call
  tests. Add floating registers and record passing only on ABI evidence.
- **M3 — original real project pressure:** tinyAES and cJSON first;
  reuse pinned version, unchanged source, differential harness; then zlib,
  Lua and heavier projects where errors expose genuine general patterns.
- **M4 — scale and architecture refactor:** When AArch64/LoongArch64 have
  enough practical coverage, extract only *demonstrated* code/testing
  duplication; never start by broad-rewriting x86/RV64 internals.

## Explicit nonclaims

This M0 contains **no AArch64 or LoongArch64 instruction selection, ABI
call lowering, ELF relocations, built artifacts, QEMU passes, zlib
results, or Linux kernel support**. Every unsupported machine request
must be rejected rather than silently using the x86/RV64 backend.
Existing two backend tests remain the regression oracle.
