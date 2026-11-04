# MiniLD target boundaries and actual build coverage (2026-11-04)

## What is built today

`make build/release/bin/minic-ld` builds one **host executable**, not a
multi-target backend set. The executable's target choices are intentionally
limited and must not be inferred from the host machine or from MiniC/MiniAS
target coverage. A host x86-64 `minic-ld` is still primarily an RV64 linker.

| Requested output target | CLI / link implementation | Build / acceptance status |
| --- | --- | --- |
| RV64 ELF64 little-endian | `elf64lriscv`: ET_REL, ET_EXEC, ET_DYN, PIE | Existing RV64 subset and separate integration contracts |
| RV32 ELF32 little-endian | `elf32lriscv`: generic ET_REL; special Linux compat-vDSO ET_DYN | ET_REL five-target contract; no generic ELF32 ET_EXEC / PIE |
| x86-64 ELF64 | `elf_x86_64`: ET_REL and baseline static ET_EXEC | GNU AS/LD differential, real native Linux syscall runtime |
| AArch64 ELF64 | `aarch64linux`: ET_REL and baseline static ET_EXEC | ELF64/RELA, absolute and cross-object CALL26 relocation; local QEMU runtime pending |
| LoongArch64 ELF64 | `elf64loongarch`: ET_REL and baseline static ET_EXEC | ELF64/RELA, absolute and cross-object B26 relocation; local QEMU runtime pending |

This matrix reports target **link** support, not whether the linker itself
can be cross-compiled to execute on these architectures. The latter is a
separate host-tool portability task and does not establish target correctness.

Evidence in the source tree:

- `tools/minic-ld/main_core.inc` chooses an explicit emulation, and an
  immutable target policy refuses link modes which do not have a backend.
  All shared/PIE dispatch now uses `minild_link_shared_inputs_target`: the
  Driver no longer guesses RV32 from an emulation string.
- `linker/src/linker_target_elf.inc` owns five ELF identity, e_flags and
  attributes policies; Common admission and -r output borrow this profile.
- `linker/src/elf32_compat_vdso.c` implements its own narrow RV32 vDSO path.
- `linker/src/linker_riscv_fixup.inc` owns RV64 final relocations; the
  new `linker_nonriscv_static_fixup.inc` owns bounded x86-64/AArch64/
  LoongArch64 static patches. Shared GOT/PLT/TLS and relaxation remain
  independent target acceptance frontiers.
- `linker/src/linker_final_image_plan.inc` owns the final class-neutral
  ProgramPlan, symbols and sections. `linker_final_elf_image.inc` alone packs
  ELF32/ELF64 records with narrow-field overflow checks; RV32 static is still
  mode-gated until RV32 relocation, GOT and ABI policies are accepted.
  The Target owns linker-script architecture and default PIE interpreter
  policy; only RV64 currently accepts scripted static and dynamic links.
- `Makefile` has one `MINILD_SOURCES`/`MINILD_BINARY` build and no backend
  binary matrix. `check-minild-multitarget-relocatable` is an actual five-target
  ET_REL contract, and `check-minild-multitarget-static` exercises three
  baseline ELF64 final targets. `.github/workflows/minild-integration-v1.yml`
  remains RV64-focused; the new focused boundary CI is separate.

## Five-target architecture exercise — 2026-11-04

The new ET_REL path is not five copied linkers. It reuses the same admission,
archive graph, canonical symbol resolution, working-section projection and
MiniELF writer with one invocation-owned output target. The producer is the
actual MiniAS corresponding to each target, not a synthetic header-only ELF.

`make check-minild-multitarget-relocatable` checks two cross-object references
and two real RELA records for each of the five targets (including RV32 ELF32),
plus GNU ld -r semantic parity on x86-64. It must reject mixed target inputs
and unsupported link modes without writing an apparent success artifact.

