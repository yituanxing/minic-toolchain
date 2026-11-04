# MiniLD linker (`minic-ld`)

MiniLD is an implemented ELF linker, currently centered on RV64. Its existing
paths include relocatable (`-r`) and static executable linking, shared/PIE
output, regular/thin archive selection, GOT/PLT/TLS relocations, Linux linker
script subsets and section GC. There is also a separately scoped RV32 vDSO
compatibility implementation. These capabilities have different acceptance
levels; this README does not claim general GNU `ld` compatibility.

Every ELF link mode (`-r`, static, shared/PIE, script and GC) now records
independent input-section identities throughout object/archive admission.
After input selection, one mode-aware work-section projection applies the
historical coalescing and alignment rules; script/GC links subsequently
materialize source sections for their own selection/layout. COMDAT,
link-order and repeated `-r` are covered by focused integration tests.
This does not imply a complete target-neutral InputSection-to-OutputSection
pipeline: output policies and ELF writers remain separate, and full
instruction relaxation and some GNU RISC-V relocation semantics are outside
the implementation.
The source-based architecture review, GNU Binutils/LLVM comparison,
ownership migrations and exact test boundaries are recorded in:

**[MiniLD input-section and MiniAS contract review](docs/architecture-input-sections-as-contract-2026-11-03.md)**

The independent MiniLD ordinary-C execution ladder and its current
five-target capability gaps are tracked in
`../docs/ci/minild-gcc-runtime-ladder-2026-11-04.md`.  A green MiniAS +
GNU BFD runtime run must not be presented as a MiniLD final-link success.

Production entry points live in `linker/src/linker.c`,
`linker/src/linker_core.inc`, `linker/src/linker_script_*.c` and
`linker/src/elf32_compat_vdso.c`. The executable driver is
`tools/minic-ld/main.c`. Existing targeted tests are under `tests/linker/`.
The included stage owners under `linker/src/linker_*.inc` remain part of one
C translation unit; they are not separately compiled linker backends.
