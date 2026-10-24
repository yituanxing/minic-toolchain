# RISC-V 32/64 target-architecture audit — 2026-10-24

## Verified baseline and provenance

Reviewed repository file-tree inventory (3056 entries) and read the
Core/Target interfaces, both RISC-V emitters, ILP32 and LP64 frame/ABI
implementations, shared Core call planning, global emitters, ISA
selection, MiniAS and current real-program CI. This is **not** a claim
that every line in all 3056 files was manually read.

* Fourarch baseline `40085473814`: x86_64, RV64, AArch64, LoongArch64
  all passed on `e61bb66538`.
* RV64I 3352-TU source-origin audit `40085102945`: PASS; not
  equivalent to a full strict-I 2064-object Linux boot.
* RV32I original AES-128 ECB GNU-vs-MiniC binary QEMU differential:
  PASS. Upstream cJSON 1.7.19 MiniC + MiniAS ELF32 compile: PASS.
  **cJSON runtime is NOT certified** by an object-only pass.
* Toolchain-focused `40085473819` remains independently FAILED in
  MiniLD `.riscv.attributes` verification (`readelf -A softfp-minild.elf`),
  while its preceding RV64I instructions and GNU-AS/MiniAS gates pass.

## Actual source boundaries (lines at baseline)

| File | Lines | Existing role |
| --- | ---: | --- |
| `riscv64/rv32_emit.c` | 837 | ILP32 Core lowering, frame, call ABI and software helpers |
| `riscv64/rv32_global_emit.c` | 127 | ILP32 globals, ELF32 relocations |
| `riscv64/core_codegen.c` | 780 | RV64 Core-to-ISA selection |
| `riscv64/frame_layout.c` | 559 | RV64 frame/liveness/offset owner |
| `riscv64/abi.c` | 295 | RV64 ABI classification |
| `common/direct_call_plan.c` | 280 | Shared Core call planning |
| `backend.c` | 91 | Common Core-to-target handoff |

```
Frontend / AST -> Core IR -> compiler/backend.c
     -> TargetInfo + DataLayout (ILP32/LP64, ISA)
     -> RV32 subtarget: rv32_emit + rv32_global_emit + ABI cursor
     -> RV64 subtarget: core_codegen + LP64 ABI + frame
     -> GNU assembly -> MiniAS ELF32/ELF64 -> link -> QEMU
```

The frontend, Core, TargetInfo, ISA registry, target-neutral layout and
some record-copy/symbol helpers are already shared. **Machine lowering
is NOT yet one XLEN-parameterized backend.** A growing separate RV32
instruction switch is architectural debt: converge by machine operation
and ABI/frame contract rather than copying 780 lines of RV64 emission.

## Concrete correctness issue fixed

The prior RV32 direct/indirect argument code used consecutive register
slots for all arguments. RISC-V ILP32 psABI requires unnamed variadic
2*XLEN values to start on an even-numbered register pair. Therefore
`printf-like(int, double)` passes the double in a2/a3, NOT a1/a2.
With a7 next, that argument spills to an 8-byte aligned stack pair,
and subsequent unnamed arguments also use the stack. The previous
outgoing-frame planner used a separate sequential-count algorithm that
could under-allocate the outgoing stack area in these cases.

One new `rv32_abi_cursor.c` now computes placement for the **same**
direct/indirect call identity consumed by outgoing frame sizing and
actual machine emission. Existing named long long split a7/stack0
behavior is retained. New GCC-defined `va_start / va_arg` callees are
exercised by MiniC-generated direct calls over true ELF32/QEMU with
both GNU AS and MiniAS assembly producers.

## Remaining ownership/debt

1. Full ILP32 variadic **callee definition** / va_list, aggregate
   by-value calls/returns, and multiword 64-bit ALU are distinct
   unqualified capabilities. Do not silently lower them as LP64.
2. cJSON full userspace runtime requires an authentic RV32 ILP32 libc,
   allocator/printf and symbol closure; current CI confirms original
   upstream object only.
3. MiniLD generic ELF32 is not equivalent to its existing compat-vDSO
   support. Separate the `.riscv.attributes` MiniLD issue from MiniC.
4. Next structural pass: move generic opcode semantics / common
   machine emitter primitives into `target/common` (or common RISC-V
   target), keep only ISA-specific physical operation choices in
   subtargets. Do this with fourarch, Linux strict-I, and RV32 original
   QEMU acceptance as fail-closed regression gates.
