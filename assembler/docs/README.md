# MiniAS 架构与清理文档入口

**建议从 [当前清理与所有权总览](current-cleanup-map-2026-11-01.md) 开始。**
这份总览是带 SHA 的现状快照；其他文档中的历史“未完成”
和“CI 已通过”只适用于其记录的提交。

| 文档 | 作用 | 阅读原则 |
| --- | --- | --- |
| [当前清理总览](current-cleanup-map-2026-11-01.md) | 数据流、所有权、限制、开放问题与下一批工作 | 最新起点，按 SHA 更新 |
| [全源码审查台账](full-source-review-ledger-2026-10-29.md) | 逐批清理与 P01–P16 历史证据 | 历史追加记录，不是动态状态栏 |
| [架构讨论决议](architecture-discussion-decisions-2026-10-31.md) | 设计原则及大量 GNU 差分结果 | 新决议可以覆盖早期判断 |
| [架构收敛与删除记录](architecture-convergence-2026-10-31.md) | 每条旧实现应删还是保留 | 每次删除必须有消费者证据 |
| [MC 架构 RFC](architecture-rfc-mc-core-2026-10-28.md) | 理想骨架、非目标与未来格式 | 提案不等于已经实现 |
| [四目标架构审计](architecture-audit-2026-10-29.md) | 最初跨模块问题和验收分类 | 仅用作基线参考 |
| [Target 边界](target-boundary.md) | ISA/ABI 的单目标修复历史 | 不自动推广到其他 ISA |
| [TargetSpec 只读所有权](target-spec-ownership-2026-11-02.md) | 对象 ABI 的不可变配置与运行期 ELF flags 独立状态 | 本轮四架构结构收口 |
| [Target DataLayout / ObjectFormat 终结边界](target-datalayout-objectformat-closure-2026-11-02.md) | 唯一 4/8 地址宽度、四 Target ABI、ELF policy 与单 Writer 全链关系 | 当前目标布局与格式边界 |
| [ObjectModel 格式语义边界](object-format-semantic-boundary-2026-11-02.md) | Section/Symbol 从源码语义到 ELF 磁盘编码的唯一投影 | 新格式不能复用 ELF 编码常量 |
| [Linux 验收证据](linux-6.6.143-runtime-acceptance-2026-10-31.md) | GNU 链接与 QEMU 的实际验收 | 必须核对当时的 SHA |
| [本批 exact-head 完整验收](minias-exact-head-full-acceptance-2026-11-02.md) | 4 架构真实 GNU Kbuild/ld/system QEMU、3×7、3,536 与运行时矩阵 | 未完成即不得宣称 P1 PASS |
| [AArch64 垂直切片](aarch64-vertical-slice.md) | 目标初期实现参考 | 历史材料 |
| [自审记录](self-audit-2026-10-28.md) | 早期源码检查 | 历史材料 |

**清理口径：** 每批只统计真实生产代码 +/−、删除了哪些旧消费者、
实际运行了什么 GNU/ELF/QEMU 测试；“源码已阅读”“文件被修改”
与“架构问题已关闭”不是同一个指标。保持单分支和 PR #667。
