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

## Batch I: ELF symbol liveness, typed Common directive ownership, and target audit

This batch follows the first Batch H remote exact-head replay. That baseline
passed the 21-shard immutable original GNU Kbuild `.s` gate and the fast
Target, MiniC, and Objcopy/Strip gates, but failed a new **strict ELF oracle**
for x86 memory forms because MiniAS emitted three unnecessary `STT_SECTION`
symbols. That is a genuine object-model divergence even though all 27
instruction encodings and relocation dimensions were equal. Never suppress
`symbols` globally in the strict comparator to make this test green.

| Layer | Verified defect / architectural seam | Batch I remedy and local oracle |
|---|---|---|
| Shared ELF | `minielf` generated an `STT_SECTION` for every section, whether a relocation used it or not; same-section ABS relocations referenced named locals where GNU uses a section symbol plus addend | Sparse, relocation-demand-driven section-symbol plan and GNU strict object comparison of four cases; x86 27-form instruction **and object** oracle now exact |
| Common conditional parser | Inactive `.if` still defined `.macro` and evaluated `.rept`, including nested expanded source frames | Skip inert syntax without side effects; GNU x86 lexical/bytes oracle |
| Common↔RV directive classification | RV maintained its own copy of Common DATA directive spellings, letting `.hword/.value/.xword/.skip/.fill/.uleb128/.sleb128` escape executable-section ISA restrictions | Common classifies RAW_DATA/ORG/OTHER once and supplies semantic kind to Target callback; 88 raw executable rejects and 22 legal DATA cases |
| RISC-V ISA state | `.option rvc` could not recover from `norvc`; explicit C profiles rejected valid toggles; legacy `norvc` skipped C opcode validation; legacy C16 emitted with `EF_RISCV_RVC=0` | Target-owned RVC state and ISA gates; actual C16 emission marks the output object RVC-capable; `.option` push/pop and three ELF flag probes; unproved GNU cross cases remain remote-gated |
| AArch64 Target | Plain scalar memory, deferred memory and `:lo12:` memory relocation paths each interpreted bracket/base/width and opcode policy | One A64-private memory operand parser and typed opcode descriptor, 10 relocation forms/8 rejects plus previous scalar/deferred coverage |
| LoongArch64 Target | FCSR/FCC, scalar FP, LSX/LASX and system/cache instructions had repeated, sometimes incorrectly GPR-classified parser branches | Target-private typed bank/shape descriptors for 23 instruction families, 44 positives/36 negatives; remote GNU bytes pending |
| x86-64 Target | 16-bit CMOV family incomplete; high-byte shift operands rejected; MOVSX/MOVZX high-byte source could read uninitialized `wd`; shift-memory operand was parsed twice | 37 GNU byte-exact instructions/10 invalid operands, shared high-byte and width constraints |
| CFI | Positive CFA save offsets rejected; `.cfi_rel_offset` unsupported | GNU `*_sf` signed DWARF CFI opcodes and CFA-relative save semantics, exact EH/debug CFI event streams, 3 invalid cases |
| Debug lines | `.file 1 "/dir/file.c"` serialized as a full-path filename and no directory table, unlike GNU | Target-neutral directory deduplication and 1-based file table indices; x86 GNU raw `.debug_line` table entries exact; byte identity still not established because GNU defaults to v3 and MiniAS currently emits v4 |
| Common storage | Unused `MiniAs.cfi_signal_frame` state coexisted with actively used `MiniAsCfiProc.signal_frame` | Delete dead global field; retain procedure-local state |

The new test scripts are registered in the existing **two** fast Target/GNU
workflows. Local combined `-Werror` build, MiniAS A0 30/30, x86 GNU object and
byte oracles, ELF/CFI probes, RV32/RV64 state and executable-data guards,
A64/Loong machine-word/negative tests all pass. **A64/Loong/RV GNU cross
comparisons remain `NOT_TESTED` locally** and must pass on the pushed exact
commit in CI; previous head results cannot certify this batch.

### Explicit next architectural work, not superficial file movement

1. **P0 provenance preservation:** canonical Linux 3×7 consumes 21 pinned
   original GNU Kbuild inputs from GitHub Actions producer run
   `40658346252`. Its 21 source artifacts total **279.74 MiB** and the
   earliest expires at **2026-11-27 08:49:40 UTC**. Preserve all 21 shard
   tarballs and three producer-evidence artifacts with SHA-256 in an immutable
   release/archive, verify exact content, change the reader, and re-run all
   21 shards before retiring the ephemeral fixed run ID. This migration is
   separate from ordinary dead-CI deletion and must not change any input.
