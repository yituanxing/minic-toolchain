# MiniC 架构收敛批次账本（C38 之后）

> 与[纯清理账本](minic-cleanup-ledger.md)分开记录。C1～C38 的 `342/342`
> 是 `src/` C/H/def 文件的首轮审读覆盖，不包括两个 CLI，也不证明五 ISA Linux
> 运行正确。这里的每一批必须写清旧代码实际删除了什么、保留的目标差异、
> 本地等价证据和没有执行的真实目标验收；功能 Bug 单独跟踪 MC 台账。

## R1：RV64 GlobalEmissionPlan payload 收敛

**基线：** C38 本地 `a9c3cb6`，对应 GitHub PR #674 远端
`400c1e7`（两者 Git tree 相同）。这是一批保持现有 RV64 输入接受域
与 GNU `.s` 输出的结构重构，不处理 Linux P0/P1 启动或新增 ISA 能力。

### 所有权迁移及旧实现退休

- `target/common/global_emit.c/.h` 将现有完整 planner 分成两种有名字的
  **同一个 payload 规划实现**：`minic_multi_global_payload_plan_build`
  只核类型、DataLayout、initializer、relocation shape；原有
  `minic_multi_global_emission_plan_build` 在此基础上继续施加 Common ELF
  `.bss/.bss.*` 对非零和 relocation 的 NOBITS 限制。已有 x86/Common
  消费者的严格 section 接受域保持不变。
- `target/riscv64/global_emit.c` 调用 payload-only planner 并重用
  `minic_multi_emit_global_payload` / 既有 shared recursive walker。退休
  RV64 私有的 Record flat/recursive 选择、homogeneous relocation array
  再分类、递归数组包装、零字节/逐 scalar dispatch 和重复 storage 查询。
  **没有**创建第二种 Global IR，也没有保留另一套生产 payload serializer。
- RV64 原有 source admission 仍先检验合法数据形状。显式 ELF section
  的 `@progbits`、默认 `.data/.data.rel.ro`、`.align`、符号 binding/
  visibility、`.half/.word/.dword`、relocation target/addend 与 label
  均保持目标私有；不得将 Common 的 NOBITS section 策略悄悄应用到 RV64。

生产代码三文件：**+80 / -211，净 -131 行**；其中 RV64 目标源码
**+48 / -196，净 -148 行**，Common 接口 **+32 / -15**。
独立新增 RV64 source/ELF 探针三个 C 文件及一个脚本，`Makefile`
将无需交叉工具的 source gate 接入 `check-fast`；严格 GNU ELF/QEMU
gate 作为显式目标，缺交叉工具退出 `BLOCKED`，不能显示为 `PASS`。

### 已执行、未执行和历史差异

| 证据 | 本批状态 |
| --- | --- |
| Release `-Werror`、`check-minic-target-boundaries` | PASS；review manifest 的历史 stale fingerprint 另计 |
| 同一 GNU 预处理 `.i`，C38 与 R1 新编译器比较 `.s` | **28/28 逐字节相同** |
| `check-rv64-global-plan-source`：13 全局对象、嵌套 relocation、flexible、bitfield、非法 section | PASS；含 C38 baseline 对照 |
| 现有 Static Record / nested union / section 专项 | 6/6 PASS |
| `check-linux-symbolic-integer-and-record`、`check-gnu-const-global-record` | PASS |
| x86 `run-x86-global-plan-abi.sh`：GNU AS/LD/ELF/native、BSS 正反例 | PASS；确保 Common 严格入口未退化 |
| RV64 GNU AS/LD/readelf/QEMU、完整 Linux P0/P1 | **NOT_RUN / 环境缺 RV64 GNU 交叉工具** |
| `check-four-backend-m4-global` | BLOCKED（缺 A64/Loong 交叉工具；C38 基线同样失败） |
| `run-gnu-zero-length-record-arrays`、`run-global-pointer-subscripts` | 旧固定 `li/la a0` 助记符断言失败；C38 基线也失败且同 `.i` 产物同字节，不计为 R1 回归 |

