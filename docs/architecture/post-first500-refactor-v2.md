# Post-first500 Refactor v2 / first500 后大重构 v2

## Status / 状态

This document is the architectural contract for the refactor that starts after the frozen Linux first500 milestone.

本文是 Linux first500 冻结节点之后的大重构契约。它描述**边界、所有权和迁移顺序**，不是要求立即实现所有目标组合。

The frozen first500 branch remains evidence. Refactor work starts from that exact behavior and must preserve it unless a deliberate semantic change is documented.

## 1. Why this refactor exists / 为什么现在重构

The first500 convergence proved that MiniC can carry substantial real C/GNU C semantics, but it also exposed ownership debt:

- parser code still owns declaration semantics that should have a semantic commit boundary;
- declarator rules are shared only through helpers, not through one transient representation;
- local and global initializer semantics are represented by different machines;
- array facts still have duplicated physical representations;
- the production RV64 path still performs target-neutral execution work that Core IR should gradually own;
- convergence productizers/materializers have outlived their role.

The goal is **not** to rewrite the compiler. The goal is to preserve proven semantic machinery while moving ownership to boundaries that can support future targets without multiplying complexity.

## 2. Long-term target dimensions / 长期 Target 维度

MiniC must be able to model these dimensions independently when real consumers arrive:

```text
Architecture
  RISC-V
  x86
  ARM

Execution width / architecture mode
  RV32 / RV64
  x86 / x86_64
  ARM32 / AArch64

ISA features
  RISC-V I/M/A/F/D/C/V/Z* ...
  x86 SSE/AVX ...
  ARM/Thumb/NEON ...

ABI
  ILP32 / LP64 families
  SysV / Win64 / AAPCS ...

OS / environment
  Linux
  FreeRTOS
  NuttX
  bare metal

Object format
  ELF
  PE/COFF
  Mach-O
```

These are **composition dimensions, not a class/product matrix**. We must not create one backend per combination.

## 3. RV32/RV64 rule / 32/64 位规则

RV32 and RV64 should share RISC-V architecture and ISA machinery. XLEN-dependent facts belong in target/data-layout/ABI queries rather than copied frontends or copied semantic backends.

Examples of real XLEN-dependent facts include:

- pointer width;
- register width;
- C target integer model where applicable;
- load/store width selection;
- ABI classification and stack slot rules;
- instruction legality where XLEN changes semantics.

A future RV32 implementation should therefore pressure existing target boundaries rather than fork `riscv64` semantics wholesale.

## 4. OS is not compiler semantics / OS 不是语言语义

Linux is a workload and environment, not the semantic architecture of MiniC.

Core language semantics must not depend on `Linux`, `FreeRTOS`, `NuttX`, or `bare metal`. OS/environment differences belong at boundaries such as runtime, CRT, syscall integration, libc/sysroot policy, executable/linking conventions, TLS policy, and platform support.

The compiler may query a target environment when the C implementation or ABI genuinely depends on it, but parser/Sema/Core must not accumulate workload-specific branches.

## 5. Required ownership shape / 目标所有权

The persistent frontend remains one semantic AST. We do not introduce a complete long-lived parsed AST plus typed AST merely to imitate larger compilers.

```text
SourceManager
  -> Lexer / TokenCursor
  -> Parser
       -> transient DeclSpec / Declarator / InitializerSyntax
       -> Sema transaction APIs
  -> Semantic AST + semantic entities
  -> verification
  -> normalization
  -> Core IR
       -> Target ABI
       -> architecture / ISA lowering
       -> machine output

Semantic global initialization
  -> InitPlan
  -> DataLayout
  -> target data / relocation emission
```

The frontend/Core boundary and the global-data/InitPlan boundary are related but not identical: static data does not need to be forced through function Core IR.

## 6. First refactor invariant: syntax probes do not mutate Program

The first implementation target is declaration ownership.

Today some declaration/inferred-array probes temporarily mutate long-lived `Program` state and then roll it back. Refactor v2 establishes this invariant:

> Parsing or probing syntax must not commit semantic state. Long-lived Program mutation occurs through an explicit semantic transaction/commit point.

Minimum useful shape:

