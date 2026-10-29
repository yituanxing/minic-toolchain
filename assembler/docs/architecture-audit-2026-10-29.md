# MiniAS four-target coupling audit — 2026-10-29

Baseline: `6eea437b8492d4a9596d0af789f2b330cfc6eadd` on the single
`refactor/minias-target-isa-abi-core` branch, PR #667 (Draft). This is an
implementation audit, not a certificate of GNU ELF parity. The baseline's
original Linux 6.6.143 GNU `.s` coverage is 9,645/9,645 **assembled but
unverified** in exact-head CI #40824500696.

## Scope and acceptance vocabulary

Read the Common parser/MCExpr/MCLayout/Fixup/CFI/Debug/ELF adapter, four
Targets, `elf/`, the CLI and target catalog, MiniObjcopy/Strip/AR/NM consumer
gates, MiniAS workflows, and the relevant cross-toolchain test scripts.
These are distinct proof stages:

1. Original GNU-generated input successfully assembled to target ELF.
2. GNU object semantic parity: section types/data/groups/link-order, symbols,
   relocation types/addends, nonrelocated instruction bits and ABI flags.
3. GNU link of the complete Linux image with MiniAS-produced input objects.
4. QEMU Linux boot and P1 repeatability on the same Image.

Only stage 1 was fully proven at the baseline commit. No stage can inherit a
previous commit's green result without running on its actual object tree.

## Which shared paths are right, and which are not?

| Pattern | Verdict | Ownership rule |
|---|---|---|
| RV32/RV64 share one RISC-V encoder family and separate ABI profiles | Correct | Same ISA family, independent XLEN/ABI policies |
| x86/A64 prefer PREL while RV/Loong use ADD/SUB relocation pairs | Capability distinction is real; inference in Common is fragile | Target defines object relocation policy; Common owns expression algebra |
| RV/A64 ALIGN callbacks used direct Section append while x86/Loong used MC byte transaction | Incorrect 2+2 historical migration | All four use a single atomic byte-emission contract; Target retains distinct NOP encoding |
| RV/x86 branch expansion has typed and source-mnemonic size paths | Mixed legacy/new model | Target relaxation should consume stable MCInst and site state, not reparse source mnemonic |
| Common literal-pool flush hardcoded A64 32-then-64 ordering and injected textual directives | Incorrect target policy leak | Common owns pooled ALIGN/DATA Fragments; Target defines ordering policy |
| RV32E ISA register restriction scanned raw operand text (`a6`) | Demonstrably wrong | Validate typed GPR fields, never bare token spelling |
| CLI included private `minias_internal.h` and enumerated four help names | Incorrect API boundary | Public invocation + dynamic target registry; old RV APIs remain compatibility wrappers |
| MiniC target catalog exposes five triples; MiniAS registers four ISA families | Intentional capability distinction | Do not equate CC support, AS input acceptance, MiniLD support and GNU linker support |

The assembler Common implementation does not contain a separate wholesale
"AArch64 + LoongArch" backend. Target-specific encoders and assembler syntax
remain distinct for sound architectural reasons. The defects are seams where
an implementation shortcut survived after a shared ownership model existed.

## This implementation batch: intended structural changes

- Bind deferred `.org/.space/.fill/.skip/.zero` expressions once to stable
  SymbolIDs and preserve value/section/relocation provenance across MCLayout.
  Same-section differences may resolve; cross-section or invalid relocation
  arithmetic must fail closed. Retain source reparse only where `.if/.elseif`
  can actually change the Fragment graph.
- Emit literal-pool alignment and typed data as owned Fragments, with
  `TargetOps.literal_pool_order`; AArch64 selects width ascending.
- Move RV32E GPR restriction behind parsed operands, share RISC-V mnemonic
  feature/XLEN metadata with its MC instruction descriptor, and eliminate the
  typed RV branch path's duplicate source parser.
- Require all four Targets' instruction and code alignment bytes to use the
  MC transactional emitter. Remove ALIGN-specific bypass from Common.
- Route `minic-as` through a public target invocation contract, with CLI
  help derived from the sole assembler Target registry.
- Close semantic-oracle false greens: compare actual file-backed section
  payloads, mask only relocation-modified instruction bits, and report
  unsupported REL/relocation semantics as **INCONCLUSIVE**, not PASS.
- Ensure MiniAS branch changes to shared ELF code enter the canonical
  MiniObjcopy/Strip/Reader/AR/NM regressions. Keep original source artifacts
  and historical diagnostic workflows; strict 3×7 remains the acceptance gate.

## Deliberately open work

1. **Typed TargetRelaxPlan everywhere:** `MiniAsTargetOps.expanded_instruction_size`
   still takes `const char *op`; x86 still duplicates measurement and encoding
   knowledge in parts of its backend. Remove the mnemonic callback only after
   GNU branch-growth and layout stress parity covers the replacement.
