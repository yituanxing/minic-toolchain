# MiniC thin Machine IR contract (2026-11-05)

Status: **owned model, common builder, independent verifier and five target
ISA opcode/serializer foundations implemented; production migration pending**.
`src/target/common/machine_ir.{h,c}`, `machine_builder.{h,c}`,
`machine_ir_verify.{h,c}` and `machine_gnu_asm.{h,c}` own and independently
verify typed physical machine objects before any machine-body emission. The
five target-specific physical opcode contracts have standalone positive and
forged-negative full-function serializer probes. The current *production*
target emitters still write assembly directly and do not consume this model,
so the complete target handoff below remains future migration work.
This decision supersedes older "defer MIR" notes written while Core was
shadow-only or before the five production targets consumed sealed Core.

### Latest ownership closure (same refactor branch)

- `CoreFunctionSet` finalization now assigns a nonzero, TU-unique source-ASM
  occurrence ID from sealed Module function order, including independently
  emitted specialization clones. The `core_machine_asm.h` ingress copies this
  ID and target machine-form proof into an owned `MachineInstruction`; it
  cannot invent identity from a template hash or a C statement index.
- Empty volatile or memory-clobber GNU ASM is represented as an effect-only
  `SOURCE_ASM` node. It is validated without a target opcode or assembler
  printer and emits no bytes, preserving the Core barrier without a text
  escape or changing existing production output.
- AArch64 and LoongArch64 now calculate their physical LP64 frame facts once
  in `MinicMachineLP64FrameContract`. Both existing target emitters borrow the
  same immutable contract through `DirectLP64BlockContext`, with no shadow
  copies of frame size, alignment, varargs, save slots or dynamic-stack state.
  ABI planning and ISA register decisions are unchanged. The complete
  instruction selectors still need conversion to MachineBuilder; these
  ownership changes are not a claim of production MIR migration.

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

### Foundation closure in the current refactor branch

- Machine operands and implicit register effects are exact-count owned arrays,
  not per-instruction reservations for 16 operands and 64 clobbers. Temporary
  selector operands are deep-copied; target symbol/address payloads retain
  relocation identity through validation and serialization.
- The whole-function verifier requires selected-target callbacks for the
  generated ISA feature set, function ABI, physical register legality, frame
  bases and offsets, symbol/address relocations, opcode operand shape,
  immediate/encoding limits and exact implicit register effects. A selected
  instruction cannot self-assert a weaker contract.
- One Core block may lower to multiple `machine_auxiliary` basic blocks.
  Address-taken `BLOCK_ADDRESS` operands are distinct from branch `BLOCK`
  CFG successors. Fallthrough requires the immediately following block and
  a terminator contract explicitly permitting it.
- The shared builder exposes typed selected machine instructions, frame
  indices, machine-only blocks and source-provenance inline ASM. The GNU
  writer first verifies the *complete* machine function and creates all local
  labels, then dispatches only selected target opcodes and independently
  approved source-ASM nodes. Unimplemented source-ASM translation fails closed.
- ISA-owned tables/printers now exist for RV32I, RV64I, x86-64 GPR/FLAGS,
  AArch64 GPR and LoongArch64 GPR, including their base calls, branches,
  memory, frame, ABI and relocation contracts. The RV32/RV64 selected
  opcode probes exercise 68/85 forms respectively. The x86 GNU assembler
  probe exercises real PC32, GOTPCREL and PLT32 relocations.

For RV32/RV64 production selection, the existing far-branch policy must
remain intact: `J_BLOCK`/`JAL_BLOCK` describe actual limited-range PC-relative
machine jumps, and are **not** equivalent to the existing unbounded
`lla` plus `jalr` expansion used for large Linux/Lua functions. That
expansion needs selected address materialization and an explicit indirect
machine CFG edge before the verifier, not a hidden serializer rewrite.

These are **production prerequisites, not completed target migrations**.
Prologue/epilogue, all emitted instruction families, runtime helper lowering,
Linux inline ASM and target-specific outlined machine regions still need to
be selected into one owned `MachineFunction` per function, with no old direct
Core-to-text instruction path after conversion.

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
