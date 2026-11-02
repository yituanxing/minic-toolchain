# RISC-V lexical ISA 与 `$x` mapping：下一批闭环实施计划（2026-11-02）

> **实施更新（2026-11-02，本批提交）：** 下文第 1 节及第 3–4 节
> 描述的是 `37394c1d` **历史基线与当时计划**，不是最新代码事实。
> 本次已按该链路完成 supported lexical `arch,+zbb/+v`、`rvc/norvc`
> 的 Target 私有 effective ISA 投影：file `Tag_RISCV_arch` 不受
> local option 污染；SourceGraph forced replay 现校验 provenance；
> PostEmit 已移除单 Section 起始 `$x` 实现，改为源事件顺序的
> section mapping state machine，以最终 layout offset 和 Common
> exact-name sink 输出真实 GNU 重复名/转换。GNU 2.42 RV32/64
> `transition/rvc/alignment/profile/profile_g/profile_v` **12/12
> 严格通过**，并作为可用 GNU toolchain 时的 A0/CI gate。
> 原始 Linux 六份 `.s` 及任意版本化 `.option arch`、多架构真实
> link/QEMU 未在本次证明；后续验收不得凭该 12 组推定全域等价。

> 状态：**设计/审查记录，不是已实现功能或验收证明。**
> 基线：`37394c1d`，`refactor/minias-target-isa-abi-core`。
> 本记录不修改当前 cleanup map、历史账本或任何 ISA 行为。

## 1. 已确认的代码事实和未封口差异

1. `invocation.c` 经唯一 `target_registry.c` 找到 Target，并调用各自的
   `resolve_profile`。`MiniAsAssemblyConfig.target_profile` 是有长度校验的
   Target 私有值副本，不借用 CLI 的 `-march` / `-mabi` 字符串。
2. `riscv_profile.c` 通过一个 `RiscvExtensionSpec` 表识别当前允许的
   `-march` extension、构造 `arch_attribute_isa`；`riscv_target.c` 创建
   TargetContext 并序列化 `.riscv.attributes`。目前只接受有限的无版本
   extension 拼写，不能把这套逻辑称作完整的版本化 ISA 解析器。
3. `.option push/pop` 管理 `MiniAsRiscvState.options`；目前 `arch,+zbb`
   和 `arch,+v` 是受限的源现场增量。`+v` 的有效指令门槛包括
   `V/F/D/Zicsr`，源区域之外不生效。`common_lowering.c:add_stmt`
   在解析 MCInst 前调用 `capture_site_state`，将 mode bits 留在
   `MiniAsStmt.site`。Target 解析使用现场选项，晚期编码可以使用
   `stmt->site`，而不需要重新解释 `.option` 文本。
4. file ISA 的 ELF `Tag_RISCV_arch` 与有效 instruction-site ISA
   **不是一个属性**。现有 GNU 对照用例已经证明：在显式 `rv32i2p1`
   / `rv64i2p1` file ISA 下，`arch,+v/+zbb` 可以合法化区域内的
   指令，同时不扩大 file `Tag_RISCV_arch`。`strict_i_xlen` 仍是
   独立的显式遗留原始机器字审计，不能被 `-march` 或现场 ISA
   自动触发。
5. `riscv_record_object_mapping_symbols` 当前只针对具有内容的
   executable `SHT_PROGBITS` Section，在 offset 0 加一个
   `$x<file ISA>`；它不扫描现场扩展变化，亦不构造 code/data
   转换边界。已记录的六份真实 Linux `+v` 源里，五份在其它语义
   维度一致的情况下仍有 mapping-symbol 差异；第六份还有其它
   Section/内容/重定位/符号差异，不能全部归因于 mapping。
6. `source_graph.c:source_graph_forced_shape_matches` 在完整源重放时
   校验 SiteID、opcode、Target form、`site.mode_bits` 和 typed operands；
   当前不校验 `site.provenance_bits`，也没有版本化有效 ISA 身份。
   这是未来增加新的现场 ISA 身份时必须同步复核的重放契约，
   **不是当前已证实的 replay 故障**。

## 2. 语义边界：三种状态不能揉成一个

| 状态 | 所有者/当前载体 | 含义与禁止事项 |
| --- | --- | --- |
| immutable TargetSpec / file ISA / ABI | `MiniAsAssemblyConfig`、`MiniAsRiscvProfile` | `-march`、`-mabi`、ELF class/e_flags 和 file `Tag_RISCV_arch` 的基线；不得因局部 `.option arch` 改写。 |
| mutable source TargetContext | `MiniAsRiscvState.options`、option stack | 源文本顺序中的 push/pop、RVC、PIC、受限 extension、norelax；完整 SourceGraph replay 时重新初始化。 |
| immutable per-Fragment ISA view | `MiniAsStmt.site`、retained MCInst | 精确 lexical 现场的 encoding/legality/mapping 输入；布局重试时不能读取文件末尾的可变 `options` 推断历史现场。 |

