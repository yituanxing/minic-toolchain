# MiniC 第三轮 Target/Driver 架构边界审查（2026-10-29）

基线：第二轮精确远端提交 `1d8b34f897ada6e4351b56b8f0320be834007f9f`，原始 Linux C `-S` 三 ISA × 七路 `21/21`，源文件 `x86_64=2758/2758`、`aarch64=4078/4078`、`loongarch64=2723/2723`；四架构运行时、RV32、RV64、native strict 工作流成功。`-S` 不代表 GNU AS/链接/Image/QEMU kernel boot。第三轮**架构与职责优先**，所有跨层变更需保住上述 exact-head 能力。

## 本轮对照的分层原则

LLVM 的 target-independent code generator 在明确的 target machine/subtarget 与 target-specific register/instruction lowering 边界之后才落入目标汇编；GCC `targetm` 和 C-family `targetcm` 分离公共算法与目标回调。MiniC 不需要照搬 LLVM/GCC 的 C++/RTL 类层次，但应保留其最重要的**方向性约束**：FrontEnd/Core 不理解五种 ISA 名称，Target 统一公开语言布局/ABI/可生成指令集等能力，机器发射由目标负责。

参考第一手文档：

- LLVM: https://llvm.org/docs/WritingAnLLVMBackend.html
- GCC internals: https://gcc.gnu.org/onlinedocs/gccint/Target-Structure.html
- GCC target hooks: https://gcc.gnu.org/onlinedocs/gccint/Target-Macros.html

## 逐项问题与修改准则

| 优先级 | 发现与真实源码位置 | 建议的结构性收敛 | 状态 |
| --- | --- | --- | --- |
| P1 | `src/target/target_info.c` 约 1,334 行，多处 `architecture == MINIC_TARGET_ARCH_*`；固定寄存器、local GNU register、clobber、ASM immediate、Loong machine builtin 与 C integer semantics 共存 | 把目标特化的 GNU source policy 和 builtin schema 归属单一 target-owned descriptor/hooks；TargetInfo 只查询统一 contract；同一个 machine form 不在 Source qualifier 与 emitter 两边维护两套真相 | OPEN，需与 GNU typed operand plan 协作 |
| P1 | `src/target/target_info.h:86` 通用 `MinicTargetInfo` 直接内嵌 `MinicRiscvIsa riscv_isa`，其头文件引入 `target/riscv/isa_features.h`；x86/AArch/Loong 也被迫携带 RISC-V 专属字段 | 下轮将 ISA supported/generated/ABI subtarget 变成 invocation-local target-owned option payload/opaque handle，经 typed target accessor 获取；避免仅重命名结构体后仍向 Core/Driver泄漏 ISA | OPEN，正在等 RV 目标 ABI 审计结果再迁移 |
| P1 | `tools/minic-cc/main.c` 原来 `#include target/riscv/isa_resolve.h` 并直接修改 `.riscv_isa`、专门判断 family，违反 Driver 无特例 | 新增 `src/target/target_machine_options.[ch]`：CLI 只传 opaque `-march/-mabi`，target registered family 层解析并返回一致错误诊断；不会让 driver 自己枚举 architecture | 本轮已修改，待整体验证 |
| P1 | `target_info.c` 既有 `language_type_capabilities` 又有 `call_lowering_capabilities`，并兼管 PIC、varargs cursor、frame builtins。**部分区分是正确的**，但 ABI 分类尚可能分散 | 将 immutable C language policy 与 invocation-specific ABI/machine capability 两层建模，先定义哪些事实 immutable，哪些随 `-mabi/-march/-fPIC` 变，再迁移并删旧权威字段 | OPEN，先立契约、保留既有 CI |
| P2 | `DataLayout` 已显式 byte order、对象宽度/对齐，Core packed bitfield fail-closed，GlobalEmissionPlan 仍按小端位次编码 | 未来新增大端目标时 GlobalEmissionPlan 对 bitfield 初始值同样 fail-closed 或依 DataLayout 编码；不在当前五个小端目标胡乱添加大端分支 | OPEN，不影响现五目标 |
| P2 | 目标 registry 已单点登记 5 个 ISA，实际 C 语言配置仍各 target 描述 | 保留单点注册 + 目标描述，禁止用“两个架构恰好 LP64”推导调用 ABI；新增目标只增加 target descriptor 和 backend，而不修改 Frontend/Core 的分发 | 已有基础，持续审计 |

## 本轮端到端验收准则

1. `rg 'MINIC_TARGET_ARCH_|riscv_isa|-march/-mabi.*RISC-V' tools/minic-cc/main.c src/frontend src/core` 为无架构知识泄漏（源码名和 ABI 注释可例外，不能有目标枚举分发）。
2. RV32I ILP32 与 RV64I/LP64、RV64IMAFD/LP64D 的 `-march/-mabi` 组合、错误拒绝、未选 ISA 默认行为前后等价；`x86_64-linux` 不接受误传 RV ISA。
3. `check-minic-target-boundaries`、native `-Werror`、五 ISA `-S`、各自 ABI/ASM regression；新 exact-head Linux 21 shard 和 four-arch runtime 全部回归。
4. 所有迁移按调用/能力闭包一批删除活跃重复实现；不把源级 parse-only 能力当成机器码生成承诺，也不删除仍有调用者的 AArch ASM text legalizer。
