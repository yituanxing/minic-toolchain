# MiniAS 对象格式语义边界（2026-11-02）

## 本批读通的数据流

`SourceGraph` 的 GNU 汇编文本进入 Common Lowering，由 `.section`、
`.type`、`.globl/.weak/.hidden` 等指令建立通用 SectionID/SymbolID；
Context 独占记录，Emission 只拥有字节，MCLayout 确定 Fragment 位置；
终结期 ObjectModel 仅解析逻辑引用/偏移，最后由 ELF Writer 投影到
`minielf` 的磁盘记录。

上一批虽然拆出了终结期 ObjectModel，Common 所保留的 Section
`type/flags/group_flags`、Symbol `bind/type/visibility` 仍直接存的是
ELF `SHT_`、`SHF_`、`GRP_`、`STB_`、`STT_`、`STV_` 编码。
这使未来格式 Writer 即使没有任何 ELF 依赖，仍会从 Common 接到
已按 ELF 编好的字段。本批在**同一份原有持久记录上**完成迁移，
没有引入另一张状态图。

## 新的唯一语义表示

| Context 数据 | 唯一语义表示 | ELF 输出位置 |
| --- | --- | --- |
| Section 类型 | `MiniAsSectionKind`：PROGBITS/NOBITS/NOTE/三类 array | `elf_writer.c` 显式映射到 `SHT_*` |
| Section 属性 | `MiniAsSectionFlags`：九个非 ELF 编码 bit | `elf_writer.c` 显式映射到 `SHF_*` |
| COMDAT 选择 | `bool group_comdat` 加原有签名身份 | `elf_writer.c` 映射 `GRP_COMDAT` |
| Symbol 绑定 | `MiniAsSymbolBinding`：LOCAL/GLOBAL/WEAK/GNU_UNIQUE | `elf_writer.c` 映射 `STB_*` |
| Symbol 类型 | `MiniAsSymbolKind`：NOTYPE/OBJECT/FUNC/TLS/GNU_IFUNC | `elf_writer.c` 映射 `STT_*` |
| Symbol 可见性 | `MiniAsSymbolVisibility`：四种取值 | `elf_writer.c` 映射 `STV_*` |

通用 enum/bitset 数字**故意不等于**相应的 ELF 磁盘编码，编译期
探针证明不能碰巧直接透传。未知类型、属性位或 Symbol enum 必须
在 ELF 投影时拒绝。ELF 专属的 `STT_FILE` 仍由 Writer 从 CU 文件
记录构造，不作为 Common 持久 SymbolKind。

SectionKey 的组签名、link-order 源引用和 `unique` ID 仍留在 Context，
因为它们是源码指明的逻辑对象关系；ObjectModel 继续只解析到
SymbolID/SectionID。终结期 `find_symbol` 指针借用改为 Context
检查的 `find_symbol_id`，不再对借来的 Symbol 指针做减法反算 ID。

## 验收与剩余边界

本批独立格式门禁直接调用生产 `elf_writer.c`、`object_model.c` 和
`minielf`：覆盖六种 Section 类型、九种 Section 标志、四种非平凡
Symbol 编码、未知 enum/flag 拒绝；同一 GNU `.s` 对比类型、标志、
大小、合并项、节数据、COMDAT 和 LINK_ORDER 的 ELF 输出。
既有 A0、四架构源输入旧新差分、GNU COMDAT/1024 LINK_ORDER、
CFI/DWARF、x86/Loong Oracle 和原子落盘仍属于必要回归。

这次**没有**引入没有消费者的 COFF/Mach-O Writer 或另一个
TargetSpec，也没有声称旧 GNU 汇编方言已格式中立：`.section`
参数和 `.type` 的 GNU 拼写仍由 Source 解析为上述语义值。
`MiniAsObjectSpec` 的 `elf32`、机器号、REL/RELA、以及通用 CFI/DWARF
根据 ELF class 推导地址宽度仍是下一组待拆开的格式/数据布局接口。
完整 Linux 原始 `.s`→GNU ld→QEMU P1 必须在最终 commit SHA 上
独立验证，不能以本地 GNU 对照或 CI 工作流代替。
