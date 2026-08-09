# MiniC Toolchain Platform Architecture / MiniC 工具链平台架构

Status: architectural baseline for the C rewrite. This document defines boundaries and extension seams; it does **not** require every subsystem to be implemented immediately.

状态：C 重写长期架构基线。本文定义边界与扩展接口，但**不要求立即实现全部子系统**。

## 1. Design objective / 设计目标

MiniC is not a line-by-line port of the historical Python implementation and is not a TinyCC-style single-pass compiler optimized primarily for minimal latency. The C rewrite keeps the useful historical behavior and validation experience, while choosing data structures and module boundaries that remain understandable, optimizable, replaceable, and self-hostable.

MiniC 不是旧 Python 实现的逐行翻译，也不采用以极低延迟为首要目标的 TinyCC 式单遍架构。C 重写保留历史实现中已经验证的语义、测试与工程经验，同时重新选择便于学习、优化、替换和自举的数据结构与模块边界。

The architecture MUST preserve these properties:

- real upstream software drives semantic priority;
- language semantics, target ABI, object format, and runtime concerns are separate;
- every major representation has explicit ownership and invariants;
- optimization can be inserted globally, not only as local parser/codegen peepholes;
- new language versions and GNU extensions can be registered without scattering ad-hoc checks through unrelated modules;
- additional targets and output modes do not require rewriting the frontend;
- native preprocessing, assembling, linking, dynamic linking, JIT/in-memory execution, and self-hosting can be added later through defined boundaries;
- external sysroots, headers, CRT, libc, and libraries remain external inputs unless a separate runtime/libc project is explicitly chosen.

架构必须保证：真实源码驱动能力；语言语义、ABI、对象格式和运行时分层；主要表示具有明确所有权与不变量；优化拥有完整插入点；C 多版本与 GNU 扩展可集中注册；新增 Target 与输出模式不要求重写前端；后续可以独立接入预处理器、汇编器、链接器、动态链接、内存执行和自举；编译其他软件时使用真实 sysroot/headers/CRT/libc，而不是由 MiniC 伪造这些环境。

## 2. Whole-toolchain topology / 整体工具链拓扑

The long-term system is a set of cooperating layers, not one universal compiler state object:

```text
                         +----------------------+
 source / command line ->| Driver / Toolchain   |
                         +----------+-----------+
                                    |
             +----------------------+----------------------+
             |                      |                      |
       preprocessing            compilation           link orchestration
       (external now,           (active now)          (external now,
        native later)                                  native later)
             |                      |
             v                      v
        token stream       Source Manager + Lexer
                                    |
                                    v
                           Parser / Declarator
                                    |
                                    v
                         Semantic typed AST
                                    |
                                    v
                         Frontend normalization
                                    |
                                    v
                              Core IR
                                    |
                                    v
                         Pass Manager / Mid-end
                                    |
                                    v
                         Target lowering / MIR
                                    |
                                    v
                  instruction selection + regalloc
                                    |
                                    v
                    assembly or object emission
                                    |
               +--------------------+--------------------+
               |                                         |
        external assembler                         native assembler
        (current path)                             (later component)
               |                                         |
               +--------------------+--------------------+
                                    |
                          object / archive inputs
                                    |
                         linker + loader metadata
                                    |
                  executable / shared object / image
```

An optional execution layer may later consume code without writing a normal executable:

```text
IR / target code -> memory object/JIT image -> relocation -> executable memory -> call
```

That mode must reuse target lowering, relocation definitions, and symbol resolution rather than creating a second compiler.

未来的“内存中运行/JIT”模式应复用 Target lowering、重定位与符号解析，而不是另写一套编译器。

## 3. Stable compiler pipeline / 稳定编译主流程

The compiler path is intentionally multi-stage:

```text
SourceManager
  -> Lexer / tokens
  -> Parser + Declarator construction
  -> semantic typed AST
  -> frontend normalization
  -> Core IR construction
  -> IR verification
  -> optimization pipeline
  -> target lowering
  -> machine representation
  -> machine optimization / register allocation
  -> assembly or object writer
```

The current implementation stops before Core IR and emits RV64 directly from normalized AST. That is a valid temporary bootstrap path, but the architectural destination is to insert Core IR at this exact boundary. Lexer, parser, semantic AST, and their tests must remain reusable after IR is introduced.

