# MiniC thin Machine IR contract (2026-11-05)

Status: **model and standalone verifier implemented; production migration
pending**. `src/target/common/machine_ir.{h,c}` and
`machine_ir_verify.{h,c}` own typed physical machine objects and validate
entire synthetic functions on all five target identities. The current target
emitters still write assembly directly and do not consume this model, so the
machine pipeline described below is the required migration destination, not
an accomplished five-target backend handoff.
This decision supersedes older "defer MIR" notes written while Core was
shadow-only or before the five production targets consumed sealed Core.

## Placement and owners

The current production handoff is
`backend.c -> program_visit.c::minic_program_visit_prepared_functions_module`.
The shared `MinicCoreFunctionPlan` already verifies Core and produces call,
object-storage, reachability and SSA-use facts. Preserve that single owner.

The new, intentionally small machine path is:

1. Target-owned ABI and frame planning consumes verified Core + shared facts.
2. Target instruction selection **builds** one `MachineFunction` containing
   native machine operations; it must not write machine instructions to `FILE*`.
3. `MachineVerifier` checks that complete function against its selected target,
   frame, operand, branch, register/effect and *generated* ISA contract.
4. A target GNU-assembly serializer prints only the verified machine function.

The existing ModuleIR is the sole global/symbol/data owner. ELF symbol and
section metadata can remain in the module writer; function prologue, blocks,
calls, return and epilogue machine instructions must pass through MIR. A
serialized `.s` line, buffered `FILE*`, or pre-rendered opcode string is **not**
a machine instruction.

## Required first-version representation

- `MachineFunction`: selected `TargetInfo`, blocks, frame references and
  function-level machine ABI metadata. It borrows immutable Core/Module
  provenance only during lowering and owns everything needed for serialization.
- `MachineBlock`: machine block identity, successor edges, indirect-entry
  property and an optional exported local label, followed by instructions.
- `MachineInstruction`: **target-scoped opcode**, typed operands, explicit and
  implicit physical-register defs/uses, clobbers and effects, optional Core
  instruction provenance for diagnostics.
- `MachineOperand`: physical register + width/class, integer immediate,
  frame-index + displacement, symbol + relocation kind + addend, block target,
  or target-native memory address.
- `MachineEffects`: memory read/write, volatility, compiler barrier, CALL,
  register clobber and structured/opaque inline ASM ownership.
- `MachineVerifier`: opcode/operand shape, valid register constraints and
  implicit effects, valid frame and block references, permitted target-selected
  instruction features and complete branch-target membership.

All **compiler-selected** opcodes must satisfy the active *generated* ISA
feature set; the ISA accepted for source inline ASM is independently checked
by SourcePolicy/GNU AS. Inline ASM remains an explicit MIR instruction with
sealed typed operands/constraints and an opaque source template. Do not parse
source ASM into invented target opcodes, and do not exempt compiler-selected
opcodes by marking them as inline ASM.

## Explicit non-goals

The first release has **no** optimizer/pass manager, scheduling, generic
register allocator, mandatory virtual registers, liveness intervals, unified
five-ISA opcode table, TableGen-style machine DSL, Core SSA redesign,
cross-target ABI allocator, COFF/Mach-O or native object encoder. Reuse the
existing target scratch-register, frame, calling convention and instruction
selection algorithms. Represent their physical decisions; do not redesign
them. Target ABI and machine instruction implementations remain target-owned.

## Migration and removal rule

Keep the work on a single refactor branch and change complete machine-function
paths, never individual opcodes behind a permanent dual dispatcher:

1. Seal remaining C-ingress/Module ownership conflicts, including typed ASM
   immediate and ELF undefined-weak versus real-definition binding.
2. Add the common owned MIR model, builder and independent verifier with
   synthetic positive/negative machine-function tests.
3. Migrate **AArch64 + LoongArch64 together**, reusing Direct LP64 planning.
4. Migrate **RV32 + RV64 together**, retaining distinct ILP32/LP64 plans and
   the strict RISC-V I-only generation policy when configured.
5. Migrate **x86-64 as a complete backend**, retaining SysV, flags, IBT,
   retpoline, kernel segments and its current register strategy.
6. Delete the old direct Core-to-instruction-text paths before merging.

A target counts as migrated only when its *entire* function machine output
goes through `MachineFunction -> verify -> GNU serializer`, with no fallback
to old direct Core instruction emission. Different targets may temporarily be
at different migration steps on the private branch, but no target is declared
done with a mixed instruction pipeline.

## Correctness gates

First compare the same original `.i` under identical ABI/ISA flags with the
old and new head: Core/ASM poison-negative probes, five-target focused `.s`,
native x86 GCC/GNU AS link and selected GNU ELF symbol/relocation checks.
MIR verifier negative probes must forge bad opcode, operand, frame, block,
implicit clobber and generated-ISA combinations and reject them. For each
complete target conversion, compare machine instruction sequences modulo
benign label spelling and formatting; no deliberate instruction optimizations
are allowed in this phase.

Do **not** rerun full Linux seven-way CI for each internal conversion. After
five-target structural closure, restore exact-commit normal programs and
Linux GNU AS/LD/Image/QEMU gates. Historical RV64 Linux or earlier compile
counts are reference baselines, not exact-head MIR runtime proof.
