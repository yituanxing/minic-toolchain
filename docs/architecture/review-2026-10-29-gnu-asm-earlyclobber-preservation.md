# GNU inline ASM source early-clobber transport — 2026-10-29

Scope: shared target-qualified GNU inline ASM operand transport, plus the
existing target-independent generic structured path. This batch **preserves
source constraint metadata**; it **does not certify a complete early-clobber
register-allocation algorithm on every target**.

## Verified source → Core → native distinction

- `parser_gnu_inline_asm.c` retains complete GNU operand constraint strings.
- `backend.c:minic_backend_qualify_inline_asm` calls the existing exact ISA
  classifier, then centrally captures output `&` into
  `MinicTargetInlineAsmContract.gnu_early_clobber[]`. It clears the entire
  array after the ISA-specific qualifier, so target authors cannot override
  or accidentally leak contradictory early bits. Inputs are always false.
- `core_lower_asm_target.c` transports this target-neutral fact to the Core
  structured operand's **separate** `gnu_early_clobber`. Do not indiscriminately
  set existing `early_clobber`: the x86/AArch/Loong native emitters currently
  treat it as their legacy Core transport/allocator form check and may reject
  formerly accepted source. Moreover, some GNU `=&r` outputs (AArch hyp
  spinlock) transport a record's destination as a Core `MEMORY_OUTPUT`, for
  which Core verifier forbids the old `early_clobber` bit.
- The generic structured path already captures `early_clobber` for its real
  `=&r/+&r` register outputs. It now also saves exactly that bit into the
  shared `gnu_early_clobber` for the same output, without altering the
  established RV64 register exclusion logic.
- `core_ir_verify.c` validates that a physical early source flag can only
  occur on an output/readwrite register or source-qualified output/RMW
  memory transport role; it rejects a scalar/memory input bearing the flag.
- Native target form recognition, register reservations, fixed registers,
  exception-table relocation, C goto CFG, and emitted assembly remain
  unchanged. The new source flag is available to a subsequent typed target
  operand allocator, but current machine lowering does **not** yet consume
  it as a universal register-interference constraint.

## Fresh executable evidence

Command:

```sh
make -j8 BUILD_DIR=build/review-earlyclobber CFLAGS=-Werror all
make -j8 BUILD_DIR=build/review-earlyclobber CFLAGS=-Werror check-fast
bash tests/compiler/c0/probe-target-gnu-earlyclobber-transport.sh \
    build/review-earlyclobber/bin/minic-cc
```

Observed output:

```text
TARGET_GNU_EARLY_SOURCE=x86_64 preserved=9
TARGET_GNU_EARLY_SOURCE=aarch64 preserved=4
TARGET_GNU_EARLY_SOURCE=loongarch64 preserved=2
PASS: x86 futex 6 atomic variants, 9 fault sites, EFAULT register 8, GNU/MiniC native exit=42
PASS: unknown futex GNU instruction suffix fail-closed
TARGET_GNU_EARLY_CONSTRAINT=PASS targets=5 qualified=3 generic_rv64=1 rv32=fail_closed
```

The first three numbers are Core transport events printed by the opt-in
`MINIC_GNU_ASM_CONSTRAINT_TRACE` (`gnu_early=1 core_early=0`); they are
**not** independently proven physical-register allocation counts. The x86
fixture is the original Linux futex template with `=&a` and `=&r` outputs;
real MiniC and GCC native executables both exit 42. This demonstrates one
concrete correct machine form, not all x86 ASM constraints.
Both the full strict build and `check-fast` exited 0 after the source
metadata and generic Core preservation changes (the final test script was
then rerun and also exited 0).

Additional source-only/fail-closed checks passed:

```sh
MINIC_CC=build/review-earlyclobber/bin/minic-cc \
  sh tests/compiler/c0/probe-aarch64-linux-hyp-spinlock-lse.sh
MINIC_SOURCE_ONLY=1 bash \
  tests/compiler/c0/probe-loong-linux-small-xchg-qemu.sh \
  build/review-earlyclobber/bin/minic-cc
MINIC_SOURCE_ONLY=1 bash \
  tests/compiler/c0/probe-aarch64-linux-cmpwait.sh \
  build/review-earlyclobber/bin/minic-cc
```

Five ISA `-S` sanity in the new probe means three target-qualified GNU
forms plus a generic RV64 `+&r` and an RV32 C baseline. The **same** RV32
specialized fixed-register `+&r` test is correctly rejected; RV32 support
for that original form is not claimed. AArch64 and LoongArch GNU cross
toolchains/QEMU were not available locally; source-only checks and negative
form rejection passed, full cross-runtime remains CI work.

## Open semantic safety gate

**OPEN:** A global verifier/allocator currently does not prove that arbitrary
target-qualified early output machine registers cannot alias live inputs. The
existing native form emitters use hand-verified, usually fixed physical
register assignments and revalidate recognized GNU forms. Before deleting
that form-specific validation or claiming complete physical semantics:

1. Attach typed physical register masks, early-exclusion edges and tied-input
   indices to a verified `AsmOperandPlan` for every target-qualified form.
2. Make every machine emitter consume and check those constraints, including
   multi-register record outputs and tied operands.
3. Mutate each early constraint to `=r`/`+r`, each input binding to a forbidden
   overlap, and each output-order/relocation operand individually. Known
   source qualifiers must still reject invalid forms; generic fallback must
   never silently accept a reserved machine template.
4. Differentially exercise original GNU Linux variants with GCC, GAS,
   native x86 and AArch64/Loong QEMU. Include `.smp_locks`, `__ex_table`,
   `.altinstructions`, and `__jump_table` object section/relocation checks.

Until those gates hold, treat this as a **verified transport fix and native
regression preservation**, not a blanket machine-allocation correctness fix.
