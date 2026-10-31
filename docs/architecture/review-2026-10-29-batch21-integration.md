# MiniC architecture review — batch 21 (2026-10-29)

This review covers **MiniC's C-to-assembly compiler**. It does not claim GNU
assembler/linker, kernel boot, or execution validation for cross targets.
The batch follows full compiler inventory and parallel, source-level reviews
of Core, target registration/source policy, x86/Common, AArch64, RISC-V, and
LoongArch64. Tests accompany changes to production contracts.

The checked C/H source inventory is: `src/core` 45, `src/frontend` 70,
`src/compiler` 11, `src/target` 208, and the two driver mains 2, or **336
compiler/driver C and header files**. Audit ledgers were partitioned among
agents and reviewed by the integration owner. This is file-level coverage;
concurrent changes still need incremental review, and it is not a claim that
all logical paths have been proven correct.

## Integrated architecture corrections

| Area | Before | Batch 21 | Remaining boundary |
| --- | --- | --- | --- |
| Core control-flow provenance | Duplicated IF/source subtree membership walks in static and dynamic reentry paths | One cycle-aware `SourceSubtreeMembership` authority, fail-closed on invalid/unknown state | Keep strengthening the full Parser→AST→Core ownership audit |
| x86-64 Structured GNU asm | Source qualifiers and machine emission separately classified textual templates | 15 of the now 108 ordered source qualifiers carry a target-owned `FormId`/variant across Core, including MXCSR and Unwind; machine revalidates immutable forms before emission | 93 still use legacy dispatch, plus four additional table-external qualifier paths |
| AArch64 stack | Two frame addressing styles, including static raw `[sp,#...]` text | All 76 audited static frame-reference sites use typed `FrameRef`; variadic incoming addresses use saved entry SP **plus consumed named stack slots**; fixed GP/FP offsets come from the one incoming ABI planner rather than C parameter counts | Two reachable text postpasses and broader variadic source-type semantics remain |
| RV32/RV64 scalar memory | Multiple inconsistent native/bytewise transports, some unaligned scalar/overflow and Structured ASM `+r`/`=r` writes | Shared `typed_memory.h` selects proven-aligned native load/store or guarded bytewise fallback; ASM synthesized ingress/egress share it; RV64 ASM planner reserves frame scratch to prevent large-frame live-operand corruption | RV GNU cross-assembler/runner verification unavailable locally |
| LoongArch LP64D varargs/ASM stores | Anonymous 128-bit values used consecutive rather than even-aligned GP registers; FPR/per-CPU ASM lvalue outputs bypassed packed-alignment guard | Target-owned unnamed-wide-register-pair policy in shared planner; Core and target-qualified ASM result writes share Loong scalar store transport | Remaining qualified ASM result families need individual reachability/equivalence audit |
| Record/data layout | Certain packed/aligned members had inconsistent record placement and `__alignof__` results | Shared effective-field-alignment logic for both paths | Full bitfield / transparent-union survey continues |
| `va_list` source policy | Architecture-dependent source checks in generic TargetInfo | Source ABI checks owned by target source policies | **P0:** x86 global `va_list` storage and builtin argument identity still inconsistent; see below |

This deliberately retains all reachable x86 machine forms. Removing a
classification function merely because another typed path exists would drop
Linux-specific atomic, privileged, ELF or exception-table semantics.

## New unresolved P0: builtin variadic source identity

- On x86-64, GCC emits 24 bytes for an external `typedef __builtin_va_list
  va_list; va_list global_cursor;`, while MiniC currently emits **8**. The
  local repro lives under `build/debug/tests/va_cursor_probe_x86_global.*`.
- MiniC currently accepts ordinary pointer arguments to `__builtin_va_start`
  and `__builtin_va_arg` on x86/AArch64 in source-to-assembly mode, where GCC
  rejects the source.
- The generic pointer-like Core cursor is useful **internally** but must not
  substitute for the source-language `va_list` type, object size, assignment
  and aggregate semantics. The next batch should unify target-owned cursor
  source identity, Parser/AST admission, Core transport and machine state.

No attempt to conceal these mismatches as a successful cross-target runtime
test has been made.

## Local verification (exact integrated working tree, before commit)

