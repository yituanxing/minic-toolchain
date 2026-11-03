# MiniLD linker (`minic-ld`)

MiniLD is an implemented ELF linker, currently centered on RV64. Its existing
paths include relocatable (`-r`) and static executable linking, shared/PIE
output, regular/thin archive selection, GOT/PLT/TLS relocations, Linux linker
script subsets and section GC. There is also a separately scoped RV32 vDSO
compatibility implementation. These capabilities have different acceptance
levels; this README does not claim general GNU `ld` compatibility.

The current implementation still merges input sections too early, and its
assembler/linker object contract is incomplete for COMDAT, link-order and
some relaxation-sensitive relocations. The source-based architecture review,
GNU Binutils/LLVM comparison, ownership migration and exact test boundaries
are recorded in:

**[MiniLD input-section and MiniAS contract review](docs/architecture-input-sections-as-contract-2026-11-03.md)**

Production entry points live in `linker/src/linker.c`,
`linker/src/linker_core.inc`, `linker/src/linker_script_*.c` and
`linker/src/elf32_compat_vdso.c`. The executable driver is
`tools/minic-ld/main.c`. Existing targeted tests are under `tests/linker/`.
