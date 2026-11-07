# MiniC x86-64 global emitter — Common plan convergence, batch 1

Date: 2026-10-29. Scope: `src/target/x86_64/global_emit.c` only. This is a
follow-up to the 73-file Common/x86-64 read-only review; no Common production
source or backend ABI/relocation encoder was changed in this batch.

## Files and read coverage

| File | Depth | Purpose |
| --- | --- | --- |
| `src/target/x86_64/global_emit.c` | full, 222 baseline lines | all payload/ELF branches and target callback behavior |
| `src/target/common/global_emit.h` | full, 107 lines | `MinicMultiGlobalEmissionPlan` API and payload variants |
| `src/target/common/global_emit.c` | planner and ELF section/binding/emitter read in full, other helper regions inspected | compare shared classification with native path; **no edits** |
| `tests/compiler/c0/probe-x86-linux-static-record-zero.sh` | full | implicit zero record runtime oracle |
| `tests/compiler/c0/probe-x86-linux-zero-extent-record-arrays.sh` | full | zero-size array ELF symbol oracle |
| `tests/compiler/c0/probe-gnu-nested-union-compound-record-array.sh` | full | nested record/union native runtime and five-target source coverage |
| `tests/compiler/c0/probe-chained-static-array-relocations.sh` | full | flattened relocations and malformed-designator rejection |

## Changes applied

1. **Deleted a duplicate all-zero record initializer scan.** For *record* globals
   without relocations only, x86 now calls the established
   `minic_multi_global_emission_plan_build`, takes the checked
   `storage_size`/`storage_alignment`, and uses the shared `ZERO` payload
   decision. This covers implicit zero and explicit all-zero slots, including
   nested records/bitfields/padding. It deliberately keeps x86's own ELF
   section selection, `.word/.long/.quad` spelling and relocation callback.
   The common plan is *not* invoked for x86 floating/scalar/relocating globals
   until their contracts can be made equivalent.
2. **Fail closed before dereferences/output.** x86 now rejects null
   output/program/target/DataLayout, a missing relocation array for a
   nonzero relocation count, and unknown visibility enums before emitting
   any symbol. The Common planner already enforced these checks. This
   reduces a target-only crash/partial-output surface without changing any
   admitted valid object.

Production change size: `global_emit.c` **+20 / -21 lines** versus starting
head; one target file touched. No new adapter layer or generic fallback.

## Why full migration is deliberately deferred

These are code-confirmed semantic differences, not just different spelling:

| Concern | Existing x86-64 | Existing Common plan | Impact / prerequisite |
| --- | --- | --- | --- |
| Initialized floating scalar/array | accepts `float` and `double` bit patterns | terminal `SCALAR_BITS` admits only integer/pointer leaves | blind adoption rejects valid x86 globals; extend checked scalar categories first |
| Mutable uninitialized default section | `.bss` only when zero/tentative flag and no initializer/reloc | early zero-without-initializer branch defaults to `.bss` regardless of flag | output/section contract needs a unified model and GNU differential |
| Explicit `.bss*` ELF attribute | x86 hardcodes `@progbits` | Common ELF section helper chooses `@nobits` | object section type mismatch; cannot silently change ABI-visible ELF metadata |
| Pointer global, nonzero relocation with initializer slot | target checks exactly one scalar relocation and zero placeholder | Common planner rejects scalar with initializer and relocation; ZERO_RELOCATIONS requires zero initializer count | plan lacks a native scalar-relocation payload variant |
| Zero-relocation classification | uses `is_zero_initialized` and zero initializer count | Common ZERO_RELOCATIONS admission does not require this flag | possible admission expansion; validate semantics, do not loosen x86 |
| Flexible record payload | target continues legacy flat/recursive choice | Common adds `RECORD_FLEXIBLE` branch | serializer behavior must be proved equivalent before switching |
| Nested relocatable pointer arrays | target selects recursive aggregate or flat relocation walk | Common has analogous classification | promising second migration once exact per-slot differential is in CI |

Additional risk: even current x86 relocation/record serializers may return
failure **after** some assembly has been written. Driver transaction-based
output protects the final `-o` file, but the emitter itself is not an
all-object preflight API. This is a separate architectural convergence item.

## Verification on the modified working tree

- `make -j8 build/debug/bin/minic-cc CFLAGS=-Werror` — **PASS**.
- `probe-x86-linux-static-record-zero.sh` — **PASS**, native GCC differential.
- `MINIC_CC=build/debug/bin/minic-cc probe-x86-linux-zero-extent-record-arrays.sh`
  — **PASS**, GNU/x86 ELF symbols for three zero-size record-array cases.
- `MINIC_CC=build/debug/bin/minic-cc probe-gnu-nested-union-compound-record-array.sh`
  — **PASS**, five-target source and native x86/GNU runtime.
- `MINIC_CHAINED_ARRAY_SOURCE_ONLY=1 probe-chained-static-array-relocations.sh`
  — **PASS**, five-target source, nine native x86 relocation records and five
  out-of-bounds negative cases. Without source-only, harness needs unavailable
  non-x86 cross-assembler executables and stops; this is an environment gap.
- `MINIC=build/debug/bin/minic-cc run-static-record-array-suffix-attributes.sh`
  — exit **0**.
- x86 compile+GCC assemble of `static_floating_aggregate.c` — **PASS**, guards
  against accidentally routing x86 FP globals to Common's integer-only path.
- x86 compiler negative `invalid_too_many_global_initializers.c` — rejected
  (`too many nested static array initializers`), as expected.
- `git diff --check -- src/target/x86_64/global_emit.c` — **PASS**.

No fresh exact-head seven-way Linux compile CI or runtime differential was
triggered by this scoped batch. That remains the parent integration gate.

## Next work in dependency order

1. Give Common plan an explicit ABI-independent scalar-relocation variant and
   validated FP constant bits path, preserving target-owned ELF spelling.
2. Specify one section-policy contract with explicit GNU `.bss*`/COMDAT tests;
   avoid changing live ELF metadata on naming alone.
3. Migrate nested pointer/record serializer choice once plan classes are
   semantically equivalent, then delete redundant target branches in a single
   tested batch.
