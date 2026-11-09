# Shared GlobalEmissionPlan ABI facts — 2026-10-29

## Scope and ownership

Second-round Common+x86 review. Edited only
`src/target/common/global_emit.{c,h}`, `src/target/x86_64/global_emit.c`,
`src/target/x86_64/asm_contract.c`, and dedicated `tests/compiler/c0`
fixtures/scripts. No Core, target registry, AArch64, RV, or LoongArch
production changes. **Not committed or pushed** by this work item.

## P1: x86 global emitter bypassed common ABI plan — fixed

### Evidence before the change

`src/target/x86_64/global_emit.c` previously repeated its own DataLayout
type flattening, array/record classification, section choice, scalar bits,
zero-relocation dispatch and nested-layout selection despite Common already
owning `MinicMultiGlobalEmissionPlan`.

The new real C source `tests/compiler/c0/x86_global_plan_abi.c` exercises
float/double payloads, scalar pointer relocation, 2D string pointer grid,
recursive record object/function relocations, flexible array member, and
two custom ELF sections. With GCC 12 `readelf -SW`:

| Section | GNU object | Original MiniC x86 object | Fixed MiniC x86 object |
| --- | --- | --- | --- |
| `.bss.minic.global` | NOBITS | PROGBITS (GAS warning) | **NOBITS** |
| `.bssish.minic.global` | PROGBITS | PROGBITS | **PROGBITS** |

Original MiniC actually emitted GAS warning `setting incorrect section type
for .bss.minic.global` even though native runtime happened to pass. The
generic Common helper also used an overly broad `.bss` **prefix** test
which would incorrectly classify `.bssish`; both issues required correction
before sharing the section policy.

### Structural correction

- `minic_multi_global_emission_plan_build` is now the **sole** payload
  classification owner for x86 and the existing common ELF emitter. It
  admits semantic float/double scalar bits and a precisely validated
  pointer-width scalar relocation (single scalar slot, zero overlay,
  correct relocation index and location kind).
- `minic_multi_emit_global_payload` is the **one** checked layout writer for
  ZERO, ZERO_RELOCATIONS, RECORD_FLAT, RECORD_RECURSIVE,
  RECORD_FLEXIBLE, ARRAY_RECORD, ARRAY_RECURSIVE,
  ARRAY_SCALAR_RELOCATIONS, SCALAR_RELOCATION and SCALAR_BITS. X86 feeds
  its own relocation and scalar-bit rendering callbacks.
- `minic_multi_emit_elf_global_section` and
  `minic_multi_emit_elf_global_binding` are shared ELF metadata helpers.
  A named NOBITS section must be `.bss` or `.bss.*`; `.bssish` stays
  PROGBITS, as independently verified against GNU. The planner also rejects
  nonzero initializer slots or symbol relocations inside a named `.bss.*`
  NOBITS section before generating invalid GAS, matching GNU's compile-time
  rejection of this source category.
- The x86 emitter no longer walks DataLayout, checks aggregate variants,
  handles scalar relocation indexes or chooses section kinds itself.
  Remaining x86 code owns `.byte/.word/.long/.quad` spelling,
  `R_X86_64_64` symbolic expressions/label targets and symbol lifecycle.
- Common's existing AArch64/LoongArch/RV consumers keep their target
  callback wiring; no two-architecture-specific selector was introduced.

## Validation (post-change)

`make -j8 build/debug/bin/minic-cc` exited 0 with the repository's strict
`-std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow`
compile settings. `git diff --check` exited 0.

`sh tests/compiler/c0/run-x86-global-plan-abi.sh` — **PASS**:

- GCC and MiniC compile, link and execute the **same real C fixture**;
  both native binaries return zero.
- Both object files have the exact `.bss.minic.global` NOBITS and
  `.bssish.minic.global` PROGBITS section types and no MiniC incorrect
  section type warning.
- Both GNU (`-c`, not merely `-fsyntax-only`) and MiniC reject the new
  `invalid_x86_bss_nonzero_initializer.c` named-BSS test.
- All **9** inspected ELF object symbol sizes match GNU, including
  `global_float=4`, `global_double=8`, `global_pointer=8`,
  `global_nested=32`, `global_pointer_grid=32`, `global_flexible=20`.
- Both object files contain **8** `R_X86_64_64` global-data relocations.
- The preprocessed global ABI fixture emits valid `-S` output for all five
  targets: RV32, RV64, x86-64, AArch64 and LoongArch64.

Existing independent regressions remain **PASS**:

- `probe-linux-global-section-reloc.sh` x86-64: 3 named sections, weak
  symbol, sparse array and record relocations, injection rejection.
