# MiniC second-pass GNU inline-ASM architecture audit — typed operand/form plan

Date: 2026-10-29. **Read-only audit.** No compiler implementation was changed
by this review. The ongoing parallel backend patches may change line numbers;
symbol names and source-file ownership below are the stable anchors. This is
an architectural risk and migration ledger, not a fresh 21-shard/QEMU PASS.

## Full source → Core → machine admission path

1. Frontend `parser_gnu_inline_asm.c:parse_gnu_inline_asm_output` records the
   complete constraint text (including `&`) and `WRITE_ONLY`/`READ_WRITE`
   access; `MinicInlineAsm` retains original template, operand identities,
   labels, register clobbers and explicit memory clobber.
2. `core_lower_asm.c:minic_core_lower_inline_asm` rejects aggregate operand
   count overflow before mutations, then uses the following ordered dispatch:
   (1) `core_asm_try_target_register_contract`, (2) target-reserved prefix
   rejection, (3) `core_asm_try_immediate_output_csr`, (4)
   `core_asm_try_structured_roles_and_asm_goto`, (5)
   `core_asm_try_atomic_and_immediate_forms`, (6)
   `core_asm_try_mixed_memory_forms`, (7)
   `core_asm_try_readwrite_and_output_specialization`, then
   `core_asm_try_empty_barrier_scalar_forms` and final opaque fallback.
   Thus there are **eight callable handlers** plus a reserved-ISA gate and
   opaque fallback; “seven-way” is an informal grouping, not literally seven
   mutually exclusive enum cases.
3. Target path: `core_lower_asm_target.c` obtains a `MinicBackend` and calls
   its `qualify_inline_asm`. Its only returned payload is
   `MinicTargetInlineAsmContract { operand_count, kinds[],
   implicit_memory_effect }`. Core turns each role into SSA value or address,
   folds qualified immediate constants, rematerializes only pure local object
   addresses when CFG blocks change, then builds
   `MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM`. It stores the frontend
   `source_inline_asm_id` for non-goto target-qualified forms.
4. Generic path: `core_lower_asm_structured.c` independently decodes `=r`,
   `=&r`, `+r`, `+&r`, `=m`, `+m`, `+A`, `m`, `r`, `rJ`, `Jr`, `rK` and preserves
   `early_clobber` explicitly; `core_lower_asm_scalar_forms.c` still owns
   several output/memory/empty-template/immediate specializations. Both
   construct Core ASM effects through partially overlapping logic.
5. A target-accepted asm-goto deliberately bypasses the target register path:
   `core_lower_asm_structured.c` resolves 1–3 symbolic `i` operands and 1–2
   C label blocks, emits an OPAQUE Core effect with deferred link-time
   immediates, and keeps `source_inline_asm_id == SIZE_MAX`. A backend must
   revalidate the exact goto form and PREL32/alternative metadata.
6. Machine dispatch: x86 `machine_emit.c` → `machine_asm.c:
   minic_x86_64_emit_structured_asm`, AArch64 `residual_emit.c` →
   `asm_contract.c:minic_aarch64_emit_qualified_structured_asm`, LoongArch64
   `residual_emit.c` → `asm_contract.c:
   minic_loongarch64_emit_qualified_structured_asm`. Each reparses the
   normalized template, checks Core kinds/types and chooses physical machine
   instructions. Opaque and asm-goto have separate native dispatches.

## Confirmed duplication and information loss

### P0 design: target-qualified `&` is not preserved in Core

`MinicTargetInlineAsmContract` in `backend.h` has no early-clobber field.
`core_lower_asm_target.c:core_asm_try_target_register_contract` zeroes the
Core structured instruction, copies `contract.kinds[i]` and source values,
but never copies the source output constraint's `&`. Consequently every
**target-qualified** Core operand has `early_clobber == false` whether the
GNU source said `=&r`/`+&r` or not. This is a definite transport-contract
information loss, **not an established wrong-code incident for every form**:
the current dedicated emitters sometimes explicitly allocate disjoint fixed
registers and recover the physical behavior.

Real affected families:

