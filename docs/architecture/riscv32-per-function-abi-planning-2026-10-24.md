# RV32I per-function ABI ownership — architecture/CI audit

Base: `e2dc77d35f2daee2aec0698e30244dc0ca139d11`.

Audited the complete MiniC backend dispatch, RV32 ILP32 caller/callee
argument paths, CoreCallView provenance, common GPR cursor policy,
Core instruction array index and block iteration, frame stack bound and
cleanup, target-aware scalar width checks, RV64 LP64 ABI and frame
ownership, AArch64/LoongArch64 validated direct-call planning, x86
SysV, five architecture CI entry points. This is an architectural
ownership/dependency audit, not a source-by-source review of every
file in the repository.

## Defect class: frame/call/receiver each replan the same argument ABI

Formerly RV32 `frame_init` walked every call's arguments once just to
reserve stack space, `rv32_emit_scalar_call_arguments` recalculated
that same cursor twice (once to store stack, once to fill registers),
and each PARAMETER instruction linearly scanned all earlier incoming
formals. This was redundant and risked divergent ABI interpretations.

## Single owner: rv32_abi_plan

```
CoreFunction/CallView
         |
rv32_abi_plan.c (once per function)
   |         |             |
incoming  each call      max stack words
fixed     immutable      in outgoing area
formals   locations
   |         |             |
rv32_call.c            rv32_frame.c
         \               /
             rv32_emit.c
```

The ABI planner validates every Core direct/indirect signature and
actual argument kind, types, locations and stack size once. It stores
incoming named formal locations plus per-instruction call locations,
including split two-word named arguments, even-register unnamed
variadics and aligned spill stack holes. The frame allocates outgoing
space from the same plan; emission uses its saved placements without
walking Core arguments or the cursor again. Call plans are indexed by
verified `MinicCoreInstructionId` and checked against their actual
instruction, rather than a host pointer subtraction. The plan is
destroyed with the frame regardless of success/failure. Call views
borrow the original Core IR only for this function emission lifetime.

RV64 keeps its independently qualified LP64[DF] ABI and frame planner;
only the already shared RISC-V GPR primitive remains target-neutral.
There is no Machine IR, runtime algorithm, assembler/linker change, or
newly supported language operation in this refactor.

## Gates

Native MiniC strict boundary checks, real x86/BusyBox, fourarch runtime
suite, original RV32 GNU/MiniAS ELF32 QEMU and varargs wide alignment,
upstream tinyAES, original cJSON compile boundary, RV64I seven frozen
Linux shards and 128-bit/softfloat QEMU. The independent MiniLD
`.riscv.attributes` failure stays separate.
