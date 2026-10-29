# MiniAS full production-source review ledger — 2026-10-29

Status: **full source read completed; first integrated architecture/correctness
batch verified locally; exact-head cross-target CI pending**.
Pinned pre-change tree: `1d9db179f59a66505c7910a1668daee5e0defb0e`,
single branch `refactor/minias-target-isa-abi-core`, Draft PR #667.

This is an explicit coverage and issue ledger, not a declaration that MiniAS
has passed GNU object parity, complete Linux linkage, or QEMU P1. All six
source domains were independently read in full; the integration owner read
the CFI/ELF/driver domain and checked cross-domain contracts. A search hit,
file inventory, or a green unit test alone never counts as source review.

## 1. Exact source coverage

The 35 files below total **30,122 lines** at the pinned pre-change tree.
`Complete` means the entire file was read, including error handling, fallback
paths and teardown, rather than just definitions found by textual search.

| Owner/domain | Production files (pre-change line counts) | Lines | Read |
| --- | --- | ---: | --- |
| Common source | `assembler.c` 6459; `expression.c` 428; `string_literal.c` 167; `invocation.c` 51 | 7,105 | Complete 4/4 |
| MC model | `minias.h` 46; `minias_internal.h` 880; `mc_expr.c` 1846; `mc_layout.c` 1116; `mc_lexer.c` 63; `mc_operand_semantics.c` 58; `mc_encode_result.c` 136; `expression_grammar.h` 59 | 4,204 | Complete 8/8 |
| RISC-V | `riscv_encode.c` 1992; `riscv_target.c` 700; `riscv_profile.c` 167; `riscv_profile.h` 36; `riscv_target.h` 111; `riscv_fixup_kinds.h` 16 | 3,022 | Complete 6/6 |
| x86-64 | `x86_64_target.c` 3572; `x86_64_fixup_kinds.h` 14 | 3,586 | Complete 2/2 |
| AArch64 | `aarch64_target.c` 5169; `aarch64_fixup_kinds.h` 34 | 5,203 | Complete 2/2 |
| LoongArch64 | `loongarch64_target.c` 2020; `loongarch64_fixup_kinds.h` 15 | 2,035 | Complete 2/2 |
| Object/debug/CLI | `cfi.c` 392; `cfi_writer.c` 450; `debug_line.c` 473; `elf_writer.c` 559; `target_registry.c` 38; `tools/minic-as/main.c` 56; `elf/include/minielf.h` 227; `elf/src/binary_export.c` 155; `reader.c` 508; `relocatable_writer.c` 960; `rewrite.c` 1149 | 4,967 | Complete 11/11 |

This inventory includes the full MiniAS production C/header implementation,
its CLI and direct shared ELF library consumers. Tests, workflows, historical
design documents and unrelated MiniC/MiniLD source are **dependency review**,
not part of the 30,122-line production-source denominator. The test and CI
inventory is separately checked against each implemented fix.

## 2. Established architecture constraints

- Common owns source events, symbol identity, MCExpr, persistent Fragments,
  fixed-point layout, generic emission, CFI/debug, ObjectModel and ELF writer.
- A Target owns ISA/ABI features, operand dialects, typed forms, instruction
  relaxation, machine bytes, private FixupKind and relocation policy.
- RV32 and RV64 deliberately share one RISC-V Target with distinct XLEN/ABI;
  x86, AArch64 and LoongArch remain separate ISA backends.
- The fifth unrelated Target must not require a Common ISA switch, private
  register shape or a copied section/ELF writer.
- No parallel legacy/new implementation is kept without an explicit migration
  and deletion plan. Every patch receives a before/after dependency audit.

## 3. Full-domain findings and evidence

