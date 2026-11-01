# MiniAS 当前清理与所有权总览（2026-11-01）

**核对基线 SHA：** `5d8d10408b4ecc6499835aa287028234e8aabdc7`
（`refactor/minias-target-isa-abi-core`，Draft PR #667）。
本文是重新检查当前生产源码目录和设计记录后整理出的状态快照；
随后的 ALIGN 接口清理应以其新提交和新 CI 单独验收。
旧台账保留历史证据，**这里不复用旧“pending CI”状态**。

## 一、读、改、完成必须分开

| 指标 | 基线结果 | 正确含义 |
| --- | --- | --- |
| 当前源码目录 | **32 文件：22 C、10 头文件** | 实际目录规模；不含 CLI 和 `elf/` |
| 历史全源码审查 | **35/35** | 对旧基线的通读，不是这次逐行重新证明 |
| 本轮清理起点 `9f563db` 起 | **22 次提交、19/32 文件曾修改** | Git 变更覆盖，绝非 59% 清理完成 |
| 生产代码净变动 | **+732 / −947，净减 215 行** | Git 实际差异，净删不等于架构关闭 |
| 上述基线的核心 CI | MiniAS ISA/ABI、x86/Loong GNU、AArch64/QEMU、MiniC native/四架构：**SUCCESS** | 只对这个 SHA 成立 |
| 严格 3,536 项 GNU 语义及全部 Linux Image/QEMU | **当前 SHA 未重跑** | 禁止套用历史结果 |

旧台账的“29/32 修改覆盖”使用更早的 10 月起点，
与上表 19/32 **不是一个基准**；文件是否触及与职责是否收敛
也不是一个概念。

## 二、真实数据流及其所有者

```text
调用/配置：Invocation、TargetSpec、ISA/ABI
  -> SourceGraph：物理文件一次读取、SourceSiteID、重放身份
  -> Source Expansion：.macro/.rept/.irp、条件、词法展开
  -> Common Parse：Section/Symbol/Stmt、共享 MC 词法与表达式
  -> MC：typed MCInst/MCExpr，布局、松弛与 Fragment 地址
  -> Target：各 ISA 编码、FixupKind/重定位意图
  -> EncodeResult：活动 Fragment 的字节/Fixup/锚点事务提交
  -> ObjectModel -> ELF Writer -> minielf ET_REL
```

Common 管源码语义、数据模型与最终对象；Target 管具体 ISA/ABI、
指令宽度和机器码；ELF Writer 负责序列化，不重新解释源语法。
COFF/Mach-O 是后续格式扩展，不允许反向污染当前 MC 语义。

## 三、32 文件清单：哪些要保留，哪里还有债

| 模块 | C 源文件 | 判断 |
| --- | --- | --- |
| Invocation/配置 | `invocation.c`、`target_registry.c`、`riscv_profile.c` | **保留**：通用注册与 RISC-V profile 不重复 |
| Source | `source_graph.c`、`source_expansion.c` | **P05 部分收敛**：缓存、宏展开、EOF 的归属已明确，仍需处理调用者耦合 |
| Common/MC 基础 | `assembler.c`、`mc_lexer.c`、`mc_operand_semantics.c` | **保留单路径**：Section/Symbol/Fragment 生命周期由 Common 管；`assembler.c` 仍是大型聚合点 |
| 表达式/布局 | `expression.c`、`mc_expr.c`、`mc_layout.c` | **合理分工**：共享语法，但 wrapping、绝对、延迟重定位求值语义不同 |
| 编码/目标 | `mc_encode_result.c`、`riscv_target.c`、`riscv_encode.c`、`aarch64_target.c`、`x86_64_target.c`、`loongarch64_target.c` | **P04 部分收敛**：输出事务唯一；四个 `encode(MiniAs*,Stmt*)` 回调仍过宽 |
| CFI/DWARF | `cfi.c`、`cfi_writer.c`、`debug_line.c` | **保留**：CFI 语义状态与两个 DWARF 消费端不同，不能只因字节写法相似就合并 |
| 对象/输入 | `elf_writer.c`、`string_literal.c` | **保留**：最终序列化与字符串解析职责独立 |

十个头文件：`minias.h`、`minias_internal.h`、
`expression_grammar.h`、`source_graph_internal.h`、
`riscv_target.h`、`riscv_profile.h` 与四个架构专属
`*_fixup_kinds.h`。它们**已包含**在上面的 32 文件中。
本次做了目录/职责和关键消费者核对，不宣称重新逐行证明
全部 32 文件没有死代码。

