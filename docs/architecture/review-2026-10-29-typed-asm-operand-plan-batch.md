# MiniC GNU inline ASM typed operand-plan batch — 2026-10-29

This batch consolidates the **Source → Core handoff**, not all ISA machine
emission. It preserves the existing target-qualified Linux machine forms,
generic Core register allocation, GNU asm-goto relocations and fail-closed
source admission. No target emitter was edited.

## Why `structured_roles` appeared with 9,559 / 9,559 `-S` successes

The same prior exact-head census (reported as `1d8b34f`, summary job
`123522553333`) printed six top `THREE_LINUX_ASM_FRONTIER
reason=structured_roles` source-form groups with **539 / 269 / 263 / 259 /
252 / 216** translation owners, while all three Linux target matrices had
**4078 / 4078, 2723 / 2723, 2758 / 2758** `.i → MiniC -S` successes.

These are not contradictory:

1. `core_lower_asm.c:core_asm_trace_status()` prints that frontier only for
   `status != MINIC_CORE_LOWER_OK`; the name is a real **failed Core attempt**
   at that point, not a count of successful instruction classifications.
2. `compiler.c` executes `minic_prepare_core_function_set()` before
   `minic_prune_inline_specializations_from_core()`. An originally referenced
   inline source/clone can therefore fail in the speculative Core set, print
   an ASM frontier and subsequently become **unreachable** after specialized
   Core call-graph pruning.
3. `minic_validate_core_functions()` checks only finally reachable defined
   functions, explicitly skipping internal `!is_referenced`. The frozen GNU
   seven-shard script records raw `asm_frontiers` from **all stderr** but
   counts `.i` as PASS only with exit 0 and nonempty `.s`; it also rejects
   a successful `.i` with any final `MINIC_CORE_FUNCTION_FAILURE`.
4. The summary script groups form fingerprints across receipts without a
   post-prune liveness join. Thus its `owners` cardinalities are counts of
   **files where a Core attempt emitted that diagnostic**, not of live
   machine emissions or proven missed Linux compilation owners.

This explains the coexistence while preserving the frontier as useful
architecture evidence. It does **not** establish that each of those six
forms is dead; a join of original function ID, final reachability and
`source_form_id` is needed to classify individual owners. The 9,559/9,559
figure belongs to that older exact-head, not this unpushed batch.

## Single typed plan, two existing selectors

| Stage | New canonical information | What remains target-owned |
| --- | --- | --- |
| GNU frontend AST | Original text, constraints, names, access, clobbers, C label targets | No target opcode emission |
| `MinicAsmOperandPlan` in `backend.h` | Typed owner (`TARGET_QUALIFIED` / `GENERIC_STRUCTURED`), stable source-form fingerprint, role kinds, outputs, `&`, `+`, numeric/named matching outputs, explicit/implicit/effective memory | Exact target qualifier still decides admissibility, ISA-specific role transport and side effects |
| Core lowering in `core_lower_asm_operands.c` | One `core_asm_publish_operand_plan()` writes all typed attributes and provenance to Core IR operands and `MinicCoreInlineAsm` | No physical registers or ISA strings in Core |
| Machine emission | Reads existing Core structured effect, source ID and target frontend form; new typed summary is available | All present x86/AArch/Loong/RV emitters, register assignment, alternative sections and relocations remain unchanged |

The former `MinicTargetInlineAsmContract` name remains a typedef for the
single `MinicAsmOperandPlan`. This allows every existing target qualifier to
continue filling only its established `operand_count/kinds[]/implicit_memory`
fields; the shared backend layer fills **all original GNU source facts** once.
The Core generic structured handler fills role kinds and calls the *same*
source-capture helper. Both paths publish via one Core function.

`source_form_id` is a stable 64-bit fingerprint of complete ordered source
template, access/constraint strings, clobbers and source flags. It is **not**
an ISA machine opcode ID and does not allow an emitter to skip exact source
or register-assignment validation. The previously separate Core ASM frontier
signature hashing was removed in favor of the same canonical computation;
the original template-only hash remains for diagnostic grouping.

The Core verifier now checks summary owner, output boundary, tied output
indices, GNU `&`/`+` on source outputs only, and explicit/implicit memory
effects against conservative Core ordering. Legacy manually constructed
Core ASM with `owner=NONE` retains its existing verification contract.