- Complete strict release build: `make -j8 MODE=release CFLAGS=-Werror
  BUILD_DIR=build/review-batch20 all` — **PASS**.
- Fast regression suite: `make -j4 MODE=release CFLAGS=-Werror
  BUILD_DIR=build/review-batch20 check-fast` — **PASS**.
- `bash tests/compiler/c0/probe-review-batch-2026-10-29.sh
  build/review-batch20/bin/minic-cc` — **PASS**, including new Core membership,
  x86 typed ASM/atomic/MXCSR/Unwind differentials, RV32/RV64 packed
  alignment and Structured ASM ingress/egress plus 8 KiB frame regression,
  Loong varargs and guarded qualified ASM writeback, AArch64 incoming
  variadic stack and ABI-register allocation for named records, and layout
  probes.
- `bash tests/compiler/c0/check-minic-target-boundaries.sh` — **PASS**; the
  existing ledger still reports stale historical inventory entries, not a
  certified exhaustive audit of every recorded blob.
- `git diff --cached --check` — **PASS**.
- For RISC-V/AArch64/LoongArch64, relevant source-to-assembly and static
  assembly checks passed; target-native GNU as/QEMU execution was **NOT RUN**
  where no corresponding toolchain was installed.
- LoongArch64's existing Core admission rejection for checked overflow is
  asserted as an **expected negative**, not incorrectly counted as a PASS.

The preceding remote branch head `082856a15733c79e7f4fe383e99fe2a0f22cde3f`
had an independently green exact-head Linux 6.6.143 seven-way census:
21/21 shards and 9,559/9,559 C→S units. **Batch 21 needs its own remote
exact-head result after publication**; the previous run is not evidence for
this changed tree.

## Change size and follow-up priority

The integrated batch changes 73 compiler/test/ledger files, roughly 3.1k
insertions and 850 deletions. Treat this as a reproducible source statistic
rather than a lines-of-work quota; the final commit stat is authoritative.

1. P0: repair builtin variadic source type/object/storage semantics across
   Parser, AST, Core and target policies, keeping internal cursor proxy
   explicit and separate.
2. P1: migrate the remaining 93 legacy x86 source qualifiers in ABI-meaningful
   groups with native and Linux probes, not by blindly removing live machine
   operations. MXCSR and Unwind ownership are now target-qualified.
3. P1: retire verified-duplicated AArch64 text postpasses and other residual
   target-local memory paths (notably Loong SSA address-alignment visitor),
   only after positive and negative equivalence tests.
4. Continue exhaustive cross-file review for unused old implementations,
   target knowledge in Frontend/Driver and source/IR/backend boundaries.

## Further review findings, not yet resolved in this batch

- Core is not a standalone typed Module: `target/backend.h` still hands
  backends both the Frontend `MinicC0Program` and Core functions, indirect
  call signatures use `function_type_id`, and ABI code queries Frontend
  records. A future type-pool/`CoreModule` design must make DataLayout and
  canonical record identities available without new target-specific
  Frontend access. This is an architectural **P1**, not a reason to split
  files prematurely.
- Inline-specialization clones still share some mutable AST body/call
  metadata while Core selects clones using call-site facts. Existing
  positive native probes pass; competing-clone negative/differential
  examples are needed before replacing this two-authority model.
- LoongArch's target-local recursive address-alignment proof and Core's
  newer shared proof traverse similar SSA provenance with different depth
  limits and global-object trust rules. Consolidate the traversal only after
  isolating target-owned root alignment policy; do not silently replace
  its global symbol behavior with RISC-V's conservative global rule.
- GNU `va_list` array/struct source assignment is also misrepresented:
  x86 `va_list a,b; a=b;` is rejected by host GCC but admitted by MiniC;
  AArch64 currently lowers the same statement as an eight-byte pointer
  copy rather than the 32-byte value. Target-owned CursorTypeIdentity
  must govern ordinary object semantics as well as builtin admission.
- This ABI cursor mismatch is recursive and observable in real code: an
  x86 `va_list[2]` receives 16 instead of 48 bytes; a record embedding a
  `va_list` receives 16 instead of 32; valid `&ap` source semantics give
  a GNU-native return code of 42 and a MiniC-native return code of 1.
  Fix source-level array/record/address semantics rather than adjusting
  a pointer proxy's `sizeof` in isolation.
