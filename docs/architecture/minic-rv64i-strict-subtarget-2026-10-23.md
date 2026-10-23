# RISC-V strict ISA profiles: explicit feature selection (2026-10-23)

An explicit ISA is a verified contract, not a hint. A single RV64 backend
receives invocation-scoped XLEN, ABI and ISA feature bits. The default
unnamed RV64/LP64D target remains unchanged and byte-identical.

| Explicit ISA | ABI | Features | Proven limits |
| --- | --- | --- | --- |
| rv64i | lp64 | I only | integers without native multiply/divide, F/D or atomics |
| rv64im | lp64 | I,M | hardware integer multiply/divide; no FP |
| rv64imafd | lp64d | I,M,A,F,D | existing RV64 hard-float backend; excludes C/Z extensions |

The explicit profiles are checked over **actual selected assembly**, not
C operators or an invented parallel emitter. This covers frame, helper,
call and function instructions. Unsupported instructions or mismatched
ABIs fail with a diagnostic and no successful assembly output. We reject
inline/file asm for explicit profiles until target-aware validation is
qualified; arbitrary encoded data must not become an ISA bypass.

A permitted A extension does NOT mean that Core atomic instructions are
implemented: RV64 atomics currently fail in existing Core admission. This
is not RV32 support, a complete RISC-V extension grammar, or software
floating-point lowering. Unqualified flags fail rather than falling back
to RV64GC.

## Reuse, quality and progression

The native gate proves three accepted combinations, absent M/F/D rejects,
illegal ISA/ABI pairs, unsupported extensions, and unchanged default output.
The existing focused suite uses the already-built MiniC and cached GNU/QEMU
cross tools for freestanding GNU-vs-MiniC RV64I and RV64IM runtime checks.
It adds no new runner, branch or duplicate compiler build and does not
trigger the seven-shard kernel on each ISA change.

Next: pass ISA capabilities deeper into target-local machine selection, with
the final full-output audit retained as a catch-all safety invariant. Add
software lowering only when qualified, then RV32 ILP32 as a separate change
requiring 32-bit ABI slots, data layout, integer legalization and ELF32.
Keep the preprocessed C .i producer's ISA macros matched to the requested
target profile when the driver takes responsibility for preprocessing.


## ISA instruction-family admission before emission

Target-local RISC-V admission now runs after the frame has computed reachable
CFG blocks and before any function machine bytes are written. It makes the
immediate hardware-lowering decision for M integer multiply/divide/remainder,
multiply-overflow, scaled pointer arithmetic, and F/D floating operations.
Unreachable Core blocks do not spuriously require a CPU extension; the
ordinary Core verifier and ABI planner are not rerun. A rejected family has
a function/block/instruction diagnostic, and no .s artifact survives failure.

Strict LP64 soft-float has a separate ABI hazard: the mature RV64 function
and call emitters still use LP64D floating argument/return registers. Until
the soft-float psABI is implemented, strict LP64 denies floating scalar and
floating-containing aggregate parameter/return signatures, including
simple floating passthrough with no arithmetic. Pointers to floating values
do not count as floating parameters. This is deliberate fail-closed
qualification, not a claim that the C float type itself is unavailable.

The final emitted-instruction audit remains essential. Explicit source
Core checks cannot see every helper, frame, call, register and assembler
pseudo expansion. The default implicit RV64 kernel and self-host compiler
retain their existing fast path, without these additional CFG/ISA checks.

## First qualified RV64I native-software alternative: MUL low 64 bits

RISC-V ISA feature selection now admits ordinary <=64-bit scalar integer
multiplication under RV64I and lowers the selected operation to a bounded
shift-add modulo 2^64 implementation in target/riscv64/multiply_emit.c.
Hardware M and the original default output still use the original MUL
instruction. Negative multipliers are shifted *logically*, guaranteeing
termination after at most 64 iterations. The result is then normalized to
the Core result type by the existing scalar conversion path. Local numeric
assembly labels avoid mutable compiler-global state and repeated-label
collisions.

The same lowering supports pointer element scaling. Under RV64I, strides
that are powers of two use one SLLI; non-power-of-two strides use the same
software MUL lowering. The default kernel-facing RV64 emitter preserves its
prior MUL sequences byte-for-byte. Explicit RV64IM keeps native MUL. Native
division/remainder, 128-bit MUL and MUL-overflow still have explicit M
requirements until separately verified software alternatives exist.

Fast tests include RV64I strictly assembled source, unsigned/signed scalar
multiply, zero/negative multiplier, power-of-two and odd-sized pointer
scaling, GNU cross-assembler ISA checks, and QEMU process results, in
addition to the unchanged legacy ISA/ABI negative coverage.

## RV64I signed/unsigned software integer divide and remainder

The RV64 integer machine selector now offers both native M and RV64I
restoring long division for scalar quotient/remainder. A 64-iteration bit
algorithm shifts quotient into t0 and tracks the full 65-bit intermediate
remainder with a separate carry check; the latter is essential for unsigned
high-bit dividends. Signed operands use two's-complement magnitudes and
restore the proper quotient/remainder signs. Existing Core width conversion
handles 32-bit scalar results; no Frontend, Core, ABI or frame duplication.

Original default RV64 and explicit RV64IM instructions stay on the exact
native div/divu/rem/remu paths. Both software alternatives are target-local
and have no dependency on libc/libgcc. C division by zero and MIN/-1 overflow
are undefined and not advertised. GNU strict -march assemblers and QEMU
real-ELF differential tests cover an integrated multiply/divide/remainder
program plus signed/unsigned 32- and 64-bit cases and a GNU hardware-M oracle.