The second stage also exercises **baseline static ET_EXEC** on three new
ELF64 targets. MiniLD resolves one cross-object absolute address for each,
checks ET_EXEC/PT_LOAD/entry/symbol identity and rejects an unknown machine
relocation without writing an output. AArch64 and LoongArch64 also check an
actual MiniAS-emitted, cross-object CALL26/B26 branch by decoding the final
instruction and comparing the computed destination with the resolved symbol.
An x86-64 Linux syscall executable runs natively and agrees with GNU ld on
identical inputs. The focused CI installs QEMU user emulators and treats guest
runtime as a required gate; at this local checkpoint they are unavailable,
so their QEMU runtime and extensive GNU final-link parity are **not** yet
accepted. Their static linker scripts,
GC, shared/PIE, GOT/PLT and TLS explicitly remain disabled.

An invocation now owns `--gc-sections` as a static option rather than a
process-global variable or a command-line wrapper. It remains an RV64-only
qualified mode until the new target GC contracts are tested.

The next architecture pass moved final `Elf64_Phdr`, `Elf64_Shdr` and
`Elf64_Sym` **out of the canonical plans**. All final metadata is now
class-neutral `MiniElfProgramHeader`, `MiniElfSection` and `MiniElfSymbol`;
the sole final serializer packs according to ELF class, checking ELF32
address/offset/size limits before publication. Three ELF64 static targets
also pass a two-stage `MiniAS -> MiniLD -r -> MiniLD -static` roundtrip.
RISC-V psABI-only synthetic symbols (`__global_pointer$` etc.) are now
Target-scoped; non-RISC-V outputs retain only Common boundaries.
The same Target profile owns shared-backend dispatch and validates incoming
ABI flags: x86/AArch64 require zero; LoongArch64 accepts only psABI v1
LP64S/F/D `0x41/0x42/0x43` and still rejects mixing flags across objects.
Tests reject invalid flags on both `-r` and static links, preserve RV32's
special compat-vDSO refusal contract, and exercise RV64 ET_DYN.

This is not full Linux distribution support: RISC-V RV32 ELF32 final output,
target-wide relocation coverage, TLS, dynamic linking and per-ISA runtime
acceptance remain separate gates. `-r` alone is not runtime correctness.

## Architecture inventory after the input/output ownership passes

The shared input and output stages are already substantial and should be
reused rather than copied for each new target:

1. InputGraph, source-only ELF/archive admission and ordered extraction.
2. Working-section projection, GC, linker-script matching and symbols.
3. Mode-owned pre-layout reservations and shared physical Layout.
4. A final OutputPlan and shared symbol/section/program-header projections.
5. ELF32/64 final serialization and a distinct ET_REL-to-MiniELF writer.

The largest remaining ISA boundary was final relocation handling: RISC-V
relocation classification and encoding appeared both in the target fixup
owner and in ET_EXEC/ET_DYN relocation scanning and application. Target
relocation policy must be isolated while each link mode continues to own
preemption, GOT/PLT/TLS **reservation** and final output-section liveness.
TLS placement itself is a final OutputPlan fact, not an ISA fact and not
independently a `.symtab`, `.dynsym` or `PT_TLS` decision.

## Multi-target bring-up acceptance, in order

Each target must pass the **same** actual output checks, not only compile a
host binary or accept an emulation flag:

1. Target selection, ELF class/endianness/machine/ABI/flags and input admission.
2. ET_REL and static ET_EXEC with GNU/readelf object identity, relocation
   values, segment and symbol-table parity.
3. Archive, script/GC, TLS, GOT/PLT and ET_DYN/PIE tests only where the
   target-specific implementation actually supports the corresponding ABI.
4. Runtime checks, from tiny programs through Linux, using appropriate
   GNU toolchain references and QEMU or hardware evidence.

The core must not enumerate five architectures in its layout, symbol and
output algorithms. New targets should supply a target description, ABI
admission and relocation/encoding hooks, and their own acceptance tests.
This document is a **status and architectural boundary record**, not a
claim that the missing backends or five-architecture runtime tests pass.