## What was intentionally retained

- Existing seven-plus ordered Core probes: immediate-only CSR `rK`, empty
  template/barrier, scalar output specializations, memory read/write and
  symbolic asm-goto do **not** all share the same Core instruction shape.
  Blindly replacing legacy `REGISTER_OUTPUT_INLINE_ASM` with structured ASM
  would change live x86/Loong/RV behavior.
- Target `requires_qualified_input_asm` negative ISA guards still prevent a
  native instruction rejected by target admission from entering generic Core
  as a lossy `r`/`i` template.
- The old `early_clobber` allocator boolean remains distinct from source
  `gnu_early_clobber`: target-qualified `=&r` with record-backed `MEMORY_OUTPUT`
  cannot be treated as a generic Core scalar register output.
- Explicit GNU `"memory"` and implicit machine memory side effects are
  separately retained. If both occur, neither is discarded.
- Native emitters still validate source templates, ISA-specific aliasing,
  GNU flags, custom object sections and relocation shapes; no output
  correctness claim is inferred from a fingerprint match alone.

## Evidence from this local working tree

```sh
make -j8 BUILD_DIR=build/asm-plan CFLAGS=-Werror all
make -j8 BUILD_DIR=build/asm-plan CFLAGS=-Werror check-fast
bash tests/core/run-asm-operand-plan.sh build/asm-plan/bin/minic-cc
MINIC_CC=build/asm-plan/bin/minic-cc \
    sh tests/compiler/c0/probe-aarch64-linux-hyp-spinlock-lse.sh
MINIC_SOURCE_ONLY=1 bash tests/compiler/c0/probe-loong-linux-small-xchg-qemu.sh \
    build/asm-plan/bin/minic-cc
```

Observed **PASS**: strict full build, `check-fast`, typed source plan
production-function unit (two numeric/named tied operands, early and both
memory-effect origins), five MiniC targets `-S` + unsupported RV32 fixed
early-clobber fail-closed, original GNU/MiniC x86 Linux futex native
exit **42 / 42**, 9 exception sites and negative ISA mutation rejection,
AArch64 hyp-spinlock 5-ISA gate, Loong small-xchg 15 negative GNU forms,
and stable `CORE_ASM_FRONTIER` signature/fail-closed test.

No GNU cross-assembler or QEMU execution for AArch/Loong was available in
this local environment. There is no claim of a new exact-head 21/21 Linux
matrix, complete ELF Image link, or native 12/12 group.

## Follow-up priorities and deletion gates

1. **P0 — qualify source ownership as a tri-state.** Today a bool target
   reject is partly caught by reserved machine prefix lists. Add typed
   `UNOWNED / QUALIFIED / OWNED_INVALID` at the Target descriptor, preserving
   every negative GNU mutation case before deleting prefix guards.
2. **P0 — physical early/tied exclusion.** Source `&` and tied indices now
   cross Core intact. A verified machine operand allocator must consume them
   and prove distinct register allocations for every target form; it is
   currently not proven globally. Existing form-specific fixed allocation
   and rejection remain authoritative.
3. **P1 — form-id machine dispatch.** Gradually attach a target-defined
   machine form enum to the typed plan and remove duplicated source/emit
   ordered classifier chains *per proven family*. Source fingerprint alone
   is insufficient. Start with Loong small-xchg/futex and AArch cmpwait,
   then x86 simpler forms; handle alternatives, `__ex_table`, asm-goto and
   LSE CASP only with object-relocation differential evidence.
4. **P1 — legacy Core scalar shapes.** Convert one effect shape at a time
   only when `REGISTER_OUTPUT_INLINE_ASM`/`OPAQUE` alias, Store sequencing,
   rK early constant specialization, empty barriers and memory scheduling
   have equivalent Core and native tests. Then delete the redundant branch,
   not merely wrap it with a new helper.
5. **P1 — target policy leakage.** `target_info.c` still contains an ISA
   switch for source register/clobber constraints, the AArch `Q` class and
   immediate restrictions. Root owns this separate interface migration;
   its source-policy callback should feed the same typed plan without
   teaching Core the ISA list.