| Priority | Domain / actual code | Finding | Closure requirement |
| --- | --- | --- | --- |
| P0 | Source replay: `assembler.c` ~4470–4506, ~6323–6380; `mc_layout.c` ~362–372, ~777–817 | Forced long branches cross full reparse by statement ordinal; SourceGraph records identity drift but does not fail. Changed conditional topology can reassign a forced branch to a different statement. | Stable source-site plus MC-shape keys, explicit missing/ambiguous fail-closed handling; remove ordinal list, preserve conditional and symbol ordering. |
| P1 | `assembler.c` ~6185–6290; `mc_expr.c` ~1658–1780; `mc_encode_result.c` | Instructions/exec alignment use an EncodeResult, while DATA/FILL/SPACE and some other fragments append directly to Section. Compatibility append state remains in `MiniAs`. | Unified fragment-level emission transaction without erasing distinct `.reloc`/CFI section-owned fixups. |
| P1 | `assembler.c` ~948–976, ~2655, ~3122 | `.fill` element-by-element and `.space`/zero padding through small repeated appends impose avoidable work on large sources. | Overflow-safe reserve and bulk fill preserving NOBITS and target alignment. |
| P1 | `assembler.c` ~260–320 | Section identity lookup remains a linear scan; thousands of named/COMDAT sections can be quadratic. | Index the full SectionKey, not the name alone; preserve first-definition semantics and distinct SectionIDs. |
| P1 | `mc_expr.c` ~1415–1416, ~1517–1518 | Symbol, addend and section/dot offsets use unchecked signed arithmetic in some emit paths. | Use one checked numeric API and test positive/negative extremes. |
| P0 | `x86_64_target.c` ~2980–3275 | Defined same-section GLOBAL/WEAK CALL/JMP/Jcc/RIP targets are sometimes folded without GNU-required relocation; symbolic MOV memory immediate uses inconsistent ABS32 vs ABS32S across duplicate paths. | Unified target-private branch/binding and memory-fixup plan, GNU bytes + offset/type/symbol/addend matrix. |
| P1 | `riscv_target.c` ~14–47, ~426–476; `riscv_encode.c` ~776–857, ~1065–1110, ~1210–1252 | RVC alignment is not coupled to immutable site feature state; `.option relax` lacks R_RISCV_RELAX/ALIGN contract; LI/LA use `MCInst.encoding` for semantic metadata; CSR zimm can be mistaken for RV32E GPR. | Single typed Target form/context, explicit supported option policy and GNU profile/ELF tests. |
| P1 | `aarch64_target.c` ~2160–2268, ~2291–3926, ~4915–5120 | Branch expression grammar duplicates Common arithmetic; a long instruction-family parse chain and differing `.arch` vs `-march` capability entry points remain. | One Target-owned profile parser, typed Common expression references and Target descriptor families. |
| P0/P1 | `loongarch64_target.c` ~1457–1493, ~1726–1864 | Branches to WEAK symbols can be fixed up as local, B16/B21 expansion is incomplete, legitimate 32-bit `li.w` constants are rejected; some parser `encoding` writes are recomputed by typed lowering. | GNU-backed weak-binding/long-branch/LI.W oracles, one Target lowering plan, deletion of stale encoding state. |
| P1/P2 | `minias_internal.h` ~221–274, ~469–574, ~780–826 | Generic `register_64` and broad `MiniAs*` Target callbacks retain ISA and lifetime coupling even though memory payloads and register banks are now private. | Introduce narrow read-only Target views and an emission sink after the fragment transaction closes. |
| P2 | CFI/debug (`cfi.c`, `cfi_writer.c`, `debug_line.c`) | Separate serializers correctly own different DWARF streams but repeat primitive little-endian/ULEB/SLEB appends; some grow helpers duplicate capacity policy. | Share only byte-buffer primitives and checked capacity logic; do not merge CFI and line-state semantics. |
| P2 | Shared ELF (`elf/src/{reader,relocatable_writer,rewrite}.c`) | Multiple bounded ELF read/write/rewrite roles are appropriate; byte order, alignment and small buffer algorithms are repeated. The assembler writes a completed ELF image directly to the output path. | Audit utility reuse and atomic output failure behavior with Reader/Objcopy/Strip/AR/NM regression gates; do not combine distinct consumers merely to reduce files. |