2. **P1 Target register-bank retention:** Common `reg_class` only distinguishes
   GPR/FPR/VECTOR. Loong VR128/XR256/FCSR/FCC, x86 high-8/REX constraints,
   and A64 SIMD need a **Target-private bank ID retained on the MC operand**.
   Keep Common ISA-neutral; do not invent an AArch64+Loong shared backend.
   Add the field and prove four Target producer/consumer paths before turning
   missing bank into a hard error. Existing `reg_class` still drives RV32E.
3. **P1 typed deferred expressions and Target→Common reverse edges:** A64
   still rewrites temporary operand strings for deferred differences; x86
   alignment still walks Common `stmts`/fragment offsets. Introduce owned
   MC expression payloads and immutable layout callbacks, not copied parsers.
4. **P1 complete DWARF/CFI ABI:** `.cfi_def_cfa %rsp,8` needs a Target-private
   register-name-to-DWARF-number interface; `.cfi_personality/.cfi_lsda`
   need proper augmentation; `.debug_line` GNU v3-v4/default state and row
   byte scheduling still diverge though source directory semantics now agree.
5. **P2 workflow cleanup:** retain original GNU producer and all original
   Kbuild evidence; archive only proven-obsolete diagnostics after a
   source-hash preserving migration. Do not delete entire legacy workflows
   merely because a newer seven-shard gate exists.

## Batch J: complete Target encode-result ownership (2026-10-29)

Architecture-only implementation after six independent Common/Target/ELF/CI
audits. This batch is **not** a new original-Linux or full GNU semantic
acceptance result. The single MiniAS branch and Draft PR #667 are retained.

### Before / after ownership

| Boundary | Previous mixed ownership | New authoritative owner |
|---|---|---|
| Instruction bytes | Target used a Common byte capture buffer | `MiniAsEncodeResult` owns one fragment's bytes until checked commit |
| Instruction Fixups | Target called `minias_add_fixup`, immediately mutating global symbols and Fixups | Target stages fragment-relative typed requests; Common validates, interns symbols and commits Fixups |
| Synthetic PCREL anchors | RISC-V encoder directly allocated/defined `MiniAsSymbol` and incremented the global counter | Target requests a private anchor; Common defines it and advances an ISA-neutral counter at commit |
| Target ELF bits | RISC-V C16 encoder directly ORed `as->elf_flags` | Target stages flag delta; Common applies it only after the fragment bytes and requests are accepted |
| AArch64 section-symbol preference | Target changed `as->fixups[last]` after insertion | Preference travels as a typed Fixup request property |
| Target operand ownership | Generic class alone lost SIMD/VR/XR distinctions; certain pseudos used hidden operand slots | Common owns opaque Target bank/form/payload storage and disposal, without decoding ISA-specific values |

The DATA, explicit `.reloc`, CFI and DWARF-generated section-owned Fixup
paths deliberately retain their existing Common semantics. A Common commit
allocation failure stops assembly before writing an output ELF; the internal
half-built assembly context is destroyed, **not** made reusable by rollback.

### Backend consolidation within the same production batch

- RISC-V `.option` mutable state and push/pop live in TargetContext; a single
  branch plan supplies layout and final encoding decisions at each layout
  iteration, and final RV Fixups/PCREL anchors are staged.
- x86-64 shares register encoding decisions across measurement and emission,
  unifies the previously duplicated memory-payload attachment paths, and
  stages all target instruction Fixups.
- AArch64 retains target-private register bank and scalar-memory form IDs;
  deferred difference payloads no longer masquerade as an extra MC operand.
- LoongArch retains distinct VR128/XR256/FCSR/FCC register banks and an
  owned LI.D lowering plan, shared by measurement and encoding.

### Exact local structural evidence

- Debug and Release (`-O2 -Werror`) MiniAS builds: PASS.
- Existing MiniAS A0: 30/30 PASS.
- New four-Target EncodeResult probe: PASS, including instruction-owned,
  explicit-section and CFI section Fixups, plus failed-output rejection.
- Static Target/Common ownership guard: PASS.
- Focused x86, AArch64, RISC-V and LoongArch probes: local paths PASS.
- AddressSanitizer/UBSan focused probes: PASS with leak detection disabled;
  LeakSanitizer could not operate in this container's process-tracing setup.
- Remote cross-GNU object parity, Linux 3x7 and Image/QEMU were **not run**.

