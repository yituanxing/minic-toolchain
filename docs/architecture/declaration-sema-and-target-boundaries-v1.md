# Declaration/Sema and Target Boundaries v1

Status: active refactor contract

This document defines the architectural constraints for the post-first500 MiniC refactor. It is intentionally narrower than a GCC/LLVM-style target framework: the goal is to preserve clean ownership and future composition without paying for abstractions before a real second consumer exists.

## 1. Immediate frontend objective

The first refactor target is declaration ownership.

Current parsing code mixes token movement, declarator syntax, name lookup, entity creation, redeclaration merging, storage/linkage rules, attributes, and persistent Program mutation. The refactor must converge toward:

```text
Token stream
  -> transient DeclSpec + Declarator
  -> declaration/Sema operation
       -> construct semantic type
       -> lookup or merge semantic entity
       -> apply semantic attributes
       -> resolve storage/linkage
       -> commit Program mutation
  -> persistent semantic AST / declaration tables
```

The parser remains responsible for syntax and source-order control. Semantic ownership must not depend on parser token state once a transient declaration description has been formed.

## 2. One persistent frontend representation

MiniC keeps one persistent semantic AST/program representation.

Do not introduce a permanent Parsed AST plus Typed AST pair merely to separate Parser and Sema. Short-lived syntax objects are allowed when they simplify ownership, but they must be discarded after semantic commit.

The existing dense-ID/arena model remains the default ownership model unless concrete evidence requires changing it.

## 3. Probe and commit rule

A syntax or semantic probe must not silently mutate long-lived Program state.

A probe may:

- advance a copied token/lexer cursor;
- inspect declarations and semantic types;
- evaluate side-effect-free semantic queries;
- build explicitly temporary data.

A probe must not leave behind persistent expressions, types, symbols, declarations, records, functions, initializer state, or other Program-owned entities.

Persistent mutation must have a visible semantic commit point. Transitional snapshots/rollback helpers may be used only to make existing violations explicit while callers are migrated; they are not the long-term substitute for non-mutating syntax representation.

## 4. Initializer ownership follows declaration ownership

A general InitPlan is the second major frontend seam, not the first.

Initializer semantics depend on the resolved declared type, object entity, storage duration, linkage, record/array completion, and attributes. Therefore declaration/Sema ownership must be stable before global and local initialization are converged.

Long-term shape:

```text
semantic declaration
  -> InitPlan
       -> automatic/local lowering -> semantic statements/Core
       -> static-storage lowering  -> data + relocations
```

Static data does not need to be forced through function Core IR.

## 5. Target dimensions are orthogonal

The current implementation is RV64/Linux-oriented, but those facts must not become frontend architecture.

Keep the following dimensions conceptually separable:

```text
Architecture
ISA / feature set
XLEN / machine width
ABI
OS / execution environment
Object format
Runtime / CRT / libc / syscall layer
```

Examples of intended future compositions include:

```text
RISC-V + RV32 + selected ISA features + ILP32 + FreeRTOS/bare metal
RISC-V + RV64 + selected ISA features + LP64  + Linux/NuttX
x86_64 + SysV ABI + Linux + ELF
x86_64 + Win64 ABI + Windows + PE/COFF
AArch64 + AAPCS-family ABI + ELF or Mach-O environment
```

These examples are design constraints, not an implementation checklist for this refactor.

## 6. RV32 and RV64 share the RISC-V architecture owner

Do not create copied RV32 and RV64 compiler backends merely because XLEN differs.

RISC-V mechanisms should be shared where semantics are shared:

- instruction families and legality;
- register model;
- ISA feature handling;
- instruction selection structure;
- relocation concepts;
- assembler syntax conventions.

XLEN/ABI-specific facts should remain explicit data or narrow policy:

- register and pointer width;
- C integer/data-model widths;
- load/store width choices;
- ABI argument/return placement;
- width-sensitive relocations;
- instruction legality that truly depends on XLEN.

A second real target should drive extraction of shared interfaces. Do not pre-build a large TargetMachine/Subtarget/MC framework.

## 7. Frontend and Core are target-neutral

Declaration/Sema, semantic AST, normalization, and generic Core IR must not encode:

- Linux syscall numbers or kernel conventions;
- ELF section/relocation machinery as C semantic facts;
- RV64 physical registers such as a0/a1;
- a fixed 64-bit pointer assumption;
- target-specific instruction mnemonics.

Target-sensitive C semantics that genuinely depend on the implementation (integer widths, plain-char policy, alignment/layout, ABI-visible type rules) must enter through TargetInfo/DataLayout/ABI boundaries rather than ad-hoc architecture checks in parser code.

## 8. ABI is not ISA

Calling convention/ABI policy remains a distinct owner from instruction-set semantics.

The same architecture may support multiple ABIs, and an ABI may vary with XLEN or execution environment. Core IR should represent calls and values without embedding physical argument registers or stack slots. ABI lowering assigns those locations later.

The existing shared RV64 ABI classifier/placement code is a foundation to preserve, not a refactor target to replace prematurely.

## 9. OS is not a compiler backend

Linux, FreeRTOS, NuttX, and bare metal are execution environments, not separate copies of semantic/code-generation logic.

OS/environment differences belong primarily at boundaries such as:

- runtime and CRT;
- syscall/API adaptation;
- libc integration;
- startup/termination;
- TLS and dynamic-linking policy where applicable;
- executable/object-format integration.

Linux kernel source is a pressure workload for the compiler. It must not become the owner of general C semantics.

## 10. Object format and later toolchain stages

ELF, PE/COFF, and Mach-O are separate from Architecture and OS even when common combinations exist.

Compiler target knowledge should eventually be reusable by the native assembler/linker/runtime work, but this refactor does not start those implementations. External preprocessing/assembling/linking remain valid while compiler ownership is stabilized.

When a real second consumer appears, reusable target facts may be extracted from compiler-only code. Do not build unused assembler/linker abstraction layers in advance.

## 11. Complexity budget

Every new abstraction must satisfy both conditions:

1. it removes a real current ownership problem or supports a real second consumer; and
2. it reduces or contains total complexity rather than merely relocating it.

Specifically avoid introducing, without demonstrated need:

- duplicate persistent ASTs;
- universal TypeId interning;
- a PassManager/SSA framework;
- MachineIR/register allocation infrastructure;
- a global Target registry/plugin system;
- copied per-OS or per-XLEN backends.

The rule is: reserve the boundary early; pay the implementation cost only when reality reaches it.

## 12. Refactor order

The planned convergence order is:

```text
clean first500 baseline
  -> declaration/Sema ownership + explicit semantic commit
  -> general InitPlan ownership
  -> canonical array/type representation cleanup
  -> incremental production migration through Core IR
  -> add the next real architecture/XLEN/ABI consumer
  -> extract only the target abstractions proven necessary by that consumer
```

No Linux 501+ semantic expansion should be used to justify bypassing these ownership boundaries during this refactor phase.
