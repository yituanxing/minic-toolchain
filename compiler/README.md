# MiniC compiler component

Ownership: C and GNU-C source semantics, Core IR lowering, RV64 code generation, and the
frozen preprocessed-C-to-assembly contract.

Public tool identity: `minic-cc`.

Current stable boundary:

```text
preprocessed C (.i)
  -> frontend / semantic analysis
  -> Core IR
  -> RV64 assembly emitter
  -> assembly (.s)
```

The production implementation is still physically stored under the historical
`src/compiler`, `src/core`, `src/frontend`, and `src/target` roots during M0a.
M0b may move those files only after the build/test path is green. This README is the
canonical ownership marker during that staged migration.

The compiler does not own assembly parsing, ELF relocatable-object construction,
archive construction, linking, or preprocessing.
