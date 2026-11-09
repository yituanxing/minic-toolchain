# RISC-V Machine IR: branch-range and source-ASM handoff

This is an implementation boundary and migration risk note, not a claim that
the RV32/RV64 production emitters have migrated to Machine IR.

## Selected branches cannot assume arbitrary Core CFG distance

- RV32I/RV64I B-type conditional branches reach approximately ±4 KiB. JAL
  reaches approximately ±1 MiB. GNU `j`/`jal` are **not** safe generic
  replacements for the existing production far-edge sequence.
- Without a physical instruction-size/layout pass, the independent RISC-V
  Machine IR verifier accepts a direct branch only to its next block or to
  the block immediately after a small, source-ASM-free intermediate block.
  All other direct edges fail closed before serialization.
- Existing production code uses `lla t0, .L...` followed by
  `jalr zero, t0, 0`; the Machine IR representation is now **two explicit
  selected instructions**: `LA_BLOCK(x5, BLOCK_ADDRESS)` and
  `JALR_BLOCK(x5, BLOCK)` with an exact CFG successor. The target verifier
  proves the immediately preceding address materialization names the same
  block and register as the jump. No serializer-hidden far-branch pseudo.
- Production conditional edges require nearby machine-auxiliary skip/arm
  blocks plus typed long jumps for both distant arms; the direct Core block
  numbers must not be wired directly into short conditional opcodes.

## Source-owned inline ASM is not compiler-selected code

- The selected I-subset still has fixed target opcode identities and exact
  physical operands. Source-owned `SOURCE_ASM` is separately admitted only
  with Core provenance, typed physical operands, verified constraints and
  independent SourceASM/GNU callbacks.
- The initial machine serializer supports named and numeric bindings, the
  GNU `%l[name]` goto substitution, escaped percent, `%n` integer negation,
  `%z` zero spelling, and address-register `A` bindings. Memory constraints
  require the corresponding read/write side effects.
- GCC source constraints with ties (`0`, `[name]`), alternative constraints,
  and `%=` unique TU-local numeric ASM instances are not yet modeled with
  trustworthy machine-level provenance. Those forms **must fail closed**;
  do not synthesize a uniqueness hash or quietly drop a clobber. Source
  instructions using M/A/F/D/CSR remain separate from the compiler's I-only
  generated-opcode policy and rely on source-target qualification.

## Production migration remaining

The independent machine policy tests prove the typed instruction model, but
`riscv64/core_codegen.c` and `riscv32/rv32_emit.c` still directly emit GNU
assembly through their extensive frame, call, scalar, wide-integer, floating,
and inline-ASM helpers. A correct production migration must change the
`FILE*`-owned helper call graph to selected MachineBuilder operations in one
coherent architecture class, retain the original frame/ABI legalization,
build all auxiliary CFG blocks, verify the whole function, and then serialize.
Do not pass captured formatted `.s` through a text parser as a pretend MIR.