- AArch64 `aarch64_linux_hyp_spin_source` accepts three `=&r` outputs in
  `aarch64_linux_hyp_spinlock_lse.c`, but `aarch64_linux_hyp_spin_emit`
  receives zero early flags. Its two union register outputs are transported
  as `MEMORY_OUTPUT` pointer roles, so blindly setting Core's existing
  `early_clobber` bit would fail `core_ir_verify.c` (which permits that bit
  only for `REGISTER_OUTPUT`/`REGISTER_READWRITE`).
- AArch64 `aarch64_linux_cmpwait_form` accepts `=&r`, `+Q`, `r`; the native
  emitter checks only that later operands have no early flag. The source
  output's constraint is not represented in the target-qualified plan.
- LoongArch `loong_linux_xchg_small_source` accepts two `=&r` outputs and
  emits physical LL/SC in `loong_linux_xchg_small_emit`; similarly
  `loong_linux_futex_cmpxchg_source` accepts `=&r` with a `t0` clobber.
- X86 Linux futex source accepts `=&a` and `=&r` in alternative/loop forms;
  the native emitter receives no explicit early-clobber proof.

**Required model:** distinguish original GNU physical-register early-clobber
constraints from Core's value/address transport kind. A record result may
be Core-address-backed yet physically an early-clobber register output. The
new plan must retain source early-clobber, tied-input relation, and register
assignment exclusions; do not rely on a zero-initialized boolean.

### P1: template/role classification is repeated after acceptance

- x86 `asm_contract.c:minic_x86_64_qualify_inline_asm` is a long ordered
  first-match chain of `*_source` predicates; `machine_asm.c:
  minic_x86_64_emit_structured_asm` is another ordered chain of
  `*_form`/`emit_*` predicates. Both must agree on precedence and variant
  numbering. The shared `*_form` helpers reduce string duplication but do
  **not** eliminate duplicate dispatch or guarantee the same winning form.
- AArch64 `minic_aarch64_qualify_inline_asm` and
  `minic_aarch64_emit_qualified_structured_asm` both classify source
  alternatives/LSE/TLBI/sysregs after Core normalized named operands.
- LoongArch `minic_loongarch64_qualify_inline_asm` and
  `minic_loongarch64_emit_qualified_structured_asm` repeat CSR/AMO/futex/
  per-CPU/small-xchg form decisions. Its SIMD path is a valuable partial
  precedent: `simd_asm.c:minic_loongarch64_simd_emit_structured` uses
  `source_inline_asm_id`, requalifies the original source, and compares
  parsed source and Core machine effects before emitting. Preserve that
  safety property in a unified plan.

### P1: “not mine” and “mine but invalid” are both `false`

`minic_backend_qualify_inline_asm` returns only bool. A rejected target
form therefore falls toward generic Core unless
`minic_backend_requires_qualified_input_asm` catches a textual prefix.
The x86 `I`/`Ir` guard and AArch64/Loong reserved opcode lists are intentional
**fail-closed protections**. They are also a second, coarse language for
ISA ownership, potentially diverging from exact target source forms. A new
target form could forget to update that list and acquire a generic fallback.
Replace with a tri-state **unowned / qualified / owned-invalid** verdict
from the same target form recognizer before any Core mutation; preserve the
existing guards until negative tests prove equivalent rejections.

### P1: effective hardware memory effect is conflated with GNU clobber

Target Core lowering computes `has_memory_clobber` as source explicit flag
OR `contract.implicit_memory_effect` OR `MEMORY_READWRITE` operand.
That Core field is needed for conservative ordering. But it no longer says
whether source GNU included literal `"memory"`. For example, x86
`x86_linux_futex_extable_source` allows explicit memory clobber only for
form 1, while all forms request implicit hardware effects. Its emitter
observes the effective Core memory flag and reparses the template. A typed
plan should retain two facts: `explicit_memory_clobber` and
`effective_memory_read/write/barrier`, each checked independently.

### P2: Core still owns one ISA-shaped shortcut

