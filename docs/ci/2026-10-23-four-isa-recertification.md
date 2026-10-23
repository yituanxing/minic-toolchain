## Verified current-main receipts (2026-10-23)

- **AArch64 and LoongArch64: PASS** on source-identical post-merge MiniC:
  [focused T1 #39823172574](https://github.com/yituanxing/minic-toolchain/actions/runs/39823172574),
  commit `d438e1a02f15be0d3cc9f449cb7e9f0d8171d371`.
  `TOOLCHAIN_FOCUSED_T1=PASS mode=all independent_test_checks=9 shared_builds=1`.
  Mandatory upstream tinyAES, cJSON, zlib, Lua, SQLite and TinyCC functional
  differential succeeded. Original BusyBox 1.36.1 (upstream GNU Kconfig/Kbuild):
  **581/581 MiniC C owners**, **0 GNU C fallbacks**, SHA attestations, original
  GNU assembler and linker, and **38 GNU/QEMU byte-identical fixtures per ISA**.
  The reported `full_all_MiniC=NO` deliberately means the non-C assembler,
  linker, libc and toolchain are GNU; it does not invalidate full C ownership.
- **x86-64 six original software projects: PASS**:
  [#39823613104](https://github.com/yituanxing/minic-toolchain/actions/runs/39823613104),
  commit `bae9cdcd16d1df4abda1060444a84ec347d6c7f0`.
  `MINIC_X86_SOFTWARE_ALL=PASS projects=6`. Covers TinyAES, cJSON,
  zlib 15 TUs/minigzip cross-roundtrip, real native Lua 5.5.0 (33/33 assembled,
  interpreter and PIE stdout comparison; extra luac and PIE bytecode identical),
  SQLite 3.54.1 GNU differential and TinyCC 36 original compiler-suite cases.
- **x86-64 BusyBox: MIXED KBUILD PASS, NOT YET FULL C OWNERSHIP**:
  [#39824576716](https://github.com/yituanxing/minic-toolchain/actions/runs/39824576716),
  commit `b963668601f60c22771781d4bbe27d24dc1d73f9`.
  Real 957-option original source Kconfig, original GNU full binary and 6
  original-runtime oracles succeeded. **585 / 593 real GNU C owners** are
  MiniC-built, all SHA-verified, **8 GNU owner fallbacks**; original GNU vs
  MiniC-mixed Kbuild **11/11 runtime fixtures byte-identical**. One extra C
  translation unit beyond the 593 link-owner cohort was probed. Remaining
  `mkswap.o`, `top.o`, `verror_msg.o`, `devfsd.o`,
  `setfiles.o`, `tls_sp_c32.o`, `chcon.o`, and `hash_md5_sha.o`.
  The separately pinned 12-owner real Kbuild acceptance passes.
- **RV64 Linux 6.6.143: REQUALIFICATION NOT YET COMPLETE.**
  Keep using historical existing 7-way `gcc-shards` producing 295/295/295/
  295/295/295/294 = **2064 original C objects**. After retiring stale
  Runtime44 pre-refactor source-hash requirements and narrowly excluding
  timestamp-derived `init/version.o`/`vmlinux.o` in GNU preflight, run
  [#39823830749](https://github.com/yituanxing/minic-toolchain/actions/runs/39823830749)
  had **4/7 successful** shards and 3 still stuck on GNU RISC-V apt package
  installation at inspection time. A bounded apt retry was added at
  `47e19308528bb9c896989345f1755cc024a5e1ad`, triggering
  [#39825131467](https://github.com/yituanxing/minic-toolchain/actions/runs/39825131467).
  **Do not claim Linux 2064/2064/QEMU P1 accepted until the receiver's current
  run logs explicitly show SHA authenticated 2064-owner union, GNU link and
  QEMU P1 runtime PASS.**

### Next engineering gate

The other three ISA real-software acceptance proofs above are complete.
The remaining blockers are infrastructure completion of the 7-way RV64 Linux
runner+receiver and the **8 x86 BusyBox C owners** listed above; AArch64 and
LoongArch64 BusyBox **full C ownership is already done**. After full RV64 Linux
P1, target the eight x86 source errors as a single batch and standardize all
three BusyBox Kconfig/ABI/real runtime fixtures before the three new Linux
ports. Do not claim GCC/assembler/linker replacements without their own proof.

## Plan and preserved CI contracts

## Scope and acceptance (not yet certified)

The four-backend MiniC restructuring and Git branch cleanup have merged to
`main`. The previous x86/RV64/AArch64/LoongArch64 certifications are historical,
so this *new* run explicitly rechecks the current compiler binaries.

### Three existing and new owners, zero cloned Linux build recipes

1. **RV64 Linux, original 6.6.143**: reuse existing
   `linux-runtime-gcc-baseline.yml` `gcc-shards` matrix
   `[0,1,2,3,4,5,6]` plus `gcc-shard-verify` receiver. Assert complete
   canonical 2,064 real MiniC C objects, SHA-checked seven shard union, GNU
   assemble/link, and QEMU P1 syscall/runtime. The opt-in commit tag is `[linux-runtime-gcc-sharded]` with
   `MINIC_CURRENT_MAIN_SEVEN_RECHECK=1` to recertify current checked-in source
   (the original Runtime44 SHA manifest belongs to pre-refactor code).
2. **AArch64 + LoongArch64**: reuse `toolchain-focused-regressions-v1.yml`
   `mode=all` one-build regression, which includes real tinyAES, cJSON, zlib,
   Lua, SQLite, TinyCC, and original BusyBox 581/581 MiniC C owners with
   SHA verification and 38 GNU-QEMU byte-exact fixtures **per ISA**.
3. **x86-64**: `minic-capability-recertify.yml` verifies pinned unmodified
   AES, cJSON, zlib, Lua, SQLite and TinyCC against native GNU, plus reports
   real upstream BusyBox 12-owner and current all-eligible-owner mixed Kbuild
   acceptance separately. Historical x86 partial BusyBox success **does not**
   mean all BusyBox C owners are MiniC; GNU fallback remains a subsequent task.

All verdicts must be taken from the **same current main commit** or a follow-up
source-identical CI commit. A successful workflow dispatch alone is not an
acceptance receipt; inspect job status, SHA, object counts and actual QEMU
output. The Linux golden-source fixture/cache must fail closed if missing.

### CI routing after cleanup

- Main source changes: native strict fast ownership/x86 ABI + focused owner
  suite; only relevant source-path pushes trigger them.
- Heavy Linux 2064 seven-shard acceptance: explicit commit tag or manual opt-in
  to the existing Linux workflow, **not** routine pushes.
- Full x86 original-source suite: manual or scoped YAML-change push.
- Pinned A/B benchmark: manual only.
- Historical Linux/ASM/linker probes: preserve original reusable/manual
  owners and archived YAML while confirming which contracts are still needed.
- Do not introduce a second 2064-shard implementation or conflate historical
  CI PASS with current-commit acceptance.

### Next phase gate

After current RV64 Linux QEMU P1 and the other architectures' six real-software
tests pass, repair any residual BusyBox original-all-C-owner gaps on x86-64.
Then requalify original BusyBox AArch64 and LoongArch64 and begin the Linux
cross-target bring-up **without silently falling back to GCC for C ownership**.

## CI scope split after SysV ABI cleanup

`minic-capability-recertify.yml` has one automatically triggered x86
BusyBox/ABI job on x86 target and exact BusyBox probe edits. The separate six
real-software projects already passed in #39824576716 and remain selectable
on demand with workflow_dispatch `mode=software` or `mode=all`.
The RV64 Linux seven-shard workflow now listens to shared MiniC and RV64
backend owner paths rather than `src/**`; x86/AArch64/LoongArch64-only emitter
edits no longer instantiate an irrelevant **skipped** Linux job. The T1
multi-architecture workflow excludes pure x86 target edits, avoiding costly
repeat GNU/QEMU 581/581 acceptance on an ABI change that only affects x86;
main's x86 strict and original BusyBox gates remain in place. All shared
frontend, Core, compiler, target-common, and Linux-source path changes still
trigger the respective owner suites. No historical artifact/cache was deleted.

## 2026-10-23 x86 BusyBox ABI batch result

After commit `6d6400b`, the unchanged GNU BusyBox 1.36.1 all-eligible
upstream Kbuild passed its full and mixed runtime oracle with **587/593 C
owners from MiniC** and **6 GCC C fallbacks**. Two substantive x86 SysV gaps
are closed: the call to libc `div()` returning an 8-byte INTEGER-class
`div_t` aggregate via RAX and `service(struct stat)` carrying a >16-byte
MEMORY-class record by value on the stack. Dedicated GCC differential
`X86_SYSV_RECORD_ABI=PASS` validates both, and existing 11 original BusyBox
Kbuild fixtures remain byte-identical. Next distinct failure families are
`context_str()` const qualifier downgrade in two SELinux files,
`sepol_set_callback` callback format declarator, x86 asm clobbers, and Core
support for `cpuid` and variadic `bb_verror_msg`.

## Native x86 completion: 590/593 current BusyBox C owners

Canonical original BusyBox 1.36.1 GNU Kbuild/957-option current-main
acceptance [#39873618503](https://github.com/yituanxing/minic-toolchain/actions/runs/39873618503)
returned `success` on `ee4a83a5361f2ac0cd1697f74212dd97f8841e45`.

- `BUSYBOX_MIXED_BULK=PASS MiniC_owners=590 GCC_fallback=3 original_full_Kbuild=1 eleven_fixture_byte_exact=1`.
- `BUSYBOX_ALLYES_FRONTIER=DISCOVERY enabled_symbols=957 attempted_C_TUs=594 mini_objects=591 cpp_fail=0 minic_fail=3 asm_fail=0 gcc_fail=0 gcc_full_build=PASS`.
- Dedicated genuine executable differential: `X86_SYSV_RECORD_ABI=PASS`
  for libc `div_t` return in RAX and >16B struct by-value stack arguments.
  The GNU `const char *`→legacy `char *` assignment (diagnosed by GNU)
  and function-pointer format attribute also compile and run identically.
  Negative check `X86_GNU_CONST_DISCARD_NEGATIVE=PASS` rejects nested pointer
  and volatile qualifier drops.
- Source and linker provenance still correspond to original BusyBox.
  **Three** unresolved C owners remain (do not claim full MiniC BusyBox):
  `libbb/hash_md5_sha.c` / `cpuid()`: multi-output GNU inline asm;
  `libbb/verror_msg.c` / `bb_verror_msg()`: x86 va_list/Core lowering;
  `networking/tls_sp_c32.c`: GNU inline asm cc/dx clobber and mixed
  constraints. These need real Core→x86 instruction/ABI work, not preprocessor
  rewrites or pretending GNU fallback is MiniC.
- The current RV64 Linux seven-shard, GNU-link and QEMU P1 attestation is
  successful at [#39823830749](https://github.com/yituanxing/minic-toolchain/actions/runs/39823830749)
  and the retry [#39825131467](https://github.com/yituanxing/minic-toolchain/actions/runs/39825131467).
  This x86/frontend change still needs its full AArch64/LoongArch64 focused
  regression completion; the strict native gate is already green at
  [#39873618508](https://github.com/yituanxing/minic-toolchain/actions/runs/39873618508).

CI ownership: native x86 strict is routine; original full BusyBox 957-option
is selected for x86 target, parser statement/parameter declaration, or
existing BusyBox replay scripts; the six original x86 software projects
are opt-in since they already passed with unchanged source. Pure x86 ISA
changes do not trigger seven Linux shards or the two 581-owner new-ISA QEMU
regressions. Only `main` branch remains.

## 2026-10-23 SysV `va_copy` completion

The previous x86 `va_copy` implementation was intentionally rejected
because treating its va_list as an ordinary pointer would incorrectly alias
both cursors. The new Core `MINIC_CORE_INSTRUCTION_VARIADIC_COPY` represents
an ABI cursor state copy rather than a pointer assignment; the x86 emitter
allocates one dedicated frame tag per cloned cursor, copies the full 24-byte
SysV AMD64 state and independently advances both tags. The existing scalar
cursor assignment on RV64 and other targets is unchanged, and RV64 explicitly
rejects the newly appended effect opcode. Core verification, value-use
tracking, IR dump and strict native executable tests cover the new semantics.
The earlier fail-closed native oracle has been replaced with a GNU-vs-MiniC
positive runtime test of both independent 17/19 long arguments.

Accepted by original BusyBox 1.36.1 full Kbuild
[#39878415198](https://github.com/yituanxing/minic-toolchain/actions/runs/39878415198):
`BUSYBOX_MIXED_BULK=PASS MiniC_owners=591 GCC_fallback=2 original_full_Kbuild=1 eleven_fixture_byte_exact=1`.
The original complete enabled-option discovery produced
`attempted_C_TUs=594 mini_objects=592 minic_fail=2`.
Thus **591 of the 593 eligible Kbuild C owners** now truly build with MiniC,
with only two original GNU C owners remaining: `libbb/hash_md5_sha.c`'s
multi-register x86 `cpuid` inline asm and
`networking/tls_sp_c32.c`'s x86 condition-code and register clobber/operand
constraints. Do not describe this as full all-MiniC BusyBox acceptance.

The strict native source/runtime test
[#39878621773](https://github.com/yituanxing/minic-toolchain/actions/runs/39878621773)
passed at `db51789d772bd5c7255fe76c3e7329917ff6a0f6`.
The full four-backend focused regression for that source revision was still
running at the time of this entry; do not claim it passed until complete.
Only branch `main` exists.
