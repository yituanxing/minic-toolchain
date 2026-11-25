# MiniC 架构收敛：当前入口与历史证据

本页是**现行导航**，不再逐提交重复存储旧代码块、计数、测试日志和已完成的 R1–R6 / IR-P0–P68 过程记录。完整原文（3,473 行）固定保存在 [收敛账本历史版本](https://github.com/yituanxing/minic-toolchain/blob/40d24f3d9ff376d86e47896320310ffd1a8c6ce6/docs/architecture/minic-refactor-ledger.md)，可按原批次/标题查看和复现。

## 当前架构与契约的真正来源

- [当前架构导航](README.md) — 实际模块与文档所有者。
- [全局 Source/AST/Core/MachineIR 交接合同](minic-global-contract-map.md) — 语言语义与目标准入、资源所有权。
- [函数契约登记](minic-function-contract-register.md) — 当前调用者、输入、失效路径和验证等级。
- [缺陷/技术债台账](minic-issue-register.md) — MC 编号、未关闭风险、当前已知失败和非问题。
- [后端机器审核登记](minic-backend-review-ledger.json) — 当前源码/输出审计事实。
- [当前构建及测试](../../Makefile)；[C0 Native Matrix](../../tests/compiler/c0/README.md) — 验证入口。

当前框架：C 前端（词法、语法、类型及声明语义）→ 经验证的 AST / Core + ModuleIR → 通用机器事实与 typed MachineIR → 目标私有 ABI/指令/ELF 选择。共享的是相同语义与所有权；RV32、RV64、x86-64、AArch64、LoongArch64 的 ISA、ABI、节区与失败策略不能因名称相似强行合并。

## 收敛的持续规则

1. 删除旧实现之前，核对生产调用、CI/脚本动态调用、反例、错误传播、分配失败回退与目标差异；不能以只读静态搜索或相似函数名当成死代码证明。
2. 重复事实应确定唯一所有者，移除其他完整实现；不要增加转发包装、第二套配置、兼容层和无用途新文件。
3. 已接受的 C/GNU 扩展输入、封闭拒绝、精确数据布局、ELF 重定位与实际运行结果必须可独立复验。
4. 只有当前提交、当前源码和真实执行的构建 / Matrix / GNU ELF / QEMU 能证明现行 PASS；旧日通过及缓存命中不自动续证。
5. 每轮只报告真实净删和验证范围；编译通过、源级汇编通过与内核运行成功严格区分。

## 历史批次锚点（保留旧链接）

## R2：Frontend relocation 旧入口退休；RV32 Global source 契约冻结

[原 R2 全文与等价验证](https://github.com/yituanxing/minic-toolchain/blob/40d24f3d9ff376d86e47896320310ffd1a8c6ce6/docs/architecture/minic-refactor-ledger.md#r2frontend-relocation-旧入口退休rv32-global-source-契约冻结)。历史结论只对应当时源码，RV32/64 的 ELF section 差异仍须由各目标 ABI 合同证明。

## IR-P46 — Coherent machine-text transport, aggregate copy, head address roles (2026-11-04)

[原 P46 全文与 GNU/MiniC 对照](https://github.com/yituanxing/minic-toolchain/blob/40d24f3d9ff376d86e47896320310ffd1a8c6ce6/docs/architecture/minic-refactor-ledger.md#ir-p46--coherent-machine-text-transport-aggregate-copy-head-address-roles-2026-11-04)。性能和汇编体积历史数字不替代当前五 ISA GNU ELF/QEMU 复验。

## 归档原则

如要复现历史优化或某项源码改动，使用以上固定提交的完整源码、脚本和原输入，而不是把旧批次代码补回当前工作树。此页不宣布 R1–P68 所提架构债务全部解决。
