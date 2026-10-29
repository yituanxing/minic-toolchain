# MiniC AArch64 backend architecture audit — 2026-10-29

Scope: `src/target/aarch64/` contains **24 C/H files, 11,080 physical lines**.
This is an AArch64-owned audit, not a whole-compiler sign-off. The existing
LP64/Common contract and runtime behavior must be re-verified after each batch.

## Source-reading ledger

`Full` means the full file was inspected in this review, including the
Linux-specific source-side qualifier and target-side machine-emission bodies.

| File | Read status | Main architectural concern |
| --- | --- | --- |
| `abi.c` | Full | Private AAPCS64 classification ownership |
| `abi.h` | Full | Contract must stay target ABI-specific |
| `asm_contract.c` | Full | 7,404-line catalog couples source GNU constraint admission, exact Linux templates, opcode selection, machine output; hundreds of raw SP accesses |
| `asm_contract.h` | Full | Narrow exported source/machine contract |
| `asm_legalize.c` | Full | Active whole-output `fgets`/`sscanf` textual repair, implicit x17 scratch |
| `asm_legalize.h` | Full | Legacy repair hooks remain public to codegen |
| `block_emit.c` | Full | Previously mixed raw SP, x19 and frame locations in terminators/records |
| `block_emit.h` | Full | Historic 12-argument terminator signature exposed backend state twice |
| `call_emit.c` | Full | Incoming vs outgoing SP interpretation and record ABI |
| `call_emit.h` | Full | ABI emission contract |
| `codegen.c` | Full | Frame construction, temporary ASM file, active final textual legalizers |
| `codegen.h` | Full | Target entry points |
| `fp_emit.c` | Full | Existing typed frame helper duplicated in memory |
| `fp_emit.h` | Full | FP emitter interface |
| `frame_emit.c` | Full | Typed owned/incoming/outgoing/physical-SP regions and scratch policy |
| `frame_emit.h` | Full | Region-tagged `FrameRef` instead of ambiguous offsets |
| `integer_emit.c` | Full | Many raw `[sp,#]` SSA references still requiring final repair |
| `integer_emit.h` | Full | Integer emitter interface |
| `memory_emit.c` | Full | Duplicated private typed access helper |
| `memory_emit.h` | Full | Memory emitter interface |
| `residual_emit.c` | Full | GNU ASM-adjacent, variadic, and unusual Core cases with raw stack output |
| `residual_emit.h` | Full | Residual lowering contract |
| `scalar_normalize.c` | Full | Target-width/sign normalization and C integer semantics |
| `scalar_normalize.h` | Full | Normalization interface |

**Confirmed full-file read: 24/24.** The long ASM catalog was completed in
successive bounded line ranges, including both central dispatchers, all
privileged SYS register catalogs, `__ex_table` fixups, KVM alternatives,
futex/exclusive atomics, MMIO, cache, exception and VHE specializations.
This certifies code *reading coverage*, not every contract's runtime validity.

## Prioritized findings and ownership boundaries

1. **P0 architecture: final textual rewriting remains on the active path.**
   `codegen.c` invokes `minic_aarch64_copy_dynamic_frame_asm` and
   `minic_aarch64_legalize_large_sp_memory` from `asm_legalize.c`. Those scan
   completed assembly as strings, parse printed instructions and rewrite SP
   references, using implicit x17 scratch. This postpones stack/frame meaning
   until after machine emission and can invalidate live-register assumptions.
   **Do not delete yet**: its live clients include raw offsets emitted by
   `integer_emit.c`, `call_emit.c`, `residual_emit.c`, `asm_contract.c`, and
   portions of `codegen.c`.
   Verified call path: `codegen.c` places the dynamic copy callback into
   `MinicDirectLP64FunctionEmitter`; the Common function emitter creates a
   `tmpfile()` *for every function containing dynamic alloca*, then copies
   through that callback. The AArch program config supplies the large-SP
   finalizer; Common's `direct_finalize` applies it to the *whole output
   assembly file*. Both are active, not merely abandoned helpers.
