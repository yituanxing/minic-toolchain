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

None.

暂无。

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
