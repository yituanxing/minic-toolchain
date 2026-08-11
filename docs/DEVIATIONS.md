# Architectural Deviations / 架构偏离记录

Temporary architectural debt is allowed; invisible architectural debt is not.

允许暂时存在架构债务，但不允许架构债务不可见。

This file records deliberate temporary departures from the project principles. A deviation is not automatically a defect: it may be the smallest safe step needed to validate a real workload. Every active entry must remain bounded and must define how it will be removed.

本文件记录项目对架构原则的有意、临时偏离。偏离不一定等同于缺陷：它可能是验证真实负载所需的最小安全步骤。但每个活跃条目都必须限制范围，并明确如何消除。

## Required fields / 必填字段

Each entry must contain:

- **ID**: stable identifier such as `DEV-0001`;
- **Status**: `Active`, `Resolved`, or `Accepted`;
- **Rule**: the principle or policy being departed from;
- **Scope**: affected files or modules;
- **Reason**: why the correct structure cannot be completed in the same change;
- **Risk**: coupling, ownership, correctness, portability, or maintenance risk;
- **Exit criteria**: concrete implementation and validation conditions;
- **Target milestone**: intended review point, not a promise of a calendar date.

每个条目必须包含：

- **ID**：稳定编号，例如 `DEV-0001`；
- **Status / 状态**：`Active`、`Resolved` 或 `Accepted`；
- **Rule / 规则**：偏离了哪项原则或规范；
- **Scope / 范围**：涉及的文件或模块；
- **Reason / 原因**：为什么无法在同一次修改中完成正确结构；
- **Risk / 风险**：耦合、所有权、正确性、可移植性或维护风险；
- **Exit criteria / 退出条件**：具体实现和验证条件；
- **Target milestone / 目标里程碑**：预期重新审查的节点，不是日历承诺。

`Refactor later / 以后重构` is not a valid exit criterion.

`以后重构` 不是有效的退出条件。

## Active deviations / 活跃偏离

## DEV-0002: deferred dynamic immediate for GNU asm goto / GNU asm goto 动态立即数延迟特化

- Status / 状态: Active
- Rule / 规则: The active compiler boundary requires accepted `.i -> .s` output to remain suitable for external target assembly/link validation; temporary architectural debt must be visible and bounded. 当前编译器边界要求已接受的 `.i -> .s` 结果能够继续进入外部目标汇编/链接验证；临时架构债务必须可见且范围明确。
- Scope / 范围: `src/target/riscv64/codegen_inline_asm.c`, `tests/compiler/c0/run-gnu-inline-asm-goto.sh`, unchanged Linux 6.6.143 discovery input. 涉及 RV64 inline-asm lowering、对应 focused gate 与 Linux 6.6.143 discovery 输入。
- Reason / 原因: The current Linux `asm goto` sites use GCC `"i"` operands whose source expressions become constants only after always-inline specialization. MiniC does not yet have an inliner/specializer, so the discovery compiler preserves the unresolved dependency explicitly as `__minic_deferred_asm_immediate_*` instead of inventing an immediate value or silently changing control-flow semantics. 当前 Linux 的 `asm goto` 使用 GCC `"i"` 约束，其源表达式只有在 always-inline 特化之后才成为常量。MiniC 尚无 inliner/specializer，因此 discovery 编译器用显式 `__minic_deferred_asm_immediate_*` 保留未解析依赖，而不是伪造立即数或静默改变控制流语义。
- Risk / 风险: MiniC can report successful `-S` discovery for a translation unit whose deferred asm immediate is not yet a final externally linkable program. Treating this discovery result as full compiler acceptance would hide an incomplete compilation responsibility. MiniC 可能对含 deferred asm immediate 的翻译单元成功完成 `-S` discovery，但该结果尚不是最终可外部链接的程序；若把 discovery 成功误当完整编译器验收，会掩盖未完成的编译职责。
- Exit criteria / 退出条件:
  1. Introduce a real inline/specialization or equivalent semantic lowering step that resolves every accepted GNU asm `"i"` operand before final target assembly emission. 建立真实的内联/特化或等价语义降低步骤，使所有已接受 GNU asm `"i"` operand 在最终目标汇编生成前得到解析。
  2. Remove `__minic_deferred_asm_immediate_*` emission and the focused test expectation for deferred specialization. 删除 `__minic_deferred_asm_immediate_*` 输出及 focused gate 中对 deferred specialization 的通过条件。
  3. Add an external RISC-V assemble/link validation case for the affected asm-goto shape and keep all frozen real-project/Linux gates at or beyond their prior frontier. 为受影响的 asm-goto 形状增加外部 RISC-V 汇编/链接验证，并保持所有冻结真实项目/Linux 门禁不退线。
- Target milestone / 目标里程碑: Function specialization/inlining boundary before full Linux object acceptance / 完整 Linux object 验收前的函数特化/内联边界。
- Related evidence / 相关证据: `run-gnu-inline-asm-goto.sh` requires the explicit deferred marker today; Linux 6.6.143 inventory currently contains four `asm goto` sites. 当前 `run-gnu-inline-asm-goto.sh` 明确要求 deferred marker；Linux 6.6.143 inventory 当前包含 4 个 `asm goto` 用例。

