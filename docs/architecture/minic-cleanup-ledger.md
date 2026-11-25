# MiniC 外围与生产代码清理：当前边界

这是**执行标准和现状摘要**，不是每次改动都追加的百页流水账。原始 C1–C63 完整审查、删除理由、KEEP 决策和逐轮验收仍可在 [固定历史清理账本](https://github.com/yituanxing/minic-toolchain/blob/40d24f3d9ff376d86e47896320310ffd1a8c6ce6/docs/architecture/minic-cleanup-ledger.md) 阅读（1,761 行）；不可把历史证据当成今天的测试结果。

## 清理目标

将 MiniC 作为独立、长期维护的工具链，消除 AI 反复生成的死接口、重复实现、不可复现的一次性实验和没有消费者的文档、测试脚本及 CI 路由。最终的衡量标准是可维护性和独立回归能力，而不是把多个文件机械合并成一个巨型文件。

## 现行保留范围

- **源码：** `src/` 的前端、Core、五目标后端与所有实际被构建调用的共享部件；重构先查 [架构合同](minic-global-contract-map.md)、[函数契约](minic-function-contract-register.md) 和 [未决问题](minic-issue-register.md)。
- **测试：** 对合法输入、错误拒绝、ABI、ELF、机器指令和真实运行有独立验证能力的 C fixture；优先复用 `tests/compiler/c0/native-matrix.json` 和既有执行器，不保留重复 Shell 编排。
- **CI：** 当前真实可触发/调用的构建、Strict、Runtime、Stage2/4、工具链与 Linux 专项。分支历史失效不等同于手动/可重用工作流无价值；只删无消费者、不可复现或完全被替代的入口。
- **文档：** 当前操作边界和未关闭缺陷继续在工作树维护；历史流水账、固定过期计数和重复证明转为明确 SHA 的 Git 历史引用。

## 已知不能冒充完成的事项

- C38 的旧文件审读覆盖率不是当前 414 个 `src` 文件的逐行审查证明。局部架构抽查也不能代替跨目标和调用图复核。
- 原历史 C61 发现的 RV64 动态 `alloca` 后端能力缺口不能靠移除失败用例解决。
- 每次外部 Linux 全量编译、GNU AS/LD、QEMU P0/P1、Stage2/Stage4、自举及 MiniAS/MiniLD 联合运行均须严格标注确切提交、是否执行和结果。
- 非精确的文本匹配若固定旧物理寄存器或函数是否被优化，可用更强的运行行为与机器指令结构断言代替；不能简单删除失败检查。
- 尚不能宣布所有外围、重复代码、历史工作分支及全架构验收已完成。

## 最近可复核的清理路径

- 先前 C60–C63 的完整批次理由与提交历史在 [冻结账本](https://github.com/yituanxing/minic-toolchain/blob/40d24f3d9ff376d86e47896320310ffd1a8c6ce6/docs/architecture/minic-cleanup-ledger.md) 中。
- 当前 C0 现役矩阵从 185 扩展到 196 个独立具名用例；新增脚本的正例、拒绝、GNU 本地差分及 AArch64 `va_copy` 拷贝事实已纳入既有 runner。 [196/196 Native Matrix 成功运行](https://github.com/yituanxing/minic-toolchain/actions/runs/45637022211) 对应提交 `1f9e6e8`；该成功记录**不**自动证明之后文档清理提交的全部工具链动态验收。
- 每轮精确文件数以当前 `git ls-files` 或完整 Git tree 为准；已清理的旧文件不恢复为源码旁边的第二份备份。

## 每次结束的最低报告合同

说明 **本轮** 的具体删除与保留、实际验证类型和失败/未运行项；净删除行数按源码、测试、配置、文档分别报告。不得用“总计历史减少”替代本轮结果；不得通过追加计划、转移文件或虚假的工作分支数量制造进展。