2. **Explicit per-Target relocation policy:** some Common MCExpr decisions
   still infer expression strategy from `fixup_relocation_type` returning zero.
   This is not evidence that ISA relocation encodings may be unified.
3. **Persistent conditional-region graph:** `.if/.elseif` source conditions
   may change the number of emitted Fragments; the surviving reparsing path
   must not be deleted solely to improve an architectural metric.
4. **Target-owned operand payloads:** the current generic `register_64` and
   base/index/scale memory envelope still reflects x86 syntax. Migrate only
   with four backend operand/ABI proofs and no copied generic algorithms.
5. **Full original GNU object semantic oracle:** old GNU Kbuild `.o` and
   the exact producer profile must be recoverable; a 9,645/9,645 candidate
   count alone is insufficient. GNU ld/Image and QEMU/P1 follow separately.

Before merging this batch: report effective **production-code** additions
plus deletions separately from tests/CI/docs, require exact-head fast CI and
original 3×7 strict summary, preserve Draft PR #667 and one MiniAS branch.

## Batch F implementation review — worktree after `16d045092`

This section records the **uncommitted Batch F implementation and CI design**,
not a new exact-head acceptance result. Changes in the working tree must be
reviewed as one architectural batch; earlier successes at `6eea437b8` do
not certify the later source tree.

