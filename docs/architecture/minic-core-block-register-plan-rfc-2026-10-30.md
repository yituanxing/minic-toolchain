# RFC: Block-local Core value registers for the four MiniC machines

Status: staged implementation. A Common one-result GP interval forwarder has
landed for LoongArch64; a full multi-register block allocator **has not**
been implemented and remains the major Linux text-size task.

## Verified progress after the original measurement

The newer exact-head GNU Linux relink artifact (`41045404678`, head
`0024b6d8`, compiler equivalent to `4502ba83`) still fails at the original
`head.o:(.ref.text+0x15c) R_LARCH_B26` reference to `start_secondary`.
However the ordinary local-address/frame folding batches reduced the full
2723-owner C executable sections from about 272 MB to **159,876,892 B**
against the original GNU **16,887,804 B**, a now **9.467x** ratio. The
recorded branch displacement is `-156,462,784 B = -149.215 MiB`, about
21.2 MiB beyond the B26 signed range. This is a confirmed GNU relocation
range failure, not a missing ELF section or the x86/AArch ABI status.

The first Common + LoongArch block register interval implementation keeps
one scalar GP result in native `$t0` when its **single semantic use** is the
next scalar integer ALU left operand or STORE input. A second stage extends
the interval across Core `OBJECT_ADDRESS`/`FIELD_ADDRESS` instructions only
when the target's existing proof certifies they emit **zero native opcodes**;
any real machine instruction, call, inline asm, compiler barrier or CFG edge
breaks this initial interval. It remains a deterministic subset of the full
register plan below, not general register allocation. AArch64 does not opt
into LoongArch's `$t0` convention.

On the **same original frozen GNU-preprocessed sources**, rebuilding the
clean 4502 compiler and the staged compiler gives these exact assembly sizes:

| Source | 4502 baseline | Adjacent + certified transparent interval | Change |
| --- | ---: | ---: | ---: |
| `arch/loongarch/kernel/vdso.i` | 90,695 B / 4,578 lines | 84,573 B / 4,262 lines | -6.75% bytes |
| `arch/loongarch/vdso/vgetcpu.i` | 11,100 B / 481 lines | 10,864 B / 469 lines | -2.13% bytes |

These samples are a **local slice**, not an extrapolation to 2723 owners.
They do not yet remove the 21.2 MiB branch-distance gap. The next phase
must admit block intervals lasting across real scalar operations, place
multiple live values in target-owned callee-saved GP registers, and remove
the memory round-trips introduced by CoreLower's temporary scalar objects.
The object-address temporary is inserted in
`src/core/core_lower_scalar_values.c`; the correct long-term Core object
promotion is a separate alias/cross-CFG analysis, not a claim that all these
locals are dead or freely substitutable.

## Why this is a Linux runtime blocker

This design is based on the LoongArch64 Linux 6.6.143 GNU/MiniC strict
differential at `d81261dd` (CI `41020466906`). The GNU C baseline reaches
QEMU P0/P1 2/2; MiniC compiles 2723/2723 C owners with GNU AS, and GNU's
partial link plus kallsyms1/2 links succeed. The final original GNU link
fails with `R_LARCH_B26` overflow in `head.o` to `start_secondary` in `smp.o`.
Do **not** change Kbuild's linker command, linker script, ELF relocation
rules, GNU-owned assembly, C compiler owner list, or QEMU pass criteria.

The C-owner executable sections total 16,887,804 bytes with GNU versus
272,143,396 with MiniC: 16.1148x, or 255,255,592 excess bytes. The 20
largest excess owners explain only 9.21% of that excess. Of the 2657
GNU-nonempty owners, the MiniC/GNU ratios have p10=11.85x, p25=14.21x,
median=16.63x, p75=19.66x, and p90=23.45x. This is a generic instruction
selection and value-lifetime problem, not an EFI archive or individual
source-file problem.

Two real, identical preprocessed C inputs from the failed-run artifact:

| Function | GNU machine instructions | MiniC machine instructions | MiniC ld/st | MiniC direct frame ld/st | MiniC `li.d` / `add.d` |
| --- | ---: | ---: | ---: | ---: | ---: |
| `arch/loongarch/kernel/vdso.i`: `vdso_mremap` | 6 | 135 | 68 | 48 | 10 / 6 |
| `arch/loongarch/kernel/vdso.i`: `vvar_fault` | 66 | 1567 | 1014 | 840 | 190 / 172 |
| `arch/loongarch/kernel/vdso.i`: `init_vdso` | 63 | 1558 | 1020 | 834 | 185 / 172 |
| `arch/loongarch/vdso/vgetcpu.i`: `__vdso_getcpu` | 12 | 219 | 128 | not measured | 26 / 24 |

