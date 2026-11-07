# MiniC 全源码审查第二轮：架构边界与修复集成（2026-10-29）

本报告承接 `review-2026-10-29-integration.md` 的 327 文件全量审计台账，定位于 **编译器** 的 Frontend → Core → Target/ABI → Driver 边界。第一轮逐文件通读的证明按五个原分区文档保存；本轮是跨分区复查、落实修复、独立交叉审计。不把单个目标的源级 `-S` 通过误称为 ELF 链接或 QEMU 运行通过。

## 第二轮已审计、落实的分区

| 分区 | 结构性变更与正确性修复 | 关联审查记录 |
| --- | --- | --- |
| Target 注册/Driver | 用单一 X-macro 注册表驱动 TargetCatalog、TargetInfo、Backend/CLI 目标目录；架构专属 ABI 实现在各目标中，不注入 Frontend/Core | `review-2026-10-29-target-registry-followup.md` |
| Common/x86-64 全局对象 | GlobalEmissionPlan 负责 payload/relocation/section；x86 仅保留机器汇编发射；纠正 `.bss` NOBITS、非零 NOBITS 初始化和 GNU ASM 大写 `I` 指令约束错误接受 | `review-2026-10-29-global-emission-plan-abi.md` |
| Frontend 声明/位域 | 合并三种重复的 static array 初始化路径，修复 tentative、显式定长/推断定长合并；解析 packed bit-field 成员 GNU attribute；DataLayout 统一成员对齐、字节序能力 | `review-2026-10-29-frontend-followup.md`，本报告集成回归 |
| Core lowering/CFG | 修复赋值左值和 GNU statement expression 的 effect 摘要；删除 IF branch 局部事实第二份 meet；packed bit-field 四条入口统一精确访存窗口，保护页复现并修复 | `review-2026-10-29-core-target-root.md` |
| AArch64 | 进一步将函数序言、调用出参、动态栈帧、间接返回对象归入 typed FrameRef 的 `OWNED/OUTGOING` 寻址；以真实保存/恢复槽验证 SRET ABI，替换过时的指令字符串假设 | `review-2026-10-29-aarch64.md` |
| RV64/LoongArch64 | 修复 LP64D 第九及后续定参浮点 GPR/栈回退，禁止尚未支持的 LP64D FP record 静默错误传指针；LoongArch packed scalar、指针与 128 位宽访存逐字节 fallback，静态可证对齐保留宽快路；修正 Loong 间接 SRET 陈旧负测 | `review-2026-10-29-riscv-loong-abi-followup.md` |
| GNU inline ASM | 独立复核 Source 约束 → Core operand → Target emission 链路；新增 GNU `=&r/+&r` 属性经 Backend → Core 的独立保真字段，不强行覆盖旧 native allocator 标志；机器发射器干涉证明仍待实施 | `review-2026-10-29-asm-operand-plan-followup.md`、`review-2026-10-29-gnu-asm-earlyclobber-preservation.md` |

## 真实缺陷证据与边界

- **packed bit-field 越界读写 P0**：旧版 x86 编译器在 `mmap` 保护页末尾 5 字节 packed record 原生测试中 SIGSEGV（退出 139）；GCC 退出 0。修改后 MiniC 退出 0；读/赋值/复合赋值/自增路径共用 DataLayout 访问窗口，必要时逐字节 LOAD/STORE，保留安全宽路径。五 ISA 汇编 `-S` 已单独检查。
- **跨存储单元 packed 位域 P0**：GCC 合法 `unsigned first:1; unsigned value:32;` 的第二字段跨 5 字节，原 MiniC fail-closed 无法生成 `-S`。现在将 Core transport 暂时提升为 64 位，仍按原 32 位 C 语义截断/符号扩展，四入口共用；GNU/MiniC `mmap(PROT_NONE)` guard page native 差分及五 ISA `-S` 通过。`unsigned long long:64` 偏移 1 位需要 65 位/9 字节仍暂不支持，明确诊断拒绝，不可宣称位域全面支持。
- **GNU 属性位域布局 P0**：成员类型尾随 `__attribute__((packed))`、显式对齐以及匿名位域在原布局流程中被忽略，GCC `sizeof`/`alignof` 与 MiniC 不同；修复并通过与 GCC 的原生差分和五目标编译。
- **x86 GNU ASM `I` 约束 P0**：源级不受 x86 目标支持的 GNU 大写立即数约束曾绕过 qualifier 检查；改为拒绝，仍保留合法小写 `i` 和已有 Linux GNU ASM 接受范围。
- **x86 全局数据 ABI P1**：`.bss` 不应写成 PROGBITS；公共 payload 和 section 事实与 x86 对齐，使用 9 个对象大小、8 个重定位与 GCC 工具链对照。
- **RV64 LP64D ABI P0**：八个浮点形参寄存器耗尽后固定浮点参数必须降级为整数形参寄存器或栈，旧路径错误拒绝；新路径覆盖直接、间接及混合参数。对于浮点成员结构体的 psABI 展平规则，旧路径会错误传递指针；本轮采取有诊断的 fail-closed，完整硬浮点聚合支持仍为 OPEN。
- **LoongArch packed 访存 P0**：不仅位域，普通 packed 32/64 位标量、指针及 16 字节 `__int128` 也会在 offset 1 处生成非对齐 `ld.w/ld.d/st.d`；现在未知对齐时逐条动态检查并按字节 gather/scatter，已证明对齐的静态对象免检查。非本机 QEMU 验证尚待 CI。
- **GNU early-clobber 语义边界 P1**：先前 `=&r/+&r` 经目标 qualifier 后未留 GNU 独立属性，本批将原始 `&` 保存在 Backend/Core 合同中，5 ISA `-S`、GNU/MiniC x86 futex 原生混合、AArch64/LoongArch 专项验证保真；仍未证明所有 native register allocator 的冲突回避，故不宣称 ABI 彻底解决。
- **验证分离**：本地原生 GNU/MiniC 差分、`-Werror`、五目标 `-S` 和 `check-fast` 是局部证据；Linux 源覆盖率需要新提交的 exact-head 21/21 远端报告；GNU AS、ELF 链接、QEMU P0/P1 是另一层验收。

