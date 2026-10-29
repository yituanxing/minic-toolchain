# MiniC architecture review — batch 22 (2026-10-29)

Scope: MiniC's C-to-assembly compiler, **not** the separate MiniAS or OS.
This batch deliberately follows batch 21's full-source inventory, removes
verified old/new dual paths, and changes the C/ABI contract at one source
boundary. It does not equate successful `-S` with GNU AS, ELF linking, or
Linux kernel boot.

## Confirmed structural changes

1. **One real `__builtin_va_list` C source object, one internal ABI state.**
   TargetInfo describes source shape only: SysV x86-64 is an array of one
   24-byte state record, AAPCS64 is one 32-byte record, and RV32/RV64/Loong
   keep their linear pointer representation. Each translation unit's Program
   owns one canonical hidden type identity. Parser, AST verifier and Core use
   that identity, including SysV array-parameter decay; Core's internal
   `VARIADIC_ARGUMENT_ADDRESS` / `VARIADIC_COPY` work on state addresses and
   do not synthesize a C-visible pointer proxy. Deleted the old x86-specific
   `MINIC_TYPE_BASE_X86_VA_LIST_CURSOR`, its `sizeof` exception, and both
   superseded source admission/copy policies. Preserved existing enum numeric
   values after removing the obsolete type.

2. **One AArch64 frame-address authority.** Two reachable assembler text
   postpasses (`copy_dynamic_frame_asm` and `legalize_large_sp_memory`) were
   removed, together with the 151-line `asm_legalize.{c,h}`, their Makefile
   and bootstrap registration, the per-function staging `tmpfile()` path,
   the Common post-emission callback slots and the unconstructed
   `MINIC_A64_FRAME_ASM_OPERAND` enum/case. Typed `FrameRef` still owns real
   offsets and live-mask scratch selection; truly physical live SP in
   `alloca`, calls, function entry/exit and source-owned hardware ASM remains.
   Before removal, the old rewrites had **zero** A/B byte differences in
   12 original Linux AArch feature fixtures, dynamic/overaligned frames,
   kexec crash-reg snapshots and large structured ASM.

3. **Six x86 source-qualified ASM families** (PortIO, AtomicRMW,
   RawCMPXCHG, TryCAS, Bitops and AtomicCC) now transmit target-owned
   `FormId + variant` through Core to an immutable machine contract;
   their previous overlapping machine string dispatch was deleted. AtomicCC
   preserves the special lock alternative / `.smp_locks` metadata. The
   ordered source table is now **21 typed / 88 legacy**, with 3 additional
   table-external qualification entrances; no unsupported family was erased.

4. **LoongArch synthetic GNU ASM result stores use typed scalar transport.**
   Migrated five existing result families, Linux packed `uaccess` carrier
   widths and guarded `+r` error ingress/egress, and PTE LL/SC, AMO fetch,
   xchg and CSR `+r` packed results. The source hardware `ld.b/h/w`,
   LL/SC, AMO, CSR and exception-table layout are not rewritten as C loads.
   Target ASM writes the *declared C output width* with proven alignment or
   a bytewise fallback; it cannot overwrite adjacent packed fields via an
   unconditional 8-byte `st.d`.

5. **Global declarations and tests follow genuine type semantics.** An
   array-typed tentative static global is accepted without `=` and retains
   its metadata. Old tests expecting `sizeof(x86 va_list)` to fail now assert
   GCC-compatible 24-byte objects. RV/Loong legacy pointer-cursor acceptance
   is tested separately from x86/AArch pointer-proxy rejection. The source
   review ledger no longer treats deleted AArch files as live inventory and
   the backend boundary gate explicitly requires no old rewrite callback.

## Exact local verification

At the integrated working tree, with a strict release compiler at
`build/review-batch20/bin/minic-cc`:

- `make -j8 MODE=release CFLAGS=-Werror BUILD_DIR=build/review-batch20 all`:
  **PASS**.
- `make -j4 MODE=release CFLAGS=-Werror BUILD_DIR=build/review-batch20
  check-fast`: **PASS** after updating stale proxy assumptions in test units.
