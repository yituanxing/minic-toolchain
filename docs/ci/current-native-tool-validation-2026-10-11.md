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

## MiniObjcopy-only Linux Kbuild frontier — 2026-10-11

The existing `miniobjcopy-strip-regressions-v1.yml` `linux-tool` job
now has an **opt-in** `linux-single` mode. It keeps GCC C compilation,
GNU assembler, GNU ar and **GNU ld in *all* Linux link stages**, while
overriding `OBJCOPY` alone with a traced MiniObjcopy wrapper. The
historical mixed MiniLD/MiniObjcopy mode is preserved; there are still
exactly three jobs and one canonical workflow. The source blob is pinned in
the active-workflow inventory and independently checked by the extended M0
archive-integrity gate.

The first true single-replacement Linux trial was
[#38153376846](https://github.com/yituanxing/minic-toolchain/actions/runs/38153376846).
It reached the native RISC-V Linux PI object rewrite and failed *correctly*
with `minic-objcopy: unsupported option: --prefix-symbols=__pi_`.
The `miniobjcopy.trace` confirms exactly five initial call sites, while
`minild.trace` is empty. This failure is native-tool feature coverage,
not an unrelated GCC failure or combined MiniLD interaction.

A narrow ET_REL rewrite implementation now supports the actual three
Linux PI options together: `--prefix-symbols`,
`--prefix-alloc-sections`, `--remove-section`. It preserves ELF section
indices and relocation symbol indices, while updating ELF section names,
allocatable relocation-section names, static symbol strings and string
offsets; relocation data remains unchanged. Fresh RV64/RV32 GNU comparisons
verify renamed symbols, sections, relocation targets and GNU-ld consumption:
[#38153921004](https://github.com/yituanxing/minic-toolchain/actions/runs/38153921004)
**SUCCESS**; both `MINIOBJCOPY_PI_PREFIX_CASE=PASS`.
The shared toolchain focused regression
[#38153920910](https://github.com/yituanxing/minic-toolchain/actions/runs/38153920910)
is also **SUCCESS**.

Now rerun the **same** real Linux single-MiniObjcopy Kbuild + fast-QEMU
gate on the corrected product. Do not classify as full historical 145/203
runtime until that complete oracle has independently been executed.

## Second MiniObjcopy-only Linux frontier — .modinfo extraction

[#38154031817](https://github.com/yituanxing/minic-toolchain/actions/runs/38154031817)
completed **FAIL** after all RISC-V PI and EFI-stub relocatable
`--prefix-symbols`/`--prefix-alloc-sections` processing succeeded.
The exact next operation, with MiniLD trace **empty**, was:
`miniobjcopy -j .modinfo -O binary vmlinux.o modules.builtin.modinfo`.
The missing `-j` option, **not** MiniLD or MiniC, blocked the GNU-only
Image producer.

MiniObjcopy now accepts `-j SECTION`/`--only-section=SECTION` for flat
binary output, selecting only the named loadable ELF sections. The
first test confirmed an important real GNU compatibility subtlety:
`-j .modinfo -O binary` from an ET_REL object with *non-ALLOC* `.modinfo`
produces an **empty** file in GNU objcopy, despite non-empty ELF section
contents. The implementation now preserves the `SHF_ALLOC` requirement
even for explicitly selected sections, and emits the valid empty binary
when none is eligible. A second regression verifies non-empty `SHF_ALLOC`
metadata against GNU objcopy byte-for-byte.

[MiniObjcopy A0/A1 regression #38155181414](https://github.com/yituanxing/minic-toolchain/actions/runs/38155181414):
**SUCCESS**, including the new allocated/nonallocated `.modinfo`
differential cases. The third real GNU-only Linux build is the next test;
its separate Image/QEMU outcome must not be predeclared as PASS.

## Third MiniObjcopy-only real Linux Image result and runtime prerequisite

[#38155265274](https://github.com/yituanxing/minic-toolchain/actions/runs/38155265274)
built Linux 6.6.143 `Image` **successfully** with GNU GCC/GNU as/GNU
ar/**GNU ld at all link stages**, and **MiniObjcopy only** replacing GNU
objcopy. The trace confirmed **44 actual MiniObjcopy invocations**,
`minild.trace` empty, final `Image` 22,033,920 bytes,
SHA-256 `baca0fe12c4a3c4237c4d2e91ce97561cb7a96d8d747d98d8632a7cf84c51de4`.
The single-tool *build* gate is thus **PASS**; the workflow overall was
**FAIL** before QEMU was started because the CI initramfs cross-compile
environment lacked `libc6-dev-riscv64-cross`
(`bits/wordsize.h` missing). This is a **CI prerequisite issue**,
not yet evidence for or against the guest running correctly.

Added the missing cross libc development headers to the existing job,
with corresponding exact workflow-identity attestations and no new jobs
or workflows. Repeat the same GNU-only MiniObjcopy build and existing
`LINUX_RUNTIME_PROFILE=fast` QEMU oracle. Do not call runtime green
until real guest output proves all required initramfs markers.

## MiniObjcopy GNU-only Linux 6.6.143 QEMU fast runtime — CERTIFIED

The current-head [#38156029304](https://github.com/yituanxing/minic-toolchain/actions/runs/38156029304),
commit `97147b799088399611be52ee3be9c0ed1e9ed894`,
completed **SUCCESS** including the actual guest execution. The job log and
downloaded artifact `miniobjcopy-linux-tool-frontier` were independently
inspected (not merely the GitHub Actions green indicator):

- **Exact substitution**: GCC + GNU as + GNU ar + GNU ld remained
  unchanged; **only** MiniObjcopy replaced GNU objcopy.
  `minild.trace` was exactly 0 bytes.
- **44 native objcopy invocations**, traced with exact argv.
  Among these: 38 RISC-V PI/EFI ET_REL three-option prefix transforms,
  one `-j .modinfo -O binary` extraction, one final
  `-O binary -R ... -S` kernel Image conversion, two VDSO
  `-G` globalization filters. Counts of option appearances are not
  mutually exclusive.
- **Image generated**: 22,033,920 bytes, SHA-256
  `e019402f4bbc8bd44f55e40d4277a3cb53de2bc95bea7eda0ce91a55152e3d56`.
  Artifact image bytes were independently hashed to confirm the CI marker.
- **Real QEMU boot**, `qemu-system-riscv64`, RISC-V Linux 6.6.143,
  `rdinit=/init`: `LINUX_RUNTIME_BOOT lane=rdinit-init qemu_status=0`.
  Original guest log `qemu-runtime/rdinit-init.log`, 14,570 bytes,
  contains exact expected `devtmpfs` initialization, PID1 handoff,
  `INIT_PROC_MOUNT=PASS`, `INIT_SYSFS_MOUNT=PASS`,
  `INIT_DEVTMPFS_MOUNT=PASS`, `INIT_CMDLINE_READ=PASS`,
  `INIT_TMPFS_MOUNT=PASS`, `INIT_TMPFS_RW=PASS`,
  `USER_SHELL_OK`, `DONE_COMMANDS`,
  `MINIC_LINUX_RUNTIME_INIT_END fail=0`, and
  `reboot: Power down`. No Oops/panic/BUG was found in that guest log.
- **Runtime oracle**: `LINUX_RUNTIME_RDINIT_INIT=PASS` and
  `LINUX_RUNTIME_EXACT=PASS release=6.6.143 profile=fast lanes=1/1`;
  `MINIOBJCOPY_LINUX_SINGLE_RUNTIME=PASS` emitted only after
  real guest assertions succeeded.
- The earlier failure [#38155265274](https://github.com/yituanxing/minic-toolchain/actions/runs/38155265274)
  was **environmental**, not a tool semantic failure: missing cross-target
  libc headers during initramfs preparation. The previously approved
  cross-header fix `libc6-dev-riscv64-cross` unblocked this replay;
  M0 source integrity remained green.

**Promotion**: MiniObjcopy is certified for **the complete Linux 6.6.143
GNU-only single-tool build and the Python-derived FAST one-lane guest
runtime** at that exact SHA. This is *not* a claim that the external,
private archival 145-module/203-function suite was executed; that separate
historical asset is not part of a public runner fixture. It is also not a
full MiniLD / MiniAS / MiniPP combination or current-MiniC self-host
certificate.

## GNU-only MiniAS full Linux/Image/QEMU independent validation

The legacy independent A0-green MiniAS GCC-subprocess boundary was archived
during CI consolidation. It is now explicitly restored as a **single opt-in
job inside the existing MiniAS focused workflow** (not a new workflow),
exposing `mode=linux-single` under the canonical push-tag router.

GNU gcc, GNU ar, GNU ld and GNU objcopy remain pinned; **only the assembler
subprocess** invoked by GCC's `-B` tool search and Kbuild's explicit
`AS` variable are redirected to MiniAS. Both executable names
`as` and `riscv64-linux-gnu-as` resolve to the same traced wrapper;
only non-production capability queries may delegate to GNU as.
The post-build gate asserts more than 1,000 actual MiniAS-assembled
production inputs, a nonempty Linux 6.6.143 Image, and QEMU P0/P1 guest
oracle markers (12 syscall probes). A build failure preserves exact
`.s` input and first-error evidence for iterative fix-and-replay.
A success preserves Kbuild trace, Image SHA and guest logs.

The new job body is checked for exact correspondence with its archived
historical original, with only reviewed routing, wrapper coverage and
evidence changes. Every one of the original 10 MiniAS jobs remains
byte-identical to its archived oracle; **M0 structure gate
[#38157161551](https://github.com/yituanxing/minic-toolchain/actions/runs/38157161551)
passed** before this live opt-in trigger.

**Current-head live proof:** request `[minias-linux-single]` now. Do not
call the assembly component Linux runtime-certified until the real Kbuild
and QEMU reports pass. No Stage0->Stage2 self-hosting or new MiniObjdump work.

## MiniAS runtime counterexample and bounded incremental Linux re-link

The first actual GNU-only single-MiniAS run
[#38157216367](https://github.com/yituanxing/minic-toolchain/actions/runs/38157216367)
finished **FAIL** (real guest, not a compiler/build failure).

- Linux 6.6.143 `Image` successfully linked with genuine GCC + MiniAS,
  GNU ar/ld/objcopy unchanged. **2,198 native MiniAS passes** and
  41 expected/diagnostic assembly probe failures; production output generated
  in **386 s using `make -j4` on ONE GitHub Runner**, not 7 cross-runner shards.
- QEMU reached **OpenSBI v1.3**, then hung before **any Linux kernel banner**
  and the 90-second limit returned `qemu_status=124`. No P0/P1 PASS is
  claimed; no proof of a particular MiniAS instruction bug yet.
- The first run preserved actual MiniAS Image, exact wrapper argv trace,
  failed native probes and the guest OpenSBI-only log. Its successful
  *build* and failed *boot* must be tracked as independent observations.

**Diagnostic modification:** preserve the existing one-Runner, one-Image
GNU-only MiniAS test and add an *in-run conditional* differential step
after P0 fails; no new Workflow and no new Job. While GCC's ephemeral
`.s` is still available, the wrapper now additionally saves **GNU as
reference objects** for seven named early-boot owners (head, entry,
mm/init, setup, sbi, FDT, init/main), without replacing the native product
or delegating production compilation. The diagnostic preserves original
MiniAS `.o` and Image, then tests:

1. Replace **only `arch/riscv/kernel/head.o`** by its GNU reference.
2. If necessary, replace the seven-owner early-init cohort; if this boots,
   attempt an in-run binary split over this limited cohort.
3. Every probe reuses the **same compiled Linux object tree** and reruns
   *only necessary downstream archives/links* with the identical Kbuild
   command, followed by a 25-second bounded fast QEMU test.
4. Capture GNU/MiniAS disassembly, ELF section-header diffs, QEMU logs,
   links and exact owner fingerprints; restore native objects and original
   Image for archival integrity.

The diagnostic is **conditional on the original runtime step failing**,
thus cannot manufacture a green Linux runtime certificate. The canonical
MiniAS owner retains its original ten jobs byte-identical; the archival
Linux job reconstruction, exact script Git SHA, manifest SHA and M0 checks
all passed [#38158542756](https://github.com/yituanxing/minic-toolchain/actions/runs/38158542756).

This is a first **bounded differential probe**, not a proof that early
startup must live in the seven selected objects. If the early owner cohort
does not explain the failure, broaden isolation using the same cached-GNU
object-set approach rather than making repeated, blind full Linux builds.

## MiniAS boot blocker — ISOLATED to one object, root cause patched

[GNU-only MiniAS differential run #38158617826](https://github.com/yituanxing/minic-toolchain/actions/runs/38158617826)
delivered a decisive same-build object-swap result. It independently
compiled Linux with MiniAS: **2,197 production assembly passes** in 448 s,
then QEMU hung at OpenSBI before the Linux banner. From the **exact same
object tree**, the incremental diagnostic replaced only
`arch/riscv/kernel/head.o` with its captured GNU as reference.
Downstream re-link and fast QEMU **BOOT PASSED**:
`MINIAS_BISECT_PROBE=BOOT_PASS label=head_gnu gnu_count=1`.
No extra Linux TU compilation or new GNU full build was necessary; the
relocation+QEMU probe took **about 11 s**. Original MiniAS Image was restored
for artifact archival. Its diagnostic archive retains both disassemblies,
ELF section inventories, the original `head.s`, GNU `head.o`, and both
QEMU logs.

**Exact code-level defect:** the preprocessed real Linux
`arch/riscv/kernel/head.S` contains `la reg, global_symbol` during
early boot, where PIC/GOT is not initialized. GNU as correctly emits
`R_RISCV_PCREL_HI20` and an `auipc + addi` pair. MiniAS emitted
`R_RISCV_GOT_HI20` and `auipc + ld` instead, even though the source
did *not* enable PIC. The reason was in
`assembler/src/riscv_encode.c`: old `use_got` was based solely on
whether the symbol was externally undefined. The parser had silently
discarded all `.option` state. This is a concrete semantic bug, not
a missing assembler opcode or a speculative alignment issue. GNU/MiniAS
`head.o` also differ in `R_RISCV_ALIGN` and section sizes;
those differences have **not** been independently cleared yet.

**Patch landed:** capture effective `.option pic`/`.option nopic` and
scoped `.option push/pop` per instruction; default GNU-compatible
NOPIC, and permit GOT for external-symbol `la` only if PIC is enabled.
The original PIC/GOT fixture is now **explicit PIC**, and an additional
GNU differential test asserts default NOPIC `la` -> PCREL twice and
scoped PIC `la` -> GOT once, with **GNU-exact code bytes**.
[Focused regression #38159624380](https://github.com/yituanxing/minic-toolchain/actions/runs/38159624380)
is **SUCCESS**, with
`MINIAS_A0_NOPIC_LA_GNU_DIFF=PASS`,
`MINIAS_A0_PIC_LA_GNU_DIFF=PASS`, and
`TOOLCHAIN_FOCUSED_T1=PASS`.

**Next live acceptance:** rebuild the same GNU-only MiniAS Linux Image,
test QEMU P0 and P1, and only then promote this particular source fix as
Linux-runtime proven. A green *single GNU head swap* establishes the
failing-owner boundary but alone cannot establish that the semantic
change repaired every other native assembly input.
