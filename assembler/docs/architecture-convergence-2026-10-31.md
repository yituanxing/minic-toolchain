# MiniAS architecture convergence: deletion and ownership ledger (2026-10-31)

Related design decisions: [2026-10-31 ISA/ABI and Parser-to-MC discussion](architecture-discussion-decisions-2026-10-31.md).
This related note records agreed principles separately from this ledger's
source modifications and verified acceptance evidence.

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

## Next deletion batch: retained variable DATA and RISC-V form policy

This batch is a targeted follow-up to the SourceGraph/SectionIdentity work,
not closure of the full M1 or T1 programs. Its independent code and runtime
proofs must be recorded under the commit that actually contains it.

- **M2 variable DATA:** `.fill/.space/.skip/.zero` compile a retained
  `MiniAsDeferredExpr` exactly once, after source-site mutable `.set`
  substitution. Both provisional placement and final MCLayout use those same
  owned SymbolIDs and expression nodes. Removed the two standalone Common
  eager parsers `minias_parse_common_fill` and `minias_parse_common_space`,
  their public declarations, and their duplicate call paths. A forward
  expression can still start at provisional size zero and settle during the
  one fragment layout fixed point. Negative `.fill` repetition remains a
  GNU-compatible empty fragment; invalid `.space` fill and width overflow
  remain rejected. The structure gate now rejects reintroduction of either
  obsolete text parser instead of requiring its continued presence.
- **T1 descriptor slice:** RV32/RV64 fixed R/I/U/branch/JAL forms now store
  their operand and alias policies alongside the existing RISC-V Target
  opcode descriptors. This removes repeated mnemonic-based decisions for
  two-operand Zbb, arithmetic-immediate aliases, shift bounds, reversed
  comparisons, zero-branches, `%hi`, `j` and `jal`. No ISA policy enters
  Common; other RISC-V pseudo/floating/vector families remain independent
  until their existing typed decoders can be certified as a whole.
- **Local gates:** Release `-Werror`, A0 30/30, SourceGraph x86 GNU oracle,
  typed variable DATA relaxation, forward-operand fixed point, mutable `.set`
  GNU byte oracle, and the Linux x86 page-table relocation/link oracle pass.
  The two variable DATA GNU oracles also pass with ASan/UBSan enabled
  (`detect_leaks=0`: LeakSanitizer cannot inspect `/proc/<pid>/task` in this
  container). RV target A0 bytes match the prior verified implementation. Local GNU
  RISC-V/LoongArch comparison and Linux P1 are **NOT TESTED** until exact-head
  GitHub Actions completes.

**Still open:** M1 shared expression syntax AST with policy-specific
evaluators, full RISC-V Target form metadata for every ISA family, private
SourceContext ownership rather than the umbrella `MiniAs` session, and W1
workflow deduplication. These are not implied by removal of the two eager
variable DATA functions or the first fixed-form descriptor slice.

## Consolidated deletion batch after `c1a5f8fa` (remote CI verified)

This batch removes additional real duplicates while continuing the ownership
boundary work. Counts compare against `c1a5f8fa`; **moved lines are not code
deleted**. The exact committed SHA and remote checks must be recorded after
the integration commit. The old four-architecture P0/P1 result belongs to
`c1a5f8fa`, not automatically to this refactor.

Measured staged diff before commit: production `assembler/src/*.c,*.h`
**+423/-499 (net -76)**; workflow YAML **+0/-196**; focused tests
**+76/-23 (net +53)**. The SourceGraph physical reader and comment scanner
were relocated from `assembler.c` to their owner and are included in both
production addition and deletion counts, **not** treated as removed algorithms.

