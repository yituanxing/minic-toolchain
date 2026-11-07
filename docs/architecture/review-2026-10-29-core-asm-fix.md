# MiniC Core GNU inline-ASM ownership: source review and fix ledger

Date: 2026-10-29. Branch starting point: `refactor/minic-target-language-boundary` at `3866af2f6`.
Scope is **MiniC** only; this record is not a Linux link/QEMU certificate.

## Source files actually read

The following six Core ASM translation units and private header were read
fully, in file order, including normalization, ordered admission, all legacy
scalar effect paths and the target-qualified path (4,292 lines *after* the
fixes recorded below). These are full-source reviews, not merely file-name
searches. The source digests describe the reviewed working-tree contents;
new edits to a file require a delta review.

| File | Reviewed | Working-tree SHA-256 after fixes |
| --- | --- | --- |
| `src/core/core_lower_asm.c` | full, 356 lines | `da66629bfff1bf09fdc65d89cfccf9d9e7b00d587120f7916bb03b60a18ffbc0` |
| `src/core/core_lower_asm_target.c` | full, 235 lines | `676e6e92df6777fd11b4ddcdf92315ba7e0bec6d877a2c5322b7ea1d83837662` |
| `src/core/core_lower_asm_operands.c` | full, 1,576 lines | `c41d5ebc0960657708476e09bfc718fe0712749eb201e5f7c151660f4c34a627` |
| `src/core/core_lower_asm_structured.c` | full, 828 lines | `b06e39a5932c1c6afffb677195a42fe91608586edc535df29eef1adcf0029492` |
| `src/core/core_lower_asm_scalar_forms.c` | full, 1,201 lines | `a4b61851d78cdeefd27f1634f964f0b2458812e0549389bdfc1efa6de2f44725` |
| `src/core/core_lower_asm_internal.h` | full, 96 lines | `7617f57bea94d97b936b4735d878588621b0b75fdd99122b2a9a906a9ada2fcc` |

Related `core_ir.h`, `core_ir.c`, `core_ir_verify.c`, `target/backend.c`,
RV64 machine ASM and AArch64/LoongArch target contract call sites were read
at relevant interfaces only. **They are partial, not full source reviews**
and belong to the other source-review owners' full ledger.

## Findings by priority