`RISC-V -march` 的 extension 依赖与 `.option arch` 的 site delta
需要共享**同一 Target 私有特性/版本决策**，但最后仍有两种投影：
file ELF attribute 仅由 file ISA 决定，`$x` mapping 候选则由
真实 GNU 规则和有效指令现场决定。不能简单把 site 特性 OR 回
`arch_attribute_isa`，也不能把 mapping symbol 名称直接用作 ISA
合法性的唯一真相。

## 3. 下一批实现顺序：一批替换完整链路，不留新旧双路径

### A. 先固定 GNU mapping oracle，而不是猜符号

在可用 `riscv64-linux-gnu-as` 的环境，以 GNU 源对象为答案，固定
`-march=rv32i2p1/rv64i2p1`、匹配 ABI 和 `-mno-relax`。每个用例
比较 `readelf -sW` 中 `$x...` / `$d` 等 mapping 符号的**原名、
SectionID、offset、同 offset 顺序**；另比对 `.text` 真正字节、
`readelf -A` 的 Tag_RISCV_arch、ELF flags 和 relocation。
先把 GNU 实测结果存为测试期望，**本记录不假设符号拼写、出现
时机或个数**。

### B. 统一 RISC-V profile / lexical extension 语义

在 Target 私有层复用 extension 识别、依赖闭包和 canonical
version 信息；先用 GNU 固定每个允许拼写/版本及其依赖。
保留 `-march` immutable base、局部 `.option arch` delta、
`-mabi` 兼容性三种单独检查。检查当前 `-march` `V` 与局部
`arch,+v` 的依赖处理是否一致（后者已有 F/D/Zicsr 闭包）；
若发现不等价，只在有 GNU 正负证据后调整。

### C. 完整地捕获有效 instruction-site 身份

用一个 Target 私有的、与版本化 profile 兼容的稳定现场表示，
覆盖文件基线和 `.option push/pop` 的结果。普通 Common 只存储
opaque site value，不解释 RISC-V extension。考虑 `site.mode_bits`
目前只有 32 位且表达的主要是 `.option` delta，不能把无限版本
状态硬塞进它，也不能在每条 instruction 复制 512 字节字符串。
如果使用 Target 私有 intern ID，应定义 SourceGraph replay 间的
稳定比较语义，避免用一次 parse 的临时序号直接跨代比较。

### D. 在既有 PostEmit/Finalization 能力中生成 mapping 转换

继续用 `riscv_record_object_mapping_symbols` / `finalize_output`，
从 `MiniAsPostEmitView` 取得**最终** Section/Fragment offset、kind、
site snapshot；用有 GNU 证据的顺序状态机识别 mapping 边界。
通过现有 `minias_post_emit_define_mapping_symbol` sink 请求
Common 创建符号。不要让 Target 获得可写 `MiniAs`、Section、
Symbol 或 Fragment 指针；不要在 Common 中写 RISC-V 指令名判断。
保持现有只读 `validate_output` 与有权限 `finalize_output` 的分层。
单个 Section 起始 `$x` 逻辑须与新状态机整块替换，不能同时保留
两个 symbol 生产者导致重复/顺序错乱。

### E. 同步验证 SourceGraph shape 与所有 Target 现场消费者

如果 site 新增有效 ISA token/版本身份，更新 forced branch shape
校验，使它比较所有足以改变编码/合法性的现场状态，而不只保留
旧 `mode_bits`。在条件拓扑、宏展开、forward branch 强制增长后
重播同一个源码时，不允许旧分支 shape 绑定到另一个 ISA 现场。
重放只应该重新生成 TargetContext 与现场快照，不应缓存旧世代的
mutable TargetContext 指针。

### F. 一次性关闭验证闭环

Release `-Werror` / 结构负向编译 / A0；现有
`probe-rv-target-profile-single-source.sh`、
`probe-rv-option-rvc-state.sh`、`probe-rv32e-typed-registers.sh`、
`probe-rv-zbb-site-gnu.sh`、`probe-rv-v-site-gnu.sh`；新增严格
mapping-symtab GNU oracle；以及上面的六份真实 Linux 产生的
`.s` 全维度严格比较。最后对同一确切 SHA 执行完整四架构
GNU link/QEMU 验收。已知差异不得简单从 comparator 排除。

## 4. GNU 样本矩阵（先记录事实，再写通过断言）

