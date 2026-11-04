# MiniC P69: Source / Core / Module owner boundaries

This batch continues PR #674 after P68 (`2b321df`). It is an architecture
refactor, **not** five-ISA Linux runtime certification. The machine writers
still emit GNU assembly directly; no Machine IR is claimed yet.

## Sealed global-object contract

The C ingress resolves declaration merging and initializers before publishing
the Core Module. A published global has two independent identities:

| Module field | Meaning | Allowed values |
| --- | --- | --- |
| `definition_kind` | ELF-level symbol/definition ownership | `REGULAR`, `ZERO_FILL`, `DECLARATION` |
| `data_policy` | Physical byte and relocation constraints | `MATERIALIZED`, `ZERO_NUMERIC`, `ZERO_ONLY` |

`REGULAR` may have any valid physical policy. `ZERO_FILL` must have `ZERO_ONLY`
physical data; `DECLARATION` must have `MATERIALIZED` and owns no data bytes.
Aliases are declaration-class entries with a distinct alias target, verified
against an actual regular definition. `ZERO_NUMERIC` admits symbolic
relocations but no nonzero numeric fragments; `ZERO_ONLY` admits neither.
Internal/weak binding and liveness are orthogonal to both axes.

Core verifies the enum whitelist, cross-state matrix, alias ownership and
physical payload even for independently constructed Modules. Invalid negative
and oversized enum values are rejected before early exits. The same sealed
definitions are consumed by Common ELF facts and all relevant machine
emitters, including RV32/RV64 weak-zero and alias behavior; the old C
`is_extern`/`is_tentative`/`is_zero_initialized` flags do not cross this
boundary. C diagnostics read names from the Compiler-owned program, not a
duplicate C `source_name` in the Core Module.

## GNU inline ASM and target admission

- Source admission retains the original GNU syntax fingerprint and constraint
  parser. Core owns normalized operand roles, physical early-clobber/read-write
  facts, memory effects and target-qualified machine-form IDs.
- Core no longer stores a source-form fingerprint or a shadow GNU output
  count; output operands must form a leading typed prefix. The publisher
  validates all ties before committing any operand plan.
- x86 RAID6/KVM and LoongArch SIMD machine consumers use the typed Core
  prefix; unrecognized forms remain fail-closed.
- GNU `vector_size` source acceptance belongs to `SourceInfo`, not the
  machine `DataLayout`. Existing x86-64 16-byte admission and four-target
  rejection behavior are preserved.
- The unassigned source `is_emission_pruned` bit and its unused legacy AST
  emission predicates were removed; Core Module reachability remains owner.

## Validation and outstanding work

Focused debug and release `-Werror` MiniC builds, Core Module fragment and
ELF-linkage contracts, ASM operand-plan and P45 machine-form tests, five-target
model checks and the MiniC target boundary script pass on the integrated
worktree. Some cross-GNU/QEMU runtime cases explicitly skip when toolchains
are unavailable. No five-target Linux runtime verdict follows from these gates.

Next is a coherent five-target CoreBlock label migration: separate an indirect
CFG entry from the canonical emitted `.Luser_*` alias. This must include
clone-local static ownership, Module owner-label relocation proofs and x86
IBT/`.head.text` behavior. After that, introduce a minimal verified Machine IR
at the prepared-function callback, reusing existing Core planning and
per-target ABI/frame rules; do not add an optimizer or a parallel legacy path.