## 4. Baseline proof and limits

- On pristine `1d9db179`, local `make MODE=release check-minias-a0`
  completed successfully, `MINIAS_A0=PASS objects=30`.
- GNU cross-assembler parity cases reported `SKIP(toolchain-unavailable)`
  locally where the corresponding host cross toolchain was absent; do not
  reinterpret these lines as GNU PASS.
- Exact-HEAD PR #667 remote MiniAS ISA/ABI workflow `40878237981` and
  x86/Loong GNU workflow `40878238132` completed successfully; their target
  steps were inspected separately.
- Earlier 9,645/9,645 original Linux inputs were **assembled, unverified**
  at a different head. Full GNU object semantic parity, complete Linux link,
  Image and QEMU P1 are not certified by this architecture audit.

## 5. Implementation and sign-off protocol

1. Keep the pinned reading inventory immutable. For every changed source,
   record the diff and re-review all its callers and dependent contracts.
2. Fix a whole subsystem/family at once, including all currently exposed
   source failures; remove replaced paths in the same batch. Record **added
   and deleted production-code lines**, separately from tests/docs/CI.
3. Before exact-head CI, run Release/A0, target-appropriate GNU positive and
   negative oracles, cross-layer ownership guards, CFI/ELF consumers and
   sanitizers where available. A missing cross GNU tool is NOT TESTED.
4. Preserve one MiniAS branch and Draft PR #667. Do not merge or declare the
   3×7/full ELF/Image/P1 strict gate passed until the same exact source tree
   has produced independent evidence.

## 6. First integrated batch — independently verified local evidence

The findings table is the *pre-change issue inventory*, not a list of still
unfixed defects. This batch deliberately combines independently audited
domains into one production-source change and retires replaced paths in the
same patch. Production additions and deletions are counted separately from
tests, documentation and workflow edits.

| Finding | Local disposition | Remaining boundary |
| --- | --- | --- |
| P0 Source ordinal/replay binding | Replaced with stable expanded SourceSite plus typed MC shape; old cross-replay ordinal API deleted. Actual numeric-local and macro `\\@` generation drift now accepted, missing/ambiguous source site fails closed. | A disappearing forced site still rejects instead of incrementally updating dependency topology; 1,024-pass guard remains. |
| P1 Common bulk data append | `.fill` and `.space` use checked one-reservation fill/pattern operations; zero-repeat UB removed, NOBITS semantics preserved. | Direct section append vs fragment-owned emission transaction is still distinct. |
| P1 MCExpr overflow | Defined modular wrap for unsigned 64-bit data/RELA and checked signed S+A-P; illegal cross-section division rejected. | Broad range proof of all indirect expression consumers remains open. |
| P0 x86 symbol binding | GLOBAL/WEAK branch and RIP relocations preserved, ABS32/ABS32S selection corrected; duplicate memory emit and Fixup routes unified. | Instruction descriptor registration still has redundant parallel tables. |
| P1 RISC-V Target state | LI/LA pseudo shape moved off raw encoding cache, CSR opcode family table unified with typed zimm, RV32 Zbb `rev8` corrected; RVC alignment and ELF flag use ISA capability independently of `.option norvc`. | GNU `R_RISCV_RELAX`/`ALIGN` semantics are not implemented and must not be advertised as done. |
| P1 AArch64 profile and deferred delta | `-march`/`.arch` share Target parser; signed `end-begin-4` forward difference and branch offset now keep the subtraction direction. | Large instruction parser and private expression dialect still need convergence. |
| P0/P1 LoongArch LI.W and binding | Typed `LI.W` lowering replaces redundant encoding cache; valid negative immediates accepted, WEAK same-section B16 retains relocation. | Far B16/B21 branch expansion requires a real GNU oracle; no speculative code was kept. |
| P1/P2 section index, fragment emission, TargetOps, CFI/ELF primitives | Audited and recorded, **not closed**. | Next subsystem-scale batches, each gated by full regression. |