2. **P1 architecture: frame access is mixed across machine families.** The
   original typed `FrameRef` covered mostly FP and memory, while integer,
   records, parameters, returns, branches, calls, structured ASM and residual
   operations printed `[sp,#]`. New migration in this batch covers the
   ordinary block/return/parameter and owned ABI subset; integer, outgoing
   call marshalling (genuine changing SP), residual, structured ASM and some
   function prologue remain an audited follow-up.
3. **P1 maintainability: 7,404-line `asm_contract.c` is two catalogs merged.**
   Source-side exact GNU/Linux template qualification and backend machine
   instruction encoding have legitimate one-to-one constraints, but the giant
   pair of dispatchers are lengthy, repetitive and difficult to audit for
   mismatched constraints/types/implicit effects. Split by instruction
   *family* with shared normalized typed descriptors, preserving original
   fail-closed admission and exact `.altinstructions`/`__ex_table` semantics.
   Blind splitting or a generic GNU template fallback would be unsafe.
4. **P1 correctness risk: legacy stack offsets are not region typed.**
   Raw `sp` represents stable local slots, entry argument slots or changing
   outgoing stack at different times; some paths manually add frame size,
   some save entry SP, some use x19. Compile-time `[sp,#]` string legalizing
   does not itself prove incoming address or dynamic-frame lifetime correct.
5. **P2 code hygiene: repeated frame access and ABI carry-over comments.**
   Duplicated FP/memory owned-slot wrappers were unified in this batch. The
   giant ASM catalog also contains repeated exact-template normalization and
   handwritten spill-slot offset arithmetic; replace those with frame APIs
   when a stable scratch/register-liveness contract is explicitly checked.

### Frame address regions and validation rule

| Region | Reference base | Why it must stay distinct |
| --- | --- | --- |
| Owned SSA/object | x19 for dynamic frame, else SP + outgoing delta | Stable callee-owned slots |
| Incoming stack arguments | Entry SP + 16 + argument offset (or saved entry SP) | Cannot substitute current outgoing SP |
| Outgoing arguments | Current SP | Caller adjusts SP around call |
| Physical SP | Current SP | Alloca and target-specific side effects |
| ASM operand spill | Owned frame with dedicated role | Explicit source contract, not textual repair |

## Applied first migration batch

- `frame_emit`: exported one typed owned-slot and owned-address entry point.
- `fp_emit`, `memory_emit`: removed duplicate owned-slot logic.
- `block_emit`: switched record addresses/copies, incoming record chunks,
  returned GP/HFA/indirect record addresses, scalar/wide returns, branch
  sources, saved callee state and SSA result writeback to typed references;
  reduced the terminator's large positional signature to its existing context.
- `call_emit`: switched incoming scalar/FP/wide arguments, return spill slots,
  and record return chunks to typed frame references. Preserved raw *outgoing*
  stack call marshalling pending a separate lifetime-aware migration.
- Unit and production smoke tests expanded for large owned displacement,
  dynamic x19, incoming frame and indirect record return paths.

**Validation at integration boundary:**

```sh
make -j8 CFLAGS=-Werror build/debug/bin/minic-cc
bash tests/compiler/c0/probe-aarch64-typed-frame-path.sh build/debug/bin/minic-cc
git diff --check
```

All above passed locally. Probe results:
`AARCH64_TYPED_FRAME_REF=PASS`,
`AARCH64_TYPED_FRAME_PRODUCTION=PASS`.
Assembly emission for `dynamic_stack_alloca.c` and the preprocessed
`aarch64_indirect_record_return_minic.c` passed. No AArch64 GNU cross-assembler
or QEMU executable was installed locally, so target machine execution and
independent GNU syntax checks remain CI gates, **not achieved results**.

## Next whole-family batches

1. Build a finer source/machine admission test matrix for `asm_contract.c`'s
   fragile privileged hardware effects and keep its exact fail-closed gate.
