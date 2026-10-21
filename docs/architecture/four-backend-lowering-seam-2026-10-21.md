# Four-backend lowering seam and deferred Machine IR (2026-10-21)

## Decision

Establish a single Core-to-target driver seam now, **without manufacturing
Machine IR (MIR) before its first real consumer**. This change is intended to
make a later MIR addition local to the backend pipeline, not a fourth
reorganization of the frontend or target selection. The existing four emitters
remain authoritative for assembly, ABI, register choices and frame placement.

This refactor is based on four real targets and the AArch64/LoongArch64 original
BusyBox 581/581 GNU/QEMU acceptance. It does not by itself improve generated
code quality, add a register allocator or retest every workload.

## Ownership and data flow

```text
parse -> C semantics / TargetInfo -> verified Core CFG
                                       |
                                backend selection
                                       |
                            optional legacy C0 layout
                                       |
                    +------------------v-----------------+
                    | backend emit seam (backend.c)       |
                    | Today: Core -> direct GNU assembly   |
                    | Later: Core -> target legalization   |
                    |        -> Machine IR -> machine pass |
                    |        -> instruction emitter        |
                    +------------------+-----------------+
                                       |
                                   GNU AS/LD
```

1. **Core owns semantics:** typed SSA values, CFG, memory effects,
   volatile/atomic semantics, source-language resolutions. Machine register
   allocation and stack offsets must not be written into Core nodes.
2. **TargetInfo/DataLayout own target C semantics:** integer widths, record
   layout and compile-time target capability queries.
3. **Backend owns target lowering:** ABI, legalize/expand, register constraints,
   stack-frame planning, instruction selection and assembly or object emission.
   `MinicBackend.emit_program` is the current one-step implementation of this
   boundary. The compiler driver never switches on an ISA for codegen.
4. **Shared global emit** owns target-independent initializer/aggregate
   traversal and data layout checks. Relocation rendering remains a target
   callback. Shared narrow LP64 aggregate predicates are isolated from global
   emission, and **do not** imply a shared x86, RV or universal ABI.
5. **RV64 remains a first-class optimized owner:** its existing C0 layout,
   spill reuse, inline-asm callee-save logic and frame decisions remain intact.
   New targets are not forced to copy them or replace them with naive slots.

## Why no MIR implementation yet?

Machine IR needs a representation contract and pass consumers. Its natural
first consumers are a real physical-register allocator with virtual-register
liveness and spill/reload, post-selection copy elimination, or machine-level
instruction scheduling. Introducing an instruction container before defining
those requirements would merely mirror Core operations with more state and
create additional translation and validation burden.

MIR must be **target-oriented**, not a second target-neutral Core. A future
first version should support:
- function/basic-block structure and stable CFG successor edges;
- target-specific opcode and operand variants;
- virtual/physical registers with register class and implicit clobbers;
- frame indices before final stack offsets;
- explicit call ABI effects, inline asm, flags, and memory side effects;
- pass verification and valid/unsupported/error distinctions.

Introduce MIR **one target at a time**, retaining the direct path for others.
The stable interface from `compiler.c` stays
`minic_backend_emit_assembly(...)`; backend internals can switch to a
machine-function builder/optimizer/emitter without changing semantic lowering.

## Implementation scope in this PR

- Register four explicit backends in `target/backend.c`; fail closed on unknown
  arch, no accidental default to LoongArch64.
- Route driver layout and emission through `target/backend.h` rather than
  four hard-coded call sites. Keep the previous RV64 layout trace boundaries.
- Compile aggregate-global emission helpers exactly once in
  `target/common/global_emit.c`; expose typed callback contracts and move
  the shared LP64 predicates into `target/common/lp64_aggregate.c`.
- Preserve all target emitter implementations, source-owner selection and
  current GNU-QEMU validation paths unchanged.

## Validation and follow-up

1. Compile MiniC with `make build/debug/bin/minic-cc` or the repository's
   standard build target, checking full `-Wall -Wextra -Wconversion` warnings.
2. Run owner-scoped four-arch CI, then compare BusyBox 581/581 strict checks,
   original SQLite/Lua/TinyCC smoke and runtime byte-diff receipts.
3. Verify target rejection for unsupported/unknown target remains explicit.
4. Before actual MIR migration, measure load/store counts, stack-frame size,
   instruction count, runtime and compile time on representative real functions,
   then choose the first ISA with a tangible machine-level optimization case.
5. First MIR proof should preserve GNU/QEMU functional differences exactly and
   keep the previous direct emitter available until the new path is certified.

Do not introduce a catch-all target DSL, generic physical register allocator or
ABI-equivalence assumption as part of this seam change.

## Second pass: extracted observed two-ISA commonality

The two new emitters had identical scalar type admission and frame object
layout functions, including auxiliary binary128/int128 slots. These are now
independent, compiled-once strategies in `lp64_scalar.c` and `lp64_frame.c`.
They are **not** imposed on mature RV64 or x86. Backend-specific ABI register
assignment, calling sequences and machine instruction legalization remain in
the owning emitters. The ELF global emission pipeline and symbol handling are
also now compiled once, with a replaceable relocation callback. Future MIR
would replace the private machine path, not frontend Core semantics.

## Mature-target audit and isolation: RV64 and x86-64

RV64's existing GNU/global record, bitfield, relocation and file-asm
functions have moved intact from `codegen_function.c` to
`riscv64/global_emit.c`, preserving RV-specific directives, ABI,
layout and optimized stack-slot reuse. The driver and machine emitter
continue calling the same functions via explicit declarations.

x86-64's ~570-line target-owned global emission has moved to
`x86_64/global_emit.c`. In contrast to AArch64/LoongArch64,
its SysV-specific aggregate/weak/section handling remains independent.
The already proven `common/global_emit.c` zero-initializer relocation
walker and ELF symbol-visibility mappings are consumed with an x86
relocation callback; those helpers retain the same offset/size guards.

Four targets now separate binary data/relocation emission from Core
machine instruction lowering. Future target Machine IR should be
introduced in the latter boundary. No universal ABI or common
physical-register allocator is asserted, and RV64's optimized frame
planner remains untouched. Source movement is not a runtime proof:
original GNU/QEMU real-project owner regressions decide acceptance.

## x86 SysV ABI owns classification independently

The codegen-internal SysV scalar storage width, one/two INTEGER-eightbyte
record admission, and incoming GPR/SSE/stack argument cursor are now
compiled once in `x86_64/abi.c` with explicit `abi.h` contracts. The
full classifier code has been moved, not rewritten. Call emission and
instruction selection still use the x86 backend's machine registers;
RV64 keeps its already separate `riscv64/abi.c`.

This directly separates semantic ABI classification from target machine
emission without demanding an immediate Machine IR. No common LP64
classifier is assumed across RV64/x86/AArch64/LoongArch64. Runtime
certification still requires original project GNU differential gates.
