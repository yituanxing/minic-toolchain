# RV32 backend / ILP32 ownership refactor — 2026-10-24

Baseline: `7a9cdd16c4061e917bc87bd91290991234d8c8d7`.

Architectural boundary review: MiniC Core/Target contract, backend registry,
all five target emission entries, RV32 ILP32 ABI, verified shared Core
call resolver, RV64 LP64 ABI/dynamic frames, RISC-V shared register
placement/opcodes, global aggregate copy, Makefile objects, native
architecture invariants, GNU ELF32/QEMU and fourarch real-program tests.
This review audits dependencies and ownership, not every repository line.

## Before

`src/target/riscv64/rv32_emit.c` was an ~800-line mixture of ILP32
frame sizing, scalar conversions, direct/indirect call argument transport,
Core instruction selection, CFG/function traversal and program output.
Other RV32-only files and the genuinely shared RISC-V record copy
also lived under the misleading RV64 subtree.

## After

```
Front End -> Core IR -> target/backend.c
                          |
          +---------------+--------------+
          |                              |
   riscv32/ (ILP32)               riscv64/ (LP64[D])
      rv32_frame.c                  frame_layout.c
      rv32_scalar.c                 scalar_emit.c
      rv32_call.c                   call_emit.c
      rv32_emit.c                   core_codegen.c
          |                              |
          +---------- riscv/ ------------+
                 instruction_select.h
                 gpr_abi_cursor.h
                 record_copy_bytes.c/h
```

RV32 frame owns outgoing-call stack area, objects, scalar slots and
stack byte addressing. RV32 scalar owns type admission and narrow
canonicalization. RV32 call owns one validated CoreCallView, shared
outgoing argument marshalling and both direct/indirect call-result
transport, eliminating duplicate result-transfer and independent call
handling branches. RV32 emit owns the Core instruction switch,
terminators, function/CFG traversal and program emission.

No new C semantics, machine IR, ABI assumptions, float algorithms or
assembler/linker changes. RV64 LP64 frame still has its own proven
Linux-relevant lifetime/stack mechanisms. Whole-branch invariants
reject the old RV32 namespace; CI target watch includes riscv32 and
riscv common. Real RV32 GNU/MiniAS ELF32 QEMU/GCC, original AES and
cJSON frontier, and fourarch runtime tests qualify the move.
