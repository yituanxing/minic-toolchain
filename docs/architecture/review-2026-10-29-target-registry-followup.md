# MiniC follow-up: single registered-target inventory and ISA option policy

Date: 2026-10-29. This batch changes only the compiler target registration
boundary and driver/target-policy queries. It does not claim codegen or QEMU
runtime correctness beyond the checks listed below.

## Confirmed architectural problem

Previously a fifth target was registered independently in three places:

- `target_catalog.c`: architecture, command-line name and staged-driver maturity;
- `target_info.c`: architecture repeated inside language/ABI model descriptors;
- `backend.c`: five architecture/name initializers plus a separate switch
  selecting the machine emitter.

Additionally both CLIs embedded the RV64 default spelling, and two driver
branches enumerated the RISC-V architecture numbers to select ISA-option
handling. This made registration drift possible even when Core and Frontend
contained no machine architecture switches.

## New invariant

`src/target/target_registration.def` is the sole inventory of registered
compiler targets. Each row identifies the architecture ID, local target
registration key, CLI spelling, standalone driver maturity and ISA-option
family. The three consumers expand the same list:

1. `target_catalog.c` emits the separately linked product-driver catalog.
2. `target_info.c` maps each catalog row to its **own** C ABI/language
   descriptor and target-specific `MinicDataLayout`; integer models may
   share C widths, but no backend, varargs or call ABI is inferred from those
   shared widths.
3. `backend.c` maps the row to the **own** machine emitter and typed GNU ASM
   qualifier. The second architecture switch and unused backend architecture
   and spelling copies have been removed. `emit_assembly` validates the
   selected backend against the same registry, and unknown targets fail closed.

Both `minic` and `minic-cc` now take their default target from the catalog.
For `-march`/`-mabi`, they query the target's `isa_family`, not hardcoded
RISC-V architecture-number tests. The product driver still admits staged
assembler/linker only when the catalog row grants that stage (currently RV64).

`MinicRiscvProfile.features` continues to describe **available target/source
ISA**; `codegen_features` remains the independently qualified **MiniC-generated
instruction subset**. This refactor does not equate them or silently extend
assembler/linker maturity.

## Verification / deliberate non-goals

- Strict full `make BUILD_DIR=build/registry-pass CFLAGS=-Werror all`: PASS.
- `check-four-target-model-m0`: PASS. Extended to check catalog, TargetInfo,
  Backend identity, uniqueness, ISA-family consistency, default RV64 and
  unknown-architecture rejection across the same five entries.
- `probe-minic-driver-target-catalog.sh`: PASS for five CLI `-S` targets,
  staged-RV64-only rejection and unsupported options.
- `check-target-selection-m0`: PASS for RV32/RV64/x86/AArch64/LoongArch
  target-selection and the historical RV64 default parity.
- `check-minic-target-boundaries.sh`: PASS; updated the structural contract
  to require one registration file instead of five duplicated descriptor IDs.
- Full `check-fast`: intentionally **not reported as passing** at this
  intermediate shared worktree. A concurrent RV64 hard-float ABI rewrite
  invalidated the old `abi_test.c:218` expectation; the owning agent is
  investigating. This is not an isolated registry failure verdict.

This batch does not alter `data_layout.c` object/bitfield algorithms,
machine ABI parameter placement, or target-specific GNU ASM legalization.
Separate follow-ups remain for typed builtin/ASM policy decomposition and
full runtime correctness. A new target should still explicitly supply its
own language descriptor and machine emitter; registration no longer requires
parallel independent architecture lists or any Frontend/Core edits.
