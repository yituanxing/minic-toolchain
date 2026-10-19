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

## M1 real executable evidence — 2026-10-19

The initial M0 negative-test-only baseline above is historical; **M1 has
now actually produced, assembled, linked and executed two independent
new target ISAs**. Each new target has a distinct Core IR → GNU ELF
assembler emitter:
`src/target/aarch64/codegen.c` and
`src/target/loongarch64/codegen.c`. The shared frontend/Core IR and
existing RV64/x86 backend files were not rewritten for this.

Implementation deliberately starts small: single-block `int`
functions, integer constants, scalar parameters in the first eight
ABI register slots, add/sub/multiply, value slots plus distinct stack
object slots, Core `object.addr`, 32-bit loads and stores, and integer
returns. Incoming AArch64 `w0`–`w7` and LoongArch `$a0`–`$a7`
are independently mapped; stack frame layout is backend-owned.
Unsupported complex functions and globals fail explicitly and leave
no partial `.s` file. **This is not** general complete C, full ABI,
floating-point, aggregate calls, variable arguments, globals, CFG,
Linux, or new platform linker support.

**Original project tooling reused:** one pinned fixture runner
`tests/compiler/c0/run-four-backend-m1.sh`, invoked by the reusable
`make check-four-backend-m1`. It uses exactly the same C fixtures,
cross-GCC reference checks, GNU assembler, GNU linker, statically
linked ELF binaries, and real `qemu-aarch64` or `qemu-loongarch64`
runtime verdict mechanism. The AArch64 GNU sysroot requires both
`gcc-aarch64-linux-gnu` and `libc6-dev-arm64-cross`; the LoongArch
GCC 13 cross compiler similarly uses `libc6-dev-loong64-cross`.
Missing CRT and libc dependencies were diagnosed from actual linker
stderr rather than silently skipping runtime.

The test exercises **four actually executing MiniC-owned binaries per
ISA**: (`return 42`), two-argument `a+b` cross-called from GNU C,
two-argument `a*b-a` cross-called from GNU C, and a `return 1000000`
constant requiring a high halfword immediate. It compares QEMU exit
codes with independent GNU compiled references (42 or 64). Two GNU
baseline executables are also executed per target. Unsupported global
initialization is a mandatory negative test.

