# MiniAS Target DataLayout / ObjectFormat 终结边界（2026-11-02）

本批与已发布 `352537a1e60db32ec9f830f2fc672e4fd74b5ac5` 的
ELF policy 独立化整合在同一个 PR #667 后续提交中。不是新增
COFF/Mach-O Writer，也没有第二套 Target Profile 或 ObjectModel。

## 单一数据流与归属

| 生命周期 / 消费者 | 唯一输入及所有者 | 禁止的旧路径 |
| --- | --- | --- |
| CLI 解析目标 ISA/ABI | 每个 Target 的 `resolve_profile` 一次生成 `MiniAsTargetSpec`，`layout.address_size` 是唯一 4/8 字节宽度 | 从 `object.elf32` 反求 CPU 地址宽度 |
| 源文件动态 ISA 和编码 | Target 私有状态、Fragment 现场 SiteState；Parse/Encode/Align 查询只读 `address_size` | 直接查询 ELF class 作为源代码当前指令模式 |
| Common CFI/DWARF/数组节 | `target_spec->layout.address_size`；Target 的 CFI ABI 须匹配其宽度 | CFI、DWARF 和 `.init_array` 分别维护 ELF32 布尔副本 |
| 目标对象格式 | `object.format` 与 `object.elf.{initial_flags,machine,rel_relocations}`；Target 固定 ELF 回调由唯一 `elf_policy` 持有 | 将 ABI 序列化策略铺散在通用 TargetOps 或多处 session 缓存 |
| 终结 ObjectModel | 原 Context Section/Symbol 与 Emission bytes 的 const 借用；实体只在 `minias_object_records.h` 声明 | Writer 为使用实体引入 `minias_internal.h`、重建影子 Section/Symbol 图 |
| 单一 ELF Writer | Driver 验证格式和地址宽度后传入只读 ObjectModel/Target policy；Writer 由 address_size 映射 ELFCLASS32/64 | 同步维护 `minias_write_elf32` 和 `minias_write_elf64` 分叉入口 |

`MiniAsTargetABIView` 仍会在每次必要 Target 调用时生成临时只读
值快照，因此源内 `.option` 的可变 ISA 语义不会被冻结到 file ABI。
RISC-V Profile 既有 `elf32` 私有解析位是输入解码缓存，并非 Common
或 Writer 的第二个地址宽度所有者。

## 故障和兼容策略

- 只有 ELF Writer 已实现：未知 `object.format` 明确拒绝，绝不静默
  生成 ELF，也不把通用 Section/Symbol 枚举当作 ELF 磁盘数值。
- 非 4/8 字节宽度在 Driver 和 Writer 两端 fail-closed；Driver 负责
  终结前的参数防护，Writer 负责其独立只读 API 的可信边界。
- x86 `-march=i386` 的 ELF32/REL、RISC-V RV32 ELF32/RELA
  与 64 位三目标的 ELF64/RELA 约束保持；`.code16/.code32`、
  `.option arch/push/pop` 不能更改固定文件 ABI。
- 固定 Target ELF 策略回调仍指向同一 `MiniAsElfTargetPolicy`，
  生成对象的 `e_flags` 则由 Emission 在配置初值上增量更新。

## 本机已实跑的确定性证据

- Release `-Werror` 编译、A0 30/30 与旧新四目标 22 fixture 差分。
- 六配置（x86_64、i386、RV32、RV64、AArch64、LoongArch64）
  GNU as：ELF class/machine、`.init_array` 类型/flags/entsize/size
  与目标一致；CFI/DWARF 有效产生。
- 五配置 GNU `.debug_frame`：CIE/FDE、CFA 规则及真实 relocation；
  i386 GNU REL CFI、RV32 GNU COMDAT 和 `ld -r`。
- 新结构门禁：非法 address_size 0/3/16 逐项拒绝，格式未知拒绝；
  8 个 TargetSpec 负向编译和 Section/Symbol 11 节 GNU 投影通过；
  已接入 `.github/workflows/minias-target-contract.yml`。

上述证据只支持边界、单编译单链接 GNU 对照，不支持将**未运行的**
Linux original Kbuild→GNU ld→system QEMU P0/P1 标为通过。对应的
四架构、3×7、3,536 与真实 zlib/Lua 核验必须读
[exact-head 验收矩阵](minias-exact-head-full-acceptance-2026-11-02.md)
中同一提交 SHA 的工作流和 guest 日志。