## 尚未封闭的设计与正确性风险

1. **GNU inline ASM 的目标 operand 计划与 early-clobber**：源约束 `&` 已保真，仍要从 Core operand → native allocator 的完整寄存器角色干涉、指令形态、memory clobber 证明；现有部分运行时代码必须保持兼容，不能删仍有调用者的 text legalizer。
2. **非本机目标的宽结构体/非对齐 ABI 和 LP64D FP record**：应在 GCC cross/QEMU 可用时实测，不得让 `-S` 冒充 runtime 完整通过。RV64 硬浮点 aggregate 目前为 fail-closed（不是实现完成）。
3. **长双精度常量**：Frontend 使用 `strtod` 解码 `long double` 字面值时可能损失超过 binary64 的尾数精度，已有 GCC/MiniC 运行差分；需准确类型常量管线后修补。
4. **AArch64 asm_legalize 文本后处理**：仍有活跃调用者；下一批需基于 typed FrameRef 全部覆盖再退役。
5. **Core Provenance/AlignmentProof**：当前 LoongArch 对任意指针的运行时对齐分流属保守方案；后续应由 Core 输出可信地址对齐事实以减少热路径检测。
6. **未来大端支持**：Core 的 bytewise packed bit-field 已显式依赖 `DataLayout.byte_order` fail-closed，但 Common 全局位域初始化仍按 little-endian bit 顺序排布。现在五 ISA 全部小端，不影响当前生成；新增大端目标之前必须统一全局排布的字节序 gate。
7. **跨 65 位的 packed 位域**：现有单标量 Core bit-field transport 上限 64 位；`uint64` 位域起点 bit 1 的 9 字节对象需要拆成 high/low 两份 Core SSA，再依 64 位 C value width 做符号、掩码和 RMW；目前保留 fail-closed 及专门负例。

## 第二轮代码冻结后的本地集成验证

统一使用 `BUILD_DIR=build/review-integration CFLAGS=-Werror`，以下均在全部六组修改冻结并合并到同一工作树后执行：

1. `make -j8 ... all`：**PASS**。
2. `make -j8 ... check-fast`：**PASS**。
3. `bash tests/compiler/c0/check-minic-target-boundaries.sh`：**PASS**。
4. `bash tests/compiler/c0/probe-review-batch-2026-10-29.sh /absolute/path/to/minic-cc`：**PASS**。包括 GCC/MiniC packed guard-page、32 位位域跨 5 字节原生差分、RV64 LP64D 参数与 FP record fail-closed、LoongArch 16 字节非对齐汇编路径、x86 GlobalEmissionPlan 9 sizes/8 reloc、early-clobber 源约束保真与 AArch64 typed frame。
5. `git diff --check`：**PASS**。
6. `rg` 复查：`src/frontend`、`src/core`、`src/compiler` 和 `tools/minic-cc/main.c` 中无 `MINIC_TARGET_ARCH_*` 分支、五 ISA 字符串硬编码；单一登记表仍须跨阶段使用。

**远端验收界限**：前一已推送版本的 Linux 6.6.143 三 ISA × 七路 21/21、9559 原始 GNU C translation owner `.i → MiniC -S` 已成功；它不能证明本次改动。新提交通过既有 `tools/ci/linux-three-isa-trigger.txt` 触发**新的 exact-head** 21/21 检查，并单独观察 native strict、四架构 runtime workflow。GNU AS、完整 Image 链接和 QEMU 启动不在当前 `-S` 百分比结论之内。
