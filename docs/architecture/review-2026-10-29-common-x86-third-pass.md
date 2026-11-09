# MiniC Common/x86 third-pass consolidation — 2026-10-29

## Ownership and audited seams

This pass follows the complete 73/73 Common+x86 source review in
`review-2026-10-29-common-x86.md` and the shared global plan in
`review-2026-10-29-global-emission-plan-abi.md`. It changes **only**
`src/target/common/lp64_scalar.c`, `src/target/x86_64/{abi.c,asm_contract.c}`
and dedicated tests. Target registry, Core, Frontend, AArch64, RV and
LoongArch are outside this ownership scope. No commits or pushes.

| Entry into target | Cross-function dependency | Current single owner | Remaining boundary |
| --- | --- | --- | --- |
| `minic_core_call_view_build` | `x86_64/call_plan.c`→`minic_x86_64_one_gp_record` / `two_gp_record`→`machine_call.c` | Target SysV/Win64 register/stack placement in `call_plan.c`; recursively classified INTEGER records in `abi.c` | Other SysV SSE/mixed aggregate classes remain explicitly unsupported; don't fake them as INTEGER |
| Integer type semantics | `x86_64/abi.c::scalar_width` and Common `lp64_scalar.c`→integer width/sign | Shared integer rank/sign width check in `minic_lp64_scalar_integer_width`; x86 wrapper owns pointer-width policy | No Frontend/Core architecture switch |
| Inline GNU ASM source | `x86_64/asm_contract.c::qualify_inline_asm`→`backend.c`→Core structured operand bindings | Source syntax and `I` qualifier are target-owned; 106 regular source matchers are an ordered table | Matchers are source policies, not machine opcodes |
| Qualified GNU ASM machine form | Core structured ASM→`x86_64/machine_asm.c::emit_structured_asm`→target instruction + ELF text | Machine register, clobber, operand and physical instruction semantics remain target-owned | Many machine branches repeat source-template detection; a future typed target ASM semantic ID would remove double parsing, but would require a Core/backend contract migration |
| Global object | DataLayout→Common `GlobalEmissionPlan`→x86 relocation/float-bit callback→transaction ELF output | One aggregate layout and section owner in Common; x86 only renders ABI-specific symbol reloc expression/directives | Flat record-array and recursive initializer walkers remain separate to preserve partial/sparse slot semantics used by RV paths; no proof that deleting flat path would preserve all Linux inputs |

## Batch A — recursive SysV aggregate ABI classifier unified

Before: `x86_one_gp_integer_fields` and `x86_two_gp_integer_fields`
independently recursively walked records/unions, bit-fields, array members,
alignment and integer leaves. This duplicated the hard semantic algorithm
and made bugs/ABI rule drift likely.

After: one `x86_integer_record_tree` takes an 8-/16-byte extent and a
record-relative byte position. The existing public one-/two-register ABI
gate functions retain their distinct top-level restrictions:

- One-GPR: exact 1/2/4/8-byte materializable aggregate sizes; packed
  integral bit-fields are permitted without imposing the ordinary scalar
  natural-alignment rule on them.
- Two-GPR: exactly 16 bytes, naturally aligned scalar leaf fields, no
  bit-field crossing an eightbyte boundary. Unsupported mixed or SSE
  records still fail closed.
- GNU empty nested records remain zero-byte NO_CLASS members.

Argument register consumption is still planned **once per call** in
`call_plan.c`, then consumed by `machine_call.c`. No copy of the classifier
was added to code emission.

## Batch B — shared width semantics

`minic_lp64_scalar_integer_width` no longer evaluates integer type/rank
twice for small and 64-bit cases. It preserves the old permitted rounded
small widths and the exact 64-bit case. `minic_x86_64_scalar_width` reuses
it for integers while retaining the target-specific 8-byte pointer gate.
Every caller still uses the current target's `TargetInfo`; there is no
hard-coded assumption that all ISA targets are signed-char LP64.

## Batch C — x86 GNU ASM source policy inventory