### Boundaries remaining after Batch J

1. **P1 typed Fragment and persistent conditional-region graph:** `.org`,
   `.fill`, `.space` still borrow source spelling and dynamic `.if/.elseif`
   may trigger whole-source reparsing; eliminate only with dependency-aware
   SourceFrame/Fragment ownership.
2. **P1 stable symbol IDs in machine operands:** remaining target-private
   `MiniAsSymbolExpr{name[256]}` and lookup-by-name in measure/encode should
   migrate to the already-owned Common SymbolID/MCExpr model.
3. **P1 complete target plans:** x86 still has unmerged measure/encode families
   and ALIGN inspects Common `stmts`; AArch64 `.req` still rewrites operand
   text before position-aware register parsing; Loong and RV have residual
   legacy opcode/feature spellings. Keep ISA policies in their own Targets.
4. **P2 source and object helpers:** collapse only proven duplicated numeric
   expression lexers, macro/rept source walkers, DWARF byte primitives and
   REL/RELA decoding; keep ELF Reader, RelocatableWriter and Rewrite roles
   separate and preserve old Kbuild artifacts before CI cleanup.

## Batch K: typed Fragment, one SourceFrame entry, and four Target plans

Architecture-only implementation after the second six-agent read-through of
the exact Batch J tree. This is one cross-layer batch, not a Linux correctness
claim. The single `refactor/minias-target-isa-abi-core` branch and Draft
PR #667 remain the only development route.

### Removed new/old parallel paths

- Common now records an ISA-neutral `MiniAsFragmentKind` for instruction,
  alignment, origin, fixed data, fill, space and raw bytes. Relayout and
  emission no longer dispatch these semantic cases by re-reading `stmt->op`.
  A Fragment-kind/payload invariant is checked before both layout and emit;
  the coarse `MiniAsStmtKind` remains for existing Target ABI callbacks.
- Physical-file source lines are captured as a `MiniAsSourceFrame` and enter
  the same `process_source_range` collector as macro, REPT and IRP frames.
  Two separate FILE-only macro/repetition collectors were removed. `.end`,
  inactive conditionals and inline statement cases retain their behavior.
- The three Common integer-expression policies (wrapping unsigned, checked
  signed and deferred MCExpr) share a lexical operator/precedence grammar.
  Duplicate tables, repeated parenthesis normalization, two unused expression
  APIs and unwritten dot-override state were removed, while the distinct
  overflow, symbol and section policies remain separate.
- x86 code alignment asks a read-only Common predecessor-site query instead
  of opening `as->stmts`; memory and ALU measure/encode use one Target-owned
  `X64EncodingPlan`. Hidden zero-operand lengths and MOV-immediate register
  values were replaced with typed form/operand representation; redundant ALU
  group maps and 17 duplicate memory-kind assignments were removed.
- AArch64 `.req` aliases are resolved only in register-typed operand parsers,
  never by textual replacement of every identifier. This fixes an observed
  symbol/register collision: an alias `sym .req x9` used beside a real symbol
  `sym:` previously emitted relocations to an undefined `x9`, while the new
  encoder retains the correct `sym` references. Q-register pair operands also
  retain their proper Target bank instead of being labeled GPR.
- RISC-V retains a per-MCInst LI lowering plan used by both measurement and
  emission. Target-private opcode/form registry, extension parsing, XLEN
  policy and canonical ELF ISA attributes now share descriptor ownership;
  unknown mnemonics fail during parse. RV32/RV64 remain one Target.
- LoongArch64 scalar, LSX/LASX and special-register families consume retained
  Target Form/Bank in encoding, instead of depending exclusively on a parsed
  final machine word. `LI.W`, `LI.D` and `LA.PCREL` share a lowering/fixup
  plan. Target-private capability gating preserves the existing public
  `-march=loongarch64` contract, without inventing GNU option spellings.

### Validation and honest limits

- Integrated Release `-O2 -Werror` build: **PASS**.
- `make MODE=release check-minias-a0`: **PASS**, 30 objects.
- Four-Target EncodeResult/fixup ownership and static Common/Target guards:
  **PASS**.
- SourceFrame inactive macro/repeat and `.end` GNU x86 bytes: **PASS**.
- GNU x86 memory, register-width and branch probes: **PASS**; LoongArch
  scalar/vector/register-bank probes and 140 pre/post object comparisons:
  **PASS** (the latter is not a GNU Loong oracle).
- AArch64 typed alias regression: **PASS**, exactly two symbolic `sym`
  relocations and no `x9` leakage.
