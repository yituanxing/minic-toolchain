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