`minic_x86_64_qualify_inline_asm` had 106 repeated adjacent
`if (source_matcher(...)) return true;` branches, followed by three special
constructors (`atomic_cc`, `nospec`, `percpu`) and a generic arithmetic
fallback. It now runs the same **106 matchers in the same order** through
`minic_x86_source_qualifiers[]`, with a tiny adapter for the sole matcher
without a program parameter. The three special constructors and fallback
remain unchanged.

An independent mechanical comparison against the `HEAD` source matcher
sequence found 109 total source names, of which the first 106 **exactly
match** the 106 registry entries (zero missing/reordered), and the final
three are the intentionally special result constructors. This is a
target-owned policy registry, not a generic ISA opcode registry. The
physical GNU machine forms in `machine_asm.c` still re-check the accepted
operand shape and fail closed.

## Verification

The following were actually run after the change, not inferred from the
older Linux compilation census:

| Check | Result |
| --- | --- |
| `make -j8 build/debug/bin/minic-cc` with strict C warnings | PASS |
| New `probe-x86-sysv-integer-record-tree.sh` | PASS: 4 ABI classes, **independent GNU caller** of MiniC callee; 5/5 target `-S` |
| `run-x86_64-record-array-sret-sysv.sh` | PASS: two-way GCC/MiniC array records, empty record, by-value stack, hidden sret |
| `run-x86_64-abi-layout-elf.sh` | PASS: 16/32/64B alignment and four GNU ELF symbol visibility cases |
| `probe-x86-msabi-scalar-native.sh` | PASS: direct/indirect 6+6, positional FP, 32B shadow, four negative ABI cases |
| `run-x86-global-plan-abi.sh` | PASS: native runtime, 9 GNU ELF sizes, 8 relocations, named-section types |
| `probe-x86-linux-kvm-hypercall-full-abi.sh` | PASS: GNU assembler, relocations, four non-x86 ISA rejection checks |
| `probe-x86-rsp-readwrite-output.sh` | PASS: RSP, output/GNU `I` qualifier and caller-saved behavior |
| `probe-x86-immediate-qualification.sh` | PASS: lowercase `i`, uppercase `I`, mixed `Ir` |
| `probe-x86-linux-lock-cc-native.sh` | PASS: three native lock-cc modes + negative |
| `git diff --check` | PASS |

`check-minic-target-boundaries.sh` was also run, but reports an **unrelated
concurrent registry migration** assertion: it still expects
`tools/minic-cc/main.c` to textually include `target/riscv/isa_resolve.h`.
The registry owner was informed. This result is **not** counted as PASS and
must be rechecked after its test assertion is synchronized with the new
target registration inventory.

For this round's three production files, relative to original `HEAD`,
`git diff --numstat` reports **211 added / 373 removed** lines, including
17 previously authored x86 GNU-`I` constraint additions in the carried
working tree. This round's own net production change is therefore
approximately **194 added / 373 removed**; the three new focused test
files add **95 lines**.

## Primary ABI references and follow-up boundaries

- [x86-64 psABI: recursive aggregate/eightbyte classes and unaligned
  member rule](https://gitlab.com/x86-psABIs/x86-64-ABI/-/blob/master/x86-64-ABI/low-level-sys-info.tex).
- [LLVM's target ABI X86 classifier](https://github.com/llvm/llvm-project/blob/main/llvm/lib/ABI/Targets/X86.cpp):
  source type classification and physical register lowering are distinct
  responsibilities. The MiniC Common+Target boundary follows this
  separation on its currently implemented conservative INTEGER subset.

The two important remaining architecture tasks are (1) a future target
ASM *semantic descriptor* that prevents duplicate source-template parsing
between qualification and emission without letting target source spellings
leak into Core, and (2) proof-backed consolidation of the surviving flat
record-array payload walker and recursive walker across **all** target
consumers, with sparse/trailing-zero record initializer tests. Merely
moving the existing Linux template-specific branches into different files
would not resolve either interface problem.