| Seam | Batch F implementation | Still requires proof |
|---|---|---|
| Target branch growth | `MiniAsTargetRelaxPlan` carries `current_size`, `required_size`, and `terminal_size`; Common MCLayout validates the retained MCInst and monotonic growth rather than calling a mnemonic-string size callback | Four Target encode/measure consistency, genuine GNU branch boundaries, source-reparse trace |
| ELF relocation decisions | `TargetOps.supports_fixup(kind,width)` makes capability explicit; Common MCExpr no longer treats numeric relocation type zero as a universal unsupported sentinel | Cross-target ELF semantic deltas, paired ADD/SUB and PREL policies, unknown relocation semantics fail closed |
| MCInst ownership | Allocator/destructor track operand `capacity` separately from count, permitting cleanup of partially parsed nested operands | Lifetime/partial-parse/failure-unwind regressions and sanitizer results on the exact commit |
| RV LI instruction selection | `rv_materialize_li` now constructs one instruction-word plan for both size measurement and encoding, removing a parallel heuristic | RV32/RV64 signed boundaries, runtime value reconstruction and real GNU semantic oracle (GNU's instruction sequence need not be byte-identical) |
| Four-target GNU evidence | One fast RV/A64 contract and one x86/Loong GNU oracle add shared 160-fragment deferred DATA/ORG/RELA stress; x86 also checks short/near branch growth | Execute with all three cross GNU assemblers in CI; only local x86 GNU stress was confirmed at this audit |
| Object semantic comparator | Mutation checks now cover relocation order, RISC-V ALIGN/RELAX addends, additive relocation fields, section references, symbol attributes and ELF header metadata | Compare genuine original Kbuild GNU objects, not just candidate assembly or synthetic probes |
| PR exact-head guarantee | All three fast PR jobs now explicitly check out `pull_request.head.sha` (or `github.sha` for dispatch) and assert `git rev-parse HEAD` before compiling | Obtain successful GitHub Actions job IDs on the newly pushed source commit |
| Shared ELF consumers | Existing MiniObjcopy/Strip push gate retains Reader/AR/NM, Objcopy A0/A1 and Strip A0 checks; explicit `[miniobjcopy-strip-regressions]` runs them even if a change is isolated to MiniAS's own ELF writer | Read exact-commit six-consumer verdict rather than inferring it from routing or historical green |

The dedicated RV32/RV64 LI probe locally reconstructed 27 expected register
values and confirmed emitted-word counts. Local cross GNU tools were absent;
the fast RV CI job now requires `riscv64-linux-gnu-as` before invoking the
oracle so a GNU comparison cannot be silently reported as optional.

The current canonical Linux replay remains the **single** pinned original
GNU Kbuild 3-target × 7-shard, `aggregate --strict`, 9,645-candidate gate.
It establishes original `.s` assembly coverage, never GNU ELF equality,
Image linkage, or QEMU/P1 by itself. The legacy generated replay is retained
for diagnostics but its aggregate is also strict. A commit message with
`[minias-replay-3x7] [miniobjcopy-strip-regressions]` selects the canonical
21-shard scan and six shared-ELF consumers on the **same one branch**, without
triggering unrelated heavy Linux/Objcopy workflows.

**Next exact-head acceptance, not yet executed for Batch F:** fast GNU jobs
for RV64, x86-64, AArch64 and LoongArch64; 21 pinned shards and strict
aggregate; canonical MiniObjcopy/Strip regressions. Preserve the PR as Draft
until the associated commit SHA, test counts and run URLs are recorded.

## Batch G architectural cleanup and remaining decision (2026-10-29)

Review scope: all Common MC/ELF/CLI sources, x86-64, RISC-V RV32/RV64,
AArch64, LoongArch64, DWARF/CFI, toolchain consumers and the exact-head
GNU/3x7 gates. Six independent task owners cross-audited the implementation;
the findings below distinguish *correct* shared policy from accidental
coupling and must not be read as full Linux object parity.

| Boundary | Batch G implementation | Meaning / next proof |
|---|---|---|
| Generic MC operand vs x86 syntax | `MiniAsMCMemory` base/index/scale/segment fields removed from Common; x86 owns `X64Memory`, MCInst holds an opaque target payload plus destructor callback | Four backend parsers do not inherit an x86 address format; check ASAN lifetime, x86 GNU bytes and negative operands |
| RISC-V ELF attributes | Target stores structured stack-align/arch/unaligned fields, serializes once; no magic offset insertion at bytes 16/18 | RV32/RV64 byte-exact ordering and contradictory attributes; GNU ISA attributes still checked on CI |
| RISC-V CSR immediates | Target reuses Common constant-expression evaluation; target retains CSR range and ISA feature checks | Remove redundant arithmetic parser, keep target encoding ownership |
| AArch64 scalar memory | Target-private 9-form / 25-mnemonic table replaces divergent scaled/unscaled/register-indexed/unprivileged logic | Do not share A64 opcode tables with Loong; compare valid machine words and negative addressing forms with GNU |
| LoongArch LSX/LASX vectors | One LoongArch-specific typed pattern/encoder handles vector classes and immediate bit widths, replaces several copied parsers | LSX VR and LASX XR are distinct ISA register classes; 48 positive and 20 negative probes, GNU differential required |
| Retained `.set/.equ` aliases | Common resolves SymbolID alias chains by iterative dependency graph with explicit cycle detection instead of rescanning all symbols at each depth/relayout | Prevent quadratic behavior in 2048-element alias chains and unnecessary source reparsing; GNU x86 branch growth must remain exact |
| ELF symbol population | Object writer builds one liveness plan for local labels, ELF relocs and COMDAT signatures rather than scanning the whole graph for every emitted symbol | 2048 temporary labels + 2048 external relocs + COMDAT GNU exact, with O(N + fixups + groups) population |
| Debug `.loc` parser | Per-invocation cursor replaces `strtok` process-global state | Independent assembler contexts are no longer coupled through this parser's hidden global cursor |
| GNU semantic oracle | ET_REL/known-ISA checks, section addresses, relocation site bounds, and `--strict`/`--require-strict` fail-closed modes | No unsupported ELF form can be silently counted as proven GNU equivalence |

**Architecture decision:** RV32/RV64 correctly share a RISC-V Target family
with separate XLEN/ABI profiles; AArch64 and LoongArch64 correctly remain
different Target parsers and encoders. Their commonality belongs in generic
MCExpr/MCFragment/MCLayout/Fixup/ObjectModel interfaces, not a fabricated
two-Target ABI or opcode translator. The historical 2+2 and x86-shaped
seams have been substantially reduced in Batch F/G.

**Still genuinely open:**

1. **P0: original GNU Linux `.o` semantic parity.** The 3x7 replay proves
   9,645 source `.s` inputs can be assembled; it does not compare original
   Kbuild GNU objects, full image linking, or QEMU boot. Require complete
   paired `.o` provenance and `run_semantic_oracle.py --require-strict`, then
   separately GNU `ld` Image and repeated QEMU P0/P1.
2. **P1: retained conditional-region graph.** Source `.if/.elseif` may add or
   remove Fragments, and mutable `.set`/`.align` conditions may change their
   topology. The current bounded source reparse for those dynamic cases is
   correct fallback until a persistent region dependency graph owns them;
   do not delete it in the name of reducing a metric.
3. **P1: multi-instruction Target pseudo lowering.** Parts of the RISC-V
   expanded pseudo flow remain legacy but proven; migrate incrementally to
   explicit retained MCInst plans only with GNU/ABI oracles for every site.
4. **P2: instruction operand MCExpr and complete object/debug parity.** CFI,
   all reloc payload masks, object section metadata and non-ELF facilities
   remain target capability boundaries; do not collapse ISA-private policies
   into Common on the assumption that two targets share an ELF class.

This is the design and local regression inventory for Batch G. Only an
exact-head CI result tied to its *actual pushed commit* can establish its
remote Linux/GNU outcome; keep PR #667 Draft while P0 remains open.

## Batch H cross-owner whole-source audit (2026-10-29)

Six Target/Common/ELF owners and an independent CFI adversarial reviewer
read the active assembler sources and traced the GNU contracts. This is an
implementation inventory before the batch's remote exact-head CI; do not
promote local cross-toolchain SKIPs or the old 9,645 `.s` aggregate into a
strict ELF `.o` parity claim.

| Layer | Proven defect or structural debt | Batch H change |
|---|---|---|
| Common directives | `.ident` silently discarded ELF `.comment` contents and metadata; `.end` was a no-op even inside macros, repeats and semicolon-separated source | Emit GNU-compatible `.comment` mergeable strings; propagate end-of-source across nested parse frames; remove repeated/dead directive cases and ISA-specific bare-name dispatch |
| x86-64 | Jcc/CMOV/SETcc condition aliases duplicated, memory measure/encode independently chose SIB/REX/disp widths; valid SETcc high-byte registers rejected | One x86 condition descriptor and memory-layout decision, AH/CH/DH/BH without REX, GNU exact opcode/EA probes |
| RISC-V | RV32 accepted RV64-only scalar FP moves/conversions and `.d` atomics; profile serialized ELF attributes then Target reparsed fixed offsets; numeric attribute values assumed a byte | XLEN-specific instruction boundary, canonical profile string serialized exactly once, ULEB128 attribute payload, wider RV32/RV64 oracles |
| AArch64 | Scalar memory forms had parallel descriptor and legacy encoder paths, plus a literal `if (0)` dead path; ADR subtraction risked signed overflow | Retain one A64-specific memory descriptor for ordinary/deferred operations; remove six redundant MC opcodes and dead branch, guard arithmetic |
| LoongArch64 | 148 scalar mnemonics repeated 9 operand-shape parsers; LSX/LASX operands silently inherited GPR class | One LoongArch-specific opcode/operand-shape table, typed FPR/VECTOR classes, GNU scalar/vector positive and negative cases |
| ELF writer | `SHF_LINK_ORDER` target resolution rescanned every section for each metadata section | Build one sorted `SectionName → SectionID` index; 1,024 independent GNU `sh_link`/relocation stress pairs |
| Strict comparator | `.symtab`, `.strtab`, `.shstrtab` skipped by name alone, allowing forged same-named real data sections to evade payload checks | Exclude only structural sections identified by actual ELF section indices and type/link relationships; add name-collision adversarial cases |
| DWARF CFI | Register numbers incorrectly capped at 255; more critically, CIE omitted pre-first-PC CFI, causing `.cfi_restore` to revert to the wrong initial state, and `.debug_frame` reused a single non-simple CIE for `simple` functions | ULEB128 register operands, Target ABI initial CFA/RA, type-and-event-sequence-specific CIEs for EH/debug, indexed reuse of full typed pre-PC event streams |

The CFI bug is a **runtime correctness issue**, not merely cosmetic ELF byte
layout. For `CFI_OFFSET r6,-16; nop; CFI_RESTORE r6`, GNU puts the initial
register save in the CIE, so the later restore refers to that state. The old
MiniAS placed the save in the FDE, making restore semantically different.
`probe-cfi-initial-state-gnu.sh` now checks GNU CIE/FDE event streams,
reused initial CIE identities and the split after the first PC advance. The
Common CFI writer indexes CIE variants by a hash of retained typed prefix
events, with exact collision verification, to avoid O(procedures²) searches.

**Architecture decision:** retain Common MC/Target ownership and one
RISC-V ISA family with distinct XLEN/ABI, keeping AArch64 and LoongArch64
independent. Continue architectural work only at proven remaining seams:

1. **P0** original pinned Linux GNU Kbuild `.o` strict semantic parity on
   the exact current source tree; then full linker Image and QEMU P0/P1.
2. **P1** Target-owned register-bank identity: one generic VECTOR class
   cannot express LoongArch VR128 vs XR256, nor specialized FCSR/FCC banks.
3. **P1** persistent dynamic conditional-region graph and typed operands for
   AArch64 deferred differences, x86 large parser flags and residual Loong
   floating-point/system instructions; preserve correct existing paths until
   independent GNU tests cover replacements.
4. **P1** GNU `.debug_line` directory/file table parity and CFI byte-format
   corner cases beyond the newly covered initial unwind semantics.
5. **P2** audit old MiniAS workflows against frozen GNU producers and legacy
   evidence before deleting anything. A historical workflow is not dead
   simply because a newer strict 3×7 gate exists.

Count production-code changes from `assembler/src` additions **and**
deletions separately from scripts, docs and workflows. Run `-Werror`, A0,
GNU x86/ELF/CFI regression and remote RISC-V/A64/Loong GNU contracts on the
new head; only then record exact run IDs and final pass/fail status.
