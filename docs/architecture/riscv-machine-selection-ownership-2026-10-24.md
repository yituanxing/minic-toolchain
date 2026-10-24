# RISC-V machine selection / global ownership audit — 2026-10-24

## Scope reviewed, without overstating coverage

Reviewed repository root and source directories, compiler-to-target backend
contract, Core IR/call planning, Target DataLayout and ISA, RV32 ILP32 frames
and argument cursor, RV64 LP64D/LP64 ABI/frame planners, x86/AArch64/LoongArch64
backend boundaries, RISC-V library call policy, original RV32 ELF32/GCC/QEMU
differential sources, Makefile build graph and four-architecture CI triggers.
This is a dependency-boundary audit, NOT a line-by-line review of every file.

## Single opcode naming owner, independent physical consumers

```
Frontend/AST -> Core IR -> backend
                         |
                 TargetInfo/DataLayout/ISA
                         |
               riscv/instruction_select.h
                    /              \
              RV32I/ILP32        RV64/LP64[DF]
               rv32_emit        scalar_emit
             rv32_abi_cursor    abi/call_emit
                     \            /
                      GNU assembly
```

Share only byte-identical base-I opcode identities (add/sub/bitwise/
shift/compare) and standard softfloat arithmetic library symbol names.
Keep semantic Core admission, type-width conversions, function linkage,
physical register/stack transport, variant-dependent M/F/D ISA decisions,
and ELF32/ELF64 layouts in their existing owners. In particular RV32
two-word wide values are not LP64 one-word slots; RV64's legacy ISA and
strict-LP64 softfloat feature gating remain unchanged.

## This batch and regression contract

- RV32/RV64 use a common scalar opcode selector; remove duplicate hand-coded
  instruction-name maps from both machine emitters.
- RV32/RV64 share standard compiler-rt/libgcc symbol naming, but not runtime
  algorithms or ABI-aware call emission.
- Extend original RV32 MiniC + MiniAS/GNU-AS + GNU-LD + QEMU + GCC independent
  differential with six signed/unsigned bitwise/shift/compare functions.
- Observe future shared RISC-V selector changes in the fourarch workflow.
  Leave MiniAS, MiniLD, MiniPP and other target backends unchanged.

This does NOT introduce Machine IR or claim ILP32/LP64 ABI equivalence.
Next convergence must reuse proven semantic operation shapes and isolate
XLEN-specific physical transport. The current separate RV32 machine switch,
callee variadic definitions and 64-bit ALU remain distinct outstanding work.

Required CI: native MiniC, fourarch GNU/QEMU, RV32/GNU differential,
RV64I seven-shard origin audit, relevant original BusyBox/Linux runtime
gates. Existing MiniLD RISC-V attributes failure is separately owned and
must not be mistaken for an RV32 machine-lowering regression.
