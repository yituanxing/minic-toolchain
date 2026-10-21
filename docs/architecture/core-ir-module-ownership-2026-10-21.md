# Core IR ownership audit (2026-10-21)

This split is based on the complete four-target Core IR implementation after PR #544.
It changes compilation-unit ownership, not the Core representation or any opcode.

- `core_ir.c` owns allocation, object/value/block mutation, instruction
  insertion and the shared admission rules for global symbols and call types.
- `core_ir_verify.c` exclusively owns storage-shape checks, instruction and
  terminator validity, SSA availability, block verification, and the whole
  function verifier. Invalid or unsupported IR still fails closed.
- `core_ir_dump.c` exclusively owns the textual IR dump and continues to
  call `minic_core_function_verify` first. Printed bytes are unchanged.
- `core_ir_internal.h` exports only four narrow common type-admission
  predicates to the Core verifier. These remain target-agnostic and must not
  become an ABI capability or Machine IR registration interface.

All verifier and dump function bodies were moved byte-for-byte. Exactly four
pre-existing helper definitions changed visibility from private to
Core-internal, with namespaced symbols and one declaration per helper in the
internal header. The global-addressable predicate intentionally admits void
linker symbols; it must **not** collide with the distinct source-to-Core
lowering predicate in `core_lower_types.c`.

Validation gate: warnings-as-errors native build, four-ISA focused GNU/QEMU
real workloads, RV64 original Linux/performance coverage, x86 ABI gates,
Core invalid-IR negative tests, and exact Core dump equality. Focused CI
success does **not** certify a full Linux 2064-TU / performance rerun.

Next architecture audit: split source-to-Core lowering by semantic/CFG
ownership, then GNU inline asm operand classification vs materialization;
do not reflexively add Machine IR or a universal target opcode layer.
