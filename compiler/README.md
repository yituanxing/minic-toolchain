# MiniC compiler component

The standalone compiler is `minic-cc`: **preprocessed C (`.i`) to GNU
assembly (`.s`)**, with selectable RV32I, RV64I, x86-64, AArch64 and
LoongArch64 targets. This component owns C/GNU-C source semantics, Core IR
construction and the handoff to target machine code generation.

## Production data flow

```text
preprocessed C (.i)
  -> Frontend: parse, verify and normalize C semantics
  -> Compiler: inline-specialization plan and live Core function set
  -> Core: verified functions and sealed ModuleIR globals/symbols
     (the source AST is released before machine emission)
  -> Common target visitor: prepared per-function CFG, call and object facts
  -> Selected target: ABI/frame planning and typed Machine IR selection
  -> Whole-function machine verification and GNU assembly serialization
  -> assembly (.s)
```

The five production targets share the ModuleIR handoff and program visitor,
but retain their own ABI and machine instruction rules. No Machine IR
optimization pass is part of this pipeline. A target's accepted GNU source
ASM forms are a separate contract from the instructions it actively emits.

## Where to read the implementation

| Concern | Current owner |
| --- | --- |
| C parser, AST, source types and semantics | `src/frontend/` |
| Pipeline, specialization, reachability and Core set publication | `src/compiler/` |
| Core function/type IR, verifier and sealed ModuleIR | `src/core/` |
| C-to-Core lowering | `src/compiler/c_lower/` |
| Target registry, machine options and data layout | `src/target/` |
| Shared program emission, preparation and machine representation | `src/target/common/` |
| Architecture-specific ABI, selection and serialization | `src/target/{riscv32,riscv64,riscv,x86_64,aarch64,loongarch64}/` |
| Standalone command-line entry | `tools/minic-cc/` |

The authoritative ownership and review references are
[`docs/architecture/README.md`](../docs/architecture/README.md) and
[`docs/architecture/minic-global-contract-map.md`](../docs/architecture/minic-global-contract-map.md).

MiniC does **not** own preprocessing, assembly parsing, ELF relocatable-object
construction, archiving, linking or binary conversion. MiniPP, MiniAS,
MiniAR, MiniLD and the object utilities have separate implementation and
acceptance boundaries. Linux builds that use GNU AS/LD do not certify native
MiniAS/MiniLD replacement.
