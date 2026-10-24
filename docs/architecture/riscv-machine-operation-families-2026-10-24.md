# Review: RISC-V machine operation family ownership — 2026-10-24

Baseline: `1689ffa0404595bfb8725cd1a913c1c94f84f0f4`.

## Reviewed boundaries and duplication

Audited the Core→Target backend handoff, ISA and ABI profiles,
ILP32 frame/scalar/call and machine instruction dispatch, RV64
`scalar_emit.c`, RV64 strict-I libgcc provider and LP64 frame,
AArch64/LoongArch64 direct instruction semantics, x86 SysV lowering,
Makefile and CI owner path gates. This is a cross-component ownership
audit, not a claim to have line-reviewed the entire repository.

The existing RV32 `rv32_emit.c` still contained virtually all Core
opcodes: IEEE software-float, scalar arithmetic, memory/object
addresses, record copy, call and block layout. It was no longer a
machine-emitter coordinator, despite a prior large split. The RV32
and RV64 integer runtime symbol naming also duplicated the GCC/libgcc
XLEN suffix policy.

## Single responsibility after this batch

```
Core IR --> Backend --> RV32 function/CFG dispatcher
                          |
            +-------------+---------------+
            |             |               |
        integer_emit     fp_emit       memory_emit
         Core scalar    LLVM softfp    pointer/record
            |             |               |
            +---- rv32_scalar / rv32_frame +---- rv32_call
                          |
       riscv/instruction_select.h
           base-I register opcodes
           libgcc 32/64 arithmetic symbols
           LLVM softfloat symbols
                          |
          GNU .s -> GNU AS or MiniAS -> QEMU
```

The entire instruction family body was moved out of
`rv32_emit.c`, leaving its switch as an explicit instruction
ownership router. All previous expression semantics, ILP32 field
layout, backend rejection policies, and real assembly outputs remain
unchanged. ABI parameter reception is now in `rv32_call.c`, with
direct and indirect call ABI staging. The RISC-V shared selector now
also owns the mapping of integer arithmetic Core kinds to standard
GNU libgcc function names for both XLEN=32 and XLEN=64. RV64
retains its own M-extension native/libcall decision and call emission.

No Machine IR, new C semantics, runtime algorithms, MiniAS/LD/PP
refactoring or implicit RV32↔RV64 frame interchange was added.

## Validation

Added native independent C table of 10 GCC/libgcc integer symbol
identities, shift/compare selector and an FP helper name. The RISC-V
CI shard runs it before original RV32 ELF32 GNU/MiniAS differential
(QEMU, AES, cJSON compilation) and RV64 i128/softfloat; four 64-bit
architecture runtime jobs remain mandatory. Strict backend CI guards
every new family owner so implementation cannot drift back into the
single old monolithic RV32 emitter.

The preexisting independent MiniLD RISC-V ELF-attributes failure is
outside MiniC and must be separately described.
