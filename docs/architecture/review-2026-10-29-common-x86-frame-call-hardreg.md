# MiniC Common/x86 frame, CALL and hard-register ownership review — 2026-10-29

## Scope and source coverage

This is a fourth pass following the **73/73 full-file Common+x86 audit**
(`review-2026-10-29-common-x86.md`) and the recursive SysV aggregate
consolidation (`review-2026-10-29-common-x86-third-pass.md`). This pass reads
the complete x86 frame/physical CALL/ABI implementation and the adjacent
Common planners again; relevant machine ASM entry and hard-register
preservation paths were independently traced through source and emission.

| Code read | Why it matters | Finding |
| --- | --- | --- |
| `x86_64/abi.c`, `abi.h`, `call_plan.c`, `call_plan.h` | Incoming/outgoing SysV/Win64 classification, ABI register inventory | Single classifier/planner consumed by ingress and CALL; Win64 extra callee-saved bank had duplicate magic register identities and extents |
| `x86_64/frame.c`, `frame.h`, `codegen.c` | Frame slots, varargs register bank, prologue/epilogue | Physical Win64 preservation and SysV register-save layout were independent hard-coded descriptions of ABI facts |
| `x86_64/machine_call.c`, `machine_emit.c`, `machine_support.c` | GP argument lifetime and return lowering, object copy, displacement | CALL planner owns location once; source record loads use caller-saved `%rax/%r10/%r11`, avoiding prepared GP arguments |
| `x86_64/machine_asm.c`, `machine_internal.h`, `asm_contract.c` | Hard-register/clobber parsing and instruction emission | Scratch `%rbx/%r12/%r13` is saved around examined machine forms, but source/machine template recognition still overlaps structurally |
| `common/direct_preflight.c`, `direct_call_plan.c`, `direct_lp64_abi.h`, `lp64_aggregate.c` | AArch64/LoongArch64 direct argument class, variadic decisions and frame facts | Shared algorithm with target-provided ABI flags, not evidence that AArch64/LoongArch64 are ABI-identical; conservatively fail closed |

The source-coverage inventory is about **actual file review**, not merely
listing paths. This round's targeted runtime evidence is separately noted
below. No other target, Frontend, Core, target registry, shared backend or
Makefile was modified.

## Verified P1: one typed physical ABI profile for Win64 nonvolatiles

Previously, three different places encoded `%rdi`, `%rsi`, `%xmm6..15`:

1. `frame.c` reserved `176` bytes and fixed offsets `+8` and `+32`;
2. `codegen.c` prologue emitted two string-literal GP saves and a loop of
   `10` XMM saves starting at index `6`;
3. `codegen.c` epilogue separately repeated the same physical inventory.

`MinicX86AbiRegisters` now owns `extra_nonvolatile_gp` and count, and the
first XMM index and count for the specific calling convention. The frame
size and placements, prologue save and epilogue restore all derive from
that single profile. Win64's actual saved bank remains **RDI/RSI +
XMM6..XMM15**, and the full save area remains **16 + 160 = 176 bytes**;
no caller/callee ABI was intentionally expanded.

The shared SysV argument-register profile now also supplies the GP bank
size used by x86 `__int128` ingress snapshots (6 × 8 = 48 bytes). It supplies
the GP/FP register-save region sizes and FP offset for `va_list`
(6 × 8 + 8 × 16 = 176 bytes). The source comment that called this an RV64
sequential cursor remains a deliberately negative cross-target distinction.

The redundant exported `minic_x86_64_align` wrapper was removed: its
callers use the already-existing `minic_core_frame_align_up` primitive.
This removes an API with zero x86-specific behavior; it does **not**
promote x86 frame layout into Core.

## Hard-register lifetime audit

- **Outgoing CALL (`machine_call.c:17–193`).** Each argument's final
  physical location is a planned `MinicX86ArgLocation`. Record memory
  transfers use `%r11` as object address and `%rax` as copy scratch.
  `__int128` uses `%r10/%r11` so earlier `%rdx`/other GP arguments are not
  clobbered. An indirect call loads its callable address into `%r11`
  **after** the argument marshalling. Large by-value aggregate copies
  adjust addresses for temporary `%rsp` reservation.
