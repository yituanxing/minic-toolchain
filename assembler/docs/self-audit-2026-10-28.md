# MiniAS 全局源码审查：独立于 GNU 对象差分的第一阶段

审查日期：2026-10-28。范围为 PR #667 的 Common、RV32/RV64 已验证路径、x86-64、AArch64、LoongArch64 与通用 ELF writer。本文件记录**源码可直接证实的设计缺口**，不能把文件“成功汇编”视为机器码与链接语义正确。

## 约束和基准

- 已验证的基准：原始 Linux 6.6.143、GNU Kbuild 3×7 的 9,645 个源对象；[固定语料 #40668316151](https://github.com/yituanxing/minic-toolchain/actions/runs/40668316151) 证明在 **85372cbf** 上 `ASSEMBLED_UNVERIFIED 9645/9645`。后续变更须重新跑当前 HEAD 的完整 21 片。
- RV32/RV64 的历史能力作为正向参考：解析后的 typed MCInst → target measure/encode/relax/fixup，再经 Common subsection/符号/ELF 写入，不在 Common 按 e_machine 判断 ISA。
- 先本地源码检查＋小型独立验收，再冻结 Kbuild 全量重放；最后才进行 GNU 汇编产物的 **字节、重定位、符号、链接、QEMU** 差分。不要用通过 9,645 个生成门槛代替后者。
- 禁止为了“100%”跳过未知指令、把错误 ELF metadata 换成 0、或者修改 GNU 原始输入；保持本 PR Draft。

## 阅读范围与架构判断

| 模块 | 源码 | 当前判断 |
|---|---|---|
| Common Lexer/符号/片段/复核/ELF | `assembler/src/assembler.c`, `minias_internal.h`, `elf_writer.c`, `elf/src/relocatable_writer.c` | Section/Fragment 与 target 解耦成立；但源代码中仍有带历史保底假设的 ELF 元数据缺口 |
| RV 基线 | `riscv_target.c`, `riscv_encode.c`, `riscv_profile.c` | ISA/ABI、机器码和调整由 target 拥有；历史 RV64 Linux 验收不能自动证明当前 HEAD 未回归 |
| x86-64 | `x86_64_target.c` | 可变指令宽度、代码模式、GNU CFI 特性由 target 决定，非通用 ELF 默认值 |
| AArch64 | `aarch64_target.c` | 固定宽度编码、direct LDR19、目标专属寄存器别名与 literal pool 路径 |
| LoongArch64 | `loongarch64_target.c` | 固定 4-byte 指令，目标单独负责 B16/B21/B26/PCALA 及 psABI |
| 元数据 | `cfi.c`, `debug_line.c` | 这两项正确性**不能**从正常汇编成功推断，需要独立验收 |

## 第一轮发现并已修复（小型验收保留独立证据）

### 2026-10-28 MCLayout 位置约束收敛（待 exact-head CI）

- 纯整数表达式 `.org N` 在 parse 时记录绝对 subsection 目标，不再把原始 padding 当成永久的 Fragment 长度；长分支和前向变长 DATA 使前序大小变化时，Common MCLayout 重新计算 padding，超出目标则明确报错，不覆盖已发出数据。
- `.org .+N`、含符号的 `.org`、依赖符号/位置的 `.if` 与 `.set/.equ` 仍保留保守重解析路径。纯词法 `.ifb/.ifc/.ifdef`、纯数字 `.if/.ifeq`、常量 `.set/.equ` 以及宏清理状态不再无条件触发第二次源码解析。
- 本地 RISC-V A0、x86 GNU byte 差分、MCLayout Stress、MCExpr ownership/byte/relocation oracle 已通过；新增 x86/RISC-V 长分支 `.org` 正反测试与四架构 GNU forward-DATA `.org` 差分，其他架构与 Linux 9645 份必须以最新提交的 CI 结果为准。
- 这是对现有 `MiniAsStmt` 的受限语义增强，不表示已经消除所有位置相关源码重解析，也不替代完整 ELF/CFI 与 Linux Image/P1 验收。

1. **目标代码对齐泄漏（P0）**：Common 创建 `.text` 时沿用 RV 的 4-byte 对齐，x86 GNU 默认 1-byte；改为 `TargetInfo.default_exec_alignment`，包括入口、显式 `.text`、`.section .text.*`。
2. **Section 类型身份（P0）**：通用 `ensure_section` 重复声明时曾直接 OR flags、改 PROGBITS → NOBITS；容易吞掉已经发出的代码。恢复第一次定义的 Type/Flags。更严格的“GNU 对自定义节重复改变属性发错”语义留在后续条目。
3. **NOBITS 静默丢数据（P0）**：旧 `minias_section_append` 遇 NOBITS 无条件增加 size，非零机器码也被吞掉；现在对非零字节 fail-closed，并把合法零填充走 O(1) checked-size 路径。
4. **GNU mergeable sections（P0）**：`aMS/aM` 丢失 `SHF_MERGE`, `SHF_STRINGS`, `sh_entsize`；现在通用 Section + ELF writer 保留标志和 entry size。
5. **GNU 命名节（P0）**：`.bssification` 错判 NOBITS、`.note.*` 错判 PROGBITS/ALLOC、`.note.GNU-stack` 的特别规则、以及被引号包裹的节名内逗号被误当分隔符；全在 Common 处理，而非三份 target 各补一次。
6. **TLS 与扩展 ELF 符号（P0）**：`.tdata/.tbss` 的 `SHF_TLS`、`@tls_object` 的 STT_TLS、`@gnu_indirect_function` 的 STT_GNU_IFUNC、`@gnu_unique_object` 的 GNU_UNIQUE 绑定、以及 GNU `@common` 实际的 OBJECT+GLOBAL 语义；OSABI 应从最终 symbols 决定，而不默认一律 `ELFOSABI_NONE`。已将上述语义放回 Common ELF 数据和共享 writer。
7. **CFI target 边界（P1）**：Common 原先用 `elf_machine == 62` 判断“x86 自动 .eh_frame”。改为 target 明示能力开关；尚不等于其他架构的 CFI 已完整实现。
8. **GNU 构造器节（P0）**：`SHT_INIT_ARRAY/FINI_ARRAY/PREINIT_ARRAY` 曾错误落到 PROGBITS 且 `sh_entsize=0`；现在公共 ELF metadata 依据目标指针宽度写入 4/8，保留生成的 relocation。
9. **GNU 本地符号数据重定位（P0）**：x86 绝对引用其他节的 local symbol，GNU 在 `.rela.init_array` 中选择 `STT_SECTION + label offset`，旧实现保留符号名。新实现是 target 显式 opt-in、Common ELF 统一 rewrite；RV 基线暂不改变。

此阶段新增正反回归放在 `tests/assembler/probe-x86-loong-base.sh`；成功以对应提交的 exact-head CI 为准，不能用上一个绿色的运行给当前 HEAD 背书。

## 后续源码自查缺口（先按根因分组，不为了过门槛假实现）

| 优先级 | 可直接从源码验证的缺口 | 根因归属 | 后续验收 |
|---|---|---|---|
| P0 | `assembler.c` 对非 x86 `.cfi_startproc/.cfi_endproc` 的保底路径只改变状态，**不生成** unwind CIE/FDE；源文件组装成功不表示回溯正确 | Common CFI/Target psABI | RV/A64/LA 各自完整真实 CFI 样本，检查 `.eh_frame/.debug_frame`、CIE/FDE/RELA 与链接后的展开；在补齐前保留明确的“不足”状态 |
| P0 | `parse_section_directive` 目前不完整支持 `SHF_GROUP`／`COMDAT`，`elf_writer` 未构造完整 SHT_GROUP 成员关联；这影响链接去重/弱符号场景 | Common Section/ELF，不是三个 target 自己分别修 | 同一 COMDAT 两个目标文件链接只保留一份、GNU SHT_GROUP 结构和成员差分 |
| P0 | 其他显式节类型（例如 `@gnu_attributes` 或非标准节）当前仍有落到 PROGBITS 的风险；标准 GNU init/fini/preinit arrays 已补实现 | Common Section→ELF | 后续先识别完整类型表及 fail-closed 不支持类型，验证 GNU flags/sh_type |
| P1 | unnumbered `.file "xxx"` 在 `debug_line.c` 的 `parse_file` 中直接返回 true；没有完整 `STT_FILE`/CU 元数据语义 | Common Debug | GNU `.file/.loc` 的符号表/DWARF line-table；异于普通可执行字节测试 |
| P1 | 用户指定自定义节重复改变 flags/type 时，GNU 在某些非保留名称会报错，而当前 `minias_ensure_section` 统一保留首次属性并继续。与“忽略变化”不是普遍等价 | Common Section 校验 | 按保留节家族与自定义节区分 strict 错误；invalid test＋正常 Linux 21 片回归 |
| P1 | `debug_line.c` 的 `compare_loc` 用全局静态 `comparator_as` 支持 qsort，作为可复用库的并行调用仍有重入风险 | Debug writer | 并发实例隔离/线程检查；保持序列化结果不变化 |
| P1 | `.size` 目前支持绝对值、已定义绝对符号与特殊 `.-symbol`，不覆盖普通 GNU 符号表达式完整范畴 | Common Symbol/Fragment | 泛化 typed expression、fragment-anchor 稳定性，严禁回退到源字符串启发式猜值 |
| P2 | `.incbin` 的先测大小、后读内容目前仅比较大小；相同长度的内容变更不能察觉（固定 Kbuild 输入哈希解决的是 corpus provenance，不是运行时竞争） | Input/Fragment | 单次读取内容快照或强哈希比对；错误时 fail-closed |

**不应立即做的事情：** 盲目把 GNU 9,645 个对象二进制完全比较后的所有差异一股脑当“指令不支持”。先厘清语义源头：Section/符号/reloc/CFI/Debug/ISA，再在同一 CI 配置下进行正式 GNU 比对。对应更早 RV64 的 3536 历史验证应单独 opt-in 重跑；当前不能宣称已经在本次 exact HEAD 通过。