## DEV-0003: call-frame introspection before inlining / 内联实现前的调用帧内省语义

- Status / 状态: Active
- Rule / 规则: Real-program acceptance should preserve the observable semantics of target/runtime-sensitive language features; optimization attributes that change observable builtin behavior must not be silently treated as semantically irrelevant. 真实程序验收应保持 target/runtime-sensitive 语言能力的可观察语义；会改变 builtin 可观察行为的优化属性不能被静默视为无语义元数据。
- Scope / 范围: `__builtin_return_address(0)`, `__builtin_frame_address(0)`, parse-only `inline`/`always_inline` function metadata, and unchanged Linux 6.6.143 discovery. 涉及 level-0 return/frame-address builtin、当前仅解析不持久化的 inline/always_inline 元数据与 Linux 6.6.143 discovery。
- Reason / 原因: Linux contains real level-0 call-frame builtin uses before MiniC has an inliner. The RV64 backend can still model the actual emitted MiniC frame exactly: return address is loaded from the entry-time saved RA slot and frame address is the current frame pointer. This is materially safer than reading live `ra` or inventing a value, but it cannot yet reproduce GCC's observable result when the containing wrapper is inlined. Linux 已出现真实的 level-0 调用帧 builtin，而 MiniC 尚无 inliner。RV64 backend 仍可精确表示 MiniC 实际生成的函数帧：return address 从函数入口保存的 RA slot 读取，frame address 使用当前 frame pointer。这比读取 live `ra` 或伪造值正确得多，但尚不能复现 GCC 在包含该 builtin 的 wrapper 被内联时的可观察结果。
- Risk / 风险: Linux `-S` discovery can move beyond call-frame builtin syntax/target lowering while runtime caller-IP/frame identity may differ from GCC for wrappers that GCC inlines. Linux `-S` discovery 可以越过调用帧 builtin 的语法与 target lowering，但对于 GCC 会内联的 wrapper，运行时 caller-IP/frame identity 仍可能与 GCC 不同。
- Exit criteria / 退出条件:
  1. Persist supported `inline`/`always_inline` semantics on function entities instead of treating them only as parse-time metadata. 将受支持的 inline/always_inline 语义持久化到函数实体，而不是仅在解析时接受。
  2. Implement and verify the required function inlining/specialization boundary before lowering call-frame builtin values whose observable result depends on inlining. 在降低可观察结果受内联影响的 call-frame builtin 前，实现并验证所需函数内联/特化边界。
  3. Add RV64 assemble/link/runtime differential tests against GCC for representative return/frame-address wrappers, including a call before `__builtin_return_address(0)` so live `ra` cannot accidentally pass. 为典型 return/frame-address wrapper 增加 RV64 汇编、链接和 GCC 运行差分，并包含 builtin 前先调用函数的用例，防止 live `ra` 错误实现误过门禁。
- Target milestone / 目标里程碑: Function inlining/specialization boundary before Linux runtime/object acceptance / Linux runtime/object 完整验收前的函数内联/特化边界。

## DEV-0004: bounded first-class enum representation / 受控的一等 enum 表示

- Status / 状态: Active, substantially narrowed / 活跃，但已大幅收窄。
- Rule / 规则: Accepted GNU C enum syntax must preserve stable type identity, incomplete/complete lifecycle, exact constant values, and target-compatible integer semantics. 已接受的 GNU C enum 必须保留稳定类型身份、incomplete/complete 生命周期、精确常量值与目标兼容整数语义。
- Scope / 范围: Program-owned `MinicEnum`/`MinicEnumerator`, `MINIC_TYPE_BASE_ENUM`, enum completion refresh, typed ConstEval, compatibility, DataLayout, and Linux 6.6.143 enum pressure. 涉及 Program-owned enum/enumerator 实体、一等 enum `MinicType`、completion refresh、typed ConstEval、兼容性、DataLayout 与 Linux 6.6.143 enum 压力。
- Reason / 原因: Foundation EnumEntity v1 now preserves stable `EnumId` identity through forward declarations, typedefs, pointers, functions and AST storage. Enumerators retain typed 64-bit bits and a completed enum caches the GCC-compatible integer representation required by the active RV64 model. The remaining shortcut is that compatible sign/rank is cached into each persisted `MinicType` and refreshed on completion instead of being queried from a canonical future TypeContext/Target integer model on demand. Foundation EnumEntity v1 已经让 forward declaration、typedef、pointer、function 与 AST storage 保留稳定 `EnumId`；enumerator 保存 typed 64-bit bits，completed enum 缓存当前 RV64 下 GCC-compatible 整数表示。剩余简化是 compatible sign/rank 仍缓存进持久化 `MinicType` 并在 completion 时刷新，而不是由未来 canonical TypeContext/Target integer model 按需查询。
- Risk / 风险: Completion currently performs a bounded Program-wide refresh of persisted enum type copies, and target integer-selection policy is still RV64-oriented. C23 fixed underlying enum syntax, `-fshort-enums`, and enum values beyond the current 64-bit ConstEval range are not represented yet. completion 当前会对 Program 中已持久化的 enum type 副本做受控刷新，整数表示选择策略仍面向 RV64；C23 固定 underlying type、`-fshort-enums` 与超过当前 64-bit ConstEval 范围的 enum value 尚未建模。
- Exit criteria / 退出条件:
  1. Move enum compatible-integer selection into the canonical target integer model and remove Program-wide cached-type refresh. 将 enum compatible integer 选择移入 canonical target integer model，并删除 Program-wide cached type refresh。
  2. Preserve enum identity/representation through the future TypeContext/SymbolTable without parser-linear tag/enumerator lookup. 在未来 TypeContext/SymbolTable 中继续保持 enum identity/representation，并移除 parser-linear tag/enumerator lookup。
  3. Add C23 fixed-underlying-type / `-fshort-enums` policy only when a real language-mode or workload requires it; until then keep those modes explicitly unsupported. 仅在真实 language mode/workload 施压时加入 C23 fixed underlying type / `-fshort-enums` policy，在此之前保持显式 unsupported。