The first sample alone spends 840/1567 instructions transporting
temporary Core SSA values through compiler-owned frame slots; an additional
362 `li.d`/`add.d` instructions mostly rebuild local addresses. These counts
are assembly measurements, **not** predictions of how many instructions a
register allocator will eliminate. For the second sample GNU also inlines
`read_cpu_id` and `get_pcpu_data`, while MiniC emits direct calls; registers
are not the whole performance gap.

One MiniC parameter/local idiom currently emits:

```asm
st.d $a0, $sp, 0
li.d $t0, 24
add.d $t0, $sp, $t0
st.d $t0, $sp, 8
ld.d $t1, $sp, 8
ld.d $t0, $sp, 0
st.d $t0, $t1, 0
```

Here an SSA temporary is stored and immediately reloaded just to write the
source parameter to a local object. A value-register plan can remove the
**SSA** roundtrip; a later, alias-proven local promotion can potentially
remove the **object** roundtrip. These are separate optimizations.

## Existing contracts to reuse, not duplicate

1. `src/core/core_ir_verify.c` resets `available_values` on **every block**;
   it rejects every cross-block SSA read, including terminators. Value IDs
   have function-wide identity but block-local lifetime. There is no Core
   PHI or cross-edge SSA preservation requirement in this version.
2. `src/core/value_uses.h` already visits the exact semantic uses of each
   Core instruction and terminator. `src/target/common/core_spill_layout.c`
   already counts uses and performs per-block last-use spill-slot reuse.
   Reuse these visitor semantics and, where practical, the same count-analysis
   workspace; do not invent another target-local operand scanner.
3. `src/target/common/direct_machine_dispatch.h` classifies each instruction,
   emits it through a machine callback, then unconditionally stores its GP
   result to the once-planned `MinicLP64FramePlan` SSA slot. LoongArch's
   integer/memory/residual emitters reload operands from those slots.
4. `src/target/common/lp64_frame.c` owns value/object frame geometry and
   currently combines SSA slot-reuse with object storage intervals and wide
   value tails. A register-only value must not silently reacquire an old
   `value_id * 8` fallback. Existing spill slots remain the correctness
   fallback for every initially unsupported value.
5. `src/target/loongarch64/frame_emit.c` owns immediate ranges and dynamic
   `$s0` frame addressing; the new generic planner must never emit target
   opcodes or choose its own physical frame offsets.

## Proposed ownership and interfaces

Add one target-agnostic planning implementation, tentatively
`src/target/common/core_block_register_plan.{h,c}`. It consumes a **verified**
`MinicCoreFunction`, a `MinicCoreBlock`, and a target-supplied register
resource/ABI policy; returns immutable per-instruction value-location and
clobber/save decisions. It contains no architecture-name switches,
instruction mnemonics, psABI register numbers, frontend AST nodes, or
GNU-assembler strings.

Suggested conceptual interface (names deliberately provisional):

```c
typedef struct MinicCoreRegisterResourcePolicy {
    unsigned gp_count;   /* GP pool indexes, not physical target numbers */
    unsigned fp_count;   /* zero initially: retain old FP spill path */
    bool (*instruction_clobbers)(const MinicCoreFunction *,
        const MinicCoreInstruction *, void *target_abi,
        uint64_t *gp_clobbers, uint64_t *fp_clobbers,
        bool *unknown_effects);
    void *target_abi;
} MinicCoreRegisterResourcePolicy;

/* Each use and result selects register pool index or existing frame slot.
 * Before each instruction: a bounded spill/reload/move list. After it:
 * retire dead values and record the result. The target encodes all moves. */
bool minic_core_block_register_plan_build(
    const MinicCoreFunction *, const MinicCoreBlock *,
    const MinicCoreRegisterResourcePolicy *,
    MinicCoreBlockRegisterPlan *);
```

The plan should represent **all** operand occurrences, not only distinct
ValueIds: `v+v` is two uses of one value. One instruction's inputs must stay
available until its final consuming machine operation; do not overwrite a
live input with the output prematurely. Use last-use counts for release and
choose a deterministic victim when the pool is full. Attach the canonical
frame offset from `MinicLP64FramePlan` (or another target's approved frame
plan) to every spilled value; size/stack alignment are never inferred from
SSA IDs. An instruction shape or ABI type outside the proven subset simply
gets the preexisting stack-only behavior.

