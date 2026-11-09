# RFC: AArch64 Typed FrameRef and Removal of Assembly-Text Rewrites

Date: 2026-10-29
Project: MiniC only; MiniAS is out of scope
Status: Proposed, design only (no production migration in this RFC)

## 1. Decision and scope

Preserve the existing `DirectLP64` Core/ABI planning and AAPCS64 classification.
Replace AArch64's *post-emission interpretation of assembly strings* with
target-owned, typed frame addressing at the point where native instructions are
emitted. Do not merge ISA instruction emitters, change the Core representation
of stack objects, or relax any inline-ASM qualification.

The two text rewrites being retired are:

1. `src/target/aarch64/asm_legalize.c:15` —
   `minic_aarch64_legalize_large_sp_memory(path)` parses generated `ldr`/`str`
   strings and rewrites large `[sp,#offset]` / `[x19,#offset]` references;
   it opens the completed `.s` for replacement.
2. `asm_legalize.c:74` —
   `minic_aarch64_copy_dynamic_frame_asm(out,input)` recognizes textual
   `mov x19,sp`, `sub/add sp`, `ret` and re-bases local frame references.

Current hooks are `aarch64/codegen.c`'s `MinicDirectLP64FunctionEmitter`
`rewrite_dynamic_asm`, plus its `MinicMultiDirectProgramConfig`
`finalize_output`. They are implemented through
`common/direct_function_emit.h` and `common/direct_program_emit.c`.

This work changes **address generation**, not ABI argument placement or
runtime behavior. A Linux C compilation success must remain a baseline, not a
reason to remove the existing runtime and assembler tests.

## 2. Exhaustive frame-reference inventory

On the baseline `minic-batch13-merge` snapshot, the literal-string inventory
from `rg -o '\[sp' src/target/aarch64/*.c` is:

| File | Literal `[sp` occurrences | Principal ownership |
| --- | ---: | --- |
| `asm_contract.c` | 137 | Qualified structured-ASM operand slots; ISA scratch liveness |
| `integer_emit.c` | 70 | Core scalar and 128-bit value slots |
| `call_emit.c` | 37 | Frame locals, incoming stack, outgoing call area, hidden returns |
| `residual_emit.c` | 28 | SSA values, object/variadic addresses, physical `alloca` SP |
| `fp_emit.c` | 22 | Floating and long-double value slots |
| `block_emit.c` | 22 | Core block operands, record arguments/returns, epilogue |
| `memory_emit.c` | 14 | Scalar/wide memory operands and result slots |
| `codegen.c` | 5 | Prologue frame record, saves, variadic register bank |
| `asm_legalize.c` | 3 | Old source-text matcher, to be deleted |
| **Total** | **338** | Literal patterns only |

**338 is not the total number of physical stack-related operations.** The
migration audit must additionally find dynamically formatted base registers
(`%s` selecting `sp`/`x19`), `[x19,...]`, `add xN,sp,xN`, explicit SP changes,
and raw templates. These are deliberately not conflated with `FrameRef` loads.
Any changed baseline must re-run the inventory before applying mechanical
rewrites; the count is an audit ledger, not a target quota.

Every call site is classified as one of the following, not merely as an
untyped displacement:

| Frame region | Meaning | Address rule |
| --- | --- | --- |
| `OWNED_FRAME` | SSA scalar/wide slot, local object, saved register, variadic save, function-local metadata | Stable `x19 + offset` if `ctx.dynamic_frame`; otherwise `sp + outgoing_sp_bytes + offset` |
| `INCOMING_ARGUMENT` | Caller-owned stack parameter | Entry-frame-record SP `+16 + stack_slot*8`; from saved entry SP when over-aligned, otherwise current stable frame base `+ctx.frame` |
| `OUTGOING_ARGUMENT` | Callee argument area after this CALL's `sub sp` | **Current physical** `sp + stack_slot*8`, never `x19` |
| `LIVE_STACK_POINTER` | Dynamic alloca, actual SP read, SP adjust, frame-record push/pop | Actual architectural SP; not rebased or rewritten |
| `ASM_OPERAND_FRAME` | Already-qualified inline-ASM Core spill address | An owned frame reference with a **contract-provided live-register mask** |

`ASM_OPERAND_FRAME` is a separate *semantic use* even if the implementation
shares the `OWNED_FRAME` address calculation. The distinction prevents
unadvertised scratch-register clobbers in LL/SC and other structured templates.

### AAPCS64 phase rules

- The prologue establishes `[x29,x30]`'s 16-byte unwind record **before**
  creating the stable `x19` anchor. Early prologue memory operations therefore
  use an explicit physical-SP/prologue access mode.
- For over-aligned local objects, `saved_entry_sp_offset` identifies a saved
  **frame-record SP**, not the caller's original pre-push SP. Incoming stack
  parameters still require the extra `+16`.
- For dynamic stack allocation, `x19` remains the owned frame base in *all*
  CFG blocks. A textual `ret` in an earlier block must never switch the base
  of a later, independently reachable block.