Integrated local gates on the shared modified tree:

- Release `-Werror`, `check-minias-a0`: **PASS, objects=30**.
- GNU host x86 `probe-x86-symbol-binding-fixups-gnu.sh`: exact `.text` and
  offset/type/symbol/addend for **14 relocations**; malformed REX/SIB rejected.
- `probe-mcexpr-signed-overflow.sh`: GNU bytes, RELA and wrap semantics exact.
- `probe-mc-source-stable-rebind.sh`: two replays, branch/source drift,
  numeric-local renumbering, macro counter renumbering, disappearing-site
  fail-closed, `.fill`/NOBITS GNU byte agreement.
- RISC-V RV32 width, RV32E CSR zimm and RVC/ELF flag local probes: **PASS**;
  GNU cross-assembler comparison **NOT TESTED locally**.
- Six x86 GNU CFI/frame regressions and GNU DWARF-line comparison **PASS**;
  ELF link-order/section index with **1,024 sections** GNU-equal.
- AArch64/LoongArch GNU oracle and ELF reader/objcopy/strip consumer scripts
  require unavailable cross assemblers in this local environment: **NOT
  TESTED**, not a pass. Corresponding workflows must run on the exact pushed
  commit before these claims can be closed.

The batch is intentionally not a declaration of complete GNU semantics,
original Linux full object parity, final Image link or QEMU boot success.

## 7. Next integration: parse-time SectionKey / Subsection indexes (pending CI)

The prior 35-file reading inventory remains the audited source baseline. This
follow-up is an actual cross-file change to the Common parser and MCLayout, not
a claim that all Target architectural debt is complete.

- **SectionKey identity:** one open-addressed, allocation-checked index holds
  the complete (name, nullable COMDAT signature, nullable LINK_ORDER source
  anchor) key. The original SectionID, first-declaration type/flags, alignment
  updates and appending to a group-specific Section are unchanged. No
  name-only lookup is used to resolve the full key.
- **Subsection identity:** a second hash uses (immutable SectionID, GNU
  subsection number). The table is refreshed immediately after MCLayout sorts
  the owned subsection array, avoiding stale ordinal aliases while retaining
  the old sorted binary-search path for layout/fixups.
- **Failure ownership:** a failed allocation does not publish a Section or
  Subsection slot. Both indexes have explicit context destruction; the ELF
  writer's independent SectionName index is retained because its ordering and
  lookup contract is different.
- **Oracle:** the GNU section/subsection test covers 4096 SectionIDs, 8192
  push/pop reopens, COMDAT same-name variants, LINK_ORDER cross-products,
  nullable link-order identity and first-declaration merge entsize.
- **Four additional probes** independently exercise x86 ModRM/register forms,
  user-owned versus generated .debug_line, and LoongArch alias/privileged
  families, all wired as fail-closed GNU steps in the existing fast oracle job.

**Verification at authoring time:** code blobs and workflow changes prepared;
no exact-head Release/A0/GNU/21-shard outcome has yet been obtained for this
batch. Do not promote the newly wired probes to PASS until a run on its actual
commit SHA has succeeded. Existing unresolved P1 items remain Fragment
emission transactions, conditional dependency graph, and target-wide form
deduplication. GNU runtime/Image/QEMU are separate correctness tracks.

## 8. Exact-head Section/Target/Debug correction and next source-audit findings

**Validated source HEAD `02c4eae95c61a80dec0d3f189d94e3bc70db2d9a`**

- MiniAS Target ISA/ABI Contract: run **40892956318**, SUCCESS.
  Both the RV32/RV64 native strict/A0 real-link job and the AArch64
  real-LD/QEMU job reported SUCCESS on that exact HEAD.