- Target milestone / 目标里程碑: Target integer model + TypeContext consolidation before multi-target GNU enum claims / 多 target GNU enum 完整声明前的 Target integer model + TypeContext 收敛。
- Related evidence / 相关证据: Linux `init/main.i` requires forward incomplete enum identity before completion and reaches `enum mm_cid_state { MM_CID_UNSET = -1U, MM_CID_LAZY_PUT = 1U << 31 }` at line 16618; the same TU later contains 64-bit positive enum values such as `0xffffffffULL << 32`. Linux `init/main.i` 既要求 incomplete enum 在后续 completion 前保持身份，又在 16618 出现 unsigned-range enum，并在后文出现 `0xffffffffULL << 32` 等 64-bit 正值。

## Resolved deviations / 已解决偏离

## DEV-0001: C0 parser performs direct lexical matching / C0 Parser 直接匹配源码字符

- Status / 状态: Resolved
- Rule / 规则: Lexer and Parser should have separate responsibilities; Parser should consume a token stream rather than raw source bytes. Lexer 与 Parser 应职责分离；Parser 应消费 TokenStream，而不是直接读取源码字节。
- Original scope / 原范围: `src/compiler/compiler.c`, C0-only parsing path / 仅限 C0 解析路径。
- Reason / 原因: The first milestone established a complete executable `.i → .s` compiler path before rebuilding the lexer/token infrastructure. 第一里程碑先建立可完整执行的 `.i → .s` 编译链路，再重建 Lexer/Token 基础设施。
- Risk / 风险: Lexical rules and grammar were temporarily coupled; directly extending that path would have duplicated token logic and weakened diagnostics. 词法规则与语法曾暂时耦合；若继续扩展旧路径，会重复 Token 逻辑并削弱诊断结构。
- Exit criteria / 退出条件:
  1. Completed: explicit token kinds and half-open source spans were introduced. 已完成：建立明确的 TokenKind 与半开 SourceSpan。
  2. Completed: a focused Lexer with positive and negative tests was introduced. 已完成：建立带聚焦正负测试的 Lexer。
  3. Completed: the production C0 Parser now consumes only Lexer/Token output; raw keyword and punctuation matching was removed from `compiler.c`. 已完成：生产 C0 Parser 只消费 Lexer/Token 输出，`compiler.c` 中的原始关键字与标点匹配已经删除。
  4. Completed: `make check-fast`, ASan/UBSan, and the pinned RISC-V GCC/QEMU runtime gate passed after replacement. 已完成：替换后通过 `make check-fast`、ASan/UBSan 和固定 RISC-V GCC/QEMU 运行门禁。
- Target milestone / 目标里程碑: C1 lexer and token-stream integration / C1 Lexer 与 TokenStream 接入。
- Resolution / 解决结果: Production flow is now file input → Lexer → Token → Parser → RISC-V assembly output. 当前生产流程为：文件输入 → Lexer → Token → Parser → RISC-V 汇编输出。
- Related commits / 相关提交: `bcfa392`, `ae3bc7d`, `197e175`, `fbbd805`, `4d57665`.

## Entry template / 条目模板

```markdown
## DEV-0001: concise title / 简短标题

- Status / 状态: Active
- Rule / 规则:
- Scope / 范围:
- Reason / 原因:
- Risk / 风险:
- Exit criteria / 退出条件:
  1.
  2.
  3.
- Target milestone / 目标里程碑:
- Related commits or issues / 相关提交或 Issue:
```

## Resolution policy / 解决策略

When a deviation is resolved:

1. move its implementation to the intended boundary;
2. run the tests named in its exit criteria;
3. change the status to `Resolved`;
4. record the resolving commit or pull request;
5. retain the entry as architectural history.

解决偏离时：

1. 将实现迁移到目标边界；
2. 运行退出条件中列出的测试；
3. 将状态改为 `Resolved`；
4. 记录解决该问题的提交或 PR；
5. 保留条目，作为架构演进历史。