- An outgoing CALL's temporary SP decrement affects `OWNED_FRAME` only when
  using non-dynamic SP-based addressing. It never affects `OUTGOING_ARGUMENT`
  offsets relative to the newly lowered physical SP.
- Return operands must be loaded before restoring `sp`/`x19`; restoring the
  physical SP and popping `x29/x30` are dedicated epilogue operations.
- `MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC` returns the *live* SP after
  adjustment. `current_stack_pointer`'s existing rejection/qualification
  behavior must not silently change.

## 3. Target-private API proposal

Implement in `src/target/aarch64/frame_emit.h/.c`, independent of the
assembler project. The illustrative signature below fixes data ownership,
not final naming:

```c
typedef enum {
    A64_FRAME_OWNED,
    A64_FRAME_INCOMING,
    A64_FRAME_OUTGOING,
    A64_FRAME_PHYSICAL_SP
} MinicA64FrameRegion;

typedef struct {
    MinicA64FrameRegion region;
    size_t byte_offset; /* incoming/outgoing stack slot converted by caller */
} MinicA64FrameRef;

typedef struct {
    const MinicDirectLP64BlockContext *function; /* borrowed; immutable */
    size_t outgoing_sp_bytes; /* explicit CALL-local SP delta */
    bool prologue_before_frame_anchor;
} MinicA64FrameView;

typedef struct {
    uint32_t forbidden_gp_mask; /* live registers or operands */
    unsigned int preferred_scratch; /* validated IP register */
} MinicA64ScratchPolicy;

bool minic_a64_emit_frame_load(FILE *, const MinicA64FrameView *,
    MinicA64FrameRef, MinicA64Register,
    MinicA64MemoryKind, MinicA64ScratchPolicy);
bool minic_a64_emit_frame_store(FILE *, const MinicA64FrameView *,
    MinicA64FrameRef, MinicA64Register,
    MinicA64MemoryKind, MinicA64ScratchPolicy);
bool minic_a64_emit_frame_address(FILE *, const MinicA64FrameView *,
    MinicA64FrameRef, MinicA64Register, MinicA64ScratchPolicy);
```

`MinicA64Register` is a typed GP/FP register class plus register number;
`MinicA64MemoryKind` explicitly describes scalar width, signed extension,
and FP/SIMD width. No caller can inject a free-form opcode or operand spelling.
Keep the Common `MinicDirectLP64BlockContext` borrowed and unchanged. A CALL
creates a local `MinicA64FrameView` and updates its `outgoing_sp_bytes` when
it emits `sub sp`; it returns the view to zero on `add sp`. Other families
use a zero-delta view. Do not create global or static mutable frame state.

The API must:

1. Check every `size_t` add/multiply before computing byte offsets.
2. Choose immediate addressing only if it is legal for the **particular**
   opcode, operand width, alignment, and displacement. Conservative fallback
   to materialized address is acceptable; a blanket `4095` criterion is not
   an exact general AArch64 rule.
3. Otherwise form the whole displacement using target-owned `movz/movk`,
   add the selected base, and access `[scratch]`. Reuse one target-native
   immediate materializer instead of duplicating it across codegen/families.
4. Reject impossible scratch requests rather than clobbering a live register.
   `x16/x17` are candidate IP registers, **not universally dead registers**.
   An ASM contract must pass its actual operand/liveness exclusions.
5. For an incoming argument under over-alignment, first load the saved entry
   frame-record SP from the owned-frame slot, then use a separate scratch for
   the parameter offset if needed. This may require two disjoint temporaries.
6. Preserve `stp/ldp` frame-record updates, outgoing call adjustments, and
   dynamic alloca as intentional physical-SP operations outside this API.

The RV64 `src/target/riscv64/frame_access.c` and LoongArch64
`src/target/loongarch64/frame_emit.c` demonstrate target-owned frame helpers,
but their register policy and immediate encodings must not be copied into
AArch64. This RFC does **not** propose a single ISA-agnostic textual emitter.

## 4. Migration sequence and implementation assignments

All subtasks use the same approved API contract and can form a few **large,
reviewable architecture commits**, not dozens of unrelated one-line patches.

| Batch | Independent implementation ownership | Exit gate |
| --- | --- | --- |
| A | `frame_emit.h/.c`: typed refs, width-aware addressing, checked arithmetic, explicit scratch; dedicated unit tests | Helper tests including large offsets, x16/x17 conflicts, `INCOMING` after alignment |
| B | In parallel: `integer_emit.c`, `fp_emit.c`, `memory_emit.c`; adapt their signatures to receive `ctx`/FrameView and eliminate local-frame string interpolation | Every supported scalar/wide slot operation uses the helper; targeted native assembly tests |
| C | `block_emit.c` + `residual_emit.c`: record returns, terminators, pointer addresses, va cursors, alloca | All return/CFG/alloca paths classified; no accidental rebasing of live SP |
| D | `call_emit.c` + `codegen.c`: AAPCS64 entry, outgoing CALL delta and cleanup, x8 sret, x19/x29/x30 saves | Existing incoming/outgoing ABI and QEMU tests pass |
| E | `asm_contract.c`: migrate all qualified operand-stack loads/stores; each template declares scratch liveness | Every Linux structured contract remains fail-closed and assembles; no ASM metadata broadened |
| F | Remove both rewrite hooks, related Common staging/finalizer branches, `asm_legalize.c/h`, and source manifests | No rewrite references or unclassified owned-frame direct SP accesses; exact-head Linux census unchanged |

