# MiniC AArch64 backend architecture audit — 2026-10-29

Scope: `src/target/aarch64/` contains **24 C/H files, 11,080 physical lines**.
This is an AArch64-owned audit, not a whole-compiler sign-off. The existing
LP64/Common contract and runtime behavior must be re-verified after each batch.

## Source-reading ledger

`Full` means the full file was inspected in this review, including the
Linux-specific source-side qualifier and target-side machine-emission bodies.

| File | Read status | Main architectural concern |
| --- | --- | --- |
| `abi.c` | Full | Private AAPCS64 classification ownership |
| `abi.h` | Full | Contract must stay target ABI-specific |
| `asm_contract.c` | Full | 7,404-line catalog couples source GNU constraint admission, exact Linux templates, opcode selection, machine output; hundreds of raw SP accesses |
| `asm_contract.h` | Full | Narrow exported source/machine contract |
| `asm_legalize.c` | Full | Active whole-output `fgets`/`sscanf` textual repair, implicit x17 scratch |
| `asm_legalize.h` | Full | Legacy repair hooks remain public to codegen |
| `block_emit.c` | Full | Previously mixed raw SP, x19 and frame locations in terminators/records |
| `block_emit.h` | Full | Historic 12-argument terminator signature exposed backend state twice |
| `call_emit.c` | Full | Incoming vs outgoing SP interpretation and record ABI |
| `call_emit.h` | Full | ABI emission contract |
| `codegen.c` | Full | Frame construction, temporary ASM file, active final textual legalizers |
| `codegen.h` | Full | Target entry points |
| `fp_emit.c` | Full | Existing typed frame helper duplicated in memory |
| `fp_emit.h` | Full | FP emitter interface |
| `frame_emit.c` | Full | Typed owned/incoming/outgoing/physical-SP regions and scratch policy |
| `frame_emit.h` | Full | Region-tagged `FrameRef` instead of ambiguous offsets |
| `integer_emit.c` | Full | Many raw `[sp,#]` SSA references still requiring final repair |
| `integer_emit.h` | Full | Integer emitter interface |
| `memory_emit.c` | Full | Duplicated private typed access helper |
| `memory_emit.h` | Full | Memory emitter interface |
| `residual_emit.c` | Full | GNU ASM-adjacent, variadic, and unusual Core cases with raw stack output |
| `residual_emit.h` | Full | Residual lowering contract |
| `scalar_normalize.c` | Full | Target-width/sign normalization and C integer semantics |
| `scalar_normalize.h` | Full | Normalization interface |

**Confirmed full-file read: 24/24.** The long ASM catalog was completed in
successive bounded line ranges, including both central dispatchers, all
privileged SYS register catalogs, `__ex_table` fixups, KVM alternatives,
futex/exclusive atomics, MMIO, cache, exception and VHE specializations.
This certifies code *reading coverage*, not every contract's runtime validity.

## Prioritized findings and ownership boundaries

1. **P0 architecture: final textual rewriting remains on the active path.**
   `codegen.c` invokes `minic_aarch64_copy_dynamic_frame_asm` and
   `minic_aarch64_legalize_large_sp_memory` from `asm_legalize.c`. Those scan
   completed assembly as strings, parse printed instructions and rewrite SP
   references, using implicit x17 scratch. This postpones stack/frame meaning
   until after machine emission and can invalidate live-register assumptions.
   **Do not delete yet**: its live clients include raw offsets emitted by
   `integer_emit.c`, `call_emit.c`, `residual_emit.c`, `asm_contract.c`, and
   portions of `codegen.c`.
   Verified call path: `codegen.c` places the dynamic copy callback into
   `MinicDirectLP64FunctionEmitter`; the Common function emitter creates a
   `tmpfile()` *for every function containing dynamic alloca*, then copies
   through that callback. The AArch program config supplies the large-SP
   finalizer; Common's `direct_finalize` applies it to the *whole output
   assembly file*. Both are active, not merely abandoned helpers.
