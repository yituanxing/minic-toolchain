# Current native tool validation — 2026-10-11

## Scope and priority

Validate only existing C-language Mini tool implementations on the runtime branch.
Do **not** restore or implement MiniObjdump, rerun historical self-hosting, or
combine native tools before isolated GNU-reference gates are established.

The reference is Linux 6.6.143 on RISC-V with GCC preprocessing/compilation,
GNU as, GNU ar, GNU ld and GNU binary utilities. In each tool's Linux proof,
replace **exactly one** producer boundary. A compiler-only MiniC campaign is
tracked separately.

## Current evidence / claims (fail closed)

- MiniC full-current campaign:
  [#38147618243](https://github.com/yituanxing/minic-toolchain/actions/runs/38147618243).
  All seven 2,064-object compile shards completed successfully; the receiver
  authenticated the complete 2,064-object MiniC pool and GNU baseline
  preflight. **Full-MiniC Linux Image link failed**, so there is no current-head
  full-MiniC Linux runtime certificate. Unresolved symbols include
  `arch_dma_set_uncached`, `dax_delete_mapping_entry` and four
  `__compiletime_assert_395..398` sites. Its prefix binary search reports
  `INTERACTION_OR_NONMONOTONE` (not a proven isolated one-object fault).
  The older **nine-current-owner + 2,055 historical owners** two-replay
  145/145 module and 203/203 function PASS remains a separate valid baseline.
- MiniPP: native preprocessing vs GCC-E differential gates exist. The
  end-to-end GCC-with-only-MiniPP Linux replacement remains unproven.
- MiniAS: frozen/real Linux assembly differential gates exist. The
  end-to-end GCC-with-only-MiniAS Linux replacement remains unproven.
- MiniAR: existing `miniar-linux-kbuild.yml` is an isolated GNU-GCC/GNU-as/
  **MiniAR**/GNU-ld Linux build and short runtime smoke. Its 90-second fast
  profile is **not** the original full 145/203 contract.
- MiniLD: existing `minild-integration-v1.yml` `linux-rel-boundaries`
  replaces GNU ld at only two selected relocatable-link outputs; other GNU ld
  calls remain. This is an **intermediate**, not a full MiniLD-only Image
  certificate. Existing static/dynamic musl tests remain separate.
- MiniNM / MiniObjcopy / MiniStrip: existing differential / ELF consumer tests
  are useful T1 correctness gates. Binary inspection and stripping are not
  equivalent to a whole-kernel boot certification.
- The unified driver has an existing independent smoke/integration gate,
  which is distinct from the compiler and full toolchain certificates.

## Reusable verification gates initiated in this campaign

One documentation-only, opt-in source-control push asks the already-active
`linux-legacy-tag-router-v1.yml` to launch the existing MiniAR Linux, MiniLD
Linux REL and MiniAS 16-real-assembly gates; the existing MiniObjcopy/Strip
regression workflow is also opted in. No new GitHub Actions workflow or compiler
source change is required.

- MiniAR Linux: `[miniar-linux-kbuild]`
- MiniLD two GNU-ld boundaries: `[minild-linux-rel-boundaries]`
- MiniAS real16 diagnostic: `[minias-real16]`
- MiniObjcopy/MiniStrip differential: `[miniobjcopy-strip-regressions]`

These are **current-head diagnostic runs**, not substitutes for an independent
single-tool, complete 145/203 Linux runtime replay. Record exact run IDs,
compiler/tool hashes, stage-specific pass/fail and genuine guest markers
before promoting a gate.

## Exit criteria before combined toolchain and new feature work

1. Each implemented tool must pass an isolated GNU-base differential and
   relevant real build/integration contract. Mark the precise supported scope.
2. For tools whose output participates in the Linux Image, demonstrate that
   exactly that tool is replaced, the other GNU tools remain pinned, and Linux
   completes the intended QEMU/functional oracle, not only a link or smoke.
3. Address remaining full-current MiniC link failures independently, preserving
   all seven independently cached 2,064-owner shards.
4. Only after independent acceptance, start two-tool integration followed by
   complete existing-toolchain integration. Postpone MiniObjdump work and
   current-compiler Stage0->Stage1->Stage2 self-host re-certification.

## Independent revalidation results — current product

- [#38149742508](https://github.com/yituanxing/minic-toolchain/actions/runs/38149742508):
  MiniAS real16 **PASS 16/16**, source generated from pinned real Linux inputs;
  MiniAR-only Kbuild and MiniLD-only two-REL-boundary Linux builds were still
  executing when this entry was written. Their final verdicts must be checked.
- [#38149742328](https://github.com/yituanxing/minic-toolchain/actions/runs/38149742328):
  **SUCCESS**. Exact GNU differential assertions observed:
  MiniNM A0 (ELF32/ELF64 REL/EXEC/DYN), MiniAR A0, MiniObjcopy A0/A1,
  MiniStrip A0. This is a format/option gate, not a Linux boot certificate.
- [#38150034665](https://github.com/yituanxing/minic-toolchain/actions/runs/38150034665):
  MiniPP A0 exact GCC-E comparison **SUCCESS**.
- [#38150034676](https://github.com/yituanxing/minic-toolchain/actions/runs/38150034676):
  MiniPP live Linux smoke and product18/batch tests both **SUCCESS**;
  pinned GCC predefines produced (378 macros, 22 attributes, 4 builtins).
  The 3352-item frozen exact check and independent Linux-only MiniPP build
  remain separate, stronger targets.
- [#38150108182](https://github.com/yituanxing/minic-toolchain/actions/runs/38150108182):
  unified driver integration started; final verification still pending.

## Next current-head opt-in proof

Run the **existing** MiniAS 3352 matrix + aggregate/C149/native35 gates,
MiniLD dynamic musl and static musl/Lua contracts. Keep distinct individual
job verdicts; never label the whole tool as certified solely by green router,
structural M0 or a partial Linux-only substitution. Do not add workflows.

## Current-head repair attestations and independent green proofs

- [MiniAR GNU-base Linux #38149742508](https://github.com/yituanxing/minic-toolchain/actions/runs/38149742508):
  `MINIAR_LINUX_KBUILD=PASS` with **633 MiniAR calls**, 629 archives,
  then `LINUX_RUNTIME_EXACT=PASS profile=fast lanes=1/1`. This is the
  **fast** Python-derived smoke, not the original full 145/203 suite.
- [MiniAS native 3536 #38150189463](https://github.com/yituanxing/minic-toolchain/actions/runs/38150189463):
  all seven 3352 shards, C149, native35, all3352 and gate3536 **SUCCESS**.
  This is the pre-executable-alignment-fix SHA; rerun after the one-line
  correctness fix below to certify the new assembler binary.
- [MiniLD dynamic + static #38150189463](https://github.com/yituanxing/minic-toolchain/actions/runs/38150189463):
  both **SUCCESS**. Dynamic MiniLD-linked musl loader plus automatic-needed
  DSO/PIE programs ran under QEMU; static musl malloc/free and Lua 5.5
  passed. These remain distinct from the full Linux-Image MiniLD gate.
- [Driver initial #38150108182](https://github.com/yituanxing/minic-toolchain/actions/runs/38150108182):
  reproducible RV64 SIGILL / 132, with `main=0x1059` in the PIE ELF
  symbol table, an impossible odd RISC-V instruction address.
- [MiniAS pre-fix regression #38150580702](https://github.com/yituanxing/minic-toolchain/actions/runs/38150580702):
  independent ELF fixture `MINIAS_EXEC_SUBSECTION_ALIGN=FAIL actual=1
  expected=4`; `assembler/src/assembler.c:parse_section_directive`
  was defaulting executable `.section` to byte alignment rather than
  the existing four-byte `.text` alignment.
- [MiniAS fixed #38150684714](https://github.com/yituanxing/minic-toolchain/actions/runs/38150684714):
  focused real16 plus `MINIAS_EXEC_SUBSECTION_ALIGN=PASS`.
- [Driver fixed #38150792607](https://github.com/yituanxing/minic-toolchain/actions/runs/38150792607):
  the **unchanged** dynamic musl+driver QEMU program passes twice,
  loader and direct launch: `MINIC_DRIVER_V0=PASS`, rc=0,
  expected `MINIC_DRIVER_OK` stdout. Its corresponding ELF artifact
  now has `main=0x105c`, restoring valid instruction alignment.
  The bug is fixed in **MiniAS**, not masked in the test.
- [MiniLD first Linux REL #38149742508](https://github.com/yituanxing/minic-toolchain/actions/runs/38149742508):
  linked Linux `Image` but correctly **FAILED** its two-call boundary
  assertion: only `arch/riscv/purgatory/purgatory.ro` appeared in the
  MiniLD trace. The wrapper matched `*/vmlinux.o`, but actual Kbuild passed
  bare `vmlinux.o` and silently used GNU ld. Fixed the **existing**
  workflow's selector (both absolute and bare paths), and updated both
  structural equivalence checkers with an exact reversible one-line
  exception; original historical source blobs remain pinned.
  [M0 #38150727475](https://github.com/yituanxing/minic-toolchain/actions/runs/38150727475)
  **SUCCESS**. A genuine two-boundary MiniLD Linux replay is executing at
  [#38150659369](https://github.com/yituanxing/minic-toolchain/actions/runs/38150659369);
  do not prematurely call it PASS.

The next opt-in MiniAS 3536 run uses the corrected executable-section
alignment and its existing 3352/C149/native35 aggregation. New/changed test
owners: **zero**. Existing toolchain functionality, not self-hosting or
new tool implementation, remains the only acceptance scope.

## Updated final exact-oracle gates and MiniLD evidence correction

- **MiniPP 3352/3352 exact**:
  [run #38150944470](https://github.com/yituanxing/minic-toolchain/actions/runs/38150944470)
  completed **SUCCESS**, six independent product shards (4×500 + 1×1000 + 352),
  all report `MINIPP_EXACT_PRODUCT=PASS`; total=3352. Tested product SHA
  `d9b9ee25cae7bda2fda7cace588e6424d16624e4`. This proves exact
  GCC 13.3.0 preprocessing over the frozen Linux 3352 corpus, **not** an
  end-to-end Linux Image produced by substituting only MiniPP.
- **MiniAS corrected 3536**:
  [run #38151040863](https://github.com/yituanxing/minic-toolchain/actions/runs/38151040863)
  completed **SUCCESS** across seven 3352 shards + C149 + native35 +
  hard aggregates. Tested commit
  `eeb896b49ee0878350a3bbc36a6577d783dd7995`
  includes the executable `.section` alignment repair and the independent
  odd-length-rodata / function-section fixture.
- **MiniLD two-boundary Linux proof**:
  [run #38150659369](https://github.com/yituanxing/minic-toolchain/actions/runs/38150659369)
  created a **real** `arch/riscv/boot/Image` after running both selected
  relocatable link stages through MiniLD. Its artifact
  `minild-boundaries.log` contains exactly:
  `arch/riscv/purgatory/purgatory.ro` and bare `vmlinux.o`.
  Nevertheless the run is **FAIL** due solely to a stale trace assertion
  that still required a slash before `vmlinux.o`.
  The original GNU fallback selector had already been fixed; now the
  assertion is corrected to `(^|/)vmlinux[.]o$`, retaining the exact-two
  requirement, without bypassing either link.
  During this patch, an unsafe string-replacement expansion was caught by
  M0 because it duplicated the third canonical job; the complete file
  was immediately reconstructed byte-for-byte from the pinned pre-edit
  workflow, with **only** the reviewed trace assertion changed. The two
  source-equivalence checkers explicitly undo exactly two reviewed one-line
  changes, and the canonical workflow inventory pins the final blob
  `e84b6032871161e16a12ed2394c2a072a9f28038`.
  The restored 648-line YAML contains exactly one copy of each three jobs;
  [M0 #38151764689](https://github.com/yituanxing/minic-toolchain/actions/runs/38151764689)
  completed **SUCCESS**. Genuine MiniLD two-stage final-link retest is
  [#38151764635](https://github.com/yituanxing/minic-toolchain/actions/runs/38151764635),
  still running at the time this paragraph was recorded.

All historical fixed-profile Linux Python 145/203 certificates remain
separate from the new-current-MiniC full 2064-object link failure.