**已知而未修复的契约差异（A8-07）：** RV64 原有显式
`section(".bss.*")` 输出 `@progbits`，对非零 initializer 仍接受；
宿主 GNU 对同样的非零 BSS C 负例拒编，Common 的 NOBITS 策略也拒绝。
本批原样保留 RV64 接受域与 `.s`，用
`invalid_rv64_global_plan_bss_initializer.c` 和 C38 baseline 明示差异；
以后须有 RV64 GNU AS/readelf 的真实 oracle，再作为独立 Bug 修复。

### 下一批不应误删的边界

RV32 global 的本地 ELF 分类尚未迁入 Common；Compiler 的三族 inline
特化与函数→静态对象→函数裁剪闭包、x86 typed/legacy ASM 双端契约、
五目标各自 psABI 均未在 R1 修改。特别不应把一次目标 payload 收敛
解释为“Core 已去 C 化”或“五架构 ABI 已统一”。

## R2：Frontend relocation 旧入口退休；RV32 Global source 契约冻结

**基线：** R1 本地 `df910fd`，PR #674 远端 `dac24b6`，Git tree 相同。
本批分开核对两个清理接缝，不能把新增 RV32 测试当成 RV32 payload
重构已完成：`src/target/riscv32/rv32_global_emit.c` 与 R1 **完全一致**。

### 已实际删除的旧接口（A11-01）

- `frontend/ast_global.c` 删掉七个零生产调用者的 relocation 包装函数；
  `frontend/ast.h` 同步撤掉七组公开声明，**生产 C/H 净删 172 行**。
  留存 `minic_c0_global_object_add_relocation` 统一 replay/slot/type/duplicate
  验证，及 Parser 实际调用的 function relocation、integer-object relocation
  两个有区别的入口。没有改变 canonical `add_global_symbol_relocation`。
- 历史 `materialize-linux-first500-aggregate-array-v1.py` 从 target-kind
  七路文本文档模板改为按值复制 relocation、只重定位目标 slot，调用
  canonical replay；两个手工 `pr75` 脚本也改用带显式 `location_kind`
  的 canonical relocation 结构。这三个工具不是当前 CI 自动消费者；
  本批只做 Python 语法验证，**没有声称在旧 pinned checkout 上重演**。
- `minic_c0_global_object_set_extern` 仍被其他手工历史脚本用作文本模板，
  本批没有借“零生产 caller”理由误删；它不是已退役的七个 relocation API。

### RV32 迁移止损与新等价门禁（A8-07/A9-09/A11-28/30）

- R2 试接 Common payload planner 的三个 RV32 分支，在保存 Record 全零
  字段指令、nested Record-array 旧拒绝、weak/显式 section 分流的前提下
  试验补丁 **+72/-24、净增 48 行**，产生两层分类；**已全部撤回，未提交**。
  当前 RV32 Record、Record-array、relocation 早已使用 Common walker；
  remaining section owner 与 ELF32 relocation、弱符号、`.half/.word/.8byte`
  是真差异，不能拿 RV64 的 R1 删除量套算为 RV32 的重复实现。
- 新增 14 个 RV32 全局对象 source/ELF/QEMU fixture、恶意 section 负例、
  裸 ELF32 `_start`，及 `run-rv32-global-plan-abi.sh`；`check-fast` 接入
  `check-rv32-global-plan-source`，交叉 GNU ELF32/QEMU 另设严格显式目标。
  同一宿主 GNU 预处理 `.i` 可选 C39/R1 baseline 逐字节对照 `.s`；
  保留历史 `.bss.* @progbits` 与 GNU NOBITS 不同的事实，不把它算 PASS。

### 已执行的证据与边界

| 证据 | R2 结果 |
| --- | --- |
| Release 全量 `-Werror` 构建，Frontend 七接口全树源码消费者检索 | **PASS**；生产调用者零；历史脚本三个模板已迁 |
| 同一 GNU `.i`，R1 baseline 与 R2，x86-64/A64/Loong/RV32/RV64 五 ISA `.s` | **5/5 逐字节相同**（一个 14 全局对象样本；不是 ELF/QEMU） |
| RV32 14 object source gate + section 注入拒绝 + baseline 对照 | **PASS**（14/14、同 `.i`、`.s` byte identical） |
| RV64 R1 source gate、GNU constant record、shared CALL relocation、static const、dead static graph、global object 目标检查 | **PASS**（现有定点门禁） |
| `check-minic-target-boundaries.sh` | **PASS**；历史 review manifest `drift=144` stale fingerprints 仍另计 |
| 历史 Python 脚本语法、`sh -n`、`git diff --check` | **PASS**；历史 materializer end-to-end **NOT_RUN** |
| RV32 GNU `as/ld/readelf`、ELF32 symbol/relocation、QEMU 双二进制 | **BLOCKED / exit 2**（缺 `riscv64-linux-gnu-gcc`）；不冒充 PASS |