`core_asm_try_immediate_output_csr` is described as Linux RISC-V CSR `rK`
specialization, while generic structured lowering also admits `rK`. This
is an ordered optimization with C-side effect transport and fixed-register
invariants; move the *register-vs-immediate choice* to a target-owned form
policy only after preserving both alternatives (and the empty-template rK
ordering case). No blind deletion is safe.

## Real source-form matrix — keep original GNU contracts

| Source fixture and GNU form | Source qualifier → Core owner → machine consumer | Non-negotiable evidence |
| --- | --- | --- |
| `x86_linux_cmpxchg16b_atomic.c`: `=@cce`, `+m`, `+a/+d`, `b/c`, `"memory"` | `x86_linux_cmpxchg128_source` → target structured → `emit_linux_cmpxchg128` | ZF boolean, 16-byte atomicity, RBX save, `.smp_locks` PREL32 metadata and native GCC differential |
| `x86_futex_extable_atomic.c`: locked CMPXCHG/XADD or retry loop, `+r`/`=&a`, `+m`, tied inputs | `x86_linux_futex_extable_source` → target structured → `emit_linux_futex_extable` | `__ex_table` recovery, tied register values, implicit vs explicit memory effect, fault/native guards |
| `probe-x86-linux-static-key-goto.sh`: `__jump_table`, `i` symbolic inputs, `%l[l_yes]` | `minic_x86_64_qualify_inline_asm_goto` → Core resolved-immediate OPAQUE goto → `emit_linux_jump_label_asm_goto` | actual CFG successor, PREL32/PIC relocations, symbol spelling, malformed label rejection |
| `x86_asm_uppercase_I_unqualified.c` and lowercase `i` counterpart: `I`/`Ir` vs `i` | x86 reserved input-ISA gate → target or reject; generic immediate path only for admissible form | `I` 0..31 ISA range, no lowercase confusion, every invalid machine form rejected |
| `aarch64_linux_hyp_spinlock_lse.c`: `=&r`×3, `+Q`, `Q`, LDXR/STXR plus LSE alternative | `aarch64_linux_hyp_spin_source` → target structured → `aarch64_linux_hyp_spin_emit` | early-clobber nonaliasing, split record output, section `.altinstructions`/`.org`, cross GAS/QEMU |
| `probe-aarch64-linux-cmpwait.sh`: `=&r`, `+Q`, `r`, WFE/LDXR/branch | `aarch64_linux_cmpwait_form` inside qualifier → target structured → corresponding emitter | read/write address + ordering and exact width/exclusive-load sequence |
| `aarch64_linux_lse_atomic_full.c`: CASP `+&r` pair, `+Q`, named inputs and `.arch_extension lse` | `aarch64_linux_lse_casp128_source` → target structured → native AArch64 LSE CASP emitter | pair register exclusion, 128-bit value, GNU named aliases, hardware/GAS/QEMU differential |
| `probe-aarch64-linux-jump-label.sh`: single `i` symbolic key and `__jump_table` | `minic_aarch64_qualify_inline_asm_goto` → Core resolved-immediate OPAQUE goto → `minic_aarch64_emit_linux_jump_label_asm_goto` | branch/NOP variant, PREL32 table, one genuine C label edge |
| `loong-linux-small-xchg.c`: `=&r`×2, `=ZC`, `ZC`, `Jr`×2 | `loong_linux_xchg_small_source` → target structured → `loong_linux_xchg_small_emit` | 8/16-bit neighbor preservation under LL/SC; no fake subword load/store; QEMU repetitions |
| Loong Linux futex CMPXCHG source in `asm_contract.c`: `+r`, `=&r`, `=ZC`, `ZC`, `Jr`×2, `t0` clobber | `loong_linux_futex_cmpxchg_source` → target structured → `loong_linux_futex_cmpxchg_emit` | old/result outputs, retry semantics, source early-clobber and register exclusivity |
| `probe-loongarch64-linux-jump-label.sh`: `i` symbolic key, `__jump_table` | `minic_loongarch64_qualify_inline_asm_goto` → Core OPAQUE resolved-immediate goto → `minic_loongarch64_emit_linux_jump_label_asm_goto` | branch/NOP alternative and actual C CFG edge |
| `probe-loongarch64-linux-simd-asm.sh`: named SIMD memory transfers and source provenance | `minic_loongarch64_simd_qualify_source` → target structured with source ID → `minic_loongarch64_simd_emit_structured` | original vs normalized opcode/register/memory-effect equivalence; reject injected forms |
| `core_asm_empty_rk_immediate.c`: zero-byte `asm volatile("": :"rK"(0))` | no machine prefix → Core empty-effect path | ordering semantics retained; must never bake into invented machine instruction |
| `gnu_inline_asm_readwrite_earlyclobber.c`: `+&r`×3, `r`, memory | generic structured role path → RISC-V register emitter | all three early-clobber flags, output vs input nonalias, native/GCC differential |

