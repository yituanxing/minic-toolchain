# MiniC 全源码架构/正确性审计：集成清单（2026-10-29）

基线分支 `refactor/minic-target-language-boundary`，起点 `3866af2f64a8480268afa4655730d8334edfa4e7`。这份清单由主协调人整合；逐文件阅读证明以同目录五份分区台账为准。**读完源文件不等于功能验收**；本轮优先完成跨层边界审查、真实缺陷修补和聚焦回归，21 分片 Linux 编译与 QEMU 运行仍需对最终提交重新触发。

## 全量审查边界

| 分区 | C/H 数 | 逐文件审计台账 |
| --- | ---: | --- |
| Frontend + Compiler | 81 | `review-2026-10-29-frontend-compiler.md`（另含两个 CLI 入口和一个公开头） |
| Core IR/Lower + Target 根目录 | 53 + 1 新增 | `review-2026-10-29-core-target-root.md`；新增共享 `core/linear_vararg_cursor.h` 已单独通读 |
| Target Common + x86-64 | 73 | `review-2026-10-29-common-x86.md` |
| Target AArch64 | 24 | `review-2026-10-29-aarch64.md` |
| Target RISCV/RV32/RV64/LoongArch64 | 92 | `review-2026-10-29-riscv-loong.md` |
| **仓库覆盖** | **323 原有内部 C/H + 1 新增 Core 头 + 2 CLI + 1 API 头 = 327 文件** | 全部通读；公开 API 为 `include/minic/compiler.h`；其它八个 minic-* 工具属于汇编/链接/预处理/objcopy 等独立工具，不在编译器审计范围 |

## 当前已实施的正确性修复（最终回归以 exact-head 为准）

- **P0 Core CFG**：常量 NULL 返回和 `_Bool` 范围剪枝保护实参、`++`、调用的副作用；`?:`、`&&`、`||` 的局部事实按 CFG 可执行路径独立保存并取交集；switch 各 case 已按 dispatch 与真实 fallthrough 合流，嵌套 case 入口另有事实屏障。动态 GCC/MiniC 对照前后验证。
- **P0 RV64 Record**：所有 `RECORD_LOAD` 采用 byte-exact 源复制；packed struct/嵌套成员及源/目标 frame odd offset 的参数/调用/返回 ABI，均走有 alignment proof 才宽访存、否则字节 gather/scatter 的路径。
- **P0/P1 前端**：GNU cleanup+goto 上下文、统一 `struct/union/enum` TagBinding lexical scope、binary32 常量解码、表达式 arena borrowed 指针生命周期、复杂 `void*` 转换限定符、声明属性失败事务顺序；另删除 205 行不可达独立静态 pointer/integer array 初始化实现（保留通用 initializer）。
- **P1 RV32/ASM**：超过八个通用 ASM register roles fail-closed；`__builtin_ffsll` 始终以 64 位 CTZ 处理高半字；变参 record 与 scalar 的 Core linear cursor 共用，RV32 target 以实际 alignment 决定 a1/a2 或 a2/a3；Core GNU `%%` 模板转义、rK 空模板、零步长下标与有效输出 effect。
- **架构收敛**：AArch64 部分 FP/record/branch/ABI owned-slot 迁移到 typed FrameRef；x86 无 relocation 的 zero Record 分类复用 Common GlobalEmissionPlan；Core opcode 计数上界与实际枚举同步、删除过期 LLVM/Linux 专名 stderr 探针及冗余二次扫描。

## 尚需按依赖关系推进的架构问题

1. **P0/P1：CFG facts 与 side effects 证明边界**。把 source expression effect summary、def/use、reentry 与 local-facts path-join 定义成单一权威语义；所有 shortcut (`if`/loop/`?:`/switch/CFG const call) 依该抽象证明；接入真实 native runtime 的负/正测试。参考 `core_lower_if.c`、`core_lower_expr_flow.c`、`core_lower_switch.c`、`core_lower_const_facts.c`。
2. **P1：GNU inline ASM 双重分发**。x86/Loong/AArch source qualifier 和 target machine emitter 各有巨大字符串或模板分发表；Core 又有 scalar/forms/structured/target 四种操作数分支。建立含 form-id、operand constraints、effect/clobber、early-clobber 的 typed AsmOperandPlan，然后分族迁移；保护 Linux CSR、`__ex_table`、alternative、futex 原式。不得先删现有活跃 legalizer。
3. **P1：TargetDescriptor/DataLayout 注册单一来源**。`target_catalog.c`、`target_info.c`、`backend.c`、CLI、DataLayout 是多份穷举；目标新增时不该改 Frontend/Core，且“目标支持的 ISA”与“MiniC 自己生成的最小 ISA”必须独立。先统一 registry 与策略查询，再删旧枚举/字符串判断。
4. **P1：AArch64 typed FrameRef 全覆盖**。`asm_legalize.c` 两套全文件/动态 frame 文本修复仍**实际被调用**；还剩 `integer_emit.c`、`residual_emit.c`、ASM、部分 prologue 和 transient outgoing SP 未迁移。必须定义 entry SP/owned/outgoing/scratch 生命周期，再逐一退役文本后处理。
5. **P1：GlobalEmissionPlan 扩充 ABI 事实**。x86 与 Common 的 float/double、section @nobits/progbits、scalar pointer reloc、flexible record 和 nested relocation 分类目前不同；扩展公共计划的 payload variants 和 section policy 后，才可删多处 target 重复分支。
6. **P1：LP64/ILP32 ABI 与 Record alignment 后续**。RV32 `va_arg` record cursor 与 variadic aggregate caller、RV64 packed aggregate argument/return 本批已修；RV64 hard-float 第九个定参等仍需统一 ABI cursor 语义及真实 QEMU 差分；不能用在宽度上碰巧相等的 x86/RV64 来替代协议证明。
7. **P1：位域、长双精度和 C tag**。四套位域 extract/merge/RMW、Core 把所有 long double 当 binary128 `__*tf3`、struct/union/enum 分离 tag registry，应收敛到类型化 layout/capability 语义对象与 scope binding。
8. **P2：性能与内存结构**。`MinicFunction` 无条件内嵌约 64×多族 specialize facts；symbol lookup/局部 effect 证明多次 O(TU) 扫描；Core object reuse 层层线性扫描。Linux profile 前不做盲目结构迁移；必须留 OOM fallback 和诊断可复现性。

## 验收界限

本地聚焦测试覆盖 `-Werror`、五 target `-S`（视测试脚本）、GNU/x86 原生差分、RV32I 严格 gate、typed FrameRef、ASM 负例。现有 `check-fast` 和集中批次 `probe-review-batch-2026-10-29.sh` 均在集成工作树 PASS；CI 已增加相同的批次命令。非本机目标的 GNU cross-assembler 和 QEMU 并未在本轮本地获得普遍可执行证据。起点 HEAD 的 21/21 Linux compile 9559/9559 **不可直接计为本轮修改通过**，必须在最终推送的 exact HEAD 上检查 CI。与此同时运行时 correctness 和 Linux QEMU 仍是下一阶段独立验收，不与 C 文件编译覆盖率混淆。
