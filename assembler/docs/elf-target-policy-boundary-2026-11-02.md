# MiniAS ELF ABI 策略单一所有权（2026-11-02）

## 完整消费者链与迁移决定

这次追踪四 Target 的 ISA/ABI profile → 源 `.section`/`.reloc` →
MCExpr、MCLayout、CFI 的 Fixup 能力检查 → PostEmit → Driver 终结
ObjectModel 借用 → ELF 符号生存性与 STT_SECTION 折叠 → `minielf` 序列化。

旧结构将同一 ELF ABI 的 11 项数字、回调和策略布尔值散放在通用
`MiniAsTargetOps`；Driver 再逐项复制到一个与之同构的
`MiniAsElfTargetPolicy`。这种重复既允许新增字段只更新某一边，也让
不消费 ELF 的一般 Target 接口显式携带 `e_machine` 与 ELF 绑定号。

现有输出格式只有 ELF，GNU `.section/.symver/.reloc` 词法仍带有
ELF 扩展。并行完成的 ObjectModel 语义批次把保留的 Section/Symbol
类型、标记、绑定与可见性改成语义枚举，由 `elf_writer.c` 唯一映射
SHT/SHF/STB/STT/STV；**这使模型语义与 ELF 数字解耦，却仍未有
COFF/Mach-O Writer**，也未实现独立地址宽度或大小端策略。

本批做一次不可兼容的跨模块迁移，不留旧字段或透明别名：

1. `minias_elf_target_policy.h` 唯一定义
   `MiniAsElfTargetPolicy`，通用 `minias_target_api.h` 只公开
   `const MiniAsElfTargetPolicy *elf_policy` 不完整类型指针；
   ELF 磁盘数字只在终结 Writer 使用，旧 `MINIAS_SHT/SHF/STB`
   宏已由独立的对象语义迁移彻底删除。
2. RISC-V、AArch64、x86（含 i386 ABI）、LoongArch 各自只保留
   一份 `static const` ELF 策略，包含机器号、ABI-aware Fixup
   支持和编码、GNU 命名 relocation、target attributes、局部符号
   `STT_SECTION` 折叠、合成表达式/CFI 锚点可见性等。
3. Driver 在 Target 初始化时检查必需策略和两个 Fixup 回调，
   终结时直接借用同一指针；不再重建回调表。ELF Writer 自己
   也对不完整策略 fail-closed。ELF 只读 `ObjectModel`、已解析
   Section/Signature/Fixup plan 和 Target ABI 值，没有反向
   `MiniAs *`、可写符号/节或第二个对象图。
4. `.reloc` 的命名解析及 MCExpr、MCLayout、CFI 的 Fixup 许可
   改从该唯一策略取值，不准通用路径自行回退为另一套“支持”。
   `ELF32 i386` 与 `ELF64 AMD64` 仍由同一回调对不可变 ABI
   值做分流，GNU 差分行为不变。
5. 增补 14 个负向/两个正向 C11 编译探针，检验通用 Target
   无 ELF 常量/旧字段、静态 ELF 策略不可写、Writer 没有
   Session 权限；结构门禁要求四个 Target 使用单一静态策略。

## 验收与余项

- `make MODE=release CFLAGS=-Werror ...`：PASS。
- A0 30/30，包括 GNU RISC-V、RV32/64 mapping/ISA、i386/AMD64
  Fixup/REL(A)、SectionKey/COMDAT/LINK_ORDER、CFI/DWARF：PASS。
- 四 Target 独立旧新差分：26 例，18 个 ELF 对象 byte-exact，
  8 个失败诊断语义一致：PASS。
- `.github/workflows/minias-target-contract.yml` 的 Target 探针与
  更新后的 `probe-elf-object-view-boundary`：PASS。
- 以上是本地结论；**新提交的 exact-head 远端四流水线**需按最终
  SHA 验证，不沿用上一轮工作流状态。

仍未完成：GNU/Linux 完整原始 `.s` → `ld` → QEMU 多架构链路、
RISC-V GNU relaxation (`R_RISCV_RELAX`/`ALIGN`) 的完整语义、
独立 COFF/Mach-O/大小端对象模型，以及更广 ISA 指令覆盖。
这轮的隔离只是使 ELF 策略拥有清晰的唯一 owner。