- `probe-chained-static-array-relocations.sh`: five-target nested grids,
  overwrite handling, 9 native x86 ELF relocs and all invalid ranges.
- `probe-gnu-global-array-static-provenance-5isa.sh`: five-target compile,
  GNU/MiniC x86 native runtime.

## Concurrent x86 I-constraint regression — fixed, separate contract

The remote four-architecture CI x86 overflow-group step failed at
`tests/compiler/c0/probe-x86-rsp-readwrite-output.sh`, not the subsequent
local-hardreg probe. MiniC accepted `asm volatile("nop" : : "I"(31))`
and emitted only `nop` while silently losing an x86-specific GNU constraint.
GNU permits value 31 for x86 `I`, but this does **not** qualify arbitrary
machine templates for Core's generic inline ASM.

Target-owned `minic_x86_64_requires_qualified_input_asm` now checks every
input constraint text for uppercase `I`, including mixed `Ir`, forcing
the existing target-qualified admission path. Lowercase GNU `i` remains
distinct: it is a generic constant and is not subject to this x86 `I`
gate. This does not add x86 branches to Frontend or Core.

Verified **PASS**:

- `probe-x86-rsp-readwrite-output.sh` including `X86_GNU_I_CONSTRAINT`.
- `probe-x86-linux-lock-cc-native.sh` real qualified Linux `I` forms.
- New `probe-x86-immediate-qualification.sh`: GCC accepts three source
  shapes, MiniC native-runs lowercase `i` and correctly fails closed for
  unqualified uppercase `I` and compound `Ir`.

## Follow-up considerations (not conflated with correctness)

The common recursive initializer walker still emits many explicit
`.zero 0` directives while preserving byte offsets. GNU GAS warns
`space repeat count is zero, ignored`; object semantics and linked runtime
are correct. It is an output-quality/performance cleanup candidate,
not evidence of an ABI mismatch. Measure output size and compile overhead
before changing the shared recursive walker.

## Independent cross-review after parallel architecture changes

This final pass was **read-only for production code** and checked changes
owned by the Core, registry and LoongArch teams. Findings and ownership:

| Priority | Evidence and boundary | Resolution or owner |
| --- | --- | --- |
| P1, real | LoongArch `memory_emit.c` originally trusted the natural **pointee type** of a `PARAMETER` (and reloaded `LOAD`) as an address alignment proof, although a pointer can point into a packed record. Its wide `__int128`/`long double` path bypassed the guarded scalar accesses by emitting dual raw `ld.d/st.d`; GCC-valid packed `char + __int128` is a concrete example. | Informed LoongArch owner, who removed parameter/reloaded-pointer proof and routed each 8-byte half through guarded byte gather/scatter. Independently ran `probe-loongarch64-packed-scalar-alignment.sh`: PASS, including two wide-half guards each for read/write and a static natural-alignment fast path. |
| P1, real capability gap | Core `core_lower_bit_fields.c` requires `bit_offset + field_width <= storage_width` and byte count no greater than the declaration-width storage size. GNU permits `struct __attribute__((packed)) X { unsigned first:1; unsigned value:32; };` (5 bytes); GCC preprocessing succeeds but MiniC x86 `-S` rejects both read/write with Core `lower-status=1`. | Added independent `tests/compiler/c0/packed_bitfield_cross_word_window.c` GCC oracle fixture, then transferred ownership to the Core team for the necessary wider RMW/extract and native-equivalence regression. This issue is not claimed fixed in this report. |
| P2, future ISA safety | Common `minic_multi_emit_record_bit_field_run` unconditionally packs record bit-fields into bytes with LSB-first little-endian numbering. The new Core byte-window explicitly checks `minic_data_layout_is_little_endian`, whereas this global-initializer path does not. | All five current TargetInfo DataLayouts are little-endian, so **no current mismatch**. Add a DataLayout byte-order gate before registering a big-endian target to fail closed instead of silently serializing incorrect bytes. |
| P2, registry integrity | `target_registration.def` is one X-macro inventory consumed in the same order by `target_catalog.c`, `target_info.c` and `backend.c`; all five current architecture IDs/names are distinct. The list begins with RV32 while the architecture enum begins with RV64, but consumers search by architecture instead of indexing with raw enum values. | **No observed index mismatch or duplicate registration.** Future duplicate IDs/spellings would be accepted by the first-match lookup; a unique-ID/name validation could make this fail closed at initialization/test time. |

GlobalEmissionPlan/DataLayout integration currently preserves the principal
ownership separation: the active `MinicTargetInfo` supplies DataLayout for
object size/alignment, the common payload plan chooses layouts and section
classes, and each target retains relocation expression and scalar-bits
rendering callbacks. No additional concrete x86/global object ABI regression
was established during this cross-review.