- **SysV argument ingress (`frame.c`, `codegen.c`, `machine_emit.c`).**
  Functions with int128 parameters snapshot the entire six-GP incoming
  argument bank before lowering any one parameter, avoiding later
  parameter clobber. Save count and frame extent now agree through ABI
  metadata.
- **Win64 called from SysV (`codegen.c`, native driver).** Win64 callee
  preserves the GP and XMM registers that SysV callees are allowed to
  overwrite. A GCC caller initializes RDI, RSI, XMM6 and **XMM15**;
  the MiniC Win64 callee invokes a deliberately clobbering SysV function.
  The native test verifies all four survive across the call. This tests
  both ends of the XMM save range rather than merely visually checking
  generated assembly.
- **GNU ASM physical specializations (`machine_asm.c`).** The identified
  `%rbx`/`%r12`/`%r13` scratch paths use corresponding push/pop or
  on-stack scratch store/restore pairs (not a global untracked reservation).
  No specific scratch lifetime regression was proven from this pass.

## Remaining architecture boundaries, explicitly not silently changed

| Priority | Location | Concern and next proof |
| --- | --- | --- |
| P1 architecture debt | `x86_64/asm_contract.c` → `x86_64/machine_asm.c` | Source GNU template recognized in policy, then partly parsed again at machine emission. Proper removal needs a target-owned typed *qualified machine-form identifier* carried through Common/Core structured ASM. Backend/Core are owned by other agents now; moving the same string checks into a new helper would not fix ownership. |
| P2 support frontier | `x86_64/abi.c`, `call_plan.c` | SysV classes outside conservative INTEGER 8/16 and supported SSE scalar cases need independent GNU caller/callee oracle; do not label a 16-byte mixed SSE record as 2-GP by guesswork. |
| P2 support frontier | `common/lp64_aggregate.c` | Cross-target packed/zero-width record classification is fail-closed. Its AArch64/LoongArch64 policy flags represent different registers and variadic conventions; using a shared function does not mean identical ABI facts. |
| P2 structural duplication | `common/global_emit.c` | Flat array/record and recursive initializer emission both remain because of partial sparse/trailing-zero semantics; safe deletion requires all-target GCC payload/relocation differential tests, not a textual deduplication. |
| P2 future support | `x86_64/abi.h` physical profile | Win64 profile currently deliberately represents the **extra** callee-saved bank beyond baseline SysV, not a universal caller-/callee-save abstraction. If another x86 ABI is added, split base+delta policy formally with ABI-native conformance tests. |

## Validation observed on this exact worktree

| Command (under `tests/compiler/c0/`) | Observed result |
| --- | --- |
| `make -j8 build/debug/bin/minic-cc` | PASS: full compiler build with default pedantic warning set |
| `cc -Iinclude -Isrc -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Werror -fsyntax-only` over `abi.c`, `frame.c`, `codegen.c` | PASS: strict `-Werror` for every changed production translation unit |
| `bash probe-x86-msabi-definition-native.sh` | PASS: GNU caller↔MiniC Win64 callee, six integer ingress, mixed FP, GP + **XMM6/XMM15** nonvolatile, four unsupported-negative gates |
| `bash probe-x86-msabi-scalar-native.sh` | PASS: six direct + six indirect calls; 32B shadow and stack/FP mixed placement |
| `bash run-x86_64-record-array-sret-sysv.sh` | PASS: GCC/MiniC bidirectional record ABI, by-value stack, hidden sret |
| `bash probe-x86-sysv-integer-record-tree.sh` | PASS: four GNU caller record shapes, 5/5 target `-S` |
| `bash run-x86-global-plan-abi.sh` | PASS: native and ELF size/reloc/section differential |
| `bash probe-gnu-variadic-single-scalar-record.sh` | PASS: 5/5 `-S`, x86 GCC/MiniC native, GP+FP overflow |
| `bash probe-gnu-variadic-restart.sh` | PASS (re-run after AArch64 owner synchronized its FrameRef shape assertion): five ISA, two independent AAPCS64 cursors, GCC/MiniC x86 native, five negative gates |
| `git diff --check` over all owned production/test diffs | PASS |

No commit or push from this agent. This round changes five production
files under `src/target/x86_64` and one existing native driver, plus
this review. Production change should be measured with `git diff --numstat`
at integration time because other agents concurrently modify the worktree.