- Shared expression grammar: four Targets match GNU x86's 60-byte DATA
  section, 8 rejected overflow/divide-by-zero cases leave no output.
- RISC-V seven focused profile/LI/RV32E/option/attribute probes: **PASS**.
  RV32I real ELF32 link/QEMU test needs absent cross GCC in this container.
- Full exact-head Linux 3x7, remote cross-GNU parity and Image/QEMU were
  **NOT RUN** for this batch. Draft PR must not be marked accepted on the
  strength of local architecture oracles.

### Remaining architecture closure work

1. **Stable machine operand SymbolID / typed relocation modifiers.** Target
   operands still allocate `MiniAsSymbolExpr{name[256]}` and resolve by name
   during layout/encoding, despite Common MCExpr having symbol identities.
   Migrate all four Targets together through a Target-neutral reference API;
   keep `@plt`, page/lo12 and ISA relocation meaning Target-private.
2. **Persistent conditional dependency graph.** `.if/.elseif/.set` may change
   Fragment count and still trigger full-file reparse after branch growth.
   SourceFrame is the front-end entry, not yet a stable conditional-region
   graph. Do not delete source replay without symbol/expansion identity and
   dependency invalidation. `.org` retains its four lexical modes for now.
3. **SourceFrame peak memory.** Physical source text is currently buffered in
   `MiniAsSourceLine[]` until parsing completes; memory is then freed before
   layout/emission. A later bounded, chunked SourceFrame reader can reduce
   peak memory without restoring the old duplicated collectors.
4. **Remaining Target families and object utilities.** Complete A64 memory
   operand typing, Loong hand-built fixed encoding, remaining x86 form
   descriptions, RV typed modifiers and `.option relax`; consolidate shared
   ELF byte/reloc primitives and DWARF context lifetimes after the MC symbols
   and conditional ownership are stable. Preserve frozen Linux input assets
   and consumer-specific ELF Reader/Writer/Rewrite roles.

## Batch L: one machine-symbol ownership model and transactional references

Architecture-only integration of six independently reviewed work streams on
the exact Batch K tree. The previous `MiniAsSymbolExpr{name[256]}` in all four
machine Target operands was not a stable MC identity: parsers duplicated name
storage, encoders repeated global string searches, and staged Fixups recreated
another symbol-name owner. These paths have now been removed together.

### Two levels of identity, one point of ELF mutation

1. **Parse-time identity:** Common interns each referenced spelling into an
   immutable `MiniAsSymbolNameId` pool, independent from `as->symbols[]`.
   `MiniAsSymbolRef{name_id,addend}` and its difference pair are held directly
   in Target MCOperands; the x86 nested memory payload holds the same inline
   value rather than another owned heap string. Targets can ask Common for
   read-only current binding during branch/layout iteration.
2. **Encode transaction:** `MiniAsEncodeFixupRequest` carries only a
   `MiniAsSymbolRef`; a synthetic-anchor request carries only a NameId. An
   instruction emitter cannot insert global Fixups or synthetic Symbols.
   Commit materializes anchors and then Fixup symbols with the prior ELF
   encounter order, including undefined-symbol promotion, `.set`, section
   folding and private RISC-V PCREL pairing. No actual ELF symbol is inserted
   merely by encountering an operand in a speculative parse or layout pass.
3. **Target-local encoding:** x86 branches/RIP operands, AArch64 page/LO12/GOT
   and deferred differences, RV CALL/TAIL/LA/LOAD/STORE/BRANCH/JAL, and
   LoongArch branch/LA.PCREL/PC HI-LO forms all use the reference API. Target
   relocation modifiers and machine encoding legality remain Target-owned.
4. **Dead API removal:** the old `MINIAS_MC_SYMBOL` and difference operand
   variants, old `minias_encode_add_fixup(name)` and
   `minias_encode_request_anchor(name)` entrypoints, mixed name/reference
   Fixup fields, and duplicate anchor-name ownership are gone. `.reloc` and
   CFI Section-owned Fixups remain because they are a distinct ownership
   model, not a compatibility path for instruction emitters.

### Batch size and local evidence

- Ten production `assembler/src` files: **+679/-393, 1,072 touched lines**;
  this is a cross-layer structural batch, not isolated spelling replacement.
- One new cross-Target symbol-transaction regression and changed operand
  lifetime, Target boundary and CI workflow checks.
- Integrated Release `-O2 -Werror` build: **PASS**; `check-minias-a0`:
  **PASS, 30 objects**; `git diff --check`: **PASS**.