2. Switch integer and residual *owned* slots to typed frame refs, handling
   scratch lifetimes and the large incoming/dynamic SP path explicitly.
3. Model outgoing call argument and transient SP delta in the frame view,
   then retire each confirmed-unused text legalizer individually.
4. Expand exact-head AArch GNU assembler and QEMU runtime checks for dynamic
   alloca, large frames, record return, variadics, C ABI and Linux ASM.

No compiler/runtime `100%` claim follows from this architecture-only batch.

## Second FrameRef consolidation: prologue and outgoing calls

This follow-up migrates additional **active** producers, keeping the first
reading ledger and prior migration history above intact.

- `codegen.c` now emits callee-owned x8/indirect-result saves, x19/LR frame
  state, variadic x0..x7/q0..q7 save areas and the over-aligned saved entry SP
  through typed `OWNED`/`PHYSICAL_SP` frame references. The intrinsic AAPCS64
  `stp x29,x30` frame record, stack adjustment and x19 anchoring remain
  explicit machine operations. We preserve their ordering: pre-anchor saves
  use physical SP; post-anchor saves use owned x19.
- `call_emit.c` now carries `outgoing_sp_bytes` in a call-local frame view.
  Owned SSA/record sources and the hidden x8 result buffer use `OWNED`; stack
  arguments for scalar, double, quad/long-double, int128, record and indirect
  arguments use `OUTGOING`. The indirect callee pointer comes from `OWNED`
  before `blr x16`. This removes call-local `offset + stack_bytes` arithmetic
  and textual `add xN, sp, xN` rewriting for those paths.
- `block_emit.c`'s prior typed epilogue remains the single save-slot reader:
  dynamic stack teardown restores saved SP/x19/LR and only then pops the x29/
  x30 frame record. No blanket replacement of physical SP instructions.
