# Toolchain Component Boundaries

Status: M0 boundary freeze for the native C toolchain.

## 1. One repository, independent tool owners

MiniC is a monorepo with one release/CI history, but each tool owns a distinct
language or file-format boundary and remains independently executable.

Planned public tool identities:

```text
minic        unified driver (future)
minic-cpp    C preprocessor
minic-cc     C compiler
minic-as     assembler
minic-ar     archiver
minic-ld     linker
minic-nm     symbol utility
minic-objdump
minic-objcopy
minic-strip
```

During M0, `minic-cc` is the canonical compiler name. The historical
`build/.../bin/minic` remains a byte-identical compatibility compiler
entrypoint so existing compiler validation does not need to migrate in the same
change. The name `minic` is reserved for the future multi-tool driver.

## 2. Frozen and active boundaries

The compiler V1 contract is:

```text
preprocessed C (.i)
  -> MiniC frontend / semantic analysis
  -> Core IR
  -> RV64 assembly emitter
  -> assembly (.s)
```

The active MiniAS milestone changes exactly one downstream boundary:

```text
frozen .i
  -> minic-cc
  -> frozen/reproducible .s
  -> minic-as              # active variable
  -> ELF relocatable .o
  -> external GNU linker
```

Direct `Core IR -> .o` emission is intentionally deferred. It may become a
later compiler backend, but it is not allowed to bypass or redefine the
assembler milestone.

## 3. Differential-oracle rule

A mature external tool remains available as a permanent differential oracle.

For MiniAS:

```text
same .s
  +-> GNU as  -> reference.o
  `-> MiniAS  -> candidate.o
```

Instruction/data encodings are compared byte-for-byte when representation is
canonical. Complete ELF objects are compared semantically: meaningful section
properties and contents, symbols, relocations, flags, alignment and ABI fields
must agree even when table order, string-table offsets or other non-semantic
layout details differ.

Promotion proceeds from focused encoding/directive/relocation tests through a
feature-cover corpus to all frozen Linux translation units, then through the
same external linker and Linux runtime gate.

The reference implementation is an oracle, not the specification. When MiniAS
and GNU `as` disagree, GNU `as` is presumed correct during diagnosis, while
the RISC-V ISA, psABI and ELF specifications remain the final authority.

## 4. Shared-library rule

Only facts or infrastructure with multiple real consumers may move under
`libs/`. Expected examples include RISC-V encoding/relocation definitions,
ELF read/write primitives, archive-format primitives and small generic support.

Tool policy stays with its owner:

- assembler grammar, expressions, directives and fixup semantics -> MiniAS;
- compiler lowering and machine realization policy -> MiniC compiler;
- linker symbol resolution and placement policy -> MiniLD;
- macro expansion/include semantics -> MiniCPP.

A tool must not call another tool's private implementation merely to avoid
implementing its own language boundary.

## 5. Native-tool replacement order

The current planned sequence is:

```text
T0  minic-cc     .i -> .s            frozen
T1  minic-as     .s -> .o            active
T2  minic-ar     .o -> .a
T3  minic-ld     .o/.a -> ELF/Image
T4  minic-cpp    .c/.S -> .i/.s
T5  minic        orchestration driver
T6  object utilities
```

The ordering is deliberate. Downstream replacement keeps the compiler input
frozen while each new boundary is isolated. Native preprocessing is deferred
because it changes compiler input and historically has subtle token/macro
semantics; its later validation must reuse the Python-era differential method
(normalized tokens/pragmas/diagnostics plus compilation of both preprocessed
outputs through the same reference compiler).

## 6. M0 corpus rule

MiniAS implementation does not begin by guessing a GNU assembler subset.
Before parser/encoder code is written, the complete frozen Linux assembly
surface is censused for mnemonics, pseudo instructions, directives, sections,
relocation operators, options, CFI, expressions and rare forms.

The census input must be reproducible from the frozen Linux preprocessed corpus
and frozen compiler identity. Sidecar dependencies such as
`kernel/config_data.gz` are part of the assembler execution environment and
must be recorded rather than silently ignored.