**下批门槛：** RV32 真 GNU ELF32 oracle 装齐后再考虑 section/weak
策略层抽象；不能为了接入 planner 而增加双份 source admission。
Frontend 其他纯历史模板由各自 pinned checkout 单独验收；本批不清除
被工具引用的 `set_extern`，不修改 inline、Core prune 或目标 ABI。

## R3：AArch64/LoongArch64 Linux static-key ASM 契约去重

**基线：** R2 本地 8a7132a，PR #674 远端 20ecc354，Git tree
相同。范围仅 A11-48 的 Linux __jump_table asm-goto Source/Core
资格合同；不是泛化任意 GNU inline ASM，也不改变
.altinstructions、异常表或普通指令生成。

### 已退休的重复路径

- target/common/linux_jump_label_asm.h 集中四段相同算法：
  空白规范化＋严格模板匹配、symbol＋可选十进制加数 lexer、
  Source asm-goto 约束/标签验证、Core immediate/CFG 双端防伪。
  删除 A64 与 Loong 各自的重复实现，仍由两边调用同一个校验器。
- 模板仍由 ISA **分别拥有**：A64 必须为 %c0，Loong 必须为 %0；
  目标 nop/b、函数/块标签和 __jump_table PREL32/PREL64 汇编发射
  仍分别留在各自 asm_contract.c 中；不把指令/ELF 输出移到 Common。
  Source 和 Core 双端资格检查继续保留。
- A64/Loong 生产 .c 合计 **+16/-153 行**，Common 新 .h 104 行，
  **生产净 -33 行**。新增 Common 白盒单测 96 行并接入 check-fast；
  两个原有脚本新增 baseline byte diff、错误 relocation modifier 负例。

### 已执行与未执行

| 门禁 | R3 状态 |
| --- | --- |
| Release 全量 -Werror 构建、Target boundary checker | **PASS**（review manifest 旧 fingerprint drift 另计） |
| A64 Linux jump-label site 3、inline clone、R2/R3 同输入 .s 对照 | **PASS，逐字节相同** |
| Loong Linux jump-label nop/branch、R2/R3 同输入 .s 对照 | **PASS，逐字节相同** |
| 两目标 alien section 与交换 %c0/%0 的非法模板 | **PASS**，拒编且未输出半成品 .s |
| Common source/Core 词法单测 | **PASS**；7 合法/11 非法 key、128 字节边界、两个模板、Core 正反例 |
| A64/Loong GNU AS、真 ELF relocation、QEMU | **NOT_RUN / 缺目标交叉 GCC**；不能当作 PASS |

**待验：** 五 ISA 最终 GNU ELF/QEMU 联合验收另计。第三个使用者
不能因共享模板解析器存在而绕过各自 ISA 指令和 relocation 合同。

## R4：x86 五组 legacy ASM 分派退休；RV64 死 scalar API 移除

**基线：** R3 本地 `bd176cd`，PR #674 远端 `bb38f326`，Git tree
相同。本批分成两个可分别检验的改变：x86 ASM Source→Core→Machine
资格归属迁移，以及 RV64 零生产调用者的旧接口删除；不改五目标 ABI、
Linux runtime、Core 类型事实或 GNU 模板接受域。

### x86：同一份 Source 资格，五族改用独立防伪的 typed Machine form

- `asm_contract.c` 的 `IP_HERE`、`ZERO_BARRIER`（7 个 form）、
  `FXSAVE`（普通/64-bit 两种 form）、`LAR_SELECTOR`、
  `IDENTITY_LOAD` 五个 Source qualifier 在 admission 时携带
  `target_machine_form_id/variant`，Source 表仍保持原有顺序、
  操作数约束与 memory/cc/fixed-register effects。
