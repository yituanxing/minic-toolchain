# RISC-V explicit ISA, ABI, and external arithmetic runtime (2026-10-23)

## Target contract and current support

MiniC shares one RV64 backend. The selected `TargetInfo` carries XLEN,
the instruction feature set, ABI and strict ISA qualification. The historical
implicit RV64 profile remains unchanged to preserve Linux/BusyBox outputs.

| ISA | ABI | Machine arithmetic | Additional runtime |
| --- | --- | --- | --- |
| implicit legacy RV64 | LP64D | existing native M/F/D codegen | existing build-provided runtime |
| `rv64i` | `lp64` | integer I operations, shift for power-of-two scaling | **external** GCC libgcc or LLVM compiler-rt for MUL/DIV/REM |
| `rv64im` | `lp64` | native M integer arithmetic | none for ordinary scalar MUL/DIV/REM |
| `rv64imafd` | `lp64d` | existing hard-float codegen plus M/A feature admission | existing build-provided runtime |

ISA/ABI mismatches and unimplemented `-march` strings fail closed. RV32,
arbitrary user inline asm, full soft-float LP64 function calling, atomics
and unqualified extension combinations remain unsupported. An ISA extension
being present does not imply all corresponding C operations are implemented.

## Compiler-owned responsibilities

`src/target/riscv64/isa.c` validates explicit ISA profiles and checks
reachable Core operations before instruction emission. The complete generated
assembly is still checked independently to catch frame/call/pseudo-instruction
paths omitted by instruction-family admission.

`src/target/riscv64/runtime_libcall.c` maps missing M arithmetic to the
**standard** RV64 LP64 integer psABI functions:
`__muldi3`, `__divdi3`, `__udivdi3`, `__moddi3`, `__umoddi3`.
The compiler never owns or implements these algorithms; it only chooses
between native hardware instructions and external runtime calls, marshals
`a0/a1`, and reads the `a0` result. A function that introduces a helper
call now reserves and saves its original return address in the existing RV64
frame plan, even when the function has no explicit C/Core CALL. This extra
frame slot is needed ONLY for ISA configurations that lower operations to
runtime calls; existing RV64 Linux output is unchanged.

Strict-I pointer offsets use SLLI for power-of-two element sizes and
`__muldi3` for non-power-of-two sizes. Because a call clobbers caller-saved
registers, the pointer base is reloaded from its frame-owned value before
the final add/sub. The software algorithms formerly in
`multiply_emit.c` and `divide_emit.c` were **deleted**. MiniC only retains
very small inline transformations such as SLLI for constant scaling.

For <=32-bit integer operations, inputs are first canonicalized to the
effective signed/unsigned scalar type before passing the full XLEN arguments
to the external helper. Normal scalar result conversion and spills remain
owned by the mature Core and frame lowering. 128-bit operations and checked
multiplication still need separate qualified lowering and may reject RV64I.

## Runtime provider and licensing

**MiniC is not the linker or runtime package.** The final build must supply
compatible libgcc/compiler-rt built for its exact ISA, data model and ABI.
A `call __muldi3` assembler file can be ISA-correct while linking against
an ABI-incompatible libgcc.a is not; never assume the host cross toolchain's
default multilib is correct for `rv64i/lp64`.

For deterministic **CI-only** linking, the regression suite vendors unchanged
GCC 14.2.0 RISC-V libgcc assembler sources `muldi3.S` and `div.S`, plus
upstream assembly macros and full GPL-3.0 / GCC Runtime Library Exception
3.1 license notices, under `tests/third_party/gcc-riscv-14.2.0/`. It builds
those third-party objects with `-march=rv64i -mabi=lp64` and never injects
them into MiniC itself. A production toolchain may use a matching GCC
libgcc or LLVM compiler-rt implementation instead.

The `minic-cc` entrypoint currently consumes already preprocessed `.i`
source; the producer of those inputs must use a matching ISA/ABI macro
context. This work does not alter MiniPP, MiniAS or MiniLD.

## Verification and next work

The native backend gate checks strict compilation, feature admission,
ABI rejections, presence of the external symbols, frame save/restore and
historical byte-exact default output. The focused cross-toolchain gate
reuses its existing single compiler build and cached GNU/QEMU tools,
assembles explicit RV64I/LP64 targets strictly, builds the upstream GCC
libgcc runtime sources, links freestanding ELF and compares results with
GCC native-RV64IM arithmetic for signed/unsigned 32- and 64-bit inputs.
Kernel 2064 objects and existing real-software acceptance remain regression
baselines, not a reason to run seven complete Linux shards per small edit.

Next prioritize verified RuntimeLibcall ABI coverage (64-bit arithmetic,
checked overflow and 128-bit only when upstream builtin availability is
confirmed), then full ABI configuration and RV32 ILP32. Do not develop more
MiniC-private arithmetic libraries.