| Area | Change | Limit still open |
| --- | --- | --- |
| Source | One `MiniAsSourceContext` contains per-pass macro, conditional, lexical and source-site state; its destructor and `.purgem` mutation live with expansion; the persistent physical file reader/comment lexer lives in SourceGraph | SourceGraph remains invocation-owned and borrowed, and `.if` semantic handling remains in the directive lowerer; phase-scoped Target views are not yet implemented |
| Common DATA | Removed duplicate `parse_i64_data` and `parse_data_integer_bits` strto* grammars; one existing unsigned arithmetic expression evaluator handles numeric DATA constants | M1 full token-to-AST grammar with three distinct evaluation policies remains open |
| RISC-V Target | Zero-operand, two-register and floating CSR aliases now use a single private `RvParseForm` authority for spelling, ISA gate, operands and encoding | Other pseudo/FP/vector instruction families require a separate verified contract |
| x86 Target | Legacy ALU/MOV/CMP/TEST b/w/l legality derives from the existing binary form table; mode gate is shared | Remaining specialized x86 system/boot forms still have their own legality rules |
| CI | Retired two obsolete generated/cached 3×7 workflow wrappers (196 YAML lines) in favor of the existing pinned 21-shard fail-closed producer/aggregator | No original corpus, producer hash, per-object provenance or P0/P1 criterion may be removed |

Local integrated checks: independent Release `CFLAGS=-Werror` build and A0
`objects=30` PASS; x86 legacy boot GNU 38/38; SourceGraph physical-cache,
source replay, stable-site rebind, conditional macro/repeat, `.end`, and
MCExpr module probes PASS; CI routing/provenance static probe PASS. GNU
RISC-V cross assembler is not installed in the local environment, and this
batch has **not** run exact-head Linux/QEMU or 21-shard GNU comparisons.

This batch subsequently landed as GitHub commit
`cb642cd2734ff58df71b1199d283b44e4c276f77` (tree
`24728ca0dfd42390da0ef3d3d4fea96cd5b9d396`) on PR #667. The
exact-HEAD remote ISA/ABI contract, x86/Loong GNU Oracle, MiniAS GCC
runtime ladder, GNU GCC A/B, BusyBox and 3536 gates all reported SUCCESS.
These remote results certify that commit only, not the following batch.

The next high-value deletion work is the shared expression syntax AST with
separate target-immediate, checked-absolute and deferred-relocation evaluators,
followed by a private SourceContext parse API and phase-scoped Target views.
Do not count the SourceGraph reader's relocation between translation units as
a deleted parser or claim the whole x86/RISC-V Target form family is complete.

## Shared expression AST, private SourceContext and target-form batch

Baseline: the exact remote tree `24728ca0dfd42390da0ef3d3d4fea96cd5b9d396`.
This second integrated batch landed as `ccf939ff77128f61933cd2cec203193091aa1dfc`
(tree `6a395966b3ad5cfbdacc4413c679f79d1ab580a2`). Its own exact-HEAD
remote MiniAS ISA/ABI contract, x86/Loong GNU Oracle, four-architecture
compiler runtime and Native Strict Backend gates all completed SUCCESS.
The AArch64 ISA/ABI job also completed its real GNU link and QEMU checks.
These results are not the same as a new 3,536-object Linux P1 certificate.

| Area | Actual architecture change | Intentionally not claimed |
| --- | --- | --- |
| M1 expression | Remove the independent recursive integer grammar from `expression.c` and the two absolute/deferred recursive grammars from `mc_expr.c`. A single token-to-typed-AST parser now feeds distinct wrapping immediate, overflow-checked absolute and bound-SymbolID deferred evaluators. Common short expressions use inline AST node storage. | Arithmetic modes, relocation algebra and `.if`'s historical single-`=` compatibility normalization are *not* a single evaluator. |
| S1 Source ownership | The SourceContext struct, macro frames and conditional stack are private to the Source module. The Common `MiniAs` holds an opaque, allocated SourceContext pointer. All `.if*`, `.else`, `.elseif` and `.endif` stack operations now belong to source expansion; `assembler.c` merely dispatches. | Moving the conditional implementation between files is a boundary change, not algorithm deletion; the phase-scoped Target view and persistent conditional dependency graph remain future work. |
| AArch64 Target | Consolidate 13 mnemonic/form selectors in a Target-private named-form descriptor; delete duplicate opcode tables, form scans and SHA encodings while preserving typed MCInst emission. | No GNU AArch64 byte-oracle claim on the local machine (cross tools unavailable). |
| LoongArch64 Target | One register+immediate form descriptor and field encoder replaces duplicate `bytepick`/`alsl` parsing/encoding branches, and shared register-bank specs enforce vector/special-reg fields. | No local GNU LoongArch byte-oracle claim (cross tools unavailable). |
| x86 Target | Share REX/ModR/M register fields, little-endian displacement/immediate writer, `.code16` mode predicate and segment prefix map across boot and legacy instruction families. | Not all specialized x86 legality paths have become descriptor-driven. |

