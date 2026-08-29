# MiniAS assembler component

Ownership: RISC-V assembly source (`.s`) to ELF relocatable object (`.o`).

Public tool identity: `minic-as`.

## Architecture

MiniAS deliberately does not build a compiler-style AST or IR. Assembly already
contains target instructions and explicit assembler directives, so the production
path is a compact two-pass assembler:

```text
assembly text
  -> statement/directive parser
  -> section + symbol ownership
  -> pass-1 offsets and sizes
  -> RV64 instruction encoding + fixups/relocations
  -> ELF64 ET_REL writer
```

The C implementation is split by ownership:

- `assembler.c`: statement parsing, sections, symbols and the two-pass driver.
- `riscv_encode.c`: RISC-V register and instruction encoding.
- `elf_writer.c`: relocatable ELF object construction.
- `tools/minic-as/main.c`: standalone command-line driver.

The older Python MiniAS is used as a semantic and bug-history reference, not as a
physical code template.

## Real-program convergence

## Linux convergence loop: full500 + dynamic frontier + fixed real16

MiniAS uses three validation scopes with different ownership. Their results must
not be conflated.

### Current frontier — dynamic failure cohort

The active development sample is **not permanently fixed**.

After every authoritative frozen first500 run, every failing TU is extracted into
`assembler/corpus/first500-frontier.txt`. That file replaces the previous frontier
and becomes the high-frequency development cohort for the next convergence batch.

The frontier exists to:

- preserve exactly the failures discovered by the latest full500 headline;
- make second blockers visible without rerunning all 500 TUs;
- allow one coherent blocker family to be fixed across all currently affected TUs;
- stay small and fast as the tail converges.

If full500 reports 12 failures, the next frontier is those 12. If the next full500
reports 7 different failures, the frontier is replaced by those 7. A stale frontier
must never be treated as the current development sample.

### Fixed real16 — regression sentinel

`MiniAS A0 Real Linux 16` remains a deterministic spread sample. It is a stable
regression sentinel and useful smoke gate, but once it is green it is **not** the
primary source of new work. Passing real16 does not mean the current full500 tail
is green.

### Frozen first500 — authoritative headline

`MiniAS A0 First500 Progress` is the full convergence line over the frozen 500
Linux translation units. It owns the only authoritative first500 headline and
the Pareto ordering of first blockers.

Its job is to:

- establish `PASS / FAIL / total=500`;
- classify one stable first blocker per failing `.s`;
- rank blocker classes by frequency;
- detect regressions outside focused/frontier samples;
- produce the next dynamic frontier cohort.

### Promotion cadence

```text
frozen first500
  -> authoritative PASS/FAIL + Pareto
  -> replace current frontier with every FAIL
  -> choose the largest coherent blocker class
  -> focused micro regression
  -> current frontier replay
  -> continue peeling second blockers inside the same frontier
  -> fixed real16 regression sentinel
  -> when current frontier is green, rerun frozen first500
  -> replace frontier again from the new FAIL set
```

Do not rerun first500 after every tiny edit while the current frontier still
contains known failures. Do rerun it when the current frontier has converged, when
the global Pareto must be refreshed, or when a broader regression must be ruled out.

GNU `as` remains the differential oracle:

```text
same frozen .s
  -> GNU as  -> reference.o
  -> MiniAS  -> candidate.o
```

Instruction bytes are compared exactly where representation is canonical. Complete
objects are compared semantically by sections, symbols, relocations, contents, flags,
alignment, and other meaningful ELF properties rather than by whole-file SHA.

MiniAS may later share format/ISA facts from `libs/`; it must not call compiler
backend internals to bypass assembly-language semantics.