| Priority | Finding and evidence | Status / architectural disposition |
| --- | --- | --- |
| P0 | `core_lower_asm_target.c` allocates structured roles without copying source GNU `&` early-clobber into the `MinicCoreStructuredInlineAsmOperand`. `core_lower_asm_structured.c` does set it. AArch64 and LoongArch machine contracts often validate source `=&r` but then require the Core flag to remain false, so the source/IR meaning is split. | **OPEN**. Changing the flag alone breaks existing target contracts. Plan one source operand/effect policy with target-normalized overlap constraints and update all affected target consumers before removing legacy behavior. |
| P0 | `core_lower_asm.c` uses first-claim-wins in seven ordered probes, while `core_lower_asm_structured.c`, `core_lower_asm_scalar_forms.c`, and `core_lower_asm_target.c` have different operand materialization and source-provenance rules. The target path rematerializes block-local `OBJECT_ADDRESS` after CFG-moving operands; the generic structured path does not. | **OPEN**. Build a shared verified operand/effect plan with transaction-preflight. Preserve target-specific assembly qualification and the proven legacy IR users; do not globally reorder probes or delete scalar emitters until every target consumer has migrated. |
| P1 | `core_lower_asm_operands.c` asm-goto immediate text baker matched `%0` inside escaped `%%0`. This changed a GNU literal `%0` into a constant and broke source semantics. | **FIXED locally**. Scan `%%` as one escaped token in both sizing and writing passes; add positive `.i -> .s` fixture checking literal `%0` and live `%0=33` together. |
| P1 | `core_lower_asm_operands.c` symbolic indexed-global address evaluated `INT64_MAX / element_size` without excluding GNU zero-sized record elements. A valid GNU empty struct array could reach divide-by-zero in diagnostic lowering. | **FIXED locally**. Treat zero stride as an unsupported relocatable indexed symbolic offset; negative GNU `.i` fixture verifies clean rejection without compiler crash. |
| P1 | `core_inline_asm_numeric_template` did not independently guard `SIZE_MAX` template length or absent nonzero-count operand arrays. | **FIXED locally**. Fail closed before length+1 allocation or named operand traversal; this does not expand hardware admission. |
| P1 | `core_lower_asm_structured.c` `rK` immediate register-output fast path stored `source->is_volatile` in Core, but Core's opaque effect constructor requires the flag true. A legal GNU **nonvolatile** output-bearing ASM was rejected before the later correct value-producing handler could claim it. | **FIXED locally**. Store the output producer as a retained Core effect (`true`), like the existing later specialization path; new authentic `addi` GNU `.i -> .s` fixture failed before the fix and passes after. |
| P1 | `core_lower_asm_structured.c` CSR `rK` fast path rewinds a seven/eight-instruction assignment tail by shape and global ID. It is tightly coupled to incidental Core materialization and contains extensive trace-only handling. | **OPEN**. Replace with an explicit source-local constant proof and dead-store plan after a differential check; do not delete now because RV64 uses this optimization. |
| P2 | `core_lower_asm_scalar_forms.c` retains four historical specialized Core opcodes, even though generic role forms cover many volatile shapes. Target machine emitters still consume them; they are not established dead code. | **RETAIN** pending cross-target consumer inventory. Remove only after replacing native emitters and proving exact GNU fallback semantics. |

## What the local change proves

- `make -j8 build/debug/bin/minic-cc CFLAGS=-Werror`: PASS after concurrent AArch64 edit stabilized.
- `bash tests/compiler/c0/probe-core-asm-goto-percent-escape.sh`: PASS (`literal %%0` preserved; true `%0` becomes 33).
- `bash tests/compiler/c0/probe-core-asm-zero-stride.sh`: PASS (invalid GNU zero-stride symbolic operand rejected without crash).
- `bash tests/compiler/c0/probe-core-asm-nonvolatile-rk.sh`: PASS (previously failed: `status=2`, Core lowering error).
- `bash tests/compiler/c0/probe-core-asm-empty-rk.sh`: PASS (existing zero-byte input barrier admission preserved).
- `sh tests/compiler/c0/run-gnu-inline-asm-goto.sh`: PASS (historical RV64 label + immediate functionality).
- `bash tests/compiler/c0/probe-linux-core-asm-unclaimed-census.sh build/debug/bin/minic`: PASS (LoongArch reserved ISA fail-closed census).
- `bash tests/compiler/c0/check-minic-target-boundaries.sh`: PASS (review inventory reported stale digests from concurrent edits; no invalid/removed source files).

The current GitHub full 21-shard Linux census predates these uncommitted
changes. **No exact-head seven-shard run has been requested for this batch.**

## Next coherent structural migration

1. Define an immutable Common `AsmOperandEffectPlan` that records GNU
   access/memory/clobber/fixed-register/early-clobber/source-id before any
   Core value or machine object is created. Target qualifiers contribute
   native ISA constraint identity, not their own duplicate Core lowering.
2. Have both target-qualified and generic paths commit the exact same
   normalized Core effect under one preflight transaction. Migrate target
   emitters that currently rely on missing `early_clobber`; retain native
   early-clobber non-aliasing and ISA-specific fail-closed contracts.
3. Delete each now-unreachable specialized handler and machine consumer in
   one reviewed semantic batch. Verify GNU `asm goto`, `+r`/`=&r`, memory
   effects, register clobbers, symbolic `i` and `rK`, and all five targets.

This is a proposed next batch, **not** a claim that P0 is solved.