Measured production diff (11 `assembler/src` files): **+1,346/-1,634,
net -288**. This includes the relocations of conditionals and SourceContext
declarations; only the eliminated independent grammars and target-private
decision/encoder paths represent genuine duplicate deletion. Focused test
diff (seven files including three new tests): **+238/-3, net +235**. No
CI workflow was retired in this batch. Documentation is counted separately.

The new regression corpus adds a standalone AST evaluation-policy test,
AArch64 42 positive opcode words plus 8 negative forms, and LoongArch
10 positive immediate boundaries plus 8 negative cases. The four-architecture
GNU shared-expression grammar probe and existing deferred ownership tests
must remain gate inputs. Local full Release `-Werror`/A0 30/30 passed; other
integrated results and remote exact-HEAD SHA follow the final commit.

## Affine MCExpr source-grammar deletion batch

Baseline: the verified `ccf939ff77128f61933cd2cec203193091aa1dfc`
commit. The preceding batch unified integer/absolute/deferred *syntax*,
but fixed-width GNU DATA still routed relocations through six independent
textual matchers that split strings at `+`, `-`, `/` and `>>` and repeatedly
stripped parentheses. Those paths could disagree about precedence and
lexical `.set` ownership despite using the canonical AST elsewhere.

This batch replaces those recognizers with **one AST-to-affine MCExpr
lowerer**. It accepts up to two relocatable SymbolIDs and one typed Fragment
dot, tracks signed coefficients, and chooses the existing semantic fixup
kind without retaining source text. Constants and relocatable fields consume
the **same parse-site AST**; no final emission reparses source. Public
section-symbol difference and `.size`-end consumers now use this lowerer
as well. The obsolete paren-strip API, top-level-minus/divisor scanners,
data-prefix relocation scanner, and their declarations are deleted in the
same patch. The three distinct arithmetic/relocation policies remain.

GNU's 64-bit *modular* relocation addends are deliberately separate from
the checked signed absolute evaluator: `MAX-(-MAX)` must yield signed RELA
`-2`, not an intermediate overflow rejection. Cross-section division still
fails closed. Added a GNU x86-64 exact data/relocation oracle with nested
arithmetic, parentheses, dot, `.size` and nonlinear negative coverage;
included this in A0 and extended the ownership gate to ban the old parsers.

Measured source change (two production files): **+308/-509, net -201**;
test change (two existing scripts plus one new probe): **+80/-0**. This is
actual removal of duplicated recognition algorithms, not a file move; no
workflow YAML was removed. Local Release `-Werror`, A0 30/30, the new GNU
affine oracle, 23 executed expression/source/ELF probes and targeted x86,
AArch64 and LoongArch local probes passed. The `pyelftools`-dependent ELF
live-section probe skipped; GNU AArch64/Loong cross-tool byte oracles are
not installed locally. The exact-HEAD remote result for this batch must be
established after push, not inherited from the baseline.

The initial remote commit `f7d61934ce7e705eef109eeeda086a94092827ea`
exposed a missing legacy pair-view case in the Native A0 contract:
`.size entry,.-.Lentry` is a `DOT_MINUS_SYMBOL` AST form, but the old
symbol-pair API must still convey positive `.` and negative `.Lentry`.
The correction maps that affine form back into the existing pair DTO for
Target consumers and `.size` alternate start labels, without reviving a
text parser. The GNU DWARF/size alias oracle now passes for ELF32 and
ELF64 with `ld -r`, and is added directly to A0. The failed exact-HEAD
remote run remains a real failure; the correction requires a new commit
and independent CI validation.