```text
Token stream
  -> transient DeclSpec + Declarator
  -> Sema declaration transaction
       -> construct/resolve type
       -> lookup or merge semantic entity
       -> apply attributes
       -> resolve linkage/storage duration
       -> commit Program changes
```

This seam must work for globals first without requiring an immediate rewrite of every declaration context.

## 7. Initializer migration follows declaration ownership

A general InitPlan comes after the declaration semantic transaction because initializer semantics require a stable resolved object/type owner.

The intended progression is:

```text
legacy checked initializer representation
  -> InitPlan semantic view
  -> shared local/global checking
  -> local Core lowering OR global data/relocation lowering
  -> retire duplicated slot/assignment semantics when coverage proves parity
```

This is also the path to correct single-evaluation semantics for constructs such as GNU range designators without encoding them as duplicated assignment AST.

## 8. Core IR migration rule / Core IR 迁移规则

Core IR is the target-neutral execution boundary for functions, but this refactor does not introduce SSA, phi nodes, dominance, a generic pass manager, MachineIR, or register allocation merely because mature compilers have them.

Production migration is incremental:

1. select one supported function/expression category;
2. lower it through Core;
3. compare against direct-RV64 behavior and runtime gates;
4. make Core the production owner for that category;
5. delete duplicated direct target-neutral semantics.

The same TargetABI/DataLayout sources must be reused by old and new paths during migration.

## 9. Abstraction budget / 抽象复杂度预算

The project remains a modular monolith.

We may establish a boundary before the second consumer exists when the boundary prevents current target facts from leaking into semantics. We do **not** build a speculative framework behind that boundary.

Examples:

- keep `TargetInfo`, `DataLayout`, and ABI query seams now;
- parameterize real XLEN differences when RV32 arrives;
- generalize architecture backend interfaces when a second architecture arrives;
- share instruction encoding metadata when a native assembler becomes a real consumer;
- generalize object-format interfaces when a second object format becomes real.

Short rule:

> Leave the road open; do not pay for imaginary traffic.

## 10. Things that must not leak upward / 不允许向上层泄漏的事实

Semantic frontend and Core must not encode accidental RV64/Linux facts such as:

- physical register names (`a0`, `a1`, ...);
- fixed 64-bit pointer assumptions;
- Linux syscall numbers or startup rules;
- ELF relocation numbers;
- textual GNU assembler accidents;
- Linux corpus file/index special cases.

Target-specific facts must have an explicit owner.

## 11. Migration order / 当前迁移顺序

The current order is:

```text
P0  retire first500 productizer/materializer scaffolding
    + preserve permanent semantic tests and frozen replay

P1  declaration/Sema transaction
    + syntax-probe-no-Program-mutation invariant

P2  general InitPlan seam
    + converge local/global initializer semantics

P3  remove duplicate array ownership
    + make canonical type/layout facts authoritative

P4  migrate production function categories through Core IR
    + delete migrated direct-RV64 target-neutral semantics

P5  add the next real target pressure
    preferably RV32 first because it tests XLEN sharing with minimal ISA duplication
```

New Linux feature accumulation should not outrun these ownership migrations unless required to fix a regression in already frozen behavior.

## 12. Validation contract / 验证契约

Each refactor slice should preserve, as applicable:

- release build with `-Werror`;
- Compiler C0 gates;
- Frontend Ownership Contracts;
- Core shadow/differential gates;
- external project regressions already used by the project;
- frozen Linux 6.6.143 first500 replay.

The frozen replay claim remains a `.i -> .s` compiler/discovery gate under the project's documented deviations; it is not automatically an object/runtime-equivalence claim.

## 13. Non-goals for this phase / 本阶段不做

Do not start these merely as architecture decoration:

- native preprocessor rewrite;
- native assembler/linker/runtime rewrite;
- full type interning/TypeId conversion;
- symbol hash tables without measured need;
- SSA/optimization framework;
- MachineIR/register allocation;
- complete ISA feature database;
- one Target class for every Architecture × ABI × OS × ObjectFormat combination.

The measure of success is simpler ownership, fewer duplicated semantic engines, preserved real-software behavior, and a clean path to RV32 plus future architectures.
