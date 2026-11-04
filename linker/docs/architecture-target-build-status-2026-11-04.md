# MiniLD target boundaries and actual build coverage (2026-11-04)

## What is built today

`make build/release/bin/minic-ld` builds one **host executable**, not a
multi-target backend set. The executable's target choices are intentionally
limited and must not be inferred from the host machine or from MiniC/MiniAS
target coverage. A host x86-64 `minic-ld` is still primarily an RV64 linker.

| Requested output target | CLI / link implementation | Build / acceptance status |
| --- | --- | --- |
| RV64 ELF64 little-endian | `elf64lriscv`: ET_REL, ET_EXEC, ET_DYN, PIE | Implemented subset; host fixtures and dedicated RV64 integration jobs |
| RV32 ELF32 little-endian | `elf32lriscv`: special Linux compat-vDSO ET_DYN only | Not a general RV32 linker; fixed compatibility contract |
| x86-64 ELF64 | No target emulation or relocation implementation | Not supported or linked |
| AArch64 ELF64 | No target emulation or relocation implementation | Not supported or linked |
| LoongArch64 ELF64 | No target emulation or relocation implementation | Not supported or linked |

This matrix reports target **link** support, not whether the linker itself
can be cross-compiled to execute on these architectures. The latter is a
separate host-tool portability task and does not establish target correctness.

Evidence in the source tree:

- `tools/minic-ld/main_core.inc` selects only two RISC-V emulations and
  dispatches `-r`, static, shared and PIE to RV64-specialized public APIs.
- `linker/src/linker_riscv_input_abi.inc` is the one RV64 ELF admission and
  identity owner; it requires `ELFCLASS64` and `EM_RISCV`.
- `linker/src/elf32_compat_vdso.c` implements its own narrow RV32 vDSO path.
- `linker/src/linker_riscv_fixup.inc` owns RISC-V final instruction fixups;
  other target relocation implementations do not yet exist.
- `linker/src/linker_elf64_image.inc` serializes ELF64 only. The
  linker-script architecture and default PIE interpreter policy are still
  RISC-V-specific.
- `Makefile` has one `MINILD_SOURCES`/`MINILD_BINARY` build and no backend
  matrix; `.github/workflows/minild-integration-v1.yml` is RV64-focused.

## Architecture inventory after the input/output ownership passes

The shared input and output stages are already substantial and should be
reused rather than copied for each new target:

1. InputGraph, source-only ELF/archive admission and ordered extraction.
2. Working-section projection, GC, linker-script matching and symbols.
3. Mode-owned pre-layout reservations and shared physical Layout.
4. A final OutputPlan and shared symbol/section/program-header projections.
5. ELF64 serialization and a distinct ET_REL-to-MiniELF writer.

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
