# MiniAS — retained MCInst write boundary (2026-11-02)

The previous PreLayout/PostEmit and Symbol projection migration hid Common's
mutable Section/Symbol records but left one important loophole: every Target
`measure_mcinst`, `plan_instruction_relaxation` and `encode` callback accepted
`const MiniAsStmt *`. A `const` statement still contains a **mutable pointer**
to `MiniAsMCInst`, whose `operands[]` in turn contain `void *memory`, mutable
bound symbol refs and `void *target_payload`. A Target could accidentally
change parse-retained instruction semantics while sizing or emitting.

This batch moves all four Targets to a single phase-scoped instruction
projection, while leaving the one existing parse-time MCInst graph intact:

| Stage | Ownership/permission | Representation |
| --- | --- | --- |
| Parse | Construct retained machine operands and Target payloads | `MiniAsParseInstruction` with writable output `MiniAsMCInst *`, published into Common's private Fragment after return |
| Measure | Read current instruction, report byte count | `const MiniAsInstructionView *` |
| MCLayout/relax | Read the same retained operands and current layout generation, request a monotone plan | `const MiniAsInstructionView *` and opaque `MiniAsRelaxContext` |
| Emission/encode | Read final operands and placement, emit only through the Common transaction | `const MiniAsInstructionView *` and opaque `MiniAsEncodeContext` |

One `minias_project_instruction` in `mc_operand_semantics.c` creates the
non-owning view at the three Common dispatch sites. The caller supplies
stack storage for exactly the active operand count, checked against the
retained allocation capacity **before** reserving stack space. The projector
copies instruction/operand scalars, preserves const-borrowed SymbolRef,
SymbolDifferenceRef, ISA memory and Target lowering-plan identities, and
initializes only the active operand union arm. The instruction view has no
destructor, allocation capacity or writable pointer; its `op`/`args` are
also `const char *`. Views are rebuilt after source replay and at each
layout/encoding phase; they are not cached across generation changes.

Targets still own semantic implementations: RISC-V compressed-word sizing,
RV branch alternative planning and LI lowering, x86 code16/32 prefix and
branch plans, AArch64 scalar memory and branch payloads, LoongArch vector
operand fields and private multiword lowering all read the const projection.
The x86 source-selected prefixes use a **copy of the read-only view** to
adjust their logical offset without mutating the retained Fragment.
No ELF record/fixup representation changes; there is no alternate MCInst
heap graph or second machine-encoding parser.

## Validation and remaining limits

- Five Target translation units compile with strict `-Werror` and the
  read-phase callback types. A new negative compiler probe rejects writes to
  instruction size/opcode/operand, a bound SymbolRef, ISA memory and Target
  plan, access to lifetime destructors, and retrieving a mutable MCInst
  pointer. It is part of the required Target contract workflow.
- A separate executable unit contract exercises the real projector's scalar
  snapshot, const-borrowed nested identities, explicit union tag, live
  re-projection, capacity failure and zero-operand case.
- ASan/UBSan exposed an old x86 encoding-plan read of two operands *before*
  checking the opcode or semantic operand count. The previous MCInst always
  reserved three slots, so an absent operand looked like an initialized
  register zero. The shared x86 plan now reads only present operands, with
  zero as the old fallback for no-operand forms. Address/undefined sanitizer
  target-lowering, four-target bound SymbolRef, AArch64 mapping, x86 code16
  (38 cases), and x86/RV SourceGraph replay checks pass locally with leak
  detection disabled because this runner cannot support LeakSanitizer ptrace.
- Existing RISC-V A0 and four-target GNU/link/SourceGraph/ELF tests remain
  mandatory. The local independent old/new fixture harness covers all four
  ISAs, including matching rejections; exact-head GitHub results must be
  checked *after* pushing this commit.
- GNU opt-in 3,536 cases and Linux/QEMU P1 are **not** claimed by this batch.
- **Subsequent P04 boundary update:** all four Target parsers now receive
  only the parse-stage request, not a writable `MiniAsStmt`; site capture
  and executable ALIGN receive still narrower value projections. The
  canonical `MiniAsStmt` and its Fragment payload definitions are now
  Common/SourceGraph-private, with an independent negative compiler CI
  guard. See [Target/Fragment private boundary](target-fragment-private-2026-11-02.md).
