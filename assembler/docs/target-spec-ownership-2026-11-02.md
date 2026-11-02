# MiniAS TargetSpec and emitted Object ABI ownership (2026-11-02)

Scope: one coherent four-Target internal migration on Draft PR #667. This
records the exact code/data boundary, not a claim that arbitrary versioned
RISC-V ISA, GNU/Linux P1 or COFF/Mach-O output has been implemented.

## Before: four overlapping copies of object-profile fields

`invocation.c` asked the Target registry to resolve ISA/ABI into
`MiniAsAssemblyConfig`; `assembler.c` copied `elf32`, machine and REL/RELA
mode into a mutable `MiniAs` session as three independent fields, while
also copying `elf_flags`. The four Target implementations read the original
config. Later Common/CFI/DWARF/ELF read session copies. There was no
need for those mutable copies: source directives may change the **machine
instruction mode** but cannot change the **chosen object format**.

`elf_flags` was different: Emission can OR in EF_RISCV_RVC after choosing
the alignment or compressed instruction bytes, so its final value must not
be a false immutable alias of the command-line TargetSpec.

## After: one immutable value and one additive output state

1. Invocation resolves one `MiniAsTargetSpec` by value. It contains a
   grouped `MiniAsObjectSpec` (ELF class, initial flags, machine, REL/RELA)
   and an inline Target-private ISA/ABI profile. No borrowed CLI string or
   second target profile is introduced.
2. `MiniAs.target_spec` is a **const borrow** of that value; it is valid
   across every SourceGraph replay in `minias_assemble_config` and is never
   rewritten by a source directive, Target encoder or object writer.
   The three duplicated session fields `output_elf32`, `object_machine`
   and `output_rel_relocations` have been deleted. CFI, DWARF, Common
   lowering and alignment observe the same spec. The terminal ELF writer
   gets its machine and REL/RELA values through the Driver's short-lived
   `MiniAsElfObjectView`, never through the mutable assembly session.
3. `MiniAs.elf_flags` remains the **single mutable output flags accumulator**.
   It is initialized once per replay from `spec.object.initial_elf_flags`;
   Emission alone adds encoded flags. A source-local `.option rvc` changes
   emission policy but cannot mutate the source's target ABI or ELF class.
4. Four targets now resolve and initialize through the same typed spec;
   Target-owned mutable ISA contexts and `MiniAsStmt.site` snapshots are
   retained unchanged. The RISC-V option/mapping state machine is not
   duplicated; x86 `.code*` is still distinct from ELF32/i386; LoongArch
   needs no artificial source-local ISA stack.

The old overlapping fields and `MiniAsAssemblyConfig` type have been
removed in the same batch, not retained as a forwarding compatibility path.
The public `MiniAsInvocation` remains unchanged.

## Scope and negative proof

`tests/assembler/probe-target-spec-const-abi.sh` checks seven forbidden
consumer writes/obsolete aliases and one valid const accessor at C11
`-Werror`, then assembles six concrete target object profiles, checks ELF
class and machine, checks i386 REL versus AMD64 RELA, and exercises the
RISC-V RVC output flag after a lexical option directive. The new gate is
included in the exact-head MiniAS Target ISA ABI workflow.

Local results for this batch before publication:

- Release MiniAS `-Werror` build: **PASS**.
- A0: **30/30 PASS** (GNU RISC-V cross-oracle unavailable locally;
  those tests explicitly reported SKIP and remain remote gates).
- TargetSpec const/ABI probe: **7 negative, 1 positive, 6 profiles, 2
  relocation formats, 1 source RVC — PASS**.
- Five old/new production fixture objects: byte-identical for RV32,
  RV64, AArch64, x86-64 and LoongArch64 (including source replay and
  RISC-V lexical mapping transitions).
- Target boundary, public API, Section const ownership, x86 i386 REL,
  Source/SectionKey, ELF symbol planner and LINK_ORDER checks: **PASS**.
- `pyelftools`-dependent COMDAT test cannot run locally; AArch64 GNU
  mapping oracle and RISC-V GNU cross-oracles are not yet available.
- No exact-head remote workflow, Linux Image or QEMU success is claimed
  until separately observed on the new pushed SHA.

## Remaining work, in dependency order

1. Grow Target-private ISA/version decision tables **only** alongside
   verified GNU positive/negative inputs. In the same batch as any new
   source-local ISA feature, update `capture_site_state`, retained MCInst,
   source replay identity, alignment, mapping and final ELF attribute
   behavior. Preserve the currently supported `+zbb/+v` implementation.
2. Keep `MiniAsTargetOps` policy consolidation demand-driven: CFI and ELF
   object policy may eventually become read-only views, but there is no
   second mutable owner in this batch that justifies copying their values.
3. Defer an independent generic ObjectModel to a **real** COFF/Mach-O
   consumer and output comparison. SectionID, SymbolID, typed Fixup and
   the Emission read view already provide a sufficient stable seam. Do
   not rename ELF-only SHN/STT/SHT rules and pretend they are cross-format.
