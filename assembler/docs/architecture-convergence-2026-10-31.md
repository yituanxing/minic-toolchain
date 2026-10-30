# MiniAS architecture convergence: deletion and ownership ledger (2026-10-31)

## Contract and scope

This ledger tracks a **source-architecture refactor**, not a new Linux
runtime acceptance certificate. Start from `0d1c4d6b` on the only MiniAS
development branch, `refactor/minias-target-isa-abi-core` (Draft PR #667).
The in-flight refactor must retain the same physical GNU input, assembler-only
object provenance, downstream GNU linker and QEMU P0/P1 acceptance rules.

The objective is **one owner for each algorithm and each long-lived object**.
Deleting a duplicate means deleting its algorithm *and* its callers, unused
fields/callbacks and compatibility branch in the same batch. Moving a source
block to a new translation unit is useful boundary work but **does not count
as net code deletion**.

## Target architecture and ownership seams

| Stage | Durable owner | Accepts / produces | Explicitly excluded |
| --- | --- | --- | --- |
| Invocation + registry | `invocation.c`, `target_registry.c` | `TargetSpec`/profile | Target-name switches in CLI or Common |
| Source | SourceGraph + source-expansion layer | physical source, macro frames, stable `SourceSiteID` | section storage, machine encodings, ELF writer |
| Common source semantics | directive/statement lowerer | section/symbol identities, immutable typed fragments | target opcode classification |
| MC + layout | `MCInst`, `MCExpr`, `MCFragment`, `mc_layout.c` | bound `SymbolID`, final addresses, relaxation/fixup requests | reparsing ISA text during emission |
| Target | four separate ISA backends (RV32/64 is one family) | parse typed operands, choose instruction forms, encode bytes + semantic fixups | growing Section storage or emitting ELF objects |
| Object output | `elf_writer.c` + `minielf` | finalized sections, symbols, relocations -> ET_REL | macro expansion and target instruction grammar |

The existing common typed fragment transaction, section indexes and retained
branch plans are strengths and must not be replaced by new parallel paths.
`MiniAs` still exposes an excessively broad mutable session to these phases.
The final ownership boundary should use phase-specific source/MC/target views;
extracting files is only the prerequisite to shrinking that authority.

## Verified deletion decisions (do not infer dead code from line count)

| ID | Current seam / evidence | Decision | Proof required before closure |
| --- | --- | --- | --- |
| S1 | `assembler.c` interleaves ordinary directive processing, SourceGraph stable-site replay and recursive macro/rept/irp source expansion | Separate `source_graph.c` and `source_expansion.c`, retaining stable replay; this moves ~1,950 lines but initially deletes no algorithm | `-Werror`, A0, source-site/conditional/macro GNU stress, same original input |
| R1 | `riscv_encode.c`: scalar bitfield assembly repeated in `rv_plain_machine_word` and final `rv_encode_mcinst` | One scalar-word builder, remove the second R/WORD/I/LOAD/STORE/U construction | RV typed, RVC and reloc probes; cross GNU marked NOT TESTED if unavailable |
| E1 | `mc_expr.c` eager/deferred `>>` differ for a final shift of 63 and negative operands; GNU `((first-second)>>63)+1` has observable data width | Repair the typed deferred evaluator, retain separate relocatable-expression semantics | New exact GNU bytes oracle, unresolved section expression fail-closed |
| M1 | `expression.c:104-308` and `mc_expr.c:123-487,1089-1201` each parse a subset of GNU expression syntax; a further ~400 lines split symbol±dot, pair subtraction and division using source strings | Introduce one token-to-AST grammar, then delete the old *grammars* only when wrapping immediate, absolute and deferred-relocation evaluators consume the AST with their original distinct policies | GNU precedence/shift/overflow/reloc/algebra/forward-symbol/cross-section matrix; avoid a one-mode evaluator |
| M2 | `.fill/.space` initially measure operands and later compile the same source expressions as variable-data ASTs | Compile once at parse time, measure/resolve the retained expression; delete old size-only reparse path | symbolic forward references, NOBITS, negative/large count, branch-dependent layout GNU tests |
| A1 | AArch64 `object_attributes` callback only zeros an object already zero-initialized by the common writer | Delete callback and redundant registration | ELF attributes/object tests and GNU AArch64 probe |
| X1 | x86 `.code16/.code32` allowlist independently mirrors actual supported parser forms | Do **not** yet delete: unify typed feature/mode policy first, then delete allowlist | GNU `code16`/`code32`, invalid operands, current Linux boot object parity |
| T1 | `riscv_encode.c` table describes ISA family/features while alias/operand-shape branches re-identify mnemonics | Move verified family rules into *RISC-V-private* descriptor metadata; delete mirrored matching clauses | family-wide GNU positive/negative oracle and ABI feature gates |
| O1 | ELF SectionKey omits GNU `unique,<u32>`, inherited group flag `?`, and optional non-COMDAT group linkage | Model section *identity* and grouping during Common parse, not as an ELF writer patch | same-name unique 0/1/2, inherited/ordinary groups, sh_entsize, link-order, GNU `readelf`, `ld -r` |
| W1 | Multiple 3×7 YAML entry points share the same scanner and aggregation script | Keep one authoritative runner with explicit corpus/provenance parameters; only retire old workflow after comparison of jobs/artifacts | 21/21 shards, 9,645/9,645 corpus, producer SHA/config/source digest and strict aggregate preserved |

## Deliberate retention (not old duplicates)

- `.if/.elseif` may change fragment topology; bounded full source reparse is a
  correct, necessary fallback until a persistent conditional dependency graph
  can prove local invalidation. Stable source-site identity must survive.
- `expression.c` shares one u64/i64 parser with a shift-mode difference;
  Common absolute and deferred expression evaluators have different symbol,
  arithmetic and relocation contracts. Do not merge by textual similarity.
- x86/AArch64/Loong/RISC-V instruction grammar and ABI relocation families
  **must remain target-private**. Two instructions of equal width are not the
  same encoding algorithm.
- `elf/reader.c`, `relocatable_writer.c`, `rewrite.c` and the assembler ELF
  adapter have separate read/create/rewrite roles, not competing assemblers.
- RISC-V `.option relax` currently rejects unsupported positive relaxation;
  do not accept it silently without R_RISCV_RELAX / ALIGN support.
- Historical public assembler entry points and original frozen GNU replay
  inputs remain until compatibility/retention contracts are explicitly met.

## Next subsystem-scale batches, in order

1. **Source ownership:** complete S1; group source frames, macros,
   conditionals and stable-site state under a private `SourceContext`; remove
   old umbrella fields only after all callers use source APIs. Keep one
   source replay controller, not a second source parser.
2. **ELF section identity:** implement O1 in the parser, SectionKey hash,
   SectionID reuse and ObjectModel; do not make `unique=0` mean absent.
   `?` inherits the *current effective* group; ordinary section groups and
   COMDAT must have distinct ELF group policies.
3. **Target form ownership:** perform X1 and T1 by coherent opcode families,
   not individual mnemonics. Derive legality, measurement and final encoding
   from the same typed plan. Keep RVC, target relocations and mode gates.
4. **Expression ownership:** migrate M1/M2 in one interface contract but
   independently verified grammar/evaluation slices. Parse once, bind final
   SymbolID and affine relocation terms, choose an explicit evaluation mode.
   Preserve `.set`, numeric-label, dot, deferred `.org` and pseudo-immediate
   semantics instead of forcing every consumer to use the same arithmetic.
5. **Phase interfaces:** narrow `MiniAs *` callbacks to TargetParseView,
   TargetLayoutView and target EncodeSink after source and MC ownership
   stabilizes; prohibit Target writes to Common symbol/section internals.
6. **CI cleanup:** consolidate W1 only with a before/after verification
   matrix. A retired workflow is not permission to retire its corpus or
   downgrade P0/P1/strict GNU controls.

For each merged batch, report **production additions, deletions, net delta,
relocated lines, tests and CI/doc lines separately**, plus exact HEAD and
the distinction between A0, ELF GNU parity, GNU-linked Image, and repeated
QEMU P1. Never transfer a green result from an earlier commit.

## First implementation batch, locally verified before push

- **S1 implemented:** SourceGraph and source expansion now have separate C
  owners and one private source header. SourceGraph owns source-frame release
  and stable forced-branch replay; source expansion owns macro teardown,
  comment lexing and recursive repeat dispatch. `assembler.c` is reduced from
  7,336 lines to roughly 5,400 after the simultaneous SectionKey additions;
  nearly two thousand lines were **moved, not erased**. Cross-TU private
  functions were explicitly `minias_`-namespaced to avoid generic linker
  symbols; RISC-V-only backend prototypes moved out of Common's internal API.
- **R1 implemented:** RVC measurement and final R/WORD/I/LOAD/STORE/U
  emission consume the same target-private scalar machine-word builder;
  duplicate bitfield assembly deleted. AArch64's zero-only attributes
  callback and LoongArch's duplicate privileged mnemonic catalog deleted.
- **E1 implemented:** deferred right-shift now supports 63 and matches GNU's
  64-bit logical shift semantics for same-section symbol differences. Invalid
  cross-section shifts still reject without producing an ELF object.
- **O1 implemented:** a single immutable SectionIdentity includes an
  explicit-presence bit plus full u32 unique ID, group signature and
  link-order anchor; `?` inherits current effective group on the parse side;
  Section group flags propagate to the shared ELF serializer. Same-signature
  mixed ordinary/COMDAT members take GNU's group-level COMDAT promotion;
  a repeated identical SectionKey with changed flags rejects.
- **Mandatory local gate:** independent Release `CFLAGS=-Werror` build and
  `check-minias-a0` passed (30 objects). The A0 now runs GNU exact 4-Target
  deferred-shift and 3-case SectionKey probes. SectionKey GNU `ld -r` 3/3,
  4,096-section/8,192-subsection stress, x86 symbol-fixup oracle,
  AArch64 branch/literal pool oracle and SourceGraph x86 GNU / RV local
  tests passed. RISC-V and LoongArch GNU cross comparisons and QEMU are
  **NOT TESTED** on this workstation; remote exact-head CI is still needed.

**Not implemented in this batch:** M1/M2 expression AST consolidation,
X1/T1 target form canonicalization, full SourceContext state ownership and W1
workflow retirement. They remain queued behind this batch's remote evidence.