- MiniAS x86/Loong GNU Oracle: run **40892956449**, SUCCESS, job
  **123514355880**. The four newly wired GNU probes are success:
  **63** x86 typed ModRM forms / **6** negative cases with exact GNU
  instruction bytes and relocations; **4096** unique Sections,
  **8192** indexed Subsections and **8192** hot push/pop switches,
  including distinct COMDAT/LINK_ORDER keys, GNU ELF section payload
  exact; user-owned/nonempty and synthesized `.debug_line` must not
  silently concatenate, but empty explicit sections work; LoongArch
  **5** psABI alias instructions, **9** privileged forms,
  **2** PCALA relocations, **11** GNU invalid cases and **2**
  intentionally MiniAS-only unsupported relocation combinations,
  with exact GNU bytes/relocs for the supported forms.
- Combined delta relative to `005e9b63` (before this next initialization
  change): six commits, **six** modified production C/header files,
  **+318/-115 production lines**; tests, CI and docs are separate.
  Those patches removed the duplicate x86 register opcode table, mapped
  LoongArch GPR `$s9` and FPR ABI bank aliases, added complete
  Section/Subsection indexes, and rejected dual-owner DWARF line tables.
- The Loong GNU probe itself found a false negative assertion: GNU GAS
  syntactically accepts some misplaced PCALA modifiers. Preserve MiniAS's
  fail-closed supported-domain policy, but NEVER report that syntax as
  a GNU rejection. The revised test names the two populations separately.

**Additional whole-module lifecycle finding:** initial Section creation
in `minias_init_for_target` ignored allocation errors and attempted
subsection creation or Target initialization with incomplete Common
state. Make initialization return a checked result, preserve context
teardown and prohibit parse/output after a failed init. This extra
change is NOT covered by the above exact-head results until its own
new revision is run.

**Open structural debt, explicitly not closed by this batch:**

1. Common Fragment byte ownership is still split: DATA/FILL/SPACE and
   nonexecutable padding append directly, while instructions/code ALIGN
   stage and commit EncodeResults. A unified ownership migration must
   preserve SymbolID/Fixup ordering, CFI and .reloc section fixups,
   NOBITS semantics, and fail-closed partial objects across all Targets.
2. SourceGraph keeps immutable physical SourceFrame and stable expansion
   site IDs but lacks dynamic conditional read sets / semantic
   dependency graph; full state is reparsed in exception cases.
3. RISC-V link relaxation R_RISCV_RELAX/ALIGN is a specific ABI policy,
   not a generic Common instruction option. It is NOT fully supported.
4. AArch64 has many source-mnemonic `strcmp` families (439 call sites
   across the target, but not all are duplicates); a data-driven Target
   form catalog with independent GNU negative/error-path oracles is the
   right next complete-Target refactor, rather than blindly rewriting
   individual comparisons.
5. ELF/CFI and object writer must retain distinct serialization and
   output-ownership roles even if shared checked grow/ULEB primitives
   are consolidated.
6. Original Linux 9,645 GNU input AS coverage is not the same as
   complete original GNU object parity, final Image link or QEMU P1;
   those are separate correctness milestones.

## 9. Source architecture comparison added (documentation-only)

See `architecture-comparison-llvm-gas-2026-10-29.md` for the
current exact-head 35-file, 30,735-line LLVM MC / GNU GAS comparison,
ownership map, concrete newly exposed issues, and the next large
subsystem refactor order. This is not new production-code acceptance.
High-confidence new cases reproduced on the host GNU x86 assembler:
ELF section `unique,<number>` identity, and `.section` `?`
inheritance of COMDAT. RISC-V `.option relax` accepted with no
per-site semantic change and no R_RISCV_RELAX/ALIGN implementation
is a confirmed source-level contract gap; GNU cross-oracle is pending.

The 35-file baseline and prior six-domain read are historical coverage
claims. This comparison reruns whole-inventory/module-boundary scans
and focused call-chain review; it does not claim separately scheduled
subagents or an independent fresh line-by-line pass of each file.