The register **policy** lives with the machine:

- LoongArch LP64D chooses hardware registers and call/inline-asm clobbers,
  initially a small caller-safe or callee-saved pool with an explicit save
  contract. `$s0` remains reserved for dynamic-frame anchor; currently-used
  `$t0`-`$t8` are **not** an assumed free pool. The target must prove scratch
  registers do not overlap planned residents.
- AArch64, RV32/RV64 and x86-64 supply their own hardware register pools,
  call ABI preservation, scratch exclusions, and return/argument placement.
  They share the plan algorithm, not a fabricated shared ABI or code emitter.
- For each physical callee-saved register actually used, allocate a bounded
  prologue/epilogue save region via the existing frame owner. Include
  dynamic `alloca`, stack realignment, incoming stack args, varargs, sret,
  and all return edges in the save/restore verification.

The Common dispatcher and ISA callbacks need a location-aware emission
contract, not textual assembly rewrites. A target callback should ask for
the planned location of operand `v`, request a temporary when necessary, and
report where the result resides. The existing unconditional `store_gp`
callback becomes **conditional**; its old implementation stays only while
unmigrated instruction families use stack fallback.
For ordinary arithmetic, comparisons, pointer math, and scalar loads/stores,
the native selector must accept the **actual input and destination physical
registers** from the plan. Merely moving every `$t0` result into `$s1` and
back before each following operation would exchange two memory instructions
for two register moves and **would not** solve the code-size problem. The
backend's existing fixed-scratch and destructive-operand constraints must be
made explicit before selection; true fixed-register ABI operations may still
need moves.
An SSA result with no remaining semantic uses needs no artificial spill
writeback (but its instruction's actual observable effects still execute).
For small functions, include prologue/epilogue callee-save costs in the
allocation choice: saving a new register to avoid one load can increase code.

### Barriers, aliasing and correct-by-construction fallback

- Initially support only integer/pointer values fitting a single GPR.
  Float, int128, long double, aggregates, unhandled GNU asm forms, and exotic
  dynamic ABI carriers continue through the existing frame path.
- Ordinary `volatile` and atomic accesses remain executable memory effects
  at their original Core instruction order. Cache **SSA results**, never
  assume an externally observable lvalue's memory is unchanged. Calls may
  alias memory objects but cannot mutate immutable Core SSA values.
- On ordinary calls, use only ABI-proven preserved locations; spill any
  live value in a target-clobbered physical register before the call and
  reload on first later use. For an unclassified call ABI, conservatively
  spill and invalidate the pool.
- On opaque GNU inline assembly, structured assembly of unknown physical
  clobbers, compiler memory barrier, asm-goto, or any uncertainty, flush
  all live planned values to approved spill slots and invalidate cache
  mappings. Machine asm outputs must be captured with exact constraints.
  Never speculate over an unmodeled asm template.
- At **each** CFG block boundary invalidate all mappings. The verifier's
  block-local use contract makes this safe without PHI insertion. Terminator
  inputs must be read before invalidation and all branch edges preserved.
- If plan construction cannot prove physical register availability,
  resource budgets, exact clobbers, frame bounds, or legal width, reject
  optimization for that block and use the existing instruction-by-instruction
  spill path. It is not an acceptance failure or a GNU C fallback.

## Staged changes, obsolete paths, and gates

### First LoongArch emitting slice: whole-block admission, no mixed state

The first implementation should be deliberately narrower than the eventual
location-aware dispatcher above. It must **produce real machine-code savings**
before broadening the allocator, while retaining the existing stack path as
the only fallback. A read-only use counter alone is not an optimization.

1. The **Common** planner computes `remaining_uses` from the existing
   `core/value_uses.h` visitor, including the block terminator. It assigns
   deterministic pool indexes to block-local scalar integer/pointer SSA
   results, retiring a register **after** the complete instruction consumes
   its operands. Duplicate occurrences (`x+x`) decrement two uses but keep
   the input resident until the opcode finishes. The first slice rejects
   blocks needing more than the available pool instead of guessing a spill
   victim. It never stores a value in a different live value's planned slot.
2. Admission is **all or nothing per Core block**, decided before streaming
   any assembly. Initially admit scalar integer/pointer constants, ordinary
   parameter ingress, object/global/function addresses, field/pointer
   arithmetic, common scalar arithmetic/comparisons, and safe scalar
   loads/stores, only after all necessary instruction shapes, legal widths,
   known alignment, register needs and terminator forms pass target-owned
   checks. Reject calls, inline assembly of either form, fixed-register
   traffic, compiler/atomic barriers, unproven unaligned accesses, dynamic
   stack allocation, floating/wide values, aggregate transport, indirect
   branches and unmodeled forms **for this first slice**. The whole block
   then executes exactly the existing stack implementation. Calls and
   inline assembly are not assumed harmless simply because the chosen pool
   is normally callee-saved; later *mixed-mode* support must explicitly
   flush/invalidate residents and model call/asm clobbers at each barrier.
3. The **LoongArch policy** maps pool indexes to `$s1` through `$s8`
   (`$r24`-`$r31`), which the LoongArch ELF psABI and Linux kernel register
   convention mark callee-saved. The current native emitter reserves `$s0`
   as the stable realigned/dynamic frame base; it actively uses `$t0`-`$t8`
   in arithmetic, alignment fallback, large-offset addressing, calls and
   GNU inline-asm lowering. In particular, `frame_emit.c` may destroy `$t3`
   while materializing an address, and `asm_contract.c` uses `$t6`-`$t8`.
   **None of these scratch registers is a free SSA home.** `$r21` is the
   Linux per-CPU register, never part of the pool. The target must reject
   user-visible assembler forms that can clobber planned static registers;
   this first slice avoids the ambiguity by rejecting any block with asm.
4. The once-built Common function plan unions the *actually used* static
   register mask across admitted blocks. Before the existing checked LP64
   frame finalization it reserves nonoverlapping saved-register slots with
   `minic_core_frame_reserve`; the resulting mask/offsets enter the immutable
   block context. Native LoongArch prologue saves **every selected** static
   register once, and every `RETURN` edge restores them before frame
   teardown, including realigned `$s0` frames and hidden-result returns.
   The existing RA, `$s0`, saved-entry-SP, indirect-result and varargs
   locations must remain disjoint and retain their ABI meaning. No blanket
   save of all eight registers for a one-value block: count prologue and
   epilogue cost before enabling a function whose optimization would grow
   code.
5. Eligible LoongArch opcodes must consume the **real input registers** and
   produce the actual allocated destination directly (e.g. `add.d $s3,
   $s1, $s2`), with safe distinct operand/result selection at the last-use
   boundary. Moving `$t0` into `$s3` after the original frame reloads merely
   relocates the bloat. Refactor the existing native instruction families so
   the unchanged stack fallback and admitted register path use the same
   semantic shape checks and machine-opcode selection; do **not** introduce
   a second copy of every LoongArch arithmetic/memory algorithm. Common
   dispatch suppresses its unconditional GP writeback only for a result
   proven resident in the plan. The original reusable Core spill slots
   stay allocated for all other blocks, and block boundaries invalidate all
   register mappings (the verifier rejects cross-block SSA reads).

Concrete first-wave source touch points (the final patch should explain any
expansion of this list):

| Owner | Required change |
| --- | --- |
| `src/target/common/core_block_register_plan.{h,c}` **new** | One block-local semantic use/lifetime allocator and immutable register/stack locations; no LoongArch register names or opcode strings. |
| `src/target/common/direct_function_plan.{h,c}` and `direct_block_context.h` | Function-wide used-mask union, checked static-register save space, immutable plan handoff. |
| `src/target/common/direct_machine_dispatch.h` | Route admitted blocks through a location-aware callback and make GP writeback conditional; preserve original path for whole-block rejection. |
| `src/target/loongarch64/codegen.c` and `block_emit.c` | Define pool/ABI policy, emit prologue and all-return-edge saves/restores, pass real result/terminator locations. |
| `src/target/loongarch64/integer_emit.c`, `residual_emit.c`, `memory_emit.c` | Share existing semantic checks, expose direct physical register operands for the supported scalar families; leave wide/FP/unaligned/asm paths unchanged. |
| `src/target/loongarch64/call_emit.c` | Handle admitted scalar parameter ingress; calls remain an admission barrier in the first slice. |
| `Makefile`, `tests/compiler/bootstrap/b0-sources.txt`, `tests/compiler/c0/check-minic-target-boundaries.sh` | Wire the **single** Common planner into native builds, self-host bootstrap source inventory, and no-duplicate-allocator architecture gates. |

Instrument opt-in counters for eligible/rejected blocks, scalar results
placed in registers, planned spill/reload pairs removed, physical saved
registers and total added prologue/epilogue instructions. Add GNU-vs-MiniC
native execution tests for distinct operands, repeated operands, dead values,
six-to-nine simultaneously live values, signed/narrow normalization,
parameter registers and stack parameters, memory alias/volatile order,
branch and return terminators, all-return-edge restoration, source labels,
asm/call fallback, realigned dynamic frames and hidden-result ABI. Test the
actual generated GNU assembly and `.o` sizes, not just planner output. Run
the complete four-ISA `check-fast`/runtime gates even when only LoongArch
emission changes because the function planner and dispatcher are shared.

Initial engineering estimate (not a budget or acceptance shortcut): Common
planner **350-600** lines, LoongArch opcode/ABI transport **500-900**,
function/frame/dispatcher integration **150-250**, and tests **200-350**,
plus build/bootstrap manifests. The first slice may reject many real Linux
blocks; its measured coverage on the frozen 2723-owner corpus decides when
to add bounded eviction and per-instruction barrier flushing. Do not claim
the RFC's 50% text-size *goal* from this subset, nor claim that shaving text
alone proves `R_LARCH_B26` link-range safety.

1. **Read-only planner and tests.** Extract/reuse the current
   `value_uses.h`/spill-layout count walk once; assert def/use, last-use,
   duplicate operand, two-address/destructive instruction, terminator,
   barrier, and deterministic resource pressure behavior. Instrument counts:
   eligible scalar values, spills, reloads, flushes, and callee saves.
   No emitted machine bytes change yet.
2. **One complete target integration, not competing algorithms.** Integrate
   the plan into `direct_machine_dispatch.h` and the LoongArch native
   `block_emit`, `integer_emit`, `memory_emit`, `residual_emit`, `call_emit`,
   `fp_emit` as applicable. Route every participating family through a
   single location resolver. Keep untouched families on the **existing**
   spill path, and ensure fixed scratch/clobber contracts for all native
   emitters. Adapt `lp64_frame.{h,c}` to save only used callee registers.
3. **Migrate AArch64, RV32/64, x86-64** by providing target-specific
   policies and emitter transport. Do not copy the block allocator into
   each architecture. The Core IR and frontend never branch on target name.
4. **Delete superseded paths only after verification.** Once all cases
   route through location-aware transport, remove the unconditional GP
   writeback in Common dispatch and obsolete target-local `ld/st SSA slot`
   branches. Keep `core_spill_layout`, per-target ABI frame logic, and
   spill emission for proven fallback; they are not dead code.
5. **Separate future Core memory promotion.** Only then analyze
   non-address-escaping automatic locals and add semantic local scalar
   forwarding/promotion in the Core pipeline. It must respect volatile,
   atomic, `asm` memory clobbers, pointer alias/escape and CFG joins;
   this is **not** a target-specific register-allocation shortcut.

Required tests at every integrating step:

- Core unit: one-use, repeated-use, many simultaneous values, high register
  pressure/eviction, dead result, callee-save profitability, signed/unsigned
  widths, calls, duplicate operand,
  different terminators, verifier-forbidden cross-block value, zero blocks.
- Four-ISA standalone runtime: arithmetic chains, fan-out, loops, branches,
  nested direct/indirect calls, varargs, alloca, packed/unaligned accesses,
  volatile/atomic, structured/opaque asm, asm-goto, computed goto,
  long double/int128, aggregates, and sret; compare GNU baseline output.
- Linux three-ISA exact-head strict differential; MiniC alone compiles all
  frozen C `.i`, GNU AS/LD links unchanged objects, and original QEMU P0/P1
  executes. An assembly-level unit PASS does **not** imply runtime PASS.

Quantitative acceptance: report code size for the same 2723 C owners and
instruction families (real `vdso.i`, `vgetcpu.i`, and at least one large
ordinary C owner) before/after, plus exact GNU link result and `head.o`
to `start_secondary` relocation span. The first major phase should target
at least **50% reduction** from the present 272 MB text as a *goal*, not a
prediction. Merely shortening immediate encoding or individual record copies
cannot solve a 16x distributed expansion. Even a total below 136 MB is not
proof of valid per-relocation range: inspect the actual GNU section layout.