- Four-Target DATA matches the GNU x86 57-byte oracle. Host x86 relocation
  entries match GNU, including 8 relocations, lexical `.set`, weak/global
  undefined references and local section-symbol folding: **PASS**.
- RISC-V @plt, GOT/PCREL, private anchors, symbolic LOAD/STORE, local labels,
  subsection JAL and 1150-NOP growing branch: **PASS**.
- Four-Target EncodeResult, expression grammar, alias and SourceFrame
  controls; AArch64 typed alias and x86 GNU memory bytes: **PASS**.
- Operand-lifetime partial-failure tests: **PASS**, including AddressSanitizer
  address-only mode; LeakSanitizer remains unsupported here.
- GNU AArch64/RISC-V/LoongArch cross-oracle, Linux 3x7, complete link and
  Image/QEMU have **NOT** been run in this batch. Draft PR #667 must not be
  marked runtime-correct or merged on these local results alone.

### Remaining architectural seams for the next full audit

1. **P1 MCExpr symbol-difference bridge:** Common's new reference resolver
   still converts two NameIds to temporary spelling DTOs to reuse the checked
   difference arithmetic in `mc_expr.c`. Extract one underlying checked
   symbol-pair resolver; do not add a fourth expression-evaluation algorithm.
2. **P1 dynamic conditional-region ownership:** `.if/.elseif/.set` may still
   cause whole-source reparse after relaxation. Persistent SourceFrames and
   typed Fragments are prerequisites, not a completed dependency graph.
3. **P1 remaining Target plans:** AArch64 memory/form duplication, x86
   measure/encode families, Loong direct word forms and RV modifiers/feature
   modes need independent whole-Target audits before deleting code.
4. **P2 MC/data and ELF/DWARF helpers:** some symbol-ID MC data expressions
   still construct temporary name DTOs for emission. Move this at the same
   time as the symbol resolver, then examine the separate ELF and DWARF
   consumer boundaries rather than blindly merging utility functions.

## Batch M: single Fragment discriminator and Target-owned ISA state

The next architecture-only integration uses the complete six-agent Batch L
read-through. It closes actual old/new parallel paths across Common and all
four Targets. It does not add any requested instruction family or claim a
full Linux runtime acceptance. Branch and PR remain unchanged; PR #667 stays
Draft.

### Common contract (no Target-dependent semantics)

- `MiniAsStmtKind` and `stmt.kind` were redundant with the typed retained
  `MiniAsFragmentKind` and had zero consumers in the four Targets. All
  creation, validation, layout, emission, and EncodeResult checks now use the
  single Fragment kind. The source-spelling fields remain only where the
  source/diagnostic grammar still genuinely needs them.
- The checked same-section symbol-difference arithmetic is owned once by
  MCExpr. The new NameId difference interface no longer recreates two
  temporary 256-byte source-name expressions for a second lookup.
- MCData held actual Object SymbolIDs but its encoder reconstructed names
  to rediscover those same symbols. The Common emitter now uses the original
  SymbolID directly through the same Fixup recorder, preserving lexical
  `.set` binding, undefined-local promotion, ELF symbol order and relocation
  provenance. Section-owned `.reloc`/CFI remain distinct on purpose.
- Generic `target_options`, never-read `profile_features`, dead MCExpr
  variants/unused multiplier are removed. Every Target owns its mutable ISA
  state; immutable per-instruction `site.mode_bits` is captured via the
  existing Target callback. The ELF writer still sees only generic object
  information, not architecture-specific instruction rules.
- A real full SourceFrame reparse now reports
  `minias-source-reparse:pass=N` under `MINIAS_TRACE_RELAYOUT`, so conditional
  replay cannot silently fall out of the regression suite. The dynamic
  conditional dependency graph itself is not yet replaced.

### Four independent Target closures

- **RV32/RV64:** removed the entire private LI-plan pointer hash; MCInst's
  normal Target-owned `target_payload` destructor owns the retained lowering
  plan. Form parsing now carries typed XLEN/required-feature policy to one
  post-parse gate; no separate pre-parse mnemonic-feature scan. Unrecognized
  form families fail closed. AMO/LR/SC share target-private AQ/RL and width
  suffix decoding. The strict final machine-word check remains as intentional
  raw-ISA bypass defense, not dead code.