当前实现仍从 Normalized AST 直接进入 RV64 后端；这是允许的临时启动路径。Core IR 的正式插入点固定在 Normalized AST 与 Target lowering 之间，因此引入 IR 不应推翻 Lexer/Parser/Typed AST。

## 4. Representation ownership / 表示层与所有权

### 4.1 SourceManager

Owns source buffers, file identities, line tables, include/macro provenance when the native preprocessor arrives, and compact source ranges.

SourceManager 管理源码缓冲区、文件 ID、行表，以及未来原生预处理器加入后的 include/macro 来源信息。

Rules:

- AST/IR nodes should store compact `SourceRangeId` or offset ranges rather than duplicating line/column triples;
- diagnostics map ranges to human-readable locations lazily;
- preprocessed numeric line markers are translated into source mapping, not treated as arbitrary syntax to skip.

### 4.2 String interning

Identifiers, symbol names, tag names, section names, and attribute names use stable `StringId` values backed by an interning table.

目的：消除重复字符串分配和线性 `memcmp`，同时让符号表、类型系统、IR 与对象层共享稳定名称 ID。

### 4.3 Type system

Types use stable interned `TypeId` identities. Semantic type data is target-neutral; size/alignment/ABI classification live behind `TargetInfo` / `TargetLayout`.

类型系统只表达 C 语义身份；RV64 的 long 宽度、指针宽度、record layout、ABI 分类不得写回通用 Type/AST。

Expected categories include:

- builtin scalar types and qualifiers;
- pointers;
- arrays including incomplete/VLA forms when supported;
- function types including variadic metadata and calling-convention attributes;
- records/enums through declaration IDs;
- attributed or qualified wrappers only when semantic identity requires them.

### 4.4 Semantic AST

The AST represents C source semantics, scopes, declarations, value categories, source-level control flow, initializers, and extension constructs that still matter semantically.

It is **not** the target machine representation and should not accumulate stack offsets, physical registers, ELF visibility encoding, or relocation instructions.

Stable IDs and Program-owned pools remain a good base and should be retained during compaction.

### 4.5 Core IR

Core IR is the first representation designed explicitly for whole-function analysis and transformation.

Required properties:

- explicit basic blocks and control-flow graph;
- typed values with stable IDs;
- explicit memory operations;
- explicit calls, conversions, integer/floating operations, comparisons, branches, switch, phi/block arguments or another documented SSA construction;
- no C declarator syntax, typedef names, or parser artifacts;
- target-independent integer widths and pointer operations through DataLayout queries;
- verifier-enforced dominance/CFG/type invariants;
- source locations retained for diagnostics/debug info;
- serialization/debug printing possible for differential tests.

Initial IR need not be SSA on day one if that would slow semantic bring-up, but the representation must have a defined migration path to SSA and must not encode evaluation order in hidden recursive AST traversal.

第一版 IR 可以先采用显式临时值 + CFG，再通过独立 pass 构造 SSA；但不能把执行顺序继续隐藏在递归 codegen 里。

### 4.6 Machine IR / target representation

Target lowering converts Core IR into target-specific operations and ABI decisions. A later Machine IR owns virtual registers, register classes, stack slots, call-frame information, target opcodes, and relocation-bearing symbol references.

Core IR 不承担 RV64 寄存器与栈帧职责；Machine IR 才负责虚拟寄存器、栈槽、指令与调用约定。

## 5. Pass architecture / Pass 架构

Pass infrastructure must be small and data-oriented, not an inheritance framework.

Suggested contract:

```text
Pass descriptor
  - stable name
  - required IR level
  - run(context, unit)
  - preserved/invalidated analyses
  - optional debug dump hooks
```

The Pass Manager owns ordering, verification checkpoints, statistics, and analysis invalidation. Individual passes own only their algorithm-specific state.

Potential optimization pipeline:

```text
canonicalize
 -> CFG simplify
 -> constant propagation/folding
 -> dead code elimination
 -> mem2reg / SSA construction
 -> SCCP
 -> CSE / GVN
 -> loop simplification
 -> strength reduction
 -> target-independent lowering cleanup
```

Not all passes are immediate milestones. The important rule is that adding them later does not require rewriting parser or target codegen.

Optimization levels (`-O0`, `-O1`, `-O2`, later size/debug modes) are Driver-selected pass pipelines, not `if (optimize)` checks scattered throughout frontend code.

## 6. Frontend extensibility / 前端扩展机制

### 6.1 LanguageOptions

One explicit language configuration is passed into the frontend:

```text
standard: C89/C90, C99, C11, C17, C23
GNU dialect: off/on
pedantic mode
char signedness policy where target/compiler option controls it
selected compatibility switches
```

Parser/sema decisions query `LanguageOptions`; they do not silently assume GNU mode globally.

### 6.2 Declarator engine

All declaration contexts share one declarator representation:

```text
DeclSpec -> Declarator -> Type construction -> declaration semantics
```

The declarator model supports identifiers, pointers/qualifiers, arrays, functions, parenthesized declarators, abstract declarators, attributes, and source ranges. Locals, globals, typedefs, parameters, record fields, casts/type-names, and function definitions reuse it.

### 6.3 Extension registries

GNU and implementation extensions are grouped by semantics rather than scattered string comparisons:

- Attribute registry;
- Builtin registry;
- Pragma registry;
- extension keyword/token hooks where necessary;
- target builtin registration through the Target interface.

A registry entry describes parsing shape, semantic validation, allowed attachment points, and lowering consumer. Unknown ABI/layout-changing attributes must never be silently ignored.

扩展机制追求“集中分发 + 明确消费者”，不是动态插件 ABI。自举阶段仍然可以全部静态链接。

## 7. Symbols and scopes / 符号与作用域

Symbol lookup must scale to Linux-sized translation units.

Use interned names plus hash-backed scope tables while preserving C namespace rules:

- ordinary identifiers;
- tags;
- labels;
- record/union members.

Scopes form an explicit parent chain. Declarations receive stable `DeclId`/`SymbolId` values. Shadowing and linkage are semantic operations, not repeated full-array scans.

This design permits later indexing, LTO symbol tables, debug info, and linker integration without changing parser syntax code.

## 8. Target architecture / Target 架构

A target implementation is a cohesive bundle, not a collection of `#ifdef RISCV` checks.

Conceptual interfaces:

```text
TargetInfo
  integer widths/ranks
  pointer width/address spaces
  endian
  char signedness default
  long double model

DataLayout
  sizeof/alignof
  record/union layout
  bitfield layout

TargetABI
  argument/return classification
  varargs
  calling convention
  stack alignment

TargetLowering
  Core IR -> Machine IR

TargetAsmPrinter
  Machine IR -> textual assembly

TargetObjectWriter
  Machine IR -> object sections/symbols/relocations
```

The active RV64 backend can migrate into these interfaces incrementally. Text assembly remains a valid output mode after native object emission exists.

## 9. Object, assembler, linker, dynamic linking / 对象、汇编、链接与动态链接

These are later toolchain components with their own representations and tests.

### Object layer

A common object model owns:

- sections and section fragments;
- symbols/bindings/visibility;
- relocations;
- string/symbol tables;
- target-specific relocation kinds through the Target.

ELF is one object-format implementation. The compiler may write ELF objects directly later without forcing the standalone assembler to exist first.

### Native assembler

The assembler consumes assembly syntax into target machine instructions + object fragments. It reuses the target instruction/encoding/relocation definitions used by the backend.

### Native linker

The linker consumes object/archive/shared-library metadata through an object reader, resolves symbols, lays out sections, applies relocations, produces executable/shared objects, and can later emit dynamic relocation/PLT/GOT metadata.

### Dynamic linking

Dynamic linking support is split deliberately:

- static linker emits ELF dynamic metadata and relocations;
- runtime loader is a separate runtime/loader component if MiniC ever chooses to implement one;
- otherwise the compiler/linker may target the platform's existing dynamic loader and libc.

This separation avoids the historical Python tendency to mix compiler, linker, and runtime policy in one path.

## 10. Driver and toolchain configuration / Driver 与工具链配置

The Driver owns user-visible orchestration:

- language mode and optimization level;
- target triple / CPU / feature selection;
- sysroot, include search paths, library search paths;
- external-vs-native stage selection during migration;
- output kind: preprocess, assembly, object, executable, shared object, archive, IR dump, machine dump, memory execution;
- diagnostics and reproducibility options.

The Driver must not implement parsing, instruction selection, or linker algorithms itself.

A future `Toolchain` object describes platform defaults (CRT objects, dynamic loader, standard libraries, assembler/linker choice) separately from `TargetMachine`.

## 11. In-memory execution / 内存执行

A later fun/educational mode similar in spirit to TinyCC's in-memory execution is explicitly supported by the architecture but is not allowed to distort the normal compiler pipeline.

Possible route:

```text
source -> normal frontend -> Core IR -> normal target lowering
      -> memory object writer -> relocation resolver
      -> executable memory manager -> exported symbol lookup -> call
```

This mode reuses:

- Target ABI;
- machine code encoder;
- symbol/relocation model;
- runtime helper resolution.

It does not bypass IR, semantic verification, or target lowering.

## 12. Debug information and diagnostics / 调试信息与诊断

Debugging is a separate cross-cutting subsystem even though no debug-info emitter exists yet.

Prepare now by preserving source locations through AST -> IR -> Machine IR. Later DWARF generation consumes those mappings plus type/declaration metadata at object-emission time.

Diagnostics are frontend/toolchain diagnostics; debug info is target/object metadata. They should share SourceManager identities but not one state object.

目前不需要立即实现 DWARF，但不能在 IR 设计中丢掉源码映射，否则未来只能重做数据流。

## 13. Self-hosting path / 自举路线

Self-hosting is a staged validation property, not a reason to keep the compiler simplistic.

Requirements for bootstrap-critical code:

- ISO C11 implementation baseline unless the project explicitly raises it;
- deterministic generated files or minimal code generation;
- no mandatory host dependency that MiniC cannot eventually replace or drive;
- ability to build a stage-1 compiler with an external compiler, then use MiniC to compile stage-2 and compare behavior/artifacts at selected boundaries.

Suggested stages:

```text
S0 external compiler builds MiniC
S1 MiniC compiles compiler C sources to assembly/object using external surrounding tools
S2 S1 builds S2
S3 reproducibility/differential checks
then independently replace preprocessor / assembler / linker when their milestones begin
```

Full native-toolchain self-hosting therefore does not block the active compiler milestone.

## 14. Performance architecture / 性能架构

Performance work must be measurable and representation-aware.

Foundational choices:

- Arena allocation for phase-lifetime objects;
- interned strings and types;
- stable compact IDs instead of pointer-rich graphs where practical;
- hash-backed symbol tables;
- contiguous pools for AST/IR nodes and variable-length operand slices;
- side tables for analyses/layout rather than inflating every node;
- phase timers, allocation counters, node counts, hash statistics, and peak-memory telemetry;
- deterministic IR dumps for A/B comparison.

Do not prematurely micro-optimize parser branches while expressions remain hundreds of bytes wide and symbol lookup is linear. Data representation and algorithmic complexity come first.

## 15. Migration plan from the current C compiler / 从当前 C 编译器迁移

No rewrite-from-zero is required. Preserve validated semantics and move boundaries deliberately.

### Phase A — make source-of-truth honest

- materialize staged discovery semantics into committed C source;
- keep discovery branches as history;
- CI must compile exactly the checked-in `src/` tree;
- register any remaining temporary deviations.

### Phase B — frontend data foundation

- SourceManager + compact ranges;
- StringId interning;
- TypeId interning;
- hash-backed scoped symbols;
- generic Declarator;
- LanguageOptions and thin extension registries;
- target layout state moved out of frontend AST.

These changes should preserve focused and real-project behavior.

### Phase C — Core IR v0

Introduce one function at a time:

```text
Normalized AST -> Core IR -> verifier -> RV64 lowering
```

Initially keep optimization empty or canonicalization-only. During migration, a temporary differential mode may compile the same function through old direct AST->RV64 and new IR->RV64 paths to compare runtime behavior, but production must converge to one path.

### Phase D — analysis and optimization

Add measurements first, then passes justified by profiles and code-quality goals.

### Phase E — native object emission

Add Machine IR/encoding/object writer while preserving textual assembly as a debugging/reference output.

### Phase F — later toolchain stages

Native preprocessor, assembler, linker, archiver/object utilities, dynamic-link support, and optional in-memory execution become separate milestones and must reuse the common Target/Object/Driver infrastructure.

## 16. Non-negotiable review invariants / 审查不变量

Every architectural change must answer:

1. Which layer owns this data?
2. What is its lifetime?
3. Which invariant verifies it?
4. Is this source-language, target, object-format, driver, or runtime state?
5. Does it improve or damage the ability to optimize the whole function/program later?
6. Can another target/language version/extension be added without editing unrelated modules?
7. Does it keep real upstream source unchanged?
8. Does it preserve a path to self-hosting?
9. Is temporary migration debt visible with an exit criterion?
10. Can the behavior be frozen by focused and real-program tests?

If these questions have no clear answers, the design is not ready to become a permanent compiler interface.
