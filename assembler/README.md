# MiniAS assembler component

Ownership: RISC-V assembly source (`.s`) to ELF relocatable object (`.o`).

Planned public tool identity: `minic-as`.

M0 contains no assembler implementation. The first assembler work is a frozen-corpus
census and differential oracle harness against GNU `as`.

```text
same frozen .s
  -> GNU as  -> reference.o
  -> MiniAS  -> candidate.o
```

Instruction bytes are compared exactly where representation is canonical. Complete
objects are compared semantically by sections, symbols, relocations, contents, flags,
alignment, and other meaningful ELF properties rather than by requiring whole-file
SHA equality.

MiniAS may later share format/ISA facts from `libs/`; it must not call compiler
backend internals to bypass assembly-language semantics.
