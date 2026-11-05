# MiniLD five-target core closeout (2026-11-05)

This checkpoint closes the remaining *architecture ownership* seams without
claiming that every user-space linking mode is implemented.  The original GNU
Linux object pools and their five QEMU P1 proofs remain the runtime baseline;
the current-head GNU AArch64 ordinal oracle and affected frozen Linux replay
are independent recertification gates.

## Single-owner path

1. **InputGraph** retains immutable ELF input symbol/section/file provenance.
   Resolver owns canonical symbols and records a linker-script PROVIDE only
   when the definition actually takes effect.  Fixed-point script trials
   restore the entire original symbol snapshot, including this provenance.
2. **WorkSections/Layout** own mutable fragment selection, synthetic GOT/PLT,
   placement and relaxation.  A synthetic work section must never be counted
   as an original BFD input-section-local dynamic symbol.
3. **Target Profile** declares ABI/mode/erratum capabilities and classifies
   machine-specific final relocations.  Driver and FinalSymbols do not
   enumerate target machines or instruction relocation numbers.
4. **FinalSymbols/OutputPlan/ProgramPlan/ELF writer** own one checked output
   projection.  Unadmitted --emit-relocs transformations fail closed; only
   the target that supplies a post-fixup classifier can enable that mode.

There are six explicit ELF descriptors (RV32, RV64, x86-64, i386 setup,
AArch64 and LoongArch64).  They use named initializers so extending a target
contract cannot silently shift positional ABI callbacks.  The public
multi-target static API no longer says ELF64 when RV32 ELF32 is admitted, and
unused RV64-only convenience entry points have been removed.

Every frozen Linux script/GC policy preflight and the five-target runtime gate
probe now share `tools/ci/minild-check-target-policies.py`. It reads the
descriptor's named `.emulation`, `.page_size` and `.supports_*` fields,
not ordinal booleans or a hardcoded structure width. Missing, duplicated,
contradictory or invalid fields fail closed; the runtime probe additionally
exercises an explicitly disabled target mode if a future target gates it.
This preserves the ownership migration in CI itself, including the older
four-architecture frozen-input workflows.

## Kernel discarded-dynsym exception

AArch64 Linux links with a discarded physical .dynsym may retain dynamic
symbol ordinals inside .rela.dyn.  BFD's live *source* input-section local
classes and actually applied hidden PROVIDEs precede unresolved weak global
symbols.  An allocated synthetic .got is not another source-local class.
Duplicate hidden undefined declarations across source objects do not add a
forced-local ordinal, and CALL/JUMP-only references do not make one.

This remains target-owned kernel behavior.  The ordinary ET_DYN shared
linker's real dynsym reservation/fill pipeline is deliberately separate.
Neither a frozen symbol name nor a hardcoded kernel symbol index is admitted.
The final ordinal is checked to fit ELF64 r_info's 32-bit symbol field.

## Qualification

- Native strict compilation and the phase/target structural gate.
- Five-target ELF boundary fixtures, including GNU BFD local-symbol oracles,
  RV32 ELF32, x86 retained final relocations and target-specific fixups.
- Cross-GNU AArch64 single/repeated hidden-UND ordinal comparison.  The test
  compares complete raw .rela.dyn records, not merely its own expected index.
- Affected frozen GNU Linux full image/QEMU P1 replay after the implementation
  changes; prior successes are a baseline, not a substitute for new-head CI.

The single translation unit plus ordered `.inc` modules is intentional: it
preserves phase order without duplicating persistent owners.  Complete
user-space PIE/DSO coverage and unrelated debug formats are feature work,
not reasons to reopen this architecture migration.

## Correctness-first command-line policy

MiniLD deliberately does not accept the full GNU ld option set. Every
accepted option must have a checked owner for the selected mode, or be an
explicitly verified no-op. This follows MiniAS/MiniC's target-ABI-first
approach: `-m`, object ELF class and byte order determine the output target;
the linker validates object ABI and relocations rather than interpreting
compiler `-march` / `-mabi` switches itself. Optimization-only flags and
unrelated debug format expansion are outside this phase.

Correctness-critical groups are:

1. Target/object identity: `-m`, `-EL` / `-EB`, `-r` / `-static` / `-shared`
   / `-pie`, ELF32/ELF64, target relocation and ABI contracts.
2. Link graph: ordered objects, `-L` / `-l`, `--start-group` /
   `--end-group`, `--whole-archive`, `-u`, weak/strong/COMMON/COMDAT rules.
3. Final image: `-T`, `-e`, `-n`, `-z max-page-size`, section GC,
   build-id, interpreter and static/dynamic program-header placement.
4. Observable output: static `-S` removes non-allocated debug sections;
   shared `-S` fails closed if a debug section remains. `-X` and
   `--emit-relocs` have separate admitted owner contracts.

Archive group membership and whole-archive extraction are separate axes:
one group is rescanned to a fixed point, each `--end-group` terminates that
domain, and `-l` archives obey the same group/whole classification as
explicit `.a` arguments. The native GNU BFD differential checks cyclic
archives, mixed ordinary objects, independent groups and unreferenced
whole-archive members.

Options parsed by the CLI but without a selected output owner must not
silently disappear: `-r -T/-S/--build-id`, static-only
`--soname/--needed/--dynamic-list`, dynamic `--build-id`, duplicate
`--needed` and dynamic `-z nodefaultlib` are rejected until implemented.
GNU `-r -e` is a proven no-op in the admitted relocatable mode (ELF e_entry
remains zero and `-r --gc-sections` is already rejected), so the real Linux
purgatory sublink is admitted without invented behavior.

`-z noexecstack` now has a coherent ELF owner across modes. The immutable
admitted input `.note.GNU-stack` sections determine default RW/RWE
`PT_GNU_STACK` policy; an explicit `-z noexecstack` forces RW. Final
ProgramPlan emits the class-neutral ELF32/ELF64 header for ET_EXEC/ET_DYN,
while OutputPlan consumes ordinary input stack notes instead of leaking
them as final sections; the script layout reserves the extra header slot.
GNU explicit `PHDRS`
suppresses the synthetic stack program header. For `-r`, the relocatable
output owner synthesizes a non-executable `.note.GNU-stack` when none was
present, while retaining original flags on existing input notes. These
semantics are independently exercised on the five Linux target ELF classes.
Explicit linker-script `PHDRS` definitions of `PT_GNU_STACK` themselves
remain unsupported script syntax, not silently rewritten.

The three targeted CLI probes run on small assembly inputs and compare
GNU/MiniLD effects without recompiling Linux. After any output-changing
linker patch, recertify the exact frozen Linux link and QEMU P1 for each of
the five targets. Historic P1 success alone is not a new-head PASS.
