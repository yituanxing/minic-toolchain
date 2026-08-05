# C1 Validation Recovery / C1 验证状态收口

## Purpose / 目的

This note records the repository-state audit performed before block-scope work. It exists to distinguish committed source files from capabilities that are actually connected to the permanent differential gate.

本文记录进入块作用域开发前执行的仓库状态审计，用于区分“源码文件已经存在”和“能力已经接入永久差分门禁”。

## Recovered boundary / 已收口边界

The call AST already reserved eight argument IDs, while the production Parser and RV64 caller/callee paths still capped execution at four. The eight-parameter and recursive programs also existed without all of them being listed by the aggregate real-program script.

CALL AST 已经预留八个实参 ID，但生产 Parser、RV64 调用方和被调用方仍把可执行链路限制在四参数；八参数与递归程序虽然已有源码，也未全部进入聚合真实程序脚本。

The branch now connects the following boundary consistently:

当前分支已经一致接通：

- zero through eight signed `int` parameters / 0～8 个有符号 `int` 参数；
- exact-arity calls using `a0` through `a7` / 使用 `a0`～`a7` 的精确参数数量调用；
- aligned temporary argument preservation across nested calls / 嵌套调用期间使用对齐临时槽保护实参；
- callee spills into the first eight local slots / 被调用者将参数保存到前八个局部槽；
- aggregate entries for eight parameters, recursive factorial, and mutual recursion / 八参数、递归阶乘和互递归的聚合门禁条目。

## Validation rule / 验证规则

A clean-checkout CI workflow now runs host checks, sanitizers, focused RV64 tests, and the real-program GCC/MiniC differential matrix. The Ubuntu glibc cross environment is supplementary; the pinned musl toolchain remains the formal project validation environment.

新增干净检出的 CI 工作流，执行宿主检查、Sanitizer、聚焦 RV64 测试和真实程序 GCC/MiniC 差分矩阵。Ubuntu glibc 交叉环境仅为补充；固定 musl 工具链仍是项目正式验证环境。

Block-scope implementation must not proceed past its first data-model commit until the clean-checkout gates report a result or the failure is diagnosed and recorded.

在干净检出门禁产生结果，或失败原因被诊断并记录前，块作用域实现不得越过首个数据模型提交。