2. **P1 architecture: frame access is mixed across machine families.** The
   original typed `FrameRef` covered mostly FP and memory, while integer,
   records, parameters, returns, branches, calls, structured ASM and residual
   operations printed `[sp,#]`. New migration in this batch covers the
   ordinary block/return/parameter and owned ABI subset; integer, outgoing
   call marshalling (genuine changing SP), residual, structured ASM and some
   function prologue remain an audited follow-up.
3. **P1 maintainability: 7,404-line `asm_contract.c` is two catalogs merged.**
   Source-side exact GNU/Linux template qualification and backend machine
   instruction encoding have legitimate one-to-one constraints, but the giant
   pair of dispatchers are lengthy, repetitive and difficult to audit for
   mismatched constraints/types/implicit effects. Split by instruction
   *family* with shared normalized typed descriptors, preserving original
   fail-closed admission and exact `.altinstructions`/`__ex_table` semantics.
   Blind splitting or a generic GNU template fallback would be unsafe.
4. **P1 correctness risk: legacy stack offsets are not region typed.**
   Raw `sp` represents stable local slots, entry argument slots or changing
   outgoing stack at different times; some paths manually add frame size,
   some save entry SP, some use x19. Compile-time `[sp,#]` string legalizing
   does not itself prove incoming address or dynamic-frame lifetime correct.
5. **P2 code hygiene: repeated frame access and ABI carry-over comments.**
   Duplicated FP/memory owned-slot wrappers were unified in this batch. The
   giant ASM catalog also contains repeated exact-template normalization and
   handwritten spill-slot offset arithmetic; replace those with frame APIs
   when a stable scratch/register-liveness contract is explicitly checked.

### Frame address regions and validation rule

| Region | Reference base | Why it must stay distinct |
| --- | --- | --- |
| Owned SSA/object | x19 for dynamic frame, else SP + outgoing delta | Stable callee-owned slots |
| Incoming stack arguments | Entry SP + 16 + argument offset (or saved entry SP) | Cannot substitute current outgoing SP |
| Outgoing arguments | Current SP | Caller adjusts SP around call |
| Physical SP | Current SP | Alloca and target-specific side effects |
| ASM operand spill | Owned frame with dedicated role | Explicit source contract, not textual repair |

## Applied first migration batch

- `frame_emit`: exported one typed owned-slot and owned-address entry point.
- `fp_emit`, `memory_emit`: removed duplicate owned-slot logic.
- `block_emit`: switched record addresses/copies, incoming record chunks,
  returned GP/HFA/indirect record addresses, scalar/wide returns, branch
  sources, saved callee state and SSA result writeback to typed references;
  reduced the terminator's large positional signature to its existing context.
- `call_emit`: switched incoming scalar/FP/wide arguments, return spill slots,
  and record return chunks to typed frame references. Preserved raw *outgoing*
  stack call marshalling pending a separate lifetime-aware migration.
- Unit and production smoke tests expanded for large owned displacement,
  dynamic x19, incoming frame and indirect record return paths.

**Validation at integration boundary:**

```sh
make -j8 CFLAGS=-Werror build/debug/bin/minic-cc
bash tests/compiler/c0/probe-aarch64-typed-frame-path.sh build/debug/bin/minic-cc
git diff --check
```

All above passed locally. Probe results:
`AARCH64_TYPED_FRAME_REF=PASS`,
`AARCH64_TYPED_FRAME_PRODUCTION=PASS`.
Assembly emission for `dynamic_stack_alloca.c` and the preprocessed
`aarch64_indirect_record_return_minic.c` passed. No AArch64 GNU cross-assembler
or QEMU executable was installed locally, so target machine execution and
independent GNU syntax checks remain CI gates, **not achieved results**.

## Next whole-family batches

1. Build a finer source/machine admission test matrix for `asm_contract.c`'s
   fragile privileged hardware effects and keep its exact fail-closed gate.
2. Switch integer and residual *owned* slots to typed frame refs, handling
   scratch lifetimes and the large incoming/dynamic SP path explicitly.
3. Model outgoing call argument and transient SP delta in the frame view,
   then retire each confirmed-unused text legalizer individually.
4. Expand exact-head AArch GNU assembler and QEMU runtime checks for dynamic
   alloca, large frames, record return, variadics, C ABI and Linux ASM.

No compiler/runtime `100%` claim follows from this architecture-only batch.