- `bash tests/compiler/c0/probe-review-batch-2026-10-29.sh
  /absolute/path/to/minic-cc`: **PASS**, including all new x86, Loong,
  AArch FrameRef and real source `va_list` probes.
- `bash tests/compiler/c0/check-minic-target-boundaries.sh`: **PASS**, backend
  source inventory 206 files, zero missing/removed/invalid entries, 125
  **historically drifted source blobs** not falsely counted as deep-reviewed.
- x86 real GCC/MiniC runtime source `va_list`: **2/2 each PASS**, including
  24-byte arrays/embedded objects, `va_copy`, array-function-parameter
  adjustment and actual libc `vsnprintf`. AArch 32-byte source `-S` PASS;
  RV32/RV64/Loong pointer compatibility `-S` PASS. AArch QEMU NOT RUN.
- Loong three new packed ASM gates: five synthetic result families, four
  uaccess carrier forms and four atomic/CSR forms pass GNU C source,
  MiniC target `-S`, width/alignment checks and explicit negative cases.
  Loong QEMU NOT RUN.
- AArch typed-frame, overalign/alloca, kexec snapshot, large 45/640 structured
  ASM, x17-value-preserving scratch selection **PASS**; cross GNU AS/QEMU
  unavailable locally. x86 six typed families: true GNU/ELF tests and
  malformed form-ID/variant/template rejection **PASS**.

The previous remote batch 21 commit had exact-head native/strict gates green;
its Linux 21-shard discovery job was **skipped by the explicit campaign gate**.
Batch 22 requires new remote exact-head evidence after push. The 21-way GNU
Linux C census is independent of runtime GNU AS/link/Image/P1.

## Second audit: real remaining boundaries (not silently resolved)

| Priority | Evidence | Next treatment |
| --- | --- | --- |
| P0 | Valid Loong GNU packed output code still directly stores **13** synthetic result values in `add_unless`, `sub_positive`, normal/masked `cmpxchg`, `small_xchg` and two futex forms; each reproduced as `-S` | Batch 23 use the existing typed store helper in one sweep and preserve the LL/SC/faulting source memory effects |
| P1 | x86 still calls 83 textual classifiers 161 times in machine ASM; PCI MMCONFIG and 6 CPUID shapes have duplicate source/machine split, same `cpuid` text but different Core operand plan | Batch 23 `FormId+variant` for PCI and CPUID, with exact operand count/clobber/RBX-save verification and GNU native oracle |
| P1 | Core `VARIADIC_ARGUMENT_ADDRESS.copy_destination` has no remaining `true` producer, but AArch residual and Common profile retain old special branches | Retire the unused state and branch after exact-head acceptance |
| P1 | `MinicLocal` legacy array booleans/count and canonical `MinicArrayType` both remain authoritative for local arrays; `ast.c` branches between them | Design one typed local-array canonicalization and remove old compatibility fields with 5-target oracles |
| P1 | Frontend's general type enum still includes AArch NEON/SVE base kinds; TargetInfo embeds RISC-V ISA shape | Separate genuinely target-specific opaque type registry/feature descriptions from generic language type identity |
| P1 | Speculative `parser_attribute.c` shallow Parser copy can append to the shared Program AST; backend reachability bit changes Core scan policy by target | Reproduce ownership failure/measure orphan expressions and unify a documented transaction/emit boundary |
| P1 | AArch structured ASM currently rejects most forms in any dynamic alloca function, even those with owned slots safely represented by x19/FrameRef; `rbit` form reproduced | Narrow the source/target capability gate per physical-SP dependence, retaining strict liveness proof |
| P2 | Three inline specializers repeat AST expression-owner and call indexes; `tools/minic/main.c` still has intentionally RV64-only full link/CPP route | Do not mix optimization/Driver scope into this source/ABI contract batch |

Inventory: the compiler/driver audit initially recorded 336 tracked C/H
files; removal of the two old AArch source files leaves **334** on this
branch (`src/target` 206, other groups unchanged). The second-pass Compiler
and both CLI mains were re-read 13/13; additional reviewers inspected Core,
Frontend/Target, x86, RISC-V/Loong and AArch critical paths. A file inventory
is **not** a statement that every logical branch was proven or all 334 files
have a fresh deep-read receipt. Source blob drift remains explicit for the
next incremental audit.