| 维度 | 最小源例 | 预期检验重点 |
| --- | --- | --- |
| RV32I2p1、RV64I2p1 | 无 `.option` 的 `addi` | 基线 file 属性、Section 起点/映射基线。 |
| `arch,+zbb` | `push; arch,+zbb; ctz; pop; addi` | Zbb 仅现场合法；`Tag_RISCV_arch` 不升级；符号转换由 GNU 决定。 |
| `arch,+v` | `push; arch,+v; vsetvl; fadd.s; fadd.d; csrr; pop` | V/F/D/Zicsr 的现场依赖闭包与 ELF arch 分离；CSR 的 privileged tag 单独比较。 |
| nested regions | `push +v; push +zbb; ctz; pop; vsetvl; pop; addi` | 层级栈恢复、同一 Section 多现场切换、真实 mapping 顺序。 |
| rejected outside site | `pop` 后再次 `vsetvl` / `ctz` | 当前显式 Base-I 下必须拒绝 typed ISA escape。 |
| data/code/align | `.byte`/`.word`、`.insn`、`.p2align` 与相邻指令 | GNU 何处使用 `$d`、`$x`、是否有零宽 transition；防止从 mnemonic 推测 FragmentKind。 |
| sub/section changes | `.subsection`、返回前一 subsection、切换 `.text.foo` | 最终 offset 顺序，不可按源码原始行顺序输出对象映射。 |
| empty regions | `push +v; pop`，无任何 instruction | 栈操作本身是否产生 mapping，不作假设。 |
| replay | 宏/条件 `.if` 改变分支长度且跨局部 ISA 模式 | SiteID + effective ISA identity，反向验证无错绑。 |
| Linux original `.s` | `ptrace/signal/traps/vector/vcpu_vector/core` 六份 | 严格符号维度与其他内容、section、reloc 维度独立核对。 |

现有 GNU site 探针主要比对**指令字节与 `.riscv.attributes`**，
尚未以 mapping-symbol 名称/位置/顺序作为严格门槛。没有
`riscv64-linux-gnu-as` 的环境可以运行 MiniAS-only 正负用例，
但应报告 `GNU_MAPPING=SKIP`，不能称对象语义一致。
**已知不一致的新 mapping gate 不能直接加进 A0 强制通过组**；
先保留独立诊断，待 D 步完整实现后升级为 mandatory。

## 5. 四架构职责与未来格式边界

- **RISC-V**：在 Target 端闭合 effective ISA/site delta、版本依赖
  与 `$x`/data mapping 转换；保留 file attribute 与 optional
  Base-I audit 各自的源头。
- **AArch64**：`capture_site_state` 已记录 feature mode 和
  `.inst`/alignment `provenance_bits`，PostEmit finalization 处理
  `$x/$d`。这是可复用的阶段边界案例，但 RISC-V 不能照抄
  AArch64 的具体 mapping 规则。未来如果编码依赖 arch revision，
  其现场 snapshot 也要覆盖 revision，而非只捕获 feature bits。
- **x86-64/i386**：`.code16/.code32/.code16gcc` 是 immutable
  per-site encoding mode；ELF32/i386 或 ELF64/AMD64 是 object ABI，
  不得让局部 `.code32` 重新选择 object format。
- **LoongArch64**：当前 Target profile 没有已支持的局部 ISA
  extension 切换，不为对称性虚构 option stack 或 mapping symbols。

P08/P12 暂不在本批：`MiniAsTargetOps`、Common Section/Symbol
和 `elf_writer.c` 仍携带真实 ELF machine/class、SHF/SHT、STB/STT、
重定位格式、GROUP、symbol liveness 等语义。待有实际 COFF/Mach-O
输入、ObjectWriter 和跨格式 symbol binding 验收后，才据消费者
拆分共通 ObjectModel 与 ELF 私有序列化；现在只守住 Target/MC
阶段权限，不创建空 ObjectFormat/COFF/Mach-O 框架。

## 6. 本次只读核对与剩余里程碑

现有预构建 `build/release/bin/minic-as` 的局部探针：

- target profile attribute byte-exact：**8 PASS**；
- `.option` RVC state：**PASS**，GNU binary unavailable；
- RV32E typed registers：**PASS**，GNU byte oracle skip；
- `arch,+v` GNU site 探针：**SKIP**，缺
  `riscv64-linux-gnu-as`。

这不是当前多人同时修改的源码重编译验收，更不代表 mapping 语义
已完成。P02/P07 预计还需要 **2–3 个带真实 GNU 对照的完整工程
批次**：先稳定 oracle，再闭合 state/encoding/mapping，再复核
SourceGraph replay 和 Linux 严格对象。这个估算不包含依赖真实第二
ObjectFormat 消费者的 P08/P12，也不能代替 full Linux/QEMU 结果。