The migration should first keep the old finalization path enabled as a
behavioral safety net while systematically reducing its work. Only after the
new FrameRef API owns every local frame access may Batch F disable it and
demonstrate the absence of any output differences that matter for ABI/ISA.
Do not remove the old hooks just because the new API compiles.

**Cross-agent conflict boundaries:** B's three files can be edited in
parallel. C and D should coordinate shared `block_emit.h`, `call_emit.h`, and
the `block_emit.c` dispatcher only at integration. E owns only
`asm_contract.c` after the API stabilizes. F owns `codegen.c`, Common hook
retirement, and build manifests after other users have finished editing.

### Final deletion checklist

- `aarch64/codegen.c` no longer registers
  `minic_aarch64_copy_dynamic_frame_asm` or
  `minic_aarch64_legalize_large_sp_memory`.
- `common/direct_function_emit.h` no longer creates a temporary per-function
  emission stream solely to support dynamic-SP text substitution.
- `common/direct_program_emit.[ch]` no longer carries the unused
  `finalize_output` path, once all remaining DirectLP64 configs are checked.
- Delete `src/target/aarch64/asm_legalize.c/.h` and their references in
  `Makefile` and `tests/compiler/bootstrap/b0-sources.txt`.
- All remaining literal physical-SP instructions in AArch64 are covered by
  an explicit allowlisted semantic category (prologue, epilogue, outgoing
  arguments, alloca/live-SP, validated opaque ISA template).
- Whole-program Linux C census, native assembly and QEMU regressions pass on
  the **same final commit**. A pass rate from a prior configuration is not
  evidence for the newly refactored head.

## 5. DirectLP64 policy boundary: frozen in this campaign

`common/direct_call_plan.c`, `common/direct_function_plan.c`, and
`common/lp64_frame.c` remain the owners of ABI *meaning* and placements.
FrameRef consumes their verified offsets. In particular, do not alter:

- AAPCS64 `x8` hidden indirect-record result pointer or the independent
  `x0..x7` argument stream.
- Aggregate classification (empty records, INTEGER-class chunks, HFA,
  indirect records), register assignments, `stack_first` semantics, or
  `MinicLP64ArgumentLocation` representation.
- Stack argument area size and 16-byte alignment, including the record and
  `__int128` all-or-stack AAPCS64 rules (different from LoongArch LP64D).
- Exact `frame_size`, `frame_alignment`, `saved_entry_sp_offset`, variadic
  register-save layout, and callee-saved `x19` policy.
- The 16-byte `x29/x30` unwind record, and when x30 is additionally saved
  for nested direct calls.

Address changes and ABI policy changes need **separate commits and CI
evidence**. In particular, a local `sp + stack_bytes + slot` in old
`call_emit.c` must become a `FrameView` delta for `OWNED_FRAME`, not a
modification to Common's planned ABI argument offset.

## 6. Existing regression hooks and new tests

Reuse the repository's existing scripts:

- `tests/compiler/c0/probe-lp64-overaligned-stack-frame.sh`
- `tests/compiler/c0/probe-lp64-many-parameters.sh`
- `tests/compiler/c0/probe-lp64-empty-record-call-abi.sh`
- `tests/compiler/c0/probe-lp64-indirect-record-abi-qemu.sh`
- `tests/compiler/c0/probe-aarch64-indirect-record-return-qemu.sh`
- `tests/compiler/c0/probe-aarch64-large-structured-asm-spills.sh`
- `tests/compiler/c0/probe-aarch64-linux-smmu-dynamic-dmb.sh`
- `tests/compiler/c0/probe-aarch64-linux-large-pointer-stride.sh`
- `tests/compiler/c0/probe-direct-lp64-core-record-snapshot-qemu.sh`

Add targeted tests for all load/store operand widths around native offset
encoding boundaries, large-address scratch collisions, wide pair slots,
outgoing CALL stack-active local reads, repeated alloca, entry-SP recovery
with over-alignment, multiple returns and later reachable CFG blocks,
`va_start` after a dynamic frame, and fail-closed unsupported ASM. Verify the
resulting `.s` with GNU AArch64 assembler, not only MiniC `-S` acceptance.

The primary acceptance condition is preserving the exact-head Linux C file
compile census already established for MiniC. Runtime correctness expansion
remains a *subsequent dedicated campaign*; existing runtime tests are still
mandatory regression protection during this structural migration.
