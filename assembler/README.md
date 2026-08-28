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
