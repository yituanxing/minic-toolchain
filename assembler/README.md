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

## Dual-line Linux convergence loop

MiniAS uses two independent validation lines. They answer different questions and
must not be conflated.

### Fast line — deterministic real16

`MiniAS A0 Real Linux 16` is the high-frequency feedback line.

Its job is to:

- expose the next concrete assembler blocker quickly;
- validate a narrow implementation change against real Linux-produced assembly;
- print exact first-blocker context for failed translation units;
- keep iteration latency low while a blocker class is being implemented.

A `16/16` result is useful focused evidence, but it is **not** the project-wide
Linux coverage headline.

### Full line — frozen first500

`MiniAS A0 First500 Progress` is the full convergence line over the same frozen
500 Linux translation units used as the compiler safety corpus.

Its job is to:

- establish the real coverage headline (`PASS / FAIL / total=500`);
- classify exactly one first stable blocker per failing `.s`;
- rank blocker classes by frequency (Pareto);
- detect regressions outside the real16 sample;
- decide which blocker family should be attacked next.

The frozen selection must stay stable while comparing iterations. Focused or
real16 results must never be reported as though they were a first500 result.

### Promotion cadence

The normal development loop is:

```text
frozen first500
  -> rank first blockers by frequency
  -> choose the largest coherent blocker class
  -> implement the general assembler capability
  -> micro regression
  -> real16 / exact focused replay
  -> repeat focused fixes while the same class is converging
  -> frozen first500 again
  -> update the only authoritative 500-TU headline
```

Do not rerun first500 after every tiny edit when real16/focused evidence is enough
to continue the same blocker class. Do rerun first500 after a coherent batch has
moved, when choosing the next Pareto class, or whenever a regression outside the
sample must be ruled out.

The full line requires internal/error-style assembler failures to remain explicit:
a new internal failure must not be hidden as an ordinary unsupported capability.
The frozen Linux corpus is the primary development pressure. MiniAS is run on real
compiler-produced `.s` files and failures are grouped by the first stable diagnostic
class, for example:

```text
unsupported-directive:.foo
unsupported-instruction:bar
unsupported-reloc-instruction:lla
unsupported-expression:...
```

A capability is added because a real input requires it, then the same batch is
replayed before widening the batch. Static census tooling is auxiliary and must not
block assembler implementation.

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
