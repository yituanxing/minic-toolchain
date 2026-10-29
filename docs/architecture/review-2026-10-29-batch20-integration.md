# MiniC 全源码架构复核与批次 20 集成记录（2026-10-29）

本记录仅覆盖 **MiniC 编译器**，不把 MiniAS 或 NaSeed 的进度混入。沿用
`review-2026-10-29-integration.md` 的原始逐文件台账及后续分区报告；
审计对象包括 Frontend、Compiler、Core、TargetInfo/DataLayout、五目标后端、
公共 ABI、源语言扩展策略、CLI 入口和公开编译 API。

## 全源码审计与增量覆盖

最初 **323 个编译器内部源/注册文件**，加两个 CLI 入口、一个公开 API 头，
在前轮已建立逐文件记录；自当时基线新增加了一个 Core 公共头、六个
`src/target/source_policy/` C/H 文件、两个 target machine option C/H 文件和
一个 target-registration `.def` 文件。因此本轮 `src/` 下有 **333 个源/注册路径**，
加两个 CLI 入口和一个公开 API 头，共 **336 个审读对象**。
新增文件也已按目标语言和机器选项边界补审。
审读与修复是两种独立状态，禁止将文件名清单当成功能正确性证明。

| 分区 | 本轮复核范围 | 重要结论 |
| --- | --- | --- |
| Compiler + RISC-V 共用 + RV32 | 11 + 12 + 16 = 39 个文件，约 7812 行，逐段复读 | 确认 RV32 packed 64-bit scalar 无对齐保护的 `lw/sw`；其它 P1 候选需 GNU 差分 |
| Core | CFG reentry、source effects、独立 standalone lower | indexed/fallback 不能删一条；IF、SWITCH、LOOP、generic tail 对 reentry 的证明强度不同 |
| Target Common + x86-64 + 根目录 | 54 + 19 + 13 份源/注册数据，结合原逐文件记录和本轮 diff 复审 | GNU ASM source qualifier 106 个，绝大多数共享模板；machine 仍重复文字匹配，typed FormId/payload 尚未闭环 |
| RV64 + LoongArch64 | psABI cursor、prepared calls、source GNU alias、machine ASM | RV64 LP64 soft-float FP-record direct/indirect CALL 误拒绝；Loong 外部 alias 特权范围过宽 |
| AArch64 | 结构化 ASM、frame/ABI、large-SP finalizer | 76 个原始静态 SP 槽中 31 个可迁 typed FrameRef；动态帧和实时物理 SP 需区分 |
| Frontend + Target language | GNU 函数属性、opaque builtin 名字、wide FP 常量 | 长双精度原始 `strtod` 舍入问题需要 parser→Core→目标格式合并处理 |

## 本批架构修复及保留边界

1. **RV64 soft-float ABI**：Core admission、frame sizing、direct/indirect call emission
   共用由真实 `-march/-mabi` profile 构造的 immutable prepared call placements。
   LP64 聚合 FP leaf 仍按 GPR psABI 搬运；尚未实现的 LP64D FP-record 保留拒绝。
2. **LoongArch Linux syscall alias**：通用 TargetInfo 只做普通 C 函数类型兼容；
   `sys_SUFFIX -> __se_sys_SUFFIX` 的 ABI transport 特许移至 Loong source policy，
   验证真实符号名、返回值、参数数量/大小/种类，不能误开放任意函数 alias。
3. **Core reentry 证明**：以带 reason/policy 的统一查询替换三套重复 AST 扫描，
   同一契约覆盖 indexed 与 standalone fallback；与动态 Core 已绑定边分开。
   `CASE/DEFAULT`、取标签地址、外部 goto、asm goto 使用适合各调用者的 gate。
4. **AArch64 typed frame**：31 处静态 `[sp,#]` 归入 owner-owned typed FrameRef；
   crash register snapshot 的前置 large-SP load 不再借用尚未保存的 x17。
   对真实硬件 SP 的 counter-ordering 依赖保留；其余 45 处及仍活跃的
   large-SP legalizer/动态-frame 兼容路径不能在无证明时删除。
5. **Target source language**：x86 GNU security/function target 属性的名字和
   feature-value 白名单，以及 AArch header-only opaque 类型和 vector PCS 名词
   均迁往目标 owned policy；Frontend 只负责语法与 typed effect。
6. **公共 DataLayout**：GNU 零大小 record 的 array/record 两条 scalar relocation
   walker 都不能用 `SIZE_MAX / 0`；零大小成员无 scalar slot，后续非零成员仍
   使用真实布局 offset。两条旧路径已有 UBSan `division by zero` 复现，
   新测试在 UBSan、GCC/MiniC native、五 ISA 源生成验证。

## 新发现、未解决和不得误报的问题

| 等级 | 真实风险 / 架构债 | 当前处理边界 |
| --- | --- | --- |
| P1 | RV32I packed record 64-bit scalar 访问使用未对齐 `lw/sw` | 已有真实 `-S` 反例；下一批完善 Core 访问对齐事实和 RV32 保守 bytewise fallback，不能假定硬件支持 misaligned |
| P1 | `long double` 字面量在 Frontend 先经 `strtod` 丢精度 | 已有 GCC/MiniC 退出码差分；需目标 FP 格式及 Core carrier 共审，不能仅替换 `strtold` |
| P1 | x86/Loong/AArch source GNU ASM source-to-machine 分发仍重复字符串分类 | 建 typed semantic FormId+operands，禁止把 source hash 冒充机器 opcode |
| P1 | x86/AArch `va_copy` 游标类型特例仍在 TargetInfo | 下一批搬至 target-owned `VariadicCursorTypePolicy` |
| P1/P2 | RV32 inline ASM 64-bit output 是否错误清高半字 | 未完成合法 GNU GCC operand-pair 差分，不得断言缺陷已修 |
| P2 | 部分 parser inline specialization 重复 AST 归属扫描、若干 fallback/死分支 | 先保持语义隔离，再用 perf/probe 证明是否可删 |

## CI 证据分层

- **上一批已推送头** `33d130831f0ccebabec0974a314034a4d4c692ff`：
  真实 Linux 6.6 源 C→MiniC 汇编 **21/21 分片**，AArch64 4078/4078，
  LoongArch64 2723/2723，x86-64 2758/2758，总 **9559/9559**；
  `THREE_LINUX_CENSUS=COMPLETE`、`diagnostic_signatures=0`，GNU AS/link/QEMU 未运行。
- **本批本地（改动已冻结）**：统一 `BUILD_DIR=build/review-batch20`
  `CFLAGS=-Werror` 完整 `all`、`check-fast`、
  `check-minic-target-boundaries.sh`、`probe-review-batch-2026-10-29.sh`
  均 **PASS**。后者现已包含 typed Core reentry indexed/fallback、RV64 LP64
  direct/indirect record、Loong syscall alias 正负例、DataLayout 两处 UBSan、
  AArch64 512-SSA crash snapshot 回归及五 ISA `-S`。涉及交叉 GNU GAS/QEMU
  不可用的子阶段显示 **SKIP**，绝不冒充通过。
- **本批远端**：新提交后的 exact-head 21 分片与 native/运行 workflow
  仍待独立检查；不继承上一批的 9559 源文件通过率，也不把 source `-S`
  当做 Linux Image 成功。
