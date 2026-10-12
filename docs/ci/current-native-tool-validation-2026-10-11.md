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

## MiniAS-only Linux + QEMU P0/P1 runtime — CERTIFIED after NOPIC fix

The corrected MiniAS build, GNU differential micro oracles, complete Linux
6.6.143 Image and genuine guest P0/P1 **all passed at the same commit**:
[#38159723042](https://github.com/yituanxing/minic-toolchain/actions/runs/38159723042),
head `40f86d913d4bac2f96d6db5664058b48839f0b54`,
job **SUCCESS**.

- Single-substitution contract: **GNU GCC front end, GNU ar, GNU ld, GNU
  objcopy retained; only production GNU as replaced with native MiniAS**.
  The traced wrapper recorded 2,226 real assembler invocations,
  **2,197 native PASS**, 41 expected/probe FAIL records (these include
  exploratory Kbuild capability invocations), one delegated non-production
  assembler query. Seven additional **GNU shadow references** were generated
  for independent early-boot comparison without replacing any native
  production object.
- `Image`: **26,228,224 bytes**, SHA-256
  `9e14ff79eef744cc154eb6a3f46cf9d0351af5294abb30015f17803469ae5d59`,
  independently verified by hashing the downloaded CI artifact.
  Linux build reported **243 s** on one GitHub runner with `make -j4`;
  this runtime is runner-dependent and is not proof of a MiniAS performance
  optimization compared with earlier 386 s / 448 s builds.
- **QEMU P0 full: PASS** — `rdinit=/init` guest boot,
  `LINUX_RUNTIME_RDINIT_INIT=PASS`, real process/proc/sysfs/devtmpfs/tmpfs
  operations and shell markers; independent `rdinit=/bin/sh` second
  QEMU lane `LINUX_RUNTIME_RDINIT_SH=PASS`.
  `LINUX_RUNTIME_EXACT=PASS profile=full lanes=2/2`.
- **QEMU P1: PASS** — independent syscall lane
  `RUNTIME_V2_SYSCALL_END pass=12 fail=0` and
  `LINUX_RUNTIME_EXACT=PASS profile=p1 lanes=1/1`.
  `LINUX_RUNTIME_V2_PROFILE=PASS profile=p1 checks=22 fatal=0`.
  Final `MINIAS_LINUX_GCC_RUNTIME=PASS p0=PASS p1=12/12`.
- Actual QEMU logs for all three lanes contain `Linux version 6.6.143`,
  no Kernel panic/Oops/BUG, and clean guest powerdown.
- The early-boot incident now has a full closed proof chain:
  **unpatched MiniAS fails QEMU** ->
  **only GNU `arch/riscv/kernel/head.o` swap boots (~11 s; one GCC
  compile, eight AR and four LD messages)** ->
  **ELF comparison identifies incorrectly emitted GOT relocations**
  -> **fix GNU default NOPIC + scoped `.option`** ->
  **GNU-exact dedicated micro test and full Toolchain T1 PASS**
  -> **all-native MiniAS Image now boots and passes P0/P1**.

**Promotion boundary:** MiniAS is certified for **one-tool replacement on
full Linux 6.6.143 and independent genuine QEMU P0 two lanes + P1 12
syscall checks**. This is a stronger runtime oracle than the MiniObjcopy
one-lane FAST certificate, but **not** the separate private external
Python-era 145-module/203-function archive, which was not executed here.
No all-native multi-tool integrated build or self-hosting is implied.

## Linux multi-tool promotion: pending staged real executions

The completed independent certificates for MiniAS, MiniAR, MiniObjcopy and
MiniPP's frozen GCC-exact preprocessor inputs support a **staged production
matrix**, not an assertion that all tools have passed together. Before
running all native tools at once, exercise the previously archived
**three-final-link MiniLD + MiniObjcopy boundary** under GNU GCC/as/ar.
Retain the GNU reference path, capture the first failing native MiniLD
argv/output, and repair MiniLD before expanding replacements.
The GNU baseline remains the reference for every stage, and only a real
Image + QEMU oracle can promote a combination to runtime-certified.
The current run requests this previously defined mixed owner
`[miniobjcopy-linux-tool]`; no workflow or job is added.

## Same-tree three-native-tool integration: explicit opt-in real acceptance

The existing MiniAS focused owner now has `mode=linux-integrated`.
After its fully certified MiniAS-only Image and QEMU P0/P1, the **same
runner and same compiled native-MiniAS .o tree** switches only the Kbuild
`AR` and `OBJCOPY` command identities to MiniAR/MiniObjcopy, retains
GNU GCC+GNU LD, and re-archives/relinks. It forbids any further MiniAS
production assembler invocations in that phase, requires >50 native AR
calls and at least one native objcopy call, and executes the same full P0
two-lane and P1 12-syscall guest oracles. First-difference build/QEMU
logs and trace files are persisted with original Image SHA. The independent
single-tool paths remain unchanged, and this adds **zero** workflows
and **zero** jobs. The M0 invariant checker reconstructs the archived
GNU-only MiniAS job and checks the dedicated integration script by pinned
Git blob, rather than weakening or skipping the source audit.

[Structural M0 #38160776213](https://github.com/yituanxing/minic-toolchain/actions/runs/38160776213)
is **SUCCESS**. The next push triggers exactly one integrated mode
`[minias-linux-integrated]`. If any native component fails together,
report the first actual failing stage and do not call the combination
runtime-certified. MiniPP and MiniLD are still separately staged; neither
is silently claimed as part of this three-native-tool run.

## Stage III: reproducible native-final-link + MiniObjcopy Image

[#38160503540](https://github.com/yituanxing/minic-toolchain/actions/runs/38160503540)
**SUCCESS**: unchanged GNU GCC/as/ar, native MiniLD on **all three
late Linux vmlinux/kallsyms link outputs**, and native MiniObjcopy on
**44** real Kbuild calls. The full Linux 6.6.143 Image was generated
(22,033,608 bytes, SHA-256
`db7bb695bb9dcd5985a726e0ea3b5d54b32f01c195b6c91ee117bd146d999693`).
This is a **combined tool Image proof**, *not yet QEMU*.

To avoid a second large kernel compilation, the existing three-job
MiniObjcopy owner gained exact stored-artifact `linux-replay` mode.
It downloads the immutable original artifact by source run ID,
checks the original Image hash plus precisely 3 native MiniLD and 44
native MiniObjcopy trace entries, builds only the GNU runtime initramfs,
and boots that **exact restored kernel** through QEMU P0 (2 lanes) and
P1 (12 syscall tests). It never invokes Kbuild/compilation in replay
mode. The original single-tool and original mixed build modes remain
available; M0 source gate [#38161317571](https://github.com/yituanxing/minic-toolchain/actions/runs/38161317571)
confirmed the workflow still contains exactly the same three jobs.
The next tagged push `[miniobjcopy-linux-replay]` executes the cached
Image runtime check; it must not be assumed green before QEMU output.

A separate all-native stage of MiniAS+MiniAR+MiniObjcopy reused the
same MiniAS object tree; it **failed fast in ~6 s during EFI STUBCPY**,
not during GNU-only MiniAS boot: native MiniObjcopy rejected
`drivers/firmware/efi/libstub/efi-stub-helper.o`,
`efi-stub.o`, and `fdt.o` as `ELF rewrite failed: invalid-section`.
See [#38160817819](https://github.com/yituanxing/minic-toolchain/actions/runs/38160817819).
This is a concrete **cross-tool object-format compatibility frontier**.
The direct MiniAS Image before the second relink still passed P0/P1;
do not relabel this stage as integrated success. Exact failing objects
and ELF sections must be collected and compared against GNU objects
before touching code or proposing a GNU fallback.

## Cross-tool EFI STUBCPY compatibility evidence capture

The first MiniAS+MiniAR+MiniObjcopy same-tree relink
[#38160817819](https://github.com/yituanxing/minic-toolchain/actions/runs/38160817819)
is **FAIL** only after MiniAS alone built Linux and passed P0/P1.
The native combination completed hundreds of MiniAR re-archives, then
MiniObjcopy rejected three native MiniAS ET_REL objects from
`drivers/firmware/efi/libstub` with `invalid-section`. This is not
an assertion that MiniAS Linux runtime fails in isolation: its QEMU
result remains green at the identical source SHA.

The integrated relink harness now saves the exact 3 failed
`.o` files, ELF headers, section table and relocations on failure,
plus GNU-as same-source `.o` objects and the original temporary
assembly source, captured during initial GCC assembly. This is a
bounded diagnostic extension (still one canonical MiniAS workflow/job);
its source identities and archival first ten jobs passed M0
[#38161541049](https://github.com/yituanxing/minic-toolchain/actions/runs/38161541049).
The tagged replay `[minias-linux-integrated]` will classify the
MiniAS->MiniObjcopy interoperation failure from exact ELF evidence
before selecting the responsible native implementation. The separate
MiniLD-final QEMU archived-Image run is independent and does not
require a new kernel compilation.

## MiniLD final-link 3/3 — Image PASS but QEMU FAIL, actual ELF defect

The exact stored Image from the 3-MiniLD-final + 44-MiniObjcopy
full build was replayed **without any kernel recompilation** in
[#38161443134](https://github.com/yituanxing/minic-toolchain/actions/runs/38161443134).
Artifact identity and call provenance passed:
`MINILD_FINAL_IMAGE_REPLAY=PASS origin=38160503540
native_ld=3 native_objcopy=44 reused_full_image=1`.
**QEMU did not boot Linux**: only OpenSBI printed, no Linux banner,
`qemu_status=124` after 90 s, P1 not reached. This is a genuine final
link/runtime compatibility failure, not a GCC/MiniAS compile failure.
Do not promote this mixed native linker to runtime-certified.

The raw CI artifacts were downloaded and the actual ELF `vmlinux`
compared against the earlier GNU-LD + native-MiniObjcopy real
FAST-passing Image (both Linux 6.6.143):

- GNU linker `vmlinux`: **9 program headers**, including **7 PT_LOAD**;
  `Image` **22,033,920** bytes, QEMU PASS.
- MiniLD linker `vmlinux`: **10,279 program headers**, almost all
  independent `PT_LOAD`, many with `p_align=1`;
  `Image` **22,033,608** bytes, QEMU pre-banner FAIL.
- Both ELF entry addresses are `0xffffffff80000000` and ELF
  `.head.text` virtual placement is present; this alone does not
  establish correctness of the actual binary or relocations.
- MiniLD's own `linker/src/linker_core.inc`,
  `static_write_executable()` scripted branch, counts **one PT_LOAD
  per allocated input section** and then emits each verbatim with
  `p_align=1`; this is a concrete non-GNU executable layout defect
  at full-kernel scale. The GNU/Image binary bytes also differ over
  much of `.text`. Simply reducing `e_phnum` to 9 without
  reconstructing proper section/file/LMA grouping and relocation
  semantics is **not** a valid fix.

This narrows the remaining native full-link acceptance gate to the
**linker-script output class and PT_LOAD layout + relocation** path.
Repair against paired GNU output sections/addresses, and rerun this
same short QEMU artifact proof before declaring full-tool replacement.

## MiniAS → MiniObjcopy EFI STUBCPY invalid-section: isolated fix

The CI capture [#38161595104](https://github.com/yituanxing/minic-toolchain/actions/runs/38161595104)
yielded three real MiniAS-vs-GNU same-source ELF object pairs for Linux
EFI stub `efi-stub.o`, `efi-stub-helper.o`, and `fdt.o`.
Native MiniAS ET_REL objects were structurally readable by GNU
`readelf`. Real STUBCPY arguments include `--prefix-symbols=__efistub_`,
`--prefix-alloc-sections=.init`, and
`--remove-section=.note.gnu.property`.

**Root cause proved from ELF section offsets and code:** MiniObjcopy
`elf/src/rewrite.c` `minielf_rewrite()` increases the in-memory
`.strtab` section size while prefixing symbols. A later input-range
check erroneously asserts `old_section.offset + new_section.size <=
original_input_ELF_file_size`. This is impossible for sufficiently
large prefixed symbol tables, even though the enlarged section bytes
reside in the separately allocated `modified_data` buffer. For the
three actual objects the synthetic `.strtab` old file offsets +
expanded sizes were 5,897 > 5,584 bytes; 15,545 > 13,608 bytes;
and 11,681 > 10,376 bytes respectively. **GNU and MiniAS inputs
should not be conflated with invalid ELF**; the error is in native
MiniObjcopy's handling of synthetic buffers.

The fix checks original byte ranges only for unchanged sections,
validates synthetic buffer length against the rewritten section size,
and keeps output bounds validation. The new GNU differential
regression expands 128 symbols under the actual EFI STUBCPY triple
options for both RV32/64 and checks GNU-exact symbol inventory,
rewritten relocation and GNU-LD ET_REL consumption.

## MiniObjcopy cross-tool ELF regression — verified

The source fix in `elf/src/rewrite.c` preserves the original ELF input
range guards for unchanged sections but validates rewritten string table
buffers against their own enlarged allocated byte lengths, not against
the original ELF input EOF. This directly addresses the three archived
MiniAS-produced EFI `invalid-section` witnesses.

Real RV32/RV64 GNU difference tests in
[#38184224857](https://github.com/yituanxing/minic-toolchain/actions/runs/38184224857)
passed:
`MINIOBJCOPY_EFI_STRTAB_CASE=PASS bits=64 names=128 ...`,
`MINIOBJCOPY_EFI_STRTAB_CASE=PASS bits=32 names=128 ...`,
`MINIOBJCOPY_EFI_STRTAB=PASS` and the complete
`MINIOBJCOPY_STRIP_REGRESSIONS_V1=PASS`. Both native outputs remained
GNU-linker consumable, with GNU-exact `nm` symbol inventory.
This is the unit/differential gate; the actual Linux MiniAS+MiniAR+
MiniObjcopy integrated boot is only certified after its separately
triggered `[minias-linux-integrated]` complete Image/QEMU P0+P1 run.

## MiniAS + MiniAR + MiniObjcopy: native joint Image achieved; exact QEMU pending

The post-ELF-rewrite fix run
[#38184298699](https://github.com/yituanxing/minic-toolchain/actions/runs/38184298699)
reused the Linux tree previously built with native MiniAS. Its first
Image and QEMU P0/P1 **PASSED** MiniAS-only before promotion. In the
second Kbuild phase, **all 633 native MiniAR** operations and **all 44
native MiniObjcopy** operations completed, and the exact integrated
Image was emitted: **26,228,224 bytes**, SHA-256
`6b8ec7591c1e8270b4e1bdb301fccbec0352447c9c895df7e07156eed6a674d1`.
This conclusively removes the previously reproducible 3-file EFI
`invalid-section` failure.

The run's *job* is still marked FAILURE, because an overly strict
speed/incrementality assertion forbade **any** additional MiniAS
invocation. Changing the native AR/OBJCOPY Kbuild command identities
actually caused **88 extra native assembly invocations** (2,197 ->
2,285), not a 2,197-object full recompile. The complete linked Image
is preserved by the failure evidence uploader. Do not misreport this
as a native runtime PASS: the speed assertion aborted before integrated
QEMU. A subsequent failure diagnostic caused another temporary trace
increase, which is not part of the integrated Image.

The original three-job MiniObjcopy owner now has a **no-Linux-rebuild**
artifact replay: `[miniobjcopy-native-triple-replay]`. It downloads
this exact retained 3-tool Image, validates the Image SHA, native AR
and objcopy trace counts and genuine prior MiniAS QEMU evidence, builds
only a fresh GNU initramfs, then runs QEMU P0 (two lanes) and P1 (12
syscalls). The path has no native/GNU fallback into relinking and adds
no workflow and no job. [M0 #38184967137](https://github.com/yituanxing/minic-toolchain/actions/runs/38184967137)
passed. Only successful *guest output* promotes triple native use.

## CERTIFIED: GNU GCC/LD + MiniAS/MiniAR/MiniObjcopy triple-native Linux QEMU P0/P1

The **first full native 3-tool combination has passed actual guest runtime**.
The exact non-recompiled Image originated in the full Kbuild
[#38184298699](https://github.com/yituanxing/minic-toolchain/actions/runs/38184298699),
which executed MiniAS, MiniAR and MiniObjcopy while GNU GCC and GNU LD
were unchanged. It was recovered from the original signed CI artifact
and verified by SHA-256 before guest execution.

**Definitive QEMU replay:** [#38185012989](https://github.com/yituanxing/minic-toolchain/actions/runs/38185012989)
**SUCCESS**, run `MINIAS_MINIAR_MINIOBJCOPY_IMAGE_REPLAY=PASS`,
`MINIAS_MINIAR_MINIOBJCOPY_LINUX_RUNTIME=PASS p0=2/2 p1=12/12`.

Evidence and precise bounds:

- Genuine 6.6.143 production tool invocations:
  **2,197** native MiniAS initial passes + **88** incremental native
  MiniAS passes during command-identity change; **633** MiniAR AR calls
  and **44** MiniObjcopy calls. GNU GCC preprocessing/compilation and
  GNU LD linking were unchanged. Note: the artifact's MiniAS trace
  additionally records the later failed-job *diagnostic* replay;
  those later invocations are NOT the source of the certified Image.
- Image: **26,228,224 bytes**, SHA-256
  `6b8ec7591c1e8270b4e1bdb301fccbec0352447c9c895df7e07156eed6a674d1`,
  verified independently against the uploaded QEMU replay artifact.
- Genuine QEMU P0: `rdinit=/init` and `rdinit=/bin/sh`
  **2/2**, `LINUX_RUNTIME_EXACT=PASS profile=full lanes=2/2`.
- Genuine QEMU P1: **12/12** syscall probes,
  `LINUX_RUNTIME_V2_PROFILE=PASS profile=p1 checks=22 fatal=0`,
  `LINUX_RUNTIME_EXACT=PASS profile=p1 lanes=1/1`.
- Raw P0/P1 guest logs inspected independently: Linux version string,
  USER_SHELL_OK on init lanes, normal powerdown on all three,
  no kernel panic or Oops.
- The initial complete build job was marked **failure solely because
  an overly strict Kbuild-incrementality assertion disallowed even
  88 additional native assembly calls**. The Image and all three
  actual native tool stages completed successfully. The exact artifact
  replay, *not* a reinterpretation of the first job's red status,
  certifies the runtime. The incremental harness is now tightened to
  require `added_native_as <= 128` rather than zero, still rejecting
  a full kernel recompile. Source guard pins the script identity.
- No new CI workflow or job; existing original 3-job MiniObjcopy owner
  performed the artifact replay.

**Promotion:** native MiniAS + MiniAR + MiniObjcopy are jointly
certified to build and run Linux 6.6.143 in GNU GCC + GNU LD boundaries.
This is *not yet* GNU-independent full native toolchain, since MiniLD
final kernel linking still exhibits an independent pre-banner QEMU
failure, current MiniPP has only frozen GCC input exactness rather
than all native live-Kbuild replacement, and current MiniC still has
known full-link deficiencies. Proceed with isolated MiniLD final-layout
fix, then staged native compiler/PP composition; do not dilute this
certification's boundaries.

## MiniLD final-link follow-up: coalesced PT_LOAD, runtime blocker remains

The GNU-GCC/LD + three-native-tools certification remains the frozen
[#38185012989](https://github.com/yituanxing/minic-toolchain/actions/runs/38185012989).
**No MiniLD full-kernel QEMU promotion follows from this section.**

The genuine MiniLD-final vmlinux/Image artifact from
[#38160503540](https://github.com/yituanxing/minic-toolchain/actions/runs/38160503540)
was compared with the GNU-LD passing vmlinux/Image from
[#38156029304](https://github.com/yituanxing/minic-toolchain/actions/runs/38156029304).
Old MiniLD vmlinux: 10,279 individual PT_LOADs. GNU-LD: 7 PT_LOADs.
A standalone, no-rebuild ELF simulation merged only equal-permission,
adjacent file/VMA/LMA intervals, never crossing a NOBITS tail. The
10,279 segments became **40**, without changing any section contents,
section address, or relocation.

**Negative control:** converting the coalesced *old* vmlinux to a raw
Image yielded a **byte-identical** 22,033,608-byte Image, SHA-256
db7bb695bb9dcd5985a726e0ea3b5d54b32f01c195b6c91ee117bd146d999693,
relative to the original failing MiniLD image. Therefore segment-count
reduction *alone* cannot fix the previously observed raw Image QEMU
pre-banner failure. It is an ELF correctness/scalability repair,
**not yet a Linux runtime fix**.

MiniLD static_write_executable now coalesces segments only across
contiguous file/VMA/LMA intervals of equal ELF permissions with no
NOBITS tail, preserving each section's address, data and relocations:
[code 5dd28e9b](https://github.com/yituanxing/minic-toolchain/commit/5dd28e9b04d40efa44abcc8ebf9fd33fa6839197).
Existing script fixture now checks three PT_LOADs, joining adjacent
text inputs while keeping page-separated init text/data distinct:
[test f305a668](https://github.com/yituanxing/minic-toolchain/commit/f305a6680433a046b45065d8c28744b29fc79d39).
Existing one-job T1
[#38186085433](https://github.com/yituanxing/minic-toolchain/actions/runs/38186085433)
passed all eight owner checks including
MINILD_SCRIPT_A1=PASS ... loads=3. Existing MiniObjcopy canonical
[#38186085415](https://github.com/yituanxing/minic-toolchain/actions/runs/38186085415)
also passed. No workflow or job was added.

**Separate semantic gap:** GNU-LD emits allocated
.pecoff_edata_padding PROGBITS (0x120 bytes at 0xffffffff815034e0),
while MiniLD has no such section. Script output BYTE(0) and advancing
dot by ALIGN are not materialized in MiniLD's output section data.
Actual GNU Image ends at 0x1503600, MiniLD at 0x15034c8 (312 bytes
earlier). This is a reproducible ELF/script-output difference,
but **has not been shown to cause the pre-banner boot hang**.
The next gate is an isolated GNU-differential test for script-generated
output bytes and a targeted source fix, *before* another full Linux
rebuild or claiming QEMU success. Preserve the existing triple-native
certificate.

### Script-generated output-byte repair and differential witness

The omitted GNU-style generated output section has now been addressed
without changing compiler, assembler, archiver, or the three previously
certified native tool identities:

- [MiniLD patch b94444f](https://github.com/yituanxing/minic-toolchain/commit/b94444f35f33782e83779221c266bf895542d4ab)
  materializes script-only output sections containing BYTE and alignment
  fill, with a bounded record of explicit BYTE payloads. The new
  synthetic SHT_PROGBITS/SHF_ALLOC section participates in exact
  VMA/LMA/file layout and PT_LOAD writing.
- [GNU difference test 09ee098b](https://github.com/yituanxing/minic-toolchain/commit/09ee098b34e087ca63e17ed1cef126449b87eb2f)
  checks a generated BYTE(0x5a) + ALIGN(0x200) output against GNU ld,
  compares the full extracted **504 generated bytes** byte-for-byte,
  and asserts 4 total PT_LOADs in the small ET_EXEC fixture.
- The unchanged eight logical owners in the existing single-job
  [T1 run #38186460299](https://github.com/yituanxing/minic-toolchain/actions/runs/38186460299)
  finished SUCCESS with
  MINILD_SCRIPT_A1=PASS loads=4 generated=gnu-exact-504-bytes
  and TOOLCHAIN_FOCUSED_T1=PASS independent_test_checks=8.

This passes a **small genuine GNU differential**, not the Linux
Image boot test. Since Linux's own script has the observed omitted
.pecoff_edata_padding class, perform the existing mixed native
MiniLD-final + MiniObjcopy production Image test next. Full P0/P1 QEMU
promotion still requires replay of *that new exact Image*, with a
recorded origin run ID and SHA rather than the old failed Image.

### Latest repaired MiniLD Linux image: real QEMU FAIL and early CPU trace

Repaired native MiniLD final-3 + MiniObjcopy-44 Image build
[#38186542705](https://github.com/yituanxing/minic-toolchain/actions/runs/38186542705)
completed: 22,033,920 bytes, SHA-256
c2d9e69d00d93ab1938dc306390b15109cfb4c11d168585bc282fef24b767681.
It matches GNU Image *length*, not bytes. Exact retained-Image
QEMU replay [#38187952222](https://github.com/yituanxing/minic-toolchain/actions/runs/38187952222)
failed: only OpenSBI, 90s timeout, no Linux banner or P1.

The CPU trace
[#38188236424](https://github.com/yituanxing/minic-toolchain/actions/runs/38188236424)
confirmed entry into *actual* Linux S-mode:
pc=0x80200000 -> 0x802010d8 (_start_kernel) -> 0x80c05fec
(setup_vm) -> 0x80e18cfc (__pi_fdt32_ld).
The FDT walk had offsets progressing 0 to 1754; its reported total
size was 5799. CPU-instruction logging hit the 16-MiB cap after 6s
before reaching the failure: this is NOT proof of an infinite FDT loop.

The next targeted probe is a QEMU HMP monitor live PC/register snapshot
with no slow CPU instruction logging. This reuses the identical Image,
adds no workflow or job, and keeps the GNU-reference and
triple-native certification unchanged. Linux runtime remains FAIL
until genuine P0/P1 guest evidence exists.

### QEMU real-speed snapshot harness correction

[#38188821115](https://github.com/yituanxing/minic-toolchain/actions/runs/38188821115)
correctly kept the repaired Image source fixed, but its initial
HMP monitor command probe returned only the interactive '(qemu)'
prompt rather than CPU registers. That is a **diagnostic harness
defect**, not evidence of a new linker semantic failure.
To obtain unambiguous registers without slowing the guest,
the existing replay diagnostic now uses QMP capabilities plus
human-monitor-command(info registers) and captures the returned
machine-readable QMP response. The exact Linux Image and full
P0/P1 oracle remain unaffected.

### Decisive Linux runtime root-cause witness: two-byte __param misalignment

The QMP CPU snapshots in
[#38188937061](https://github.com/yituanxing/minic-toolchain/actions/runs/38188937061)
reached `panic()`/udelay: three CPU samples had PCs
ffffffff80942a70, ffffffff80942a70, ffffffff80942a6a.
Guest trap registers preserved `sepc=ffffffff80032de6`
(parse_args, instruction `lbu a4,0(a3)`),
`scause=0xd` (load page fault) and
`stval=ffff80e000600000`.
Therefore the earlier missing banner conceals an actual kernel
page fault, not an OpenSBI non-entry.

**Source/data/ELF three-way proof** on the immutable failing build
[#38186542705](https://github.com/yituanxing/minic-toolchain/actions/runs/38186542705):

- MiniLD's `__param` section begins at VMA ffffffff810d6460, but
  `__start___param` had VMA ffffffff810d645e (two bytes EARLIER).
- The *exact 8 bytes* read at that erroneous symbol location in the
  MiniLD vmlinux are **00 00 60 00 e0 80 ff ff**, yielding precisely
  the faulting pointer `ffff80e000600000`. Two bytes later the
  valid first pointer is `ffffffff80e00060`.
- The independently QEMU-passing GNU-linked vmlinux's
  `__start___param` equals its `__param` section VMA
  ffffffff810ba760, so it begins on the correct input record.
- `script_layout_process_output()` originally recorded output-section
  `dot` (and evaluated `__start___param = .`) BEFORE
  `script_layout_place_section()` aligned the actual input to 8 bytes.
  GNU ld aligns the output section before evaluating its initial symbol
  assignment. This accounts for the exact 2-byte discrepancy.

Repair [9b322b64](https://github.com/yituanxing/minic-toolchain/commit/9b322b64de32e0c60ad66155dccfd4de6b8df5c7)
performs the maximal participating-input alignment (when output VMA
is implicit) before evaluating script output symbols. A
[GNU differential fixture 6b87390](https://github.com/yituanxing/minic-toolchain/commit/6b87390facd0f5f49aa50241fff56caad65c6fb9)
constructs intentionally unaligned dot=0x11002 and an 8-byte-aligned
`__param`, asserting GNU-exact `__start___param`/stop addresses,
start=0x11008 and emitted section bytes.

**Classification**: deterministic linker source/ELF fault, not yet
runtime-certified fixed until a new real native-linked Image is built
and its SHA-pinned P0/P1 replay succeeds. Retain the separate
GNU-bound three-native Linux certification as before.

### 2026-10-12 __param GNU-differential gate: green; exact fresh Image required

The existing eight-owner single-job T1
[#38189242216](https://github.com/yituanxing/minic-toolchain/actions/runs/38189242216)
reported `TOOLCHAIN_FOCUSED_T1=PASS` with all 8 checks and the new
`MINILD_SCRIPT_A1=PASS ... param-start=gnu-exact-0x11008`.
Both symbol boundaries and all section bytes agree with GNU ld.
This proves the intentionally misaligned stand-alone ELF fixture,
not yet the full Linux image.

Next run the unchanged historical combined native MiniLD-final
(3 actual linker invocations) plus MiniObjcopy (44) Linux build as
an opt-in production oracle. Save SHA of the **fresh** Image and
require the real guest P0/P1 oracle on that new exact artifact.
No GNU fallback or preemptive runtime promotion.

### Full Linux relink after __start___param alignment fix: exact Image

[#38189325106](https://github.com/yituanxing/minic-toolchain/actions/runs/38189325106)
built Linux 6.6.143 with GNU GCC/as/ar and native MiniLD on
three final links, plus native MiniObjcopy on 44 Kbuild operations.
Image: 22,033,920 bytes, SHA-256
9707487fe26d2050b6eff442377c73a2439f78268c143f5e2afd8e96d27a518d.
Downloaded vmlinux verifies __param section VMA ==
__start___param == ffffffff810d6460, both start/end symbols
now section-relative (not ABS), and the first 64-bit table entry is
ffffffff80e00060. This is the direct corrected Linux witness for
the previously observed 2-byte offset and parse_args page fault.

This is not yet a QEMU boot PASS. The existing no-rebuild QEMU P0/P1
replay is pinned to this exact newly generated Image, with the full
90-second per-lane timeout and retained on-failure QMP snapshots.
The separately certified triple MiniAS+MiniAR+MiniObjcopy GNU-LD
baseline remains unchanged.

## CERTIFIED: Native MiniLD three-final + MiniObjcopy Linux 6.6.143 QEMU P0/P1

The previously blocking native MiniLD final kernel link is now **actually
runtime-certified** in its exact GNU GCC/as/ar + MiniLD + MiniObjcopy
boundary. This is a distinct two-native-tool certificate, not the
still-untried four-native combination.

- Full Linux Image producer:
  [#38189325106](https://github.com/yituanxing/minic-toolchain/actions/runs/38189325106)
  SUCCESS, exactly **3 MiniLD late Linux final-link calls** and
  **44 MiniObjcopy calls**; Image 22,033,920 bytes,
  SHA-256
  `9707487fe26d2050b6eff442377c73a2439f78268c143f5e2afd8e96d27a518d`.
  GNU GCC, GNU AS, and GNU AR remained unchanged at all other
  Kbuild call sites.
- Immutable, zero-kernel-recompile exact Image QEMU replay:
  [#38189858163](https://github.com/yituanxing/minic-toolchain/actions/runs/38189858163)
  SUCCESS. `MINILD_FINAL_IMAGE_REPLAY=PASS origin=38189325106 native_ld=3 native_objcopy=44 reused_full_image=1`.
- Genuine P0 userspace and shell: `LINUX_RUNTIME_RDINIT_INIT=PASS`,
  `LINUX_RUNTIME_RDINIT_SH=PASS`,
  `LINUX_RUNTIME_EXACT=PASS profile=full lanes=2/2`.
- Genuine P1: `LINUX_RUNTIME_EXACT=PASS profile=p1 lanes=1/1`,
  `LINUX_RUNTIME_V2_PROFILE=PASS profile=p1 checks=22 fatal=0`,
  final
  `MINILD_FINAL_MINIOBJCOPY_LINUX_RUNTIME=PASS p0=2/2 p1=12/12 origin=38189325106`.
- Independently extracted all three QEMU guest logs from artifact
  `11721581149`; each has a genuine `Linux version 6.6.143`
  banner and normal `reboot: Power down`, without Kernel panic or
  Oops; P1 includes the syscall PASS markers.
  The guest-executed Image was SHA-256-checked against the producer.
- The successful M0 structural gate
  [#38189858120](https://github.com/yituanxing/minic-toolchain/actions/runs/38189858120)
  proves the source-trigger manifest is consistent; no new
  Workflow or Job was introduced.

**Root-cause closure:** The old MiniLD Linux linked
`__start___param=ffffffff810d645e` even though the `__param`
array began at `ffffffff810d6460`. That two-byte misalignment
assembled a malformed first pointer `ffff80e000600000`, matching
the exact QMP-observed load page fault
`parse_args+0xd0; scause=0xd; stval=ffff80e000600000`,
then entered `panic()/udelay`. The output-section prealignment fix
made `__start___param == __param == ffffffff810d6460`,
fixed the actual first pointer and now booted the exact same Linux
6.6.143 test profile successfully.

**Next (not certified yet):** combine the separately passing
MiniAS+MiniAR+MiniObjcopy native triple (GNU LD) with the newly passing
MiniLD-final (GNU AS/AR), using native 4-tool same-tree relink and
the identical QEMU P0/P1 oracle. MiniPP and MiniC remain distinct
uncompleted frontiers; do not represent this certificate as a
fully GNU-independent toolchain.

### Native-four isolated opt-in, preserving the two independently certified paths

The next stage reuses the already-QEMU-certified MiniAS+MiniAR+
MiniObjcopy same-tree Linux object graph. Only after *triple* P0/P1
has passed, a new optional **third phase inside the same existing
linux-single job** reruns Kbuild with MiniLD replacing **exactly the
three final vmlinux/kallsyms links**. GNU GCC stays pinned and all
other GNU LD link stages remain GNU. The wrapper records every
GNU/native linker invocation, requires precisely three native final
links, snapshots both triple- and four-native Image SHA-256s, and
runs the same P0/P1 guest oracle on the resulting four-native Image.
The preexisting MiniAS triple-native test remains unchanged when the
opt-in is absent. No Workflow or Job is added.

Trigger is the existing MiniAS focused historical tag route:
`[minias-linux-integrated] [minias-linux-four]`, which selects its
existing `linux-integrated` reusable workflow mode and activates the
add-on via the extra tag; no extra router/job/mode was created.
Both workflow source SHA and approved same-tree linker script SHA
are guarded by existing structural CI. Runtime is not certified until
this particular new producer/test completes successfully.

### Four-native M0 routing correction

The original MiniAS reusable workflow is a pure input-selected owner.
The M0 routing audit correctly forbids it from inspecting push commit
tags directly. The additional four-native substage is therefore gated
only by its existing `linux-integrated` mode (which already requires an
explicit historical opt-in push or manual action). Inside this mode the
first triple-native P0/P1 certificate is mandatory, then MiniLD final-3
is added in the same job. No head-commit tag logic remains in the
reusable workflow, the existing tag router is unmodified, no new mode,
job, or workflow was introduced, and the original linux-single path
is unaffected. The integrated script retains an explicit optional
`MINI_TOOLS_NATIVE_LD=false` override for standalone diagnostics.

## CERTIFIED: four native tools in one Linux 6.6.143 Kbuild tree

**State: genuine Linux/QEMU P0 + P1 runtime PASS.**

Historical opt-in run
[#38190157006](https://github.com/yituanxing/minic-toolchain/actions/runs/38190157006)
on commit `d6e393f402fb725da49b3a98be5700a8379cc33f` completed SUCCESS.
The single existing MiniAS `linux-single` job first compiled Linux with
GNU GCC frontend + native MiniAS, then incrementally rebuilt in the
same tree with native MiniAR and MiniObjcopy while holding GNU LD,
and finally added native MiniLD **only for the three final
vmlinux/kallsyms links**. This was real code execution, not a
manifest-only or image-existence claim.

| Stage | Production calls / result | QEMU |
|---|---|---|
| GNU GCC + MiniAS, GNU AR/LD/objcopy | 2,198 native assemble passes; full Image in 322s | P0 and P1 12/12 PASS |
| + MiniAR + MiniObjcopy, GNU LD | 88 extra native assemble passes; 615 native AR, 42 native Objcopy; 10s incremental relink | P0 and P1 12/12 PASS |
| + MiniLD final three, other GNU LD stages retained | 89 additional native assemble passes, 11 additional MiniAR, 6 additional MiniObjcopy, exactly 3 MiniLD final links; 15s incremental relink | **P0 and P1 12/12 PASS** |

The final four-native Linux Image is **26,228,224 bytes** with
SHA-256 `407310df63a604c18a30dc5c483fecfa93ec72ea29a5aaa76845dd6fd63bc05d`.
Its SHA was recomputed independently on the preserved artifact
`minias-linux-gcc-single-cert` (artifact ID `11720693250`)
and exactly matches `native-four/Image.sha256`.

All **three** final-image QEMU guest logs were separately inspected
from that artifact: `native-four/qemu-p0/rdinit-init.log`,
`native-four/qemu-p0/rdinit-sh.log`, and
`native-four/qemu-p1/rdinit-init.log`. All report
`Linux version 6.6.143` and `reboot: Power down` without
Kernel panic or Oops. The /init lane contains `USER_SHELL_OK`,
`DONE_COMMANDS`, and `MINIC_LINUX_RUNTIME_INIT_END fail=0`;
the /bin/sh lane includes `RDINIT_SH_OK`; P1 explicitly contains
`RUNTIME_V2_SYSCALL_END pass=12 fail=0` and
`INIT_RUNTIME_PROBE=PASS`.

The run's final acceptance output is exact:
`NATIVE_FOUR_LINUX_RUNTIME=PASS components=MiniAS,MiniAR,MiniLD-final,MiniObjcopy gcc=GNU p0=2/2 p1=12/12`.
The original producer mode is intentionally still GNU GCC and retains
GNU LD in non-final link steps; it is **not** full native MiniC,
MiniPP or all-GNU-LD replacement. This is a legitimate **four native
tools together in one real Linux runtime** certificate, but not a
fully-native compiler toolchain or a bootstrap certificate.

Subsequent source-matching/routing follow-up commit
`9a32341e7c3b82b6bae605dbd0397416e346bbf3`
passed the existing M0 structural check
[#38190275151](https://github.com/yituanxing/minic-toolchain/actions/runs/38190275151);
no new workflow/job was created. The runtime-certified run uses the
slightly earlier tagged commit above, and the later change avoids a
commit-tag check inside the reusable workflow; it does not retroactively
change the certified producer evidence.

**Next separate gates**: verify MiniPP as a direct preprocessor
replacement with fixed GNU compilation and these native tools; finish
the MiniC compiler full-object runtime frontier; then a fully native
combined build and compiler/tool self-hosting/bootstrap tests.

### MiniPP true production-substitution frontier (two Linux TUs)

After the four-native Linux P0/P1 certificate, the same existing
MiniAS integrated job now attempts a *separate, fail-closed*
MiniPP producer step on `init/main.c` and `kernel/configs.c`.
These were exact in the previous live-GCC textual oracle, but
that historical wrapper delegated the actual GCC C compilation.
The new explicit `MINIPP_LINUX_SUBSTITUTE=true` mode changes this:
MiniPP produces the exact `.i`, GCC independently checks byte
equality and generates dependency metadata, and **only the
MiniPP-generated `.i` reaches GNU cc1 through
`-x cpp-output -fpreprocessed`**. Any PP error/diff fails;
no fallthrough to GNU preprocessing.

The producer commands come from the existing GNU-GCC Kbuild
`.o.cmd` files and modify only two objects, not the global
Kbuild CC identity; existing MiniAS/MiniAR/MiniLD-final/MiniObjcopy
then rearchive and relink incrementally before QEMU P0/P1.
Trace requires exactly two `SUBSTITUTE` markers, two GNU-exact
outputs, three native final links, and P0/P1 12/12. This is
not a full 3352-TU MiniPP replacement and is not certified
until a real run returns PASS. Existing four-native and
GNU-GCC runtime certificates stay immutable.

### 2026-10-12 first MiniPP producer frontier: REAL failure isolated

The initial integration run
[#38191495016](https://github.com/yituanxing/minic-toolchain/actions/runs/38191495016)
re-proved the four-native Linux chain P0 2/2 and P1 12/12
in the same full build and entered the selected
`init/main.c` MiniPP production compilation.
It did **not** pass native MiniPP production: the MiniAS wrapper
required the existing `MINIAS_GNU_ROOT` early-object shadow
reference directory, but this new script step had not exported it
into the later stage's shell environment. This was a diagnostic
harness omission, **not evidence of a MiniPP textual difference**.

The repair explicitly exports the existing shadow reference root
and tightens provenance: before the MiniPP-produced objects are
re-archived and re-linked, record their full SHA-256, size and
nanosecond modification times; after Kbuild finishes, demand
all three properties of both objects remain identical.
That prevents falsely certifying a MiniPP-sourced object if Kbuild
silently overwrites it with GCC/GNU-preprocessed output.

The new replay remains a fresh same-tree 4-native baseline followed
by exactly-two MiniPP replacement objects and bounded QEMU P0/P1.
The known-good four-native certificate remains unchanged.

### Native MiniPP selector correction: fail closed

The first attempt on run 38191495016 preserved four-native P0/P1
but never actually selected MiniPP. Kbuild's saved GCC source path was
absolute, whereas the preprocessor wrapper normalizes source names to
Linux-relative paths BEFORE checking its selection list. That run's
preserved artifact 11720884498 contains an **empty** MiniPP trace.
Its MiniAS failure therefore did not establish any MiniPP correctness
conclusion. The GNU shadow reference root is now explicitly exported
and the selector lists exactly normalized paths `init/main.c` and
`kernel/configs.c`. Two `SUBSTITUTE` traces are mandatory;
silent fallback is forbidden. Pre/post-Kbuild object bytes and
mtime_ns are compared to prevent a shadow GNU replacement.

## CERTIFIED: MiniPP native production for two Linux 6.6.143 TUs + four native tools

**Real Linux QEMU P0/P1 PASS; fifth native tool boundary reached for an explicitly selected two-TU subset.**

Historical opt-in run
[#38192265904](https://github.com/yituanxing/minic-toolchain/actions/runs/38192265904)
at exact Git head `5551a7a0480963388ad5d0aa02c17dd735352f11`
completed **SUCCESS**. The existing MiniAS `linux-single` job
first re-certified same-tree MiniAS+MiniAR+MiniObjcopy with GNU LD,
then added MiniLD to exactly three final vmlinux/kallsyms links.
Each stage passed the genuine Linux/QEMU P0 and P1 test profile.

The new final stage substituted **MiniPP as the real source of C
preprocessed bytes** into GNU cc1 (`-x cpp-output -fpreprocessed`)
for exactly two Kbuild source/object pairs:
`init/main.c` → `init/main.o` and
`kernel/configs.c` → `kernel/configs.o`.
Both MiniPP outputs were byte-identical to the matching GCC
reference (2 `RESULT status=EXACT`) and, more importantly,
the production trace contains **exactly 2** `SUBSTITUTE` records,
showing GNU-cc1 consumed MiniPP's `.i` files. No GNU preprocessing
fallback was permitted on the selected objects.
GNU GCC still supplied the predefine oracle, dependency metadata,
and C frontend.

The preserved SHA-256 + size + `mtime_ns` identity of both resulting
`.o` files was checked across the subsequent Kbuild incremental
re-archive/relink (2/2 untouched). The follow-up build added 52
MiniAS assembly passes, used native MiniAR/MiniObjcopy, and had
exactly 3 final `minild` calls; the three non-final GNU LD calls
were separately traced. Incremental stage duration was **18s**.
The final real Linux Image is **26,228,224 bytes**, SHA-256
`20094fbca72522b9e3122278c819f12c5027048cdd2595d67c26e0c3e2cb8cc8`.

Independent artifact audit:
`minias-linux-gcc-single-cert`, ID `11721244957`
([original workflow artifact](https://github.com/yituanxing/minic-toolchain/actions/runs/38192265904/artifacts/11721244957)).
The Linux `out/arch/riscv/boot/Image` was actually extracted and hashed,
matching `native-pp-two/Image.sha256`. All three actual native-PP
QEMU logs were independently opened and checked:
`native-pp-two/qemu-p0/rdinit-init.log`,
`native-pp-two/qemu-p0/rdinit-sh.log`, and
`native-pp-two/qemu-p1/rdinit-init.log`.
Each has `Linux version 6.6.143`, an orderly
`reboot: Power down`, no kernel panic or Oops.
P0 contains `USER_SHELL_OK`, `RDINIT_SH_OK`, and
`MINIC_LINUX_RUNTIME_INIT_END fail=0` across the two lanes;
P1 contains `RUNTIME_V2_SYSCALL_END pass=12 fail=0`.
The job acceptance marker is
`MINIPP_NATIVE_TWO_LINUX_RUNTIME=PASS pp=MiniPP-exact-two gcc=GNU as=MiniAS ar=MiniAR ld=MiniLD-final objcopy=MiniObjcopy p0=2/2 p1=12/12`.

**Scope limit:** this is a five-tool combination with MiniPP replacing
**exactly two** production translation units; GNU GCC remains the C
compiler, GNU LD remains for non-final link stages, and the other
Linux TUs retain GNU preprocessing. It is **not yet** all-Linux
MiniPP substitution, full MiniC runtime, full native linking, or
self-hosting/bootstrap certification. The prior four-native
certificate remains independently valid.