- The AArch64 indirect-result oracle now checks **within specific functions**
  that `str x8,[frame,#S]` and the return-copy's `add x10,frame,#S;
  ldr x10,[x10]` refer to the **same slot**, on both normal SP and dynamic
  x19 frames. This replaces obsolete global regexes tied to the old `movz;
  add x10,x19,x10` spelling without weakening the ABI proof.
- An indirect record-return through a function pointer now has an explicit
  `blr x16`/x8 address proof and an additional GNU/MiniC mixed-ABI runtime
  assertion. This replaced an obsolete *expected rejection* as the compiler
  now emits the required Core result-object contract. Actual target execution
  remains gated by available AArch64 GNU/QEMU; no fabricated PASS.

**Retired raw-output dependencies:** prologue variable register saves and
indirect-result slots; outgoing call owned-slot loads, record addresses,
stack spill stores and x8 result setup. **Not retired:**
`minic_aarch64_copy_dynamic_frame_asm` is still called by Common for dynamic
functions, and `minic_aarch64_legalize_large_sp_memory` is still a program
finalizer. `asm_contract.c`, `residual_emit.c`, and some integer/structured ASM
emitters retain many raw SP references, so deleting either legalizer now would
break active code. Text-based `sub sp`/`add sp` tracking is still needed for
those legacy call-argument and GNU ASM paths until their scratch/liveness and
stack-domain contracts are migrated.

**Local verification** (`CFLAGS=-Werror`): compiler build PASS; typed
FrameRef native unit test PASS; typed production smoke for dynamic alloca,
two outgoing SP spill slots, large >4095 displacement and record return PASS;
indirect-result machine-source oracle PASS (three remaining negative ABI
cases); AArch SMMU dynamic barrier smoke PASS; structured ASM 45/640 update
source-only smoke PASS. GNU host reference fixture returns its expected code
42. No `aarch64-linux-gnu-gcc`/QEMU executable is available locally; GNU AArch64
assembly/link and QEMU mixed-ABI runtime are **SKIP**, pending exact-head CI.

## Third whole-family migration: integer, residual and variadic state

The next architecture batch migrated **the entire integer opcode emitter**
and **the Core residual emitter** from implicit frame-relative assembly
strings into typed `MinicA64FrameRef` operations. Their active callers now
pass one `MinicA64FrameView` built from the already-planned function frame.

- `integer_emit.c`: integer arithmetic, signed/unsigned comparisons,
  checked overflow, bit-scans, narrow operations, 128-bit shifts and carry
  arithmetic all load/store SSA and wide slots using `OWNED`. No GP input in
  these paths uses x16/x17; typed frame materialization may use them as
  ephemeral scratch, without interrupting NZCV-dependent arithmetic pairs.
- `residual_emit.c`: address constants, function/block/object/record field
  addresses, scalar bitcasts, pointer arithmetic, int128 conversions,
  variadic cursor initialization, `va_copy`, `va_arg`, and dynamic alloca's
  *count* read now all use `OWNED`. Narrow signed/unsigned pointer indices
  use a typed owned **address** in x16 followed by their correct byte/half/
  word loads; converting those to a uniform 64-bit load would have changed
  C signedness and width semantics.
- `MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC` still deliberately emits
  `sub sp, sp, x10` and `mov x9, sp`, because the resulting allocation is
  the **live physical SP**, not an owned SSA frame slot. In particular, the
  variadic 32-byte cursor and GP/FP save areas are owned by the stable frame;
  moving SP for an alloca must not move those addresses.
- The dedicated `va_copy` ABI probe now checks typed cursor address formation
  instead of assuming the obsolete `movz; add x9,sp,x9` spelling. The typed
  production probe also checks native pointer-offset index loads and guards
  against reintroducing raw `sp` SSA references into either emitter.

**Reference design:** Arm's official [AAPCS64 specification]
(https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst)
defines 16-byte stack alignment, callee-saved x19–x29 and the two-word frame
record; LLVM's upstream
[AArch64FrameLowering](https://github.com/llvm/llvm-project/blob/main/llvm/lib/Target/AArch64/AArch64FrameLowering.cpp)
explicitly treats variable-sized stack objects as a separate frame-lowering
case. These support our distinction among callee-owned stable frame slots,
incoming argument area, transient outgoing arguments and physical `sp`.
Neither source justifies eliminating AArch64-specific ABI classification.

**Local checks for this exact working tree:** `make -j8 CFLAGS=-Werror
build/debug/bin/minic-cc` PASS; per-source strict C11 `-Werror` syntax on
the changed AArch emitters PASS; `AARCH64_TYPED_FRAME_REF=PASS`,
`AARCH64_TYPED_FRAME_PRODUCTION=PASS`,
`AAPCS64_SRET_SOURCE=PASS`, `AARCH64_VA_COPY_ABI=COMPILE_PASS`,
`AARCH64_LIVE_SP=PASS`, `SMMU_DYNAMIC_DMB=PASS`,
`MINIC_AARCH64_LINUX_NARROW_PTR_MIXED_MUL=PASS`; the large-pointer-stride
five-ISA source/host-native regression also PASS. Local AArch64 GNU GAS and
QEMU are absent, so foreign-architecture runtime and exact-head CI remain
**unverified**, not an invented five-ISA native-run PASS.

**Legacy paths remain active and are intentionally not deleted:** the
`asm_contract.c` machine catalog still has well over one hundred raw
`[sp,#]` instruction templates (including futex, CMPXCHG128, privileged
sysregister, KVM/EL2, LSE, cache and MMIO paths). Some of those explicitly
have live x16/x17 operands; rewriting their addresses with an implicit x17
postpass is a register-liveness risk, but blindly replacing them would be
just as unsafe. `asm_legalize.c` still has real consumers through both the
per-function dynamic copy and whole-program large-offset finalizer. Before
retirement, split the giant ASM source-admission and machine-emission families
around typed operand descriptors and a per-template scratch exclusion mask;
prove source-only and AArch64 native runtime per family. No dead-path removal
is claimed in this batch.