- `asm_contract.c` 的 **machine ingress** 使用独立的 immutable
  GNU 模板→ID/variant 匹配，阻止被改写的 Core metadata 冒充
  可信 Source。`machine_asm.c` 不再重复按模板选择这五族；仍对
  clobber、volatile、binding 种类/索引/类型、FXSAVE image 尺寸与
  `LAR` 的 ZF 语义做原有物理检查。
- 五族 Machine 旧文本分派全部删除，`emit_linux_zero_barrier` 与
  `emit_linux_ip_here` 只是把原生产指令原样移入 typed helper。
  现行 Source 表 **111 项：28 typed / 83 legacy**（R3 是
  23 typed / 88 legacy）；剩下 83 族的 source 准入与 old dispatch
  不能因本批而跳过。

### RV64：真正零调用者的旧 unsafe scalar wrappers

- `codegen_support.c` 删除静态 `emit_scalar_load/store` 与唯一使用
  它们的公开内部 `emit_scalar_load/store_for_program`；
  `codegen_internal.h` 同步撤销两组内部声明。生产 `src/` 与测试
  无 C 调用者，热路径一直经 `emit_safe_scalar_load/store` 和
  `emit_integer_conversion_for_program`，这些现役路径未动。
- `tools/dev` 的 PR75/77/m105/materialize 和 `tools/ci` 的旧
  source-patching 脚本仍含历史文字模板；`.github/workflows` / 当前
  `Makefile` 无直接执行这些脚本的入口。保留历史文件以供审计，
  **不保证其在 R4 源树上重放成功**，也不为旧脚本恢复死 API。

### 代码量及等价证据

| 实体 | R4 生产 C/H 变化 |
| --- | --- |
| x86 `asm_contract.{c,h}` + `machine_asm.c` | **+100 / -65，净 +35 行**；五份机器文本分类已被退休，但 typed dispatch/校验有成本，不虚称净删 |
| RV64 `codegen_support.c` + `codegen_internal.h` | **+0 / -65，净 -65 行**；四个死函数及两组声明 |
| **生产合计** | **+100 / -130，净 -30 行**；另新增两个测试 fixture/脚本 136 行并更新白盒测试、Makefile、文档 |

| 门禁 | R4 实测 |
| --- | --- |
| Release `-Werror` 构建；Target boundary checker | **PASS**；review manifest 仍有 144 个历史 stale fingerprint，checker 报 `PASS`，不是消除 drift |
| `make MODE=release CFLAGS=-Werror all` 后运行 `check-fast` | **PASS**；完整 `check-fast` 退出码 0，环境缺 GNU/QEMU 的子项明确 `SKIP`，非运行认证 |
| 五目标同一 GNU 预处理 `rv32_global_plan_abi.c` `.i`，R3/R4 `.s` | **5/5 逐字节相同**：x86-64、A64、Loong、RV32、RV64；这只是源级汇编差分 |
| 新五家族 `x86_typed_five_legacy_abi.c` GNU `.i`，R3/R4 `.s` | **逐字节相同**；全部 7 种 zero barrier / 2 种 FXSAVE 指令、RIP、LAR/ZF、identity load |
| 新五家族 GNU AS/LD/native 与 GNU C/native | **PASS**，两 ELF 均退出码 42、stdout/stderr 相同；Linux x86-64 宿主执行 |
| 对五家族各篡改一种模板；检查部分 `.s` | **5/5 拒绝**，R3/R4 同拒、无残留输出；white-box 额外验证 11 个 typed family 的伪造 ID/variant/模板 |
| RIP PC、zero barrier、x86 control-state/FXSAVE、x86 uaccess/LAR 的既有 GNU 定点探针 | **PASS**；与 R3 字节等价之外的目标语义证据 |
| x86/其他四目标 GNU 交叉 ELF/QEMU 全覆盖 | **NOT_RUN**；不得把 x86 native 与五目标 `.s` 对照冒充五架构运行认证 |

**边界：** R4 没有“统一 x86 ASM”为通用 GNU 支持；未知 GNU
模板继续拒绝，83 个 legacy 族及其他四架构特化尚在。RV64 死接口删除
不证明其 enum/Record copy 已经脱离 C0；见 A8-09、A8-25、A9-10、A11-39。