## 四、限制现状：解除、保留、待证明

| 限制/接口 | 真实状态 | 后续处理 |
| --- | --- | --- |
| 源码物理行固定 64 KiB | 已改为按需增长读取 | **已解除**，留 GNU 长行回归 |
| `.macro/.irp/.rept` 收集器 1024/256 字节复制上限 | 已改为复用动态缓冲；嵌套分类只认指令种类 | **已解除**，保留大输入用例 |
| 宏参数数量与展开递归上限 | 仍存在真实资源/收敛保护 | 不擅自放开 |
| Target 指定 Fixup Section、锚点所属 Fragment、指令输出 Section | 已被活动 Fragment 归属替代 | 保持结构测试阻止旧 API 回来 |
| x86 `.code16/.code32` Allowlist | 仍存在且参与模式合法性 | 先 GNU 全模式等价验证再删 |
| RISC-V `.option arch,+zbb/+v` | **站点级受限支持已经存在**，支持 push/pop，保留 file-ISA/strict-I 语义 | P02/P07 仍部分完成，不能说整个 extension 系统完成 |
| 更多 `.option arch`、正向 `relax` | 仍 fail-closed | 未有重定位/ISA 证据前不静默放行 |
| 条件拓扑触发的完整 SourceGraph 重放 | 正确性兜底 | 不以“重复运行”为由直接删除 |
| Source 字符串及 NameId/SymbolID | 两种不同身份、真实诊断消费者 | 保留；不属于无用字段 |

## 五、开放问题按依赖排序，而不是按文件数量

**P04 — 当前主阻塞。** Parse 已使用受限上下文，
Measure/Relax/PostEmit 也有阶段能力；Fixup、锚点、机器字节
写入均被活动 Fragment 限制。但四架构
`TargetOps.encode(MiniAs*, const MiniAsStmt*)` 仍有过大的
Common 状态访问权限。应先精确统计编码阶段所需的只读属性、
符号查找、目标私有状态和错误/输出能力，再**同时迁移消费者
并删除旧签名**。新接口如果只是重新取回 `MiniAs*`，
不算重构成功。

**P05 — Source/Fragment provenance。** SourceGraph 现负责
物理行、SourceSite、EOF 和稳定重放；Common/SourceContext
仍有跨层互访。继续清理要验证宏递归、条件拓扑、EOF literal
pool/local COMMON、Fragment 稳定身份以及单次物理读取。

**P02/P07、X1 — 目标 ISA/模式合法性。** 已有局部
RISC-V Zbb/V 站点支持，不代表完整版本化扩展语义；
严格 Base-I 字节审计、词法现场扩展、ELF 属性必须分开。
x86 模式白名单不能仅凭“重复检查”描述就删掉。

**P09/P14 — Reloc/名称表达式。** GNU symbol-zero
`.reloc` 已支持；完整语法及少数目标专用名字/dot 绑定未封口。
只有真实 GNU 差异才驱动扩展，不新增第二套表达式解析器。

**P03/P08/P12/O1/W1 — 延后但有边界。** 版本化 ISA、
未来 object format/endianness、格式中立符号绑定、GNU
section unique/groups，以及 3×7 workflow 收敛各需独立
输入/链接/语义对照。不能靠删除限制代码宣称完成。

**有证据应保留的实现：** full-source replay、
不同求值模式、四个 Target 私有 ISA 编码、
CFI 与 DebugLine 不同语义消费端、ELF 最终 Writer。

## 六、下一批怎样算真正“清理完成”

1. P04：逐架构核对读写能力，最终把四个宽权限编码器签名
   改成真正受限的上下文/输出接口，**旧调用链全部消失**；
   用 A0、RV32/RV64、AArch64 真链接/QEMU、
   x86/LoongArch GNU、原子提交测试验收。
2. P05：只在 SourceSite/EOF/条件重放输入可保持身份时删旧路径，
   不能另起一套 SourceGraph。
3. 统一删除证据：清掉无消费者的声明/字段/包装；
   文档记录精确 SHA、生产代码增删、测试变更和遗留问题。
4. 最终正确性要独立核对 3,536 严格 GNU 语义和 Linux
   原始输入的完整 GNU 链接/QEMU 验收，不能把强制 CI
   和这两个 opt-in 验收混成一个 PASS。

保留**单分支、单 Draft PR #667**。不为了提升 19/32
文件触达比例而修改已经稳定的 `invocation.c`、
`target_registry.c` 或 `mc_lexer.c`。