The matrix names existing source fixtures/tests where available; it is a
review checklist, **not** a claim that all these tests passed after the
ongoing concurrent edits.

## Staged replacement and deletion gates

**Batch A — pure admission, no machine emission change.** Add target-owned
`AsmFormId` and `AsmOperandPlan` with source ID, source template hash,
operand order, GNU constraints and access, early-clobber and tied-index,
fixed-register ownership, clobbers, form classification, and explicit vs
effective memory/IO/barrier effects. Qualification returns a tri-state
decision. Core materializes only the plan; keep the current native emitters
unchanged and retain the reserved-prefix denial as a backstop. Add unit tests
that verify every source `=&r`/`+&r` flag survives, including record-backed
physical output without falsely setting Core memory-address early flags.

**Batch B — migrate source/emit pairing by isolated form families.** Start
with Loong small-xchg, Loong futex, AArch cmpwait/hyp-spin, then x86 simple
ALU/bitops. Machine consumers dispatch on verified `AsmFormId`, not another
ordered template chain. Reuse exact existing `*_form` helpers for
source-normalized text matching while migrating; delete each replaced
`*_source`-vs-emitter second-dispatch pair only after positive and mutated
negative form tests. The owner of GNU constraint assignment stays the target,
not Core. Preserve source provenance (Loong SIMD already demonstrates this).

**Batch C — metadata-heavy x86 and AArch ISA.** Migrate x86 CMPXCHG16B,
futex `__ex_table`, static-key goto, alternatives; AArch LSE CASP,
`__jump_table`, encoded sysreg and `.altinstructions`. Compare original
GNU AS object sections, relocations, disassembly, flags and QEMU behavior,
not just `minic-cc -S` or the textual mnemonic. Do **not** remove dedicated
goto symbolic-immediate resolution until a plan carries the same relocation
identity and all C label successors.

**Batch D — retire overlap.** Once all form IDs are consumed, remove the
replaced target `requires_qualified_input_asm` prefix tables, duplicate
target source/machine first-match order, and replaced Core shortcut branches.
Keep generic Core register/memory/empty-template semantics and fail-closed
opaque hardware ASM admission. Audit that new ISA families require only a
target registration/policy and target implementation, never Frontend/Core
architecture switches.

### Acceptance and regressions required per batch

1. Full `-Werror` rebuild, `check-fast`, `check-minic-target-boundaries`,
   five ISA `-S`, positive and negative early-clobber/tied constraints.
2. Original GNU compiler/assembler differential for source/target matching;
   mutate one constraint, clobber, register, label, immediate range and
   alternative-section token at a time and verify **no fallback** succeeds.
3. X86 native runtime ZF, bitops, futex/error results; AArch and Loong
   GNU/GAS/QEMU for LL/SC, LSE and wide record operands.
4. Binary inspection of `.smp_locks`, `__ex_table`, `__jump_table`,
   `.altinstructions`, relocation addends and alternative padding.
5. Exact-head immutable GNU Linux 6.6.143 three-ISA × seven-shard census;
   report actual source counts/config and regressions separately from native
   runtime and final ELF/Image/QEMU acceptance.

Only delete an old branch when all of its callers and negative cases have
been enumerated, and the same target form ID drives Core transport and native
emission. This ensures a structural reduction instead of layering one more
shim over the current dispatch system.
