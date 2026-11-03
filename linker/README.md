# MiniLD linker (`minic-ld`)

MiniLD is an implemented ELF linker, currently centered on RV64. Its existing
paths include relocatable (`-r`) and static executable linking, shared/PIE
output, regular/thin archive selection, GOT/PLT/TLS relocations, Linux linker
script subsets and section GC. There is also a separately scoped RV32 vDSO
compatibility implementation. These capabilities have different acceptance
levels; this README does not claim general GNU `ld` compatibility.

Ordinary no-script final links now preserve separate input-section identities
through object/archive admission and perform name-based coalescing only once
before layout. The relocatable (`-r`) and script/GC admission paths retain
their earlier merge/materialization rules; they are not yet one universal
InputSection-to-OutputSection selection pipeline. COMDAT and link-order are
covered by focused integration tests, while full instruction relaxation and
some GNU RISC-V relocation semantics remain outside this implementation.
The source-based architecture review, GNU Binutils/LLVM comparison,
ownership migrations and exact test boundaries are recorded in:

**[MiniLD input-section and MiniAS contract review](docs/architecture-input-sections-as-contract-2026-11-03.md)**

Production entry points live in `linker/src/linker.c`,
`linker/src/linker_core.inc`, `linker/src/linker_script_*.c` and
`linker/src/elf32_compat_vdso.c`. The executable driver is
`tools/minic-ld/main.c`. Existing targeted tests are under `tests/linker/`.
The included stage owners under `linker/src/linker_*.inc` remain part of one
C translation unit; they are not separately compiled linker backends.