**Verified [GitHub Actions #39199250679](https://github.com/yituanxing/minic-toolchain/actions/runs/39199250679):**

```text
FOUR_BACKEND_M1_TARGET=PASS target=aarch64 GCC_oracle=2 MiniC_GNU_link_QEMU=4 unsupported=fail-closed
FOUR_BACKEND_M1_TARGET=PASS target=loongarch64 GCC_oracle=2 MiniC_GNU_link_QEMU=4 unsupported=fail-closed
FOUR_BACKEND_M1=PASS architectures=2 executable_QEMU_cases=8 GCC_independent_oracles=4
TOOLCHAIN_FOCUSED_T1=PASS mode=all independent_test_checks=8 shared_builds=1
```

**PR #527 independent review findings addressed:**
- P1: AArch64 GNU GAS literal `%function`, `%progbits` requires
  escaping inside `fprintf` as `%%function` and `%%progbits`;
  failing compiler `-Werror` CI proved the problem, then the
  corrected C emitter built and real QEMU tests passed.
- P2: AArch64 one-instruction `mov w9,#signed32` is not valid for
  every 32-bit constant. Use explicit `movz` plus conditional
  `movk` for both low and high 16-bit words, with fail-closed
  out-of-range constants. The high-halfword value `1000000`
  is a **real GNU-assembled, GNU-linked and QEMU-executed** case on
  both ISAs (exit 64), not merely an inspected assembly string.
- Actual Core IR, not guessed frontend code, exposed that even
  `int add(int,int)` includes local parameter objects and
  `object.addr` / `load` / `store` operations. Both emitters
  implement exactly those needed semantic operations and maintain
  separate 32-bit scalar and 64-bit pointer slots. An opt-in bounded
  `MINIC_M1_TRACE` Core dump is available for future source-driven
  failures; no speculative refactoring of the two existing backends.
- A reviewer also documented that PR #526 updated `main` with
  independent x86 SysV `long double` stack alignment and original
  BusyBox fail-closed fixes. PR #527 remains **Draft** and **must be
  synchronized with that newer main before any final merge**; M1
  CI passing does not imply the branch is rebased.

Temporary CI branch-whitelist/dependency changes were removed after
green validation; the original focused workflow is byte-identical to
the main baseline blob `cd9a880a5a707f28b5ea63c364c1844ab35e1c56`.

**Next:** grow scalar branch/control flow and stack ABI incrementally,
reuse original tinyAES/cJSON pinned project suites, record machine
code runtime and compiler provenance at each promotion gate, then
only extract demonstrated duplication.

## M2: original Core multi-block CFG, signed compare, true QEMU execution — 2026-10-19

This is the next measured increment from the previously proven M1, not
a new front end, new Core IR or new generic Machine IR.

**Source implementation:** the original AArch64 and LoongArch64 M1
emitters now iterate verified Core basic blocks and lower their distinct
local labels, `RETURN`, `BRANCH` and `CONDITIONAL_BRANCH`. Each Core
SSA value and local object retains a private per-function stack slot
across blocks. On branches, actual target code tests the saved condition
value and jumps to the correct function-scoped successor. The old RV64
and x86-64 emitters and all common C frontend/Core code remain unchanged.
No register allocator or speculative architecture-level abstraction was
introduced.

Both new ISA emitters also lower just the observed minimal Core
operations for signed 32-bit `INTEGER_LESS`, `SCALAR_EQUAL` and
`SCALAR_IS_ZERO`. AArch64 uses `cmp` and `cset`; LoongArch64 uses
`slt` and `xor` + `sltui`. Core bools and C ints remain distinct
types, with canonical 0/1 compare results in existing private value
slots. No unsigned pointer/floating/aggregate comparison support is
claimed. Unsupported Core kinds, object types and globals still
fail closed and delete any partial assembly file.

**Shared test process:** the *same* existing GNU cross-compiler,
GNU-AS/link, QEMU and deterministic C fixture harness now supports a
separate opt-in `FOUR_BACKEND_M2=1` path. The older
`make check-four-backend-m1` remains a separate independent gate.
The new `make check-four-backend-m2` expands the same test fixtures
with C source for both conditional `if` paths, a backward `while`
loop with 7 and 0 iterations, and signed less-than/equality comparisons
over negative/positive values and true/false outcomes. Cross-TU
GNU-built callers invoke the actual MiniC-owned function object.
The runner has a 15-second per-QEMU-case timeout, so an infinite-loop
backend regression cannot hang a shared CI runner indefinitely.

The new CFG/compare cases were *each* also compiled and executed
independently from unchanged C source by cross GCC on QEMU: six GNU
reference executables added, not just an invented expected exit code.
The existing four GNU oracles continue to be run. The MiniC outputs
were GNU-assembled, statically GNU-linked, and QEMU-executed.

**Actual [CI #39202223555](https://github.com/yituanxing/minic-toolchain/actions/runs/39202223555) — SUCCESS:**

```text
FOUR_BACKEND_M1=PASS architectures=2 executable_QEMU_cases=8 GCC_independent_oracles=4
FOUR_BACKEND_M2_GNU_ORACLE=PASS target=aarch64 case=branch GCC_executable=1 QEMU_exit=42
FOUR_BACKEND_M2_GNU_ORACLE=PASS target=aarch64 case=loop GCC_executable=1 QEMU_exit=42
FOUR_BACKEND_M2_GNU_ORACLE=PASS target=aarch64 case=compare GCC_executable=1 QEMU_exit=42
FOUR_BACKEND_M2_GNU_ORACLE=PASS target=loongarch64 case=branch GCC_executable=1 QEMU_exit=42
FOUR_BACKEND_M2_GNU_ORACLE=PASS target=loongarch64 case=loop GCC_executable=1 QEMU_exit=42
FOUR_BACKEND_M2_GNU_ORACLE=PASS target=loongarch64 case=compare GCC_executable=1 QEMU_exit=42
FOUR_BACKEND_M2_TARGET=PASS target=aarch64 MiniC_GNU_link_QEMU=7 CFG_branch=1 CFG_loop=1 signed_compare=1 unsupported=fail-closed
FOUR_BACKEND_M2_TARGET=PASS target=loongarch64 MiniC_GNU_link_QEMU=7 CFG_branch=1 CFG_loop=1 signed_compare=1 unsupported=fail-closed
FOUR_BACKEND_M2=PASS architectures=2 executable_QEMU_cases=14 CFG_cases=4 comparison_cases=2 GCC_independent_oracles=10
TOOLCHAIN_FOCUSED_T1=PASS mode=all independent_test_checks=8 shared_builds=1
```

**Scope:** M2 is 14 *MiniC-backed execution cases* across two ISAs
when run as a single expanded suite, plus 10 independently compiled
GNU oracles; the distinct M1 gate is also executed in CI and the
old RV64/AR/LD regression passes. These are deliberately small C
functions; this is **not** complete control flow or all GNU ABI calls
(>8 args, pointer/aggregate, floating, inter-function MiniC calls,
global data, Linux kernel remain unqualified). The branch is still a
Draft PR, and prior upstream PR #526's `main` change still requires
integration before any final merge. The temporary CI workflow branch
allowlist and cross-tool installs have been restored byte-for-byte
to the original focused workflow baseline blob
`cd9a880a5a707f28b5ea63c364c1844ab35e1c56`.

**Next driver:** Use the same measured cross GNU/QEMU fixtures for
stack-passed integer arguments and target-specific call instructions,
then original tinyAES/cJSON C owners. Keep new target ABI logic
independent and share only demonstrated test/controller duplication.


## M2 stack-argument ABI: GNU caller to MiniC 9/10 int parameters — 2026-10-19

This is a **narrow, verified extension** of the two original Core emitters,
not full new-ISA call lowering or a claim to compile tinyAES/cJSON.
AArch64 AAPCS64 and LoongArch64 lp64d each pass the first eight ordinary
`int` arguments in their own integer registers. The ninth and tenth
come from separate 8-byte-aligned slots on the **caller's** stack. Both
machine emitters now access those original stack arguments after their
own 16-byte-aligned local frame is allocated. No x86-64, RV64, shared
Frontend or Core IR implementation was changed.

- AArch64: load `w9` from `[sp, #frame + (index - 8) * 8]` and store the
  low 32 bits in the existing private SSA value slot.
- LoongArch64: load `$t0` from `$sp + frame + (index - 8) * 8` and store
  into the existing private value slot; reject any non-encodable signed
  12-bit memory offset. This deliberately supports at most 10 ordinary
  `int` parameters for now, with no silent acceptance of wider ABIs.
- Each new GNU C driver calls an independently compiled MiniC-owned
  function in a distinct translation unit. The `stack9` and `stack10`
  bodies exercise *all* arguments, including a negative tenth argument.
  The identical original C bodies plus GNU caller also compile and
  execute independently as reference executables for each ISA.
- Reuses the original `run-four-backend-m1.sh` harness; separately gated
  by `make check-four-backend-m2-stack`. Previous M1 and M2 make targets
  still run without the new cases, preserving their precise acceptance
  baselines. Unsupported globals remain an explicit fail-closed negative
  test.

**Executed CI:** [#39203749011](https://github.com/yituanxing/minic-toolchain/actions/runs/39203749011)
returned SUCCESS with independent GNU assembler/static linker/QEMU
runtime and the existing eight focused RV64/MiniAR/MiniLD checks:

```text
FOUR_BACKEND_M1=PASS architectures=2 executable_QEMU_cases=8 GCC_independent_oracles=4
FOUR_BACKEND_M2=PASS architectures=2 executable_QEMU_cases=14 CFG_cases=4 comparison_cases=2 GCC_independent_oracles=10
FOUR_BACKEND_M2_STACK_TARGET=PASS target=aarch64 MiniC_GNU_link_QEMU=9 GNU_stack_argument_oracles=2
FOUR_BACKEND_M2_STACK_TARGET=PASS target=loongarch64 MiniC_GNU_link_QEMU=9 GNU_stack_argument_oracles=2
FOUR_BACKEND_M2_STACK=PASS architectures=2 executable_QEMU_cases=18 ninth_tenth_arg_cases=4 GCC_independent_oracles=14
TOOLCHAIN_FOCUSED_T1=PASS mode=all independent_test_checks=8 shared_builds=1
```

**Honest remaining boundary:** This verifies only GNU→MiniC *callee*
entry with up to ten signed 32-bit integer arguments, not a MiniC
caller, preservation of return-address registers during nested calls,
global object relocation, pointers, floating-point, variadic or aggregate
ABI, or any original full real project. The next real pressure points
are target-owned direct calls and global-data ELF access, followed by
unchanged tinyAES/cJSON source on each QEMU target. No speculative
Machine IR or universal calling convention was added.

The temporary CI branch allowlist and cross-tool installation step
were restored byte-for-byte to original focused-workflow blob
`cd9a880a5a707f28b5ea63c364c1844ab35e1c56` after the green run.
PR #526 was subsequently merged into this branch with the explicit
two-parent merge `82ad2768cb3f837aeb027ab2cff7554aaa2c07cc`: upstream x86
codegen and original BusyBox owner-gate tests have exact main file identities.
`main` is now behind-by-0 relative to this Draft PR; the merge changed no
AArch64/LoongArch64 source semantics.