- **AArch64:** eliminated `A64_MC_MOVZ` as a second route around the typed
  WIDE_IMM encoding; consolidated named `.reloc` and Fixup-to-ELF mapping
  under one Target descriptor. `A64TargetState` now owns both `.req` aliases
  and `.arch/.arch_extension` feature state, without Common bitfields.
- **LoongArch64:** Bitfield/Bytepick/ALSL/JIRL/MOVE/Prefetch/ADDU16I,
  branch, PCALA HI/LO and single-word LI forms retain typed operands/forms
  until encode, instead of constructing the final machine word in the parser.
  Removed `LA_FORM_MISC`, silent default-GPR bank inference, and opaque
  `target_payload != NULL` plan type inference. Lowering plans carry a
  private type/form guard. Independent LSX/LASX and special-register legality
  stays in its Target.
- **x86-64:** `.code32/.code64` live in `X64TargetState` and are frozen into
  the per-fragment site snapshot. Shift/Rotate Group-2 no longer has separate
  CL-vs-immediate register parse/measure/encode paths; a single Target
  EncodingPlan computes REX, prefixes, ModR/M and immediate length for all
  three count dialects and uses the existing memory layout policy.

### Size and regression evidence

- Eleven modified production files: **+810/-744 = 1,554 touched lines**,
  with 744 deleted lines, including obsolete ownership/encoding paths.
- Release `-O2 -Werror` full build: **PASS**. Complete `check-minias-a0`:
  **PASS, 30 objects**. Static target/ownership guards: **PASS**.
- GNU x86 Group-2 and `.code32/.code64` private-state oracle: 86/86 text
  bytes, 4 symbols and 2 relocations exact. Existing GNU x86 branch,
  register/high-byte, memory and SymbolRef tests: **PASS**.
- Common 1,024-relocation GNU exact parity, four-Target symbol transaction,
  variable MCExpr fixed point, COMDAT, `.org`, immutable DATA and lexical
  `.set` checks: **PASS**.
- LoongArch 140 positive/73 negative cases: **PASS**; entire ELF objects
  compared byte-for-byte with the preceding Batch L build, including the
  large scalar/vector sample: **PASS**. Cross-GNU Loong tools unavailable.
- AArch64 six local probes and five groups of complete ELF old/new
  comparisons: **PASS**; cross-GNU toolchain unavailable locally.
- RV LI/ISA profiles/RV32E/attributes/option controls, A0 and SymbolRef:
  **PASS**; GNU RV cross-oracles that need absent local tools are **NOT RUN**
  and remain CI-only.
- New source-replay test: x86 conditional Macro/REPT, dot-dependent `.set`,
  growing 2-to-6-byte branch and **observed single SourceFrame reparse**,
  GNU text bytes and symbol value exact: **PASS**.
- The previously unreferenced 2,048-edge `.set/.equ` alias DAG is now in
  the x86/RV CI matrix; local x86 GNU symbol values, bytes and cycle/undefined
  rejection: **PASS**. New four-Target lowering-ownership probe: x86 GNU
  mixed mode exact; A64 MOVW 5 words, Loong LI 6 words, RV LI 5 words and
  four malformed cases: **PASS**. RV source replay observed one reparse with
  an 8-byte branch; remote GNU RV parity remains pending.
- A remote Batch L CI run did fail at a stale static transaction-script
  spelling assertion, after its GNU RV32 real-link/QEMU and 38 earlier
  steps passed. Batch M updates that guard to test the current EncodeResult
  commit path; treat the pending next exact-head CI as unverified until run.

### Still architectural work, in priority order

1. **P0/P1 conditional SourceGraph:** existing `MiniAsConditional` has only
   stack booleans; SourceFrame lacks stable expansion-path identity. Layout
   may reparse the entire input after a growing branch in a symbol-dependent
   conditional. Replace only with a dependency-aware conditional AST,
   immutable source-site keys and targeted Fragment/Symbol invalidation.
2. **P1 Target semantic representation:** AArch64 still rewrites some
   memory offsets and symbol differences in source strings and has multiple
   memory parsers. LoongArch needs dynamic operand tokens and a private
   descriptor index; RV still has link-relax `.option` accepted without
   modeling R_RISCV_RELAX; x86 has further family-specific measure/encode
   duplication. Address by complete family, never by combining ISA rules.
3. **P2 source and object utilities:** physical SourceFrame lines are still
   eagerly buffered; duplicated Common name-hash implementations have
   distinct parse-time and ObjectSymbol identities; ELF group/link-order
   names are valid string-level object semantics and must not be flattened
   merely because MC instruction operands now use NameIds.
