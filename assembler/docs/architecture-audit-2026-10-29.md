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
