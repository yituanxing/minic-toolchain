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

## R5：并行五线复核，x86 六族＋Loong 十四族 typed 化与死接口退休

**基线：** R4 本地 `028ec47`、PR #674 远端 `3bf0969`，Git tree
完全相同。本批由五线分别负责 x86、Loong、RV64、Frontend/Compiler、
Core/Common，负责人合并审查、跨模块回归并单次提交推送。没有改
Frontend/Core IR 的生产表示、五架构 ABI、ELF section 策略或
Linux runtime 入口；不把“工作线程数”算作覆盖文件数。

### 已接收的四块生产修改

1. **x86 六家族迁移（A8-10/A11-39）：** `VERW_CLEAR`、
   `TLB_CACHE`、`DELAY_MUL`、`DELAY_WAIT`、`LOWLEVEL_SCALAR`
   （8 种 form）、`BOOT_FLAGS`（4 种 form）由 Source qualifier
   携带 typed ID/variant，Machine 按 ID dispatch，独立 immutable
   模板机器入口及寄存器/内存/volatile/effect 验证仍保留；
   删除六个 Machine 旧文本分派。Source 表仍为 111 项，
   **34 typed / 77 legacy**（R4 28/83）。特别核对 CPUID：
   `4 outputs + 2 tied inputs` 是 typed BOOT_FLAGS 的 Core 6 operand；
   `4 outputs + 4 tied inputs` 仍由旧 `qualified()` + generic Machine
   8 operand 入口拥有。不可合并或吞掉后者。
2. **Loong 十四个 Linux ASM descriptor（A9-10）：** Source 使用
   原有唯一 descriptor table 选择 form，再传稳定序号 ID、variant0；
   Machine 合法 typed 路径按 ID 索引该表，独立检查同一 immutable
   GNU 模板和原物理 emitter，无须再扫描 14 族**选择发射家族**。
   首次审查发现被伪造的非零 ID 可以在 descriptor 校验前落入 SIMD，
   或将 ID 擦为零后落入 generic fallback；均已在最终版本关闭：
   nonzero typed 在 SIMD 前 fail-closed；`ID=0` 若模板属于 descriptor
   则明确拒绝。**注意：ID=0 的拒绝保护仍扫描 14 族模板**，
   不能说所有 machine 文本匹配物理消失，更不能把此批虚报为净删。
3. **RV64 两个死 wrapper（A8-25）：** 退休未被 `src/`/测试 C
   调用的 `classify_abi_value` 与 `emit_integer_aggregate_load_chunk`
   及内部声明。`*_profile`、safe scalar、integer conversion、
   ABI profile 等所有活路径保持；`tools/dev/ci` 历史文字补丁未修改，
   现行 `.github/Makefile` 无它们的重放入口。
4. **Frontend semantic snapshot 三 wrapper（A11-05）：** 删除仅在
   白盒测试调用的 `matches`、`only_probe_expressions_changed`、
   `rollback_probe_expressions`；现役
   `rollback_probe_expression_types` 完整保留。白盒测试改为直接
   验证 16 项 arena 计数、owned CALL overflow buffer 释放、
   backward-count 与 unrelated-arena 拒绝，并新增 expression+type
   同时增长正例。不存在剩余 `src/tests/tools/.github/Makefile`
   调用者。没有碰被现役 CI 源文本锚定的 AST parameter API。

**生产源码（10 个 `.c/.h`）+127 / -145，净 -18 行：**

| 子块 | 生产增加 | 生产删除 | 净变化 | 注释 |
| --- | ---: | ---: | ---: | --- |
| x86 typed 六族 | 71 | 48 | +23 | 真实删旧分派，typed 校验有成本 |
| Loong typed 十四族 | 56 | 12 | +44 | 单表按 ID 选择，ID=0 仍作拒绝性模板扫描 |
| RV64 零调用 wrapper | 0 | 27 | **−27** | 不触及 live profile |
| Frontend test-only wrapper | 0 | 58 | **−58** | 白盒覆盖迁至 live 入口 |
| **合计** | **127** | **145** | **−18** | 不含 Makefile、测试、文档 |

新增 x86 source/native/篡改脚本 2 个文件（160 行）、Loong
machine-typed 合成测试 1 个文件；现有 Frontend/x86 白盒扩充，
`Makefile` 将 x86 six 和 Loong descriptor gate 一并接入 `check-fast`。
Loong 测试在 release `-DNDEBUG` 下显式恢复 `assert`，避免假阳性。

### R4/R5 合并状态上的实测证据

| 门禁 | R5 结果 |
| --- | --- |
| Release 全套工具 `all`、`CFLAGS=-Werror` | **PASS** |
| `make check-fast`（含 x86 six、Loong typed machine、Frontend rollback、RV64 ABI、现有 R4/R3 旧门禁） | **PASS / exit 0**；缺 cross GNU/QEMU 的子项仍 `SKIP`，不能冒充执行通过 |
| `check-minic-target-boundaries.sh` | **PASS**，历史 review manifest fingerprint `drift=145` 仍另计 |
| R4/R5 同一 GNU 预处理 global-plan `.i`，五目标 `.s` | **5/5 BYTE_IDENTICAL** |
| R4/R5 Loong 代表性 `.i`：small-xchg、cpucfg、final opaque、packed hardware、named store、iocsr alias、checked add | **7/7 BYTE_IDENTICAL**；另子任务 8/8 差分 PASS |
| x86 六族 GNU `.i` 与 R4 baseline、GNU AS/LD/native | **逐字节相同**；GNU C 和 MiniC `.s` 两 ELF 均退出 42，CPUID6 + CPUID8 都保留 |
| x86 六族篡改模板 | **6/6 新旧均拒绝**、无残留 `.s`；white-box **17 族** ID/variant/template 防伪 PASS |
| Loong immutable descriptor machine gate | **PASS**：5 组 exact/variant、3 cross-ID、6 组合成 Core 拒绝及无部分 `.s`；已有 CPUCFG/CACOP/nospec/SIMD 负例 PASS |
| RV64 两 GNU `.i`、RV64I/LP64、ABI/LP64D/Global Plan | **PASS，R4/R5 `.s` 字节相同** |
| GNU Loong ELF/QEMU、完整五目标 Linux 运行认证 | **NOT_RUN**；不能由 `check-fast` 或 `.s` 差分外推 |

### 第五线复核，明确 KEEP 而不是强删

- `core_lower_if.c` 的 ~20 行 snapshot 看似接近 `local_facts.c`，
  但特殊事实保留、逃逸/修改 merge 规则不同；不删除。
- `core_lower.c` normalized-while 标签全表扫描证明**唯一 owner**，
  `reentry_index` 仅证明没有外部 reentry；不等价。
- Core `explicit_alignment` 早期 verifier 缺口应另做合成 Core
  负例和拒绝时机修复；不混进纯清理以改变非法 IR 接受阶段。

**下一批：** x86 还余 77 族；Loong 除这 14 个 descriptor 之外仍有
带解析载荷的 clock/AMO/futex/uaccess 等路径，需要保存真实 typed
payload 才能取消机器端重新解析。纯清理不应以“所有 target 字符串
统一”替换真实 psABI/指令语义差异；交叉 ELF/QEMU 认证另列证据。

## R6：十三组 ASM typed 身份迁移与五线独立 KEEP 复核

R6 以已提交 R5 `64dd565deea7cf8d3e8f2347ffe185c2a99a15ff`
（Draft PR #674 当时远端头 `f178cc21bd21f00676ed660c1010d36e85c92318`，
源树相同）为基线。六条并行线中，x86 与 Loong 两线实施修改；
RV32/RV64、AArch64、Frontend 三线独立证明候选仍被调用或语义
不同，选择 KEEP；第六线独立审查模板准入、typed spoof gate 和
历史证据。所有集成、提交和推送由主线负责。

### 已完成的两个迁移子块

1. **x86_64 八族：** PM_TRACE、I915_MOVNTDQA、PVCLOCK_SCALE、
   PREEMPT_STATIC_CALL、IPV6_CHECKSUM、BOOT_STRING、VDSO_SYSCALL、
   VDSO_CPUNODE。Source 形成 ID/variant，Machine 使用独立、不可变
   模板复验后进入原物理 emitter，退休八处正常执行路径的旧文本家族
   重新选择。111 个登记家族目前为 **42 typed / 69 legacy**。
   保留用于 Source 准入和 Machine 拒绝伪造输入的解析辅助函数，
   不把“旧分派退休”冒称为删除所有文本校验。
2. **LoongArch64 五族：** CLOCK、IRQ_SWITCH、INVTLB、ADD_UNLESS、
   FUTEX_RMW。原有 descriptor ID 1..14 不变，新 scalar-payload
   ID 15..19，编译期断言阻止 ID 冲突。IRQ_SWITCH 的命名 Source
   模板转为数字 Core 形式属于已验证的原规范化行为；Machine 复验
   Core 固定模板和 variant，再调用原机器发射器。缺失 ID=0 时
   仍要扫这些已认领模板作拒绝保护，不能称所有二次匹配已物理消失。
   UACCESS、PERCPU_AMO、SUB_POSITIVE 等携带复杂结构化数据的
   现役家族未迁移，避免把 payload 草率塞入一个 variant 数字。

**生产代码仅上述 5 个 `.c/.h`：+167 / −86，净增 81 行。**

| 子块 | 增加 | 删除 | 净增 | 已退休的正常机器分派 |
| --- | ---: | ---: | ---: | ---: |
| x86_64 八族 | 82 | 55 | 27 | 8 族 |
| LoongArch64 五族 | 85 | 31 | 54 | 5 族 |
| **合计** | **167** | **86** | **81** | **13 族** |

因此 R6 是职责明确化/防伪强化的**机制迁移**，不是源码行数净删减。
R1–R5 累计生产源码净 −384 行，纳入 R6 后为**净 −303 行**；
账目不含 Makefile、文档和测试。两套新正反例脚本都已接入
`check-fast`，x86 与 Loong 白盒 ID/variant 伪造用例同步扩展。

### 干净基线与实测门禁

曾发现旧路径 `build/r5-main/bin/minic-cc` 是过期/不匹配的可执行文件：
它错误拒绝 R5 实际支持的 PM_TRACE 和 Loong packed 输入，造成虚假的
“R6 放宽准入”警报。最终证据只使用从**精确 R5 Git 提交源码全新
解包、重新编译**的 `/tmp/minic-r5-baseline-TitOeI/build/r5-clean/bin/minic-cc`
与全新 R6 `build/r6-main-clean/bin/minic-cc`；旧 `r5-main` 二进制
不再作为对照证据。

| 门禁 | R6 结果与限制 |
| --- | --- |
| `make all` Release `CFLAGS=-Werror` | **PASS**，全套工具全新构建 |
| `make check-fast`（含两项新集成脚本） | **PASS / exit 0**；部分无 GNU 交叉工具/QEMU 的原子项仍为 SKIP |
| `check-minic-target-boundaries.sh` | **PASS**；历史 review-manifest `drift=145` 不因 R6 变化而宣称消除 |
| R5/R6 同一 GNU `.i`，x86/A64/Loong/RV32/RV64 | **5/5 `.s` BYTE_IDENTICAL** |
| x86 八族新旧 `.s` 与 GNU ELF/native | **BYTE_IDENTICAL**；GNU AS/LD、`.tracedata`（20B/两 relocation）、`.altinstructions`（14B）及 native GCC/MiniC 两程序退出 42；**8/8** 伪造模板拒绝、无部分 `.s` |
| x86 既有七组专项测试与 25 族机器白盒 gate | **PASS**，原来 IPv6 carries、boot-string、vDSO、preempt 等物理效果仍覆盖 |
| Loong 既有十个 GNU `.i`、新 INVTLB 四形式及 ADD_UNLESS 两宽度 | **10/10 既有 fixture 与新增 fixture 的 `.s` 字节相同**；两非法变形新旧都拒绝，无部分 `.s` |
| Loong descriptor/scalar ID、variant、Core 模板白盒 | **PASS**，含十项独立 machine spoof 拒绝与防止 ID0 fallback |
| GNU LoongArch64 ELF/QEMU、五 ISA 的完整 Linux 运行认证 | **NOT_RUN**；本地无工具，不能由 `.s` 比较外推 |

### KEEP：不制造虚假净删减

- **RV32/RV64：** `rv32_frame.c` 的大偏移 `lw t6` 可借用 t6，
  但同条件 `sw t6` 必须拒绝；Common 的 preserving-scratch
  策略拒绝域不等价，`entry_sp_offset` 仍由动态栈对齐消费。
  RV32 全局计划 14-object Source gate PASS。RV64 其余候选也有
  实际 ABI/profile 调用者，保持不动。
- **AArch64：** 40 exported、156 static 路径调用者复核后均活。
  `block_emit.c` 字节拷贝与 `call_emit.c` ABI eightbyte/x0-x1
  装卸方向、对齐约束不同；`frame_emit.c` 保守拆 offset 还要保护
  scratch live-mask。没有在缺专属语义证明时机械折叠。
- **Frontend：** 457 个公开函数与其源调用者、工具、CI 文本锚点
  复核后，剩余候选都是活入口或历史兼容锚点，没有新的安全
  零调用 wrapper 可删。无 Frontend 源码改动。

**R7 候选：** x86 仍有 69 组正常 legacy 路径，Loong 剩余复杂
payload 家族需要新的 typed 结构；两者都须先证明 Source 准入域、
Machine 防伪和原 ELF/物理效果，再退休对应旧分派。不要以不受
本机工具支持的交叉运行测试冒充实测。

## IR-P0：Core 类型迁移入口契约与定义侧 variadic 事实

起点：`2f8b74f`（R6；同一 `refactor/minic-target-language-boundary`
分支）。这是 Core-owned 类型迁移的**准备批**，并非宣称 CoreTypeId、
CoreModule 或 Machine IR 已经实现。

1. 新建 [Core 类型入口与 Module 边界契约](core-type-ingress-and-module-boundary-2026-11-02.md)：
   选择 TU 共享且 Core-owned 的 TypeContext，明确递归 Record 的两阶段
   登记、跨函数类型身份/生命周期、布局和 ABI shape、SourcePolicy 的
   typed ASM 来源证明、转换失败事务和五目标按语义家族验收。
   Module 只负责未来的编译单元函数、数据、符号和重定位，不另建
   并行 Global initializer walker；Machine IR 继续后置。
2. `MinicCoreFunction` 补齐 `is_variadic` 定义签名事实；C→Core lowering
   在入口捕获它，RV32 callee frame 改从 Core 读取，而不再为该事实
   回查 C `MinicFunction`。不改变现有 RV32 Frame、ABI 或寄存器布局算法。
3. 扩展 Core schema 的 native/ASan 形状正例，RV32 变参/非变参
   definition 对照以及 `a1..a7` 入口保存区验证；保留原 Record
   对齐与传参布局负担，不将 `MC-014/015` 已知风险标为已修复。

**本批统计（不含文档）：**生产 `+13/-1`、净 `+12` 行（4 文件）；
测试 `+83/-1`、净 `+82` 行（4 文件）。新增 P0 契约文档
142 行，架构索引新增 1 行；本节进度台账单独统计。
当前 `src` 生产 C/H/def 文件 **343** 个；历史 C38 的 342
为当时基线，不作为本批的当前分母。

| 本批本地实测 | 结果与限制 |
| --- | --- |
| Debug `minic-cc` 全构建 | **PASS** |
| `make check-core-ir-shapes` | **PASS**：12 native、12 ASan/UBSan |
| RV32 变参 Record 对齐与 callee 保存区 | **PASS**：原四组入参位置、新 variadic/fixed 定义侧对照 |
| `make check-core-ir-shapes-source` | **PASS**：五 ISA `-S`，x86 GNU/MiniC native exit 42 |
| `make check-rv64-abi` | **PASS** |
| `make check-x86_64-sysv-odd-record-abi` | **PASS**：GNU/MiniC caller/callee 四组合 |
| `make check-aarch64-fp-pointer-record-abi` | **PASS**：五目标 134 项 `-S`，A64 negative 3 项；A64 ELF/QEMU **SKIP** |
| `check-minic-target-boundaries.sh` | **PASS**；历史 fingerprint `drift=145` 另计 |
| 完整五目标 GNU ELF/QEMU/Linux 运行 | **NOT_RUN**；当前环境缺部分交叉工具链，不得记成 PASS |

仍 OPEN 的 `MC-014/015/076`、`MC-064/072/085` 等由原问题
台账独立跟踪。下一批进入现有 Core IR 的类型身份/所有权迁移，
按标量、聚合、函数签名及五目标消费者分批切换；每个切片同时
删除已被替代的 C 查询路径，不保留永久平行 Core IR v2。

## IR-P1：CoreType 消费端、Module 函数符号与 Global payload 单一所有者

起点：`06ab1bc`（`refactor/minic-target-language-boundary`）。这是
**边界迁移批**，不把重构中暴露的 Zstd/NEON/Linux 行为风险混成
临时补丁；也不声称 Target 已完全摆脱 C `MinicType`/`MinicC0Program`。

### 生产代码迁移

1. **Module 函数元数据进入五目标实际发射路径。** 通用 prepared
   visitor 向 target 显式传递已封闭的 `MinicCoreModuleFunctionSymbol`；
   函数定义/声明选择与 FunctionPlan 准备由 Module 定义状态决定。
   x86-64、AArch64、LoongArch64、RV32、RV64 的函数名、section、
   weak/internal/visibility、alignment、constructor 等符号决策，
   在附着 Module 时读取 Module 快照。x86 `.head.text` 闭包和
   Direct LP64 preflight 同步改为 Module 符号事实，Direct LP64
   variadic profile 改用 `MinicCoreFunction.is_variadic`。
   `module == NULL` 的旧独立测试入口暂留，不能把它当成生产双所有者。
2. **全局初始化语义上移到 Compiler。** `src/compiler/global_payload.c/.h`
   成为唯一的 C/DataLayout initializer 分类与递归字节/重定位 walker；
   `src/target/common/global_emit.c` 退为约 330 行的 ELF/GAS `FILE *`
   适配器。`module_ir_build.c` 不再反向包含 `target/common/global_emit.h`，
   Module 与历史 Common FILE 路径复用同一份显式 BSS 准入规则。
   构建源清单和 bootstrap B0 清单已同步登记。
3. **RV64 CoreType 消费端收口。** ISA i128、Frame sret/Record、
   函数 ABI plan、已覆盖的 admission 与 record load/copy、bitcast、
   frame-address/call 路径从 CoreTypeContext 查询；在该语义路径上
   清除 C 类型兜底。`core_type_query.h` 统一只读查询。
   原独立 `abi_test` 夹具改为真实 TypeContext/TypeId，并增加
   未封闭函数和非法形状回滚负例；没有为通过老测试恢复旧 fallback。

### 完成验证与统计

| 门禁 | 本批结果与限制 |
| --- | --- |
| `make all`（Debug） | **PASS**，全部工具构建 |
| `make MODE=release CFLAGS=-Werror BUILD_DIR=build/refactor-werror -j8 all` | **PASS**，全新严格 Release 全工具编译/链接 |
| `make -j8 check-fast` | **PASS / exit 0**，含既有大量回归测试；部分交叉运行项 SKIP |
| `check-minic-target-boundaries.sh` | **PASS**，新增 Module/C 互相矛盾的 poison probe、RV64 CoreType 与 compiler walker 所有权检查 |
| `make check-rv64-abi`、`make check-rv64-coretype-target-admission` | **PASS**，含真实 CoreType 和 rollback 负例 |
| `check-rv64-global-plan-source`、`check-rv32-global-plan-source` | **PASS**，保留各目标原数据/重定位准入 |
| `git diff --check` | **PASS** |
| GNU 交叉汇编/链接、QEMU 和五目标 Linux 端到端 | **NOT_RUN**；本地工具链不全，禁止从本机 `check-fast` 推断认证通过 |

本批变更（提交前统计；不含本节文档）：**生产 `src/` +1644/−1658，
净 −14 行；`tests/` +267/−25，净 +242 行；Makefile +1 行**。
生产统计含约 1100 行从 Target 移至 Compiler 的代码搬迁，不能把
这个表面净减少量误当成已删除 1600 行无用逻辑；价值是单一所有者
和消费路径切换。未单独修复 Linux/Zstd/NEON 缺陷。

### KEEP / 下一批必须整体处理的边界

- **Core 类型身份仍有两层。** CoreTypeId 已在部分生产消费者生效，
  但其他 Core 指令/操作数的 `MinicType` 字段和 target 家族仍未完整
  迁移；不得为一条旧类型分支补新的永久 fallback。
- **函数局部 ABI/Source ASM 仍借 C Program。** Module 已拥有
  函数符号/定义选择，不等于所有 target emitter 都不再需要源 AST；
  要按 ABI 语义族与 structured ASM 来源契约整体替换借用。
- **RV64 `INTEGER_OVERFLOW` 完整族仍属旧路径**，横跨
  `legalize.c`、`overflow_emit.c` 等，不拆成 admission 新、发射老的
  半迁移状态；其他 scalar/inline-asm 的类型查询另列后续批次。
- **Global 表示尚有格式泄漏。** Compiler global plan 的
  `default_elf_section`、Module 的 `common_elf_bss_admitted`、
  GAS 表示及 C statement label ID 需要下一轮按格式抽象切开；
  当前迁移只证明 initializer 算法单一所有者。
- **历史 `module == NULL` 独立 API 与五 ISA 交叉运行认证**分别
  等消费者改造和具备交叉工具链的 CI；删兼容入口前须先更新测试。

## IR-P2：RV64 整数标量与 checked-overflow 语义族整体切换

起点：`5031631`（IR-P1）。本批按**准入—合法化—机器发射—测试**
一起迁移 RV64 整数标量，而不是在 old/new 类型分支中选择性开洞。
同一批不修与类型所有权无关的 Linux/Zstd/NEON 行为缺陷。

1. `scalar_emit.c` 的整数常量、算术/位运算、比较、移位、
   `__int128` 双寄存器和 strict-I libcall、整数转换与 bitcast
   全部只读 `MinicCoreTypeId`、有符号性及 CoreType 布局；
   普通整数的寄存器规范化复用现有
   `minic_riscv64_emit_core_integer_conversion`，删除这个发射
   单元里所有 C `minic_type_` 查询和 `instruction->type` 决策。
2. `INTEGER_OVERFLOW` 完整族在 `legalize.[ch]`、
   `core_admission.c`、`overflow_emit.c` 同时切换。
   结果指针指向的整数类型、bool 标志、mixed signedness 的范围
   与 XLEN scratch 判定、狭窄整数规范化、strict RV64I 乘法
   libcall、最终未对齐安全存储均使用 CoreType。
   不再把 CoreTypeId 转回 C `MinicType` 后调用旧存取函数。
3. 专项原生准入测试将全部 C descriptor 保持为 void/零值，
   用真实 CoreType 构造有符号/无符号混合、16 字节拒绝、错误
   bool 返回以及未附着 TypeContext 的负例。
   静态边界门禁禁止两个机器发射单元重新引用 C 类型字段。
   补登记 P1 新建但尚未列入 backend-review manifest 的
   `core_type_query.h`，保留其他已变化源码的历史 drift 状态，
   不把测试通过冒充全模块深读认证。

| 验证 | 结果 |
| --- | --- |
| `make all`（Debug）和全新 `MODE=release CFLAGS=-Werror all` | **PASS** |
| `make -j8 check-fast`、`check-minic-target-boundaries.sh` | **PASS / exit 0** |
| `check-rv64-abi`、`check-rv64-coretype-target-admission` | **PASS** |
| P1 编译器与 P2 编译器同输入 RV64 `.s` | **11/11 BYTE_IDENTICAL**：加/减/乘溢出、mixed/packed、整数比较/位运算、`__int128`；历史 narrow-enum 禁止准入两边一致拒绝 |
| `-march=rv64i -mabi=lp64` 严格 Base-I `.s` | **5/5 BYTE_IDENTICAL**，含 overflow、packed 与 `__int128` |
| GNU 交叉 AS/LD、QEMU/Linux 五目标端到端 | **NOT_RUN**，仍需有工具链的后续验收 |

**本批变更统计（不含文档）：生产 `src/` +239/−255，净 −16 行；
`tests/` +67/−1，净 +66 行。** 这是单个 RV64 整数/溢出族
所有权迁移，非整个 Core IR 已去 C 的完成比例。

**本批尚未切完的边界：** `fp_emit.c` 的浮点/整数转换、
`inline_asm_emit.c` 中历史未封闭调用兼容分支、以及
`call_emit.c`/`memory_emit.c` 等其它 machine family 尚有 C
借用；它们不能因这批整数迁移就被声明为 Core-only。
下一批应把这一组同 ABI、Source ASM 契约一起核实，不引入
第二份整数宽度算法。

## IR-P3：五目标整数转换与 pointer/int bitcast 的 CoreType 形状归一

起点：`54b313f`（IR-P2）。本批不以 RV64 为唯一迁移边界：
`INTEGER_CONVERSION` 和 `SCALAR_BITCAST` 是同一个跨目标的
**Core 语义形状族**，一次性覆盖 x86-64、RV32、RV64、AArch64、
LoongArch64。前端 C `MinicType` 只保留在 Source→Core ingress，
两类转换内的 C `MinicType` 查询退出后端，目标独立维护寄存器、
指令、ABI 和架构特有的转换规则。

1. 新建 `target/common/core_scalar_conversion.h`，共用且只消费
   `CoreTypeContext` 的类型合法性、整数 bit-width/size、bool、
   signedness、指针与整数的转换方向、Core SSA operand/result 一致性。
   禁止类型已封存后再用 C rank/enum/char 重新求一次宽度。
2. AArch64/LoongArch64 经已有 Direct LP64 整数计划与 bitcast
   判定消费共同形状；`direct_instruction_semantics.c` 的旧
   bitcast 独立类型判定被替换为轻量 ABI 子集限制。
3. x86-64 的整数转换/bitcast 两种机器分支均切换到 CoreShape，
   使用既有 `x86_core_normalize`，保留 kernel-head VMA 数值地址
   变换及 i128 寄存器对，不重新解释 C 描述符。
4. RV32 共用一组 Core 分类和 word canonicalizer，把整数转换与
   bitcast 从 `rv32_resolved_*` 的 C 回退分支迁出；**32 位指针
   直接转 unsigned long long 的 GNU 符号扩展特例**仍由 RV32
   机器发射负责，其它 ILP32 宽度与双字传递保持原样。
5. RV64 整数转换消费相同 Shape，bitcast 准入共用另一 Shape，
   仍由 ISA/目标拒绝当前不支持的 128 位 bitcast。单测补齐一个
   原来只有类型、缺失合法结果 SSA 的假指令；共享形状现要求
   operand/result 双向一致，这不是机器语义变更。

**验证：** poisoned C `MinicType` 的独立 Core 单测覆盖
LP64/ILP32 指针、signed8、unsigned64、i128、bool、错误宽度及
缺失上下文；静态门禁锁住五目标消费点。10 份 GNU 预处理
`.i` × 5 目标的新旧 `.s` 比对结果为 **47/47 BYTE_IDENTICAL、
3/3 SAME_REJECT、0 DIFFERENT**（其中 LoongArch64 拒绝两个
旧本来就不支持的样例）；Debug 构建和全新 Release `-Werror`
构建通过。交叉 GNU AS/LD 与 QEMU 工具在当前环境不可用，
不得把 `.s` 一致性称为运行时认证。
`make -j8 check-fast`、`check-minic-target-boundaries.sh` 及
专项 AArch64 GNU native 指针样本、RV32 五目标扩宽源码检查也
已通过。源代码 `src/` **+241/−130，净 +111 行**；
`tests/` **+160/−0**；账本文档 `docs/` **+53/−0**。
新公共 header 纳入 review manifest；历史其他源码的 stale
记录保持原有状态，不把本次 CI 当作全面审计完成。

**界限：** x86/RV32 其他 Machine 族以及 RV64 的浮点↔整数
转换仍混合使用旧 C 或已封存 Core 查询，待同一语义族的下一批
整体迁走；本批不改 Zstd/NEON 等已知 Linux 行为缺陷。

## IR-P4：17 个 FLOATING_SCALAR 操作作为一个五目标语义族迁移

起点：`440d7ad`（IR-P3）。沿 Core IR 指令家族先读五目标完整
FP 发射、Direct LP64 准入、RV32 softfloat、RV64 native F/D 和
frame/libcall 所有权后，确定浮点常量、float/double 四则运算、
double 比较/取负及四类数值转换**必须一起收**：只改整数↔浮点
转换会让紧邻的常量/算术仍留两套 C/Core 类型判断，无法保证
同一个 sealed Core 值在生产者和消费者间具有唯一解释。

1. `target/common/core_floating_shape.h` 归纳全部 17 种 Core 操作：
   `CoreTypeId` 的 binary32/binary64/extended 布局、整数宽度
   signedness、operand/result SSA 形状、比较结果 signed i32，
   非法类型/错配 fail-closed。Core 类型语义一次判定，具体
   rounding、物理寄存器、ISA 和 softfloat libcall 不进入 Common。
2. `direct_instruction_semantics.c` 删除原来逐 opcode 重复的
   FP 形状判定，AArch64/LoongArch64 保留相同的薄 ABI 限制层，
   原有 FP 指令和 frame-relative 访存完全不动。
3. x86-64 SSE/x87 发射通过这一公共形状去掉本地重复类型
   检查，入口不再接收 C `program`/`target`；已有无符号 u64
   修正、x87 扩展精度、NaN 有序比较保持机器私有。
4. RV32 的整个 FP 发射器移除 C `MinicType` fallback 与
   `rv32_resolved_*`：软浮点 helper 符号、ILP32 一/双寄存器
   传递、结果 narrow canonicalizer 均由 Core 的 signedness 和
   width 驱动，继续调用既有 LLVM compiler-rt，不复制算法。
5. RV64 的整个 FP 发射器不再使用 C `instruction->type`，
   FCVT 选择源于 Core 整数尺寸/符号，`DOUBLE_TO_INTEGER` 的
   最终规范化调用已封存 CoreType；strict Base-I softfloat
   与 native F/D 的机器策略原样保留。

**验证：** 17 opcode 全枚举 C-descriptor poison 单测及非法
binary 格式/整数布局/结果 SSA/缺失上下文的负例，五目标源码
边界防回退。16 份 GNU 预处理 `.i` × 5 目标的 P3→P4
汇编差分为 **76/76 BYTE_IDENTICAL、4/4 SAME_REJECT、
0 DIFFERENT**。不把双方拒绝的 unsupported case 算作运行通过。
Release `-Werror`、`make check-fast`、目标边界测试分别独立
核验；交叉 GNU AS/LD、QEMU 在当前环境仍不可用，待有工具链
后用同一套 `.i` 做真正的运行时差分。
本批 `src/` **+231/−255、净 −24 行**；
`tests/` **+194/−0 行**。新增 1 个公共 Core shape header，
其余生产差异主要是逐目标的旧判定与 C 回退退休。

**仍未迁移的边界：** FP 参数/返回值的 ABI 分类是另一组
跨指令的 psABI 语义，不能塞进 FP 指令 shape。RV32 的其他
内存与 call 族、RV64 `runtime_libcall` 的旧整数 liveness fallback
仍需按各自完整族消除，禁止通过本次 FP 收口把其它模块冒充
成已纯 Core。

## IR-P5：五目标 CALL/参数/返回值 ABI 标量形状与聚合分类收口

起点：`eeb4839`（IR-P4）。这次以**双向调用边界**而非 RV64
单架构为单位：`CALL`、`INDIRECT_CALL` 的参数与结果、
`PARAMETER`/`PARAMETER_OBJECT` 的接收都要保持唯一的 Core 类型解释。
先核对五个 target 已存在的 ABI plan：每个目标均已独立完成
GP/FP/stack 分配，Common **不能**复制或覆盖 psABI 寄存器算法。
真正的未收口对象是发射器内的 Core-first/C-fallback 适配层。

1. 新建 `target/common/core_abi_value_shape.h`，把 void、完整 Record、
   ILP32/LP64 pointer、bool/整数宽度和符号、i128、binary32/64、
   extended 的已封存 Core layout 校验为共同值形状。`_Bool`
   Core `bit_width=8`、ABI canonicalization 可以是 1-bit，这是两层
   不同语义；拒绝错误 size/bit-width、非完整 Record 与空上下文。
2. AArch64 的 `MinicA64ScalarShape` 成为 shared Core shape 的
   AAPCS64 机器投影，删除其 Source `MinicType` 分支；四个 Record
   包装也全部只消费 Core 聚合分类。sret `x8`、HFA、`x0/x1`、
   weak GOT 的机器差异未动。
3. LoongArch64 的 wide/FP/integer/pointer/Record 分类剔除 Source
   fallback，参数和返回值的整数宽度只读取 Core；八个 GAR/FAR、
   hidden `$a0`、stack vararg slot 与指令选取继续保留原样。
   **例外是必要的 Source/Target 合约：** LoongArch machine builtin
   按 TargetInfo 返回的 `MinicType` 签名检查 C 参数、返回类型，
   并核立即数约束，这不是可退休的 ABI 布局回退。
4. x86-64 的 SysV/MS x64 Call Plan、x87 helper 和间接调用沿用
   既有物理 ABI；机器边界去掉 C 类型比对、DataLayout sret 回退、
   aggregate result 回退，调试输出的类型标记也改读 Core。
   x87 80-bit/TFmode helper 替代、调用前堆栈空间与无 SSE 策略不动。
5. RV32 的参数/结果 `rv32_resolved_*` 转为 `rv32_core_*`；
   记录参数采用 Core Record 判定和实体身份核验。RV64 的进入参数
   浮点/宽标量、整数结果 canonicalization 与 Record 入参移除
   C fallback，并复用公共形状；LP64/LP64D、软硬浮点配置和
   `a0/a1` 双字传送仍由 RV64 plan/机器控制。

**证据：** `probe-core-call-coretypes.c` 添加 bool ABI、i128、
ILP32 pointer、错误整数宽度/FP layout、完整/未完整 Record、
缺上下文的独立负例，原有 poisoned C/Direct/Indirect/名义 Record
身份测试保持；边界静态检查锁住五个机器模块不再使用
`minic_type_is_*`/DataLayout，LoongArch builtin Source 签名作为
有名字的特例。新增可选 baseline 差分脚本，以一次 GNU `-E -P`
的 `.i` 同时输入新旧编译器：**24 份 × 五目标共 120 组，98 组
`.s` 逐字节相同、22 组两边均拒编、0 差异**；双方拒绝不算通过
运行。`make -j8 check-fast`、`check-minic-target-boundaries.sh`、
Debug 与全新 Release `-Werror` 构建均 PASS。`src/` 生产代码
**+342/−453，净 −111 行**，新增一个 91 行公共 header；
`tests/` **+177/−1**，含可重跑的 24×5 差分脚本。
当前环境缺部分 GNU 交叉汇编器、链接器和 QEMU；`.s` 等价
不等于五 ISA 真实 ELF/runtime 验收。

**下一批要连读而非局部补洞：** RV32 非 call 的 memory/integer/
wide emit 仍借 `rv32_resolved_*`，RV64 `runtime_libcall` 和
其余可执行核心机器类仍有 C descriptor 查询。应按整类值消费
路径收口并在五目标上横向比对；既有 Linux runtime/ABI Bug
暂时留在独立 MC 跟踪中，勿混入 Core 边界重构。

## IR-P6：Core 数值执行形状与 RV32 整族机器消费封口

起点：`edea518`（IR-P5）。先从 Core 的 operand/result/pointee、
ILP32 实参/返回值双字传输、RV32 wide/memory/structured ASM、RV64
strict ISA 的 saved-RA libcall 条件，沿五目标共同数值形状一直读到
实际机器发射，确认所有路径在生产态已有 `CoreTypeContext`。本批
**按执行语义整族迁移并退休同一族的旧 C 分支**，没有为减小 diff
保留一套无人负责的 `resolved` 适配层。

1. Common 数值形状的六个整数溢出、窄结果与 CLZ/CTZ 判定改为
   `CoreFunction + CoreInstruction` 唯一接口，删除
   `MinicC0Program`/`MinicTargetInfo` 参数与所有 C 整数宽度和类型
   fallback；x86-64、AArch64、LoongArch64、RV64 的调用点同步改签。
   当时 GNU overflow 对 enum 目的地的源语义禁入依赖 Core 上的
   `source_overflow_destination_ineligible` 标志；该历史设计已在
   IR-P30 退休，现由 Frontend/C lowering/source ingress 在 Core 发布前
   拒绝 enum，不能误把 enum 降到普通 i8 后重新放行。
2. RV32 `scalar`、`integer`、`wide`、`memory`、`emit` 六文件及其
   internal 合约一次性删去 `rv32_resolved_*`、C scalar/width/canon/
   load/store/is-wide 实现。统一只用 Core ID 判断 ILP32 一/二
   字寄存器、signed/unsigned 装载、布尔规范化、动态 `alloca`
   计数和结果类型、Record copy 布局、指针步长、宽算术 helper、
   `%zN` 结构化 ASM 与 SBI 固定寄存器类型；pointee 读取也要求
   真正的 4 字节 ILP32 Core pointer。源程序的符号来源与地址
   对齐策略未擅自迁入 Core，保留现有 `program` 入参。
3. RV32 frame 的失败诊断也停止读取 `return_type`、`object.type`
   这类旧 C 字段，改为只输出 Core type ID/布局；帧 ABI 和栈槽
   几何保持原样。RV64 strict ISA 的 i128 除法、余数与比较
   libcall saved-RA 预判改用共享 `core_abi_value_shape`，不再
   依赖 `instruction->type`，仍使用原 compiler-rt helper。
4. 新增 RV32 CoreType poison 单测（原生 + ASan/UBSan），验证
   signed 8/16/32、bool、u64 双字、float/double、限定指针、
   invalid pointer layout 与无类型上下文的 fail-closed，并核对
   实际 `lb`、`lbu`、canonicalizer 指令。Common 数值单测覆盖
   六个 overflow/bitscan 入口及 source-qualifier 禁入负例；
   静态边界门禁禁止 C fallback 重生。RV32 单测接入 `check-fast`。

**对照与构建证据：** 用旧 `edea518` 编译器和本批编译器消费同
一份 GNU `-E -P` 输出。20 个执行类型 fixture × 五目标共 100
组：**91 组 `.s` 字节相同、9 组双方拒绝、0 差异**；并重跑前批
24 × 五目标 CALL ABI 差分：**98 相同、22 双方拒绝、0 差异**。
不同的诊断不算同字节行为，双方拒绝也不计成运行 PASS。
Debug 构建、全新 Release `-Werror`、`make check-fast`、
`check-minic-target-boundaries.sh`、原生与 ASan/UBSan 单测分别
PASS。生产 `src/` **+334/−659、净 −325 行（14 文件）**；
删除旧路径是这次的主要结果，保留五目标物理 ABI 与 runtime
选型，并未宣称 GNU 交叉 AS/LD 和 QEMU 已覆盖。

**下一大批的真实依赖：** `direct_instruction_semantics`、
`core_object_storage`/`lp64_scalar` 中还存在 standalone C fallback；
若要让 Module→Target 最终退出 `MinicC0Program`，必须先把
AArch64/LoongArch64 Linux inline ASM 的源资格证明和 typed operand
效果封存进 Core，之后再一次性迁移 common visitor、backend、
五目标 module writer 及旧测试适配器。当前 `MinicCoreModule` 已
拥有全局片段、symbol/section/alias/file ASM，不另造第二套 IR。
Linux NEON、RV32 Zstd 与 KVM 等机器/runtime 失败仍分开跟踪，
不能用边界检查通过冒充五 ISA runtime 全绿。

## IR-P7：Core 对象/栈槽全链所有权与 Direct LP64/RV64 机器回退清除

起点：`d835281`（IR-P6）。沿五目标机器消费、Common frame
所有权、LP64 aggregate/return ABI、RV64 Core admission/inline ASM
及 LoongArch64 整数发射调用链一起读完，再删除旧入口并迁移全部
生产消费者。这批不是单独换接口签名：**对象布局、interval、复用、
SSA spill、FunctionPlan** 的同一条链、**Direct LP64 调用/返回**
及 **RV64 执行类型** 各自完成 producer→consumer 闭包。

1. Common `core_object_storage`、`core_object_intervals`、
   `core_object_reuse`、`core_spill_layout`、`core_function_plan` 一律
   从 `MinicCoreFunction.type_context` 和 CoreTypeId 读取物理尺寸、
   对齐、元素数与 SSA 寿命；删除 `MinicC0Program`、C DataLayout
   和 `minic_type_*` 备用分支。五目标各自的 frame 调用点、
   shared borrow/own visitor 和已有 standalone 测试同时改签。
   ILP32 4 字节、LP64 8 字节公共布局不混淆；ABI 的栈增长方向、
   保存寄存器和宽数值 sidecar 仍留在目标。
2. Direct LP64 的 `direct_call_plan`、`direct_function_profile`、
   `direct_function_plan`、`direct_preflight`、`direct_function_emit`
   去掉已死的 C ABI Record 回调和调用/返回类型 fallback，
   保留 AArch64 AAPCS64 与 LoongArch64 LP64D 自己的 Core ABI
   分类器及实际寄存器分配。删除 Common `lp64_scalar` 与
   `lp64_aggregate` 整组旧 C record/整数查询，AArch64 旧 C
   indirect-record walker 同时删除。x86 的 Source ASM ABI 查询
   留在本机 `abi.c`，不再反向污染 Common Core 分类接口。
3. `direct_instruction_semantics` 的执行类型、指针 pointee、
   signedness、i128、浮点和结果形状只读 Core；AArch64/LoongArch64
   两个发射端整族迁签。尤其 LoongArch64 `integer_emit` 删除
   i128、width、pointer、类型相等及 i32/u32 混合溢出操作的
   C 回退，原 signed/unsigned 比较、128 位右移及宽加 carry
   的目标指令策略保持不变。
4. RV64 `memory_emit`、`core_codegen`、`codegen_support`、
   `core_admission`、`inline_asm_emit`、`record_snapshot`、
   `legalize` 同批退休旧 C 执行类型推断：标量访存与返回、
   固定寄存器、Record 类型身份、结构化 ASM 操作数宽度以及
   LP64 单寄存器约束都必须由 Core 证明；畸形 4B 指针、
   16B 单寄存器值和缺失 type context 均拒绝。独立 C Source
   ABI 分类器仍有公共测试依赖，继续保留，不误删源语义入口。

**验证：** 新 `probe-core-frame-type-ownership.c` 和
`loongarch64_core_integer_emit.c`，补充 RV64 Core admission 的
malformed 负例；Common 对象/ABI/数值原生与 ASan/UBSan 单测、
`make check-fast`、目标边界脚本、Debug 与 Release 构建、
RV64 LP64D union ABI 专项均 PASS。边界脚本新增 Common 对象
整族、Direct LP64 Core 分类及 RV64/Loong 整族禁 C 回退断言。
以 P6 编译器与本批编译器消费相同的 GNU `-E -P` 文件：
31 个执行、对象、ASM fixture × 五目标共 **155 组**，其中
**129 字节相同、26 双方拒绝、0 差异**；既有 24 个调用 ABI
fixture × 五目标 **98 相同、22 双方拒绝、0 差异**。双方拒绝
不等于正确执行；当前没有完成五 ISA GNU 交叉 ELF/QEMU 回归。
`src/` 生产代码 **+602/−1577，净 −975 行（54 文件）**。

**下一所有权边界：** `MinicCoreModule` 已封存全球数据、符号、
section/alias/file ASM，真正阻止旧五目标 Module writer ABI
退出 `MinicC0Program` 的，是 AArch64 大量 Linux GNU ASM
Source qualifier 在机器端重新鉴定源类型、EOR3 原始模板，以及
LoongArch64 SIMD 原始模板与 Core 规范模板效果等价的二次验证。
应先由 Target SourcePolicy 将明确的 machine form/variant/效果
证明封存进 Core，再一次性迁移 `backend`、`program_emit`、
五目标 writer 与事务测试；**不能只凭 `source_inline_asm_id`
绕过 fail-closed 检查**，也不应复制另一套 ModuleIR。

## IR-P8：五目标机器消费横向闭包与规范化 ASM 机器资格封存

起点：`8fd6353`（IR-P7）。P7 虽然关闭 Common 栈槽、Direct LP64
和 RV64 各自的 C 类型回退，却没有按同一执行类型所有权把 x86、
RV32、RV64、AArch64、LoongArch64 **一起**收干净。本批重读五目标
`Source→Core→ABI/Frame→Machine` 生产者、调用点和失败条件后，
以贯穿五目标的 Core 执行事实为同一个提交边界，而不是逐目标
发五个小提交。

1. x86-64 `machine_emit` 一整个执行族转为 Core-only：mixed
   signed/unsigned overflow、i128/FP/参数/返回值、普通/原子内存、
   Record/向量复制、动态栈以及 machine terminator。删除
   `machine_support` 内无用的 C normalization 和终结符旧 Program
   参数，机器 opcode、SysV ABI、`long double`、GNU Linux
   `.head.text` 与 ELF 符号抢占策略保持目标私有。
2. RV32/RV64 沿 FunctionABI→Frame→CallPlan→CALL emitter→Target
   admission 迁移整个消费者链。RV32 的 incoming ABI/frame 不再
   假借 C Program；RV64 的 sret、参数 cursor、frame offset
   和 CALL/RETURN 接受已准备的 Core ABI plan。历史上通过
   `program != NULL` 判定生产严格模式的 sentinel 改为显式
   `require_prepared_abi`，standalone 与 production 的原有
   fail-closed 差异保留，不擅自改变 LP64/LP64D 分配。
3. AArch64/LoongArch64 与 x86 共用的 Direct Core 形状 API
   删除四处仅检查非空的 C Program 参数；两个 Direct 目标的
   RecordCopy/RecordLoad/固定寄存器调用点及 x86 读取同步改签。
   可选 `backend_core_usage` 诊断也从旧 C width/函数声明改为
   Module symbol 与 CoreType，避免生产之外的诊断成为反向 C 依赖。
4. 为 **Target→Core 的 ASM 后置证明**加唯一公共入口：目标在
   原始 GNU 来源完成资格检查后，看到 Core 规范化的数字模板，
   才可标记 `target_machine_form_id/variant`；公共 wrapper 禁止
   回调篡改原先已鉴定的 operands/ties/memory effects。
   LoongArch64 LSX/LASX SIMD 整族在封存前逐指令核对两个版本的
   opcode、寄存器、偏移与占位符映射；机器端只消费 form=20、
   Core 8B pointer、operand roles/内存效果并严格解析规范模板，
   不再拿 `source_inline_asm_id` 回读 C Program 做第二次鉴定。

**验证：** 原有 LSX/LASX Linux 正例、四种 opcode/register/
immediate 负例、128 个重复来源干扰样本通过；新增独立
`probe-loong-simd-normalized-seal.c` 覆盖 named/numeric +m、
伪造寄存器、追加指令、篡改 operand_count 的恶意目标回调。
边界检查增加 x86、Common Direct、诊断 Core-only 与 Loong
post-normalization proof 防回退门槛。严格 Release `-Werror`、
Debug、`make check-fast`、RV64 ABI/Core admission、Loong SIMD
及 boundary 均 PASS。用 P7 基线与当前构建消费完全相同的
GNU 预处理输入：**51 份执行/Frame/ASM fixture × 五目标
255 组，188 `.s` 字节相同、67 双方一致拒绝、0 差异**；
调用 ABI 24×5 **98 相同、22 双方拒绝、0 差异**。
`src/` 生产修改 **41 文件 +531/−418，净 +113 行**。
新增验证逻辑不代表已经完成五 ISA GNU AS/LD 或 QEMU。

**尚不能强行删除的接口：** x86 `machine_asm.c` 的 Linux GNU
约束/C rank、AArch64 多个未封存 form 的 ASM/EOR3、Loong
非 SIMD 标量 ASM、RV 的源地址对齐/structured ASM 仍依赖
源资格事实。`backend.emit_program` 和五目标 Module writer 的
Program 双轨虽已被生产 Module 路径取代，但若在这些证明尚未
封存前直接去掉 Program，会把来源 ID/哈希误当 ISA 放行证据。
下一完整批次应先对这些剩余 ASM 族一并形成 Target-owned
machine-form proof，再一次性删除五目标 writer、Common
`program_emit`/`program_visit` 的旧 C 分支并把事务负例全部改为
Module-only；不在每个架构各留一套半迁移的 writer。

## IR-P9：五目标完整机器类型/ASM 资格闭包与生产 Module 单入口

起点：`d2597b844`（IR-P8）；不按后端一个个发小修补。本轮先重读
编译 Driver、Common TU 事务/Prepared Function visitor、五目标
`Source→Core→ABI/Frame→Machine` 调用链，发现还混着三类不同事实：
编译源类型与 GNU 约束、Core 可执行类型/对齐、ELF TU 符号联编。
因此同时迁移五架构对应消费者，并对确实需要 Source 资格的
ISA 家族建立规范化后机器证明，不能把 provenance ID 误作许可。

1. **公共生产单入口及 ELF 六种证明。** `backend` 去掉无人调用的
   `emit_program` 回调、旧 `minic_backend_emit_assembly` 入口；
   五目标生产统一注册 `emit_module`，失败绝不回退 C globals。
   `target_info` 以 `CoreModule` 的封存 linkage/visibility 实现
   object/function 各自的 TU-local、nonpreemptible、undefined-weak
   六种精确判定，空/未 finalized 的 Module fail closed。Direct LP64
   公共 BlockContext 正式携带只读 Module；x86、RV32、RV64、
   AArch64、LoongArch64 的 PIC/GOT/弱符号路径一并接入。
   `program_emit` 的 Module symbol-use/weak-hidden 决定现可**完全
   不提供 C Program**独立运作；已新增 20-case ELF、Poison C
   与原有五失败边输出事务测试。原白盒 Program 兼容入口仅限
   target 底层测试，不再可由 Backend Driver 选择。
2. **x86-64 一次转换现有全部 40 组 typed ASM dispatcher 家族。**
   原子 RMW/XCHG/CMPXCHG/TRY-CAS、bitops/PORT_IO、MMIO、
   XSETBV/FXSAVE/MXCSR、segment/FSGSBASE、boot/低级控制、
   delay、IPv6、Zstd CPUID 等在 Source 保留 exact GNU C rank
   和约束审查，Machine 只验证 typed machine form/variant、Core
   pointee/宽度/对齐、模板和物理操作数。`frame.module` 负责
   PIC nonpreemptible/undefined-weak；保留尚未显式封存的
   `target_machine_form_id=NONE` Linux exception/alt/percpu/nospec
   族原有严格 Source 验证，不伪造安全性。
3. **RV32/RV64 整条地址计算/ASM/ELF 链同时迁移。**
   `memory→overflow→structured inline ASM` 的地址对齐只读
   `CoreTypeContext` 与已封存对象 alignment；RV32 ILP32 及
   RV64I ISA 检查保留。两架构 `GLOBAL_ADDRESS`/`FUNCTION_ADDRESS`
   以 Module 确认 PIC GOT 与 lla/lla-local 的条件，RV64 ABI
   frame/admission 摆脱借 Program 非空判定严格模式的历史哨兵。
   修正 RV64 文件级 ASM 在实际切节后必须恢复 `.text`；仅
   `file_asm_count > 0` 才输出恢复指令，防止污染所有 TU 的
   汇编字节稳定性。
4. **AArch64/LoongArch64 整族 ASM 资格封存。** AArch64
   LDAR/STLR/raw MMIO read/write 各 1/2/4/8 字节共 16 个
   来源→规范机器形式，hypspin/uaccess/crash/RCU 原 C Record
   布局校验转 Core；没有 Core 生产者的 `VECTOR128_EOR3`
   死 opcode/SSA operands/NEON machine shim 一次从 Core、Target、
   分类器与测试清净，保留真实可执行 NEON Load/Store/XOR。
   LoongArch64 进一步把 non-SIMD 标量 DBAR、LL/SC、uaccess、
   percpu AMO、CSR、AMO store/fetch 统一封存 form 21–28；
   normalized template 逐条证明 operand/异常 fixup 等效，
   Machine 不再访问 `ctx->program`；800B 大 Record 的 C 名称
   仍须 Source 准入验证，其物理布局由 Core 复核。

**本批独立回归：** `make -s -j8 check-fast`、
`check-minic-target-boundaries.sh`、新 AArch64 form 16 正例/28
恶意反例、新 Loong 标量 8 族/源 C rank 反例、20-case Module
ELF linker 证明、Poison C/事务回滚均 PASS。x86 GNU 原生
RAW_CMPXCHG 36,000、RMW 6,656、TRYCAS 1,800 个差分等
PASS。用同一份 GNU 预处理 `.i` 对照 P8 的原编译器：
**执行/ASM 51×5=255（188 字节相同、67 同拒、0 差异），
ABI/CALL 24×5=120（98 字节相同、22 同拒、0 差异）**。
RV32/RV64、AArch64、LoongArch64 真实交叉 GNU AS/LD/QEMU
本地缺相关交叉工具，**未宣称 Linux 运行时五架构通过**。
本批一提交共 **59 文件、约 +1690/−807 行**；其中生产 `src/`
**48 文件、+1111/−739 行（净 +372）**，新造的正反例和
测试迁移约占另 9 文件，另有构建入口与本文档各一。

**剩余入口与后继边界：** 完整 Module-only *机器代码生成函数*
尚未完成：为单独白盒测试保留的目标 `Program` wrappers、
Common prepared-function callback 的 C decl 和 x86 `NONE`
型 Linux ASM 没有全部封存；Driver 虽已唯一选择 Module，
仍暂借只读 Program 供未迁移的 SourcePolicy 前置资格。
后续需整体为 x86 剩余 NONE 族补 Source→normalized 的
machine-form/operand/fixup 证明，再关闭五目标 function emitter
及 Common visitor 的最后 C 参数。**不要在没有证明时直接
把 `source_inline_asm_id` 当作机器准入。**

## IR-P10 — Cross-target Core ABI and Frame owners (2026-11-03)

Read as one chain: x86 SysV/Win64 incoming/outgoing Core CALL plans, Frame and standalone machine CALL fallback; AArch64/LoongArch shared Direct LP64 FunctionPlan and frame storage; RV32/RV64 frame and ABI owners (already Core-only). Remove C Program and unused TargetInfo from x86 ABI placement and the shared LP64 frame plan; propagate interfaces all the way to x86 machine CALL and Direct function emission, deleting null-only source dependencies instead of masking them. The Direct LP64 preflight still borrows C declarations for source-ASM checks. Final five-writer Module-only boundary is not yet claimed; GNU/QEMU runtime correctness remains separate from source/boundary checks.

## IR-P11 — Five-target sealed Module function declaration boundary (2026-11-03)

The common prepared visitor now withholds `MinicFunction*` from machine callbacks whenever the final ModuleIR owns the function symbol. AArch64 and LoongArch's shared Direct LP64 preflight accepts a sealed Module function even with no source declaration; RV32/RV64/x86 machine callback paths select the Module symbol without fallback. Four writer entrypoints no longer demand that sealed Module global count equal the mutable source Program global count (the fifth target shares the Direct writer). Output transaction tests deliberately contradict the source declaration while keeping the sealed Module definition, and verify the function is emitted using Module facts. Standalone source-only callers retain their original behavior. The remaining source `Program` for unsealed GNU ASM and target-specific legacy machine consumers is not declared removed.

## IR-P12 — Cross-suite stale backend consumer closed (2026-11-03)

The runtime four-architecture target-catalog probe still attempted to compile against the removed `MinicBackend.emit_program` field and failed before reaching any runtime validation. Update the only stale catalog consumer to `emit_module` and harden the cross-target boundary test to reject reintroduction of the old registration. This is source-level closure, not a GNU/QEMU runtime pass; verify in GitHub's actual four-architecture CI gate.

## IR-P13 — Standalone sealed Module output transaction and Core visitor (2026-11-03)

The shared prepared-function visitor no longer dereferences C declarations or C function count when a finalized CoreModule is present, and the shared staged output transaction no longer requires a C Program to serialize sealed globals, weak/hidden binding, relocation uses, file ASM, and Core definition visits. Product target writers may still borrow C Program for outstanding GNU/source policy; this milestone is intentionally the common transport/transaction seam only. Whitebox probes run with both poisoned source Program and NULL source Program, including mismatched declaration definedness and rollback. Output is not considered accepted without actual CI.

## IR-P14 — Published Core source-type disposal and native builtin Core-call contract (2026-11-03)

- Traced the parser's exact LoongArch builtin C signature, Core callee construction, type snapshot, call verifier and native machine emitter as one producer→consumer chain. The source policy still controls target builtin signatures and the machine still checks mnemonic/immediate/register constraints.
- Successful source-to-Core publication now frees owned source-only parameter arrays and enum compatibility tables, poisoning inline C descriptors. Import failures keep their source draft intact for rollback/retry. The physical fields and builder API remain a pending structural migration; this does NOT claim C has been removed from `core_ir.h`.
- The LoongArch machine CALL path now requests only machine-form facts and checks sealed Core return/argument type IDs against the callee's checked Core call signature. The source-only `MinicType` signature is not read by the emitter.
- Strict Core type snapshot tests and machine boundary gates verify the publication contract. GNU/QEMU/runtime remain independent acceptance gates.

### IR-P14 correction — do not publish an invalid machine-ASM contract (2026-11-03)

The attempted release/poison of source C descriptors at `snapshot_types` revealed unconverted machine consumers. AArch64's and LoongArch64's structured GNU ASM contract emitters still use source `fn->values[*].type` for exact C admission, and some target emitters still use source `ins->type`. This created *new* four-architecture runtime-gate failures despite a green common boundary gate. Revert the premature source-descriptor destruction, retain the source C state during this transitional boundary, and keep the independent LoongArch machine-builtin CoreTypeId migration. Next work must rewrite/relocate the full ASM source qualification families, seal approved effects in Core and only THEN remove C draft storage. No false claim of C-free Core or successful acceptance is permitted.

## IR-P15 — Three-ISA machine ASM Core kind closure (2026-11-03)

Read x86-64, AArch64 and LoongArch64 ASM machine contracts as one semantic family. A pointer/integer/void *execution-kind* query on a sealed Core SSA operand is now owned by Core and type-checked by the CoreType context, never the copied C type. Preserve distinct source-qualified GNU operand restrictions, exact C integer rank, and target physical register forms until those source checks are sealed: Core kind is not a C compatibility predicate or psABI classifier. The remaining strict C checks are a pending deliberate conversion, not permission to discard the entire source draft prematurely.

## IR-P16 — Core Value single-owner source descriptor and five-target publication lifetime (2026-11-03)

Follow the source producer through source-ingress validation, TypeId import,
CoreType verification, and the machine consumers before retiring C state.
`CoreValue.type` duplicated the defining `CoreInstruction.type` for every SSA
value; all C lowering families now read the *single* defining instruction via
the private, bounds-checked source-only query. Remove the duplicate field from
the published Core Value schema, including its append, enum-capture and
snapshot paths. This preserves C `long` vs `long long`, enum and qualifier
identity before snapshot instead of collapsing C semantic checks into equal
Core machine widths. A sealed Core function cannot use the source query.

The previous IR-P14 post-snapshot C-type disposal was reverted because ASM
machine consumers still used those descriptors. After IR-P15's three-ISA
machine migration, reinstate disposal **only after both Core verifiers pass**:
free source-owned signature/enum arrays and poison remaining C descriptors;
snapshot failure preserves the entire draft for retry. RV32/RV64 also delete
their last unsealed C `Program`/`DataLayout` root-alignment fallback from the
shared typed-memory adapter; eleven callers now use only sealed Core type
layout and return a conservative alignment of one without a valid Core proof.
Positive root-alignment and poisoned-C regression tests cover alignment,
offset GCD, unknown globals, malformed type and absent context.

This is **not** yet the complete structural removal of C from Core IR:
`CoreInstruction`, objects, globals, function/call signatures and fixed-register
source ingress still retain C descriptors until their source-only temporary
staging owners and APIs are migrated together. Source ASM C rank/qualifier
admission remains with C SourcePolicy; it must not be replaced by a machine
CoreType equality check. Linux runtime and cross-GNU/QEMU acceptance are
separate gates, not implied by source-level Core/ASM tests.

**Evidence/size:** 42 changed files in one coherent batch; production
26 files +183/−133, tests 13 files +168/−35, Makefile +10/−2, plus two
documentation files. All 73 direct and four alias CoreValue C-type lowering reads were
retired; `CoreValue.type` removed entirely, and the RISC-V alignment adapter's
C fallback and all eleven callers converted. `make -s MODE=debug check-fast`
exited **0**, including Core source/strict verifier, ASan/UBSan, RV32 scalar,
RV32/RV64 root alignment, AArch64/x86 machine poisoned-C and Loong descriptor
tests. `make -j8 build/debug/bin/minic-cc` and Core enum-origin tests also
passed. Cross-architecture GNU/QEMU absences remain `SKIP`/`NOT_RUN`; this is
not five-architecture Linux boot/runtime certification.

## IR-P17 — C-free published Core schema and transient SourceDraft owner (2026-11-03)

Read this migration across the complete producer-to-consumer lifecycle:
C semantic lowering → type-preserving source construction → one-shot CoreType
import → Core verification → machine consumers → destroy. The public
`core/core_ir.h` no longer imports `frontend/type.h` and contains no
`MinicType`, `MinicEnumId` or `MinicFunctionTypeId` fields or source-only
entrypoint signatures. The explicit `core/core_ir_source.h` houses the temporary
C admission API and one per-function `MinicCoreSourceDraft`, indexed alongside
Core's stable object, global, fixed-register, callee, call-signature and
instruction IDs. Core values still derive their original source type from
their one defining instruction; they do not copy a second C descriptor.

All existing `core_lower*` producers, including GNU ASM operand clones,
pass exact C source types separately into source-only append APIs. The
published instruction/object/signature/function structures carry only
sealed Core type identities and execution metadata. Preserve C's exact
integer rank, enum/record identity, direct symbol signature consistency,
indirect `FunctionTypeId`, and definition-local parameter normalization
**before** import; Core representation equality is not C type compatibility.
Both Core and SourceDraft arrays reserve storage before publishing a shared
index. A failed import clears partial Core type IDs and leaves the draft
available for retry; only success through both Core verifiers frees the entire
draft. Independent Core producers can provide typed instructions without
constructing a source draft.

This closes the **published function C-type schema boundary**, not the
remaining GNU SourcePolicy qualification or the broader source-Program
dependency in some Target machine admission paths. It introduces neither
Machine IR nor a replacement Core IR. Cross-GNU QEMU and five-ISA Linux
boots remain independent acceptance gates and are not inferred here.

**Verification:** `make -s MODE=debug -j8 check-fast` exited zero;
`make -s MODE=release -j8 build/release/bin/minic-cc` also passed.
Core Type/Import/Snapshot passed native and ASan/UBSan; Core IR ingress
shapes passed 12/12 valid/negative cases in both modes. A clean Core-only
instruction construction test preserves pre-typed Core IDs. Five-target
source/ABI/ASM boundary tests, RISC-V layout and native GNU-linked checks
ran within `check-fast`; unavailable cross-GNU/QEMU runs remain explicit SKIP.
**Scope:** 43 files including 23 production `src/` files, 18 test files,
the Makefile and this ledger; approximately +1,600/−1,260 lines.

### IR-P17 exact-head CI correction — source instruction tail rewind

The strict native gate exposed one C-free source-ingress regression that the
ordinary check-fast suite did not exercise. GNU rK register-output ASM may
remove an exact dead assignment tail, decrementing Core instruction, block and
value counts and immediately reusing the freed IDs. The first SourceDraft
version omitted the matching instruction-type count rewind, so the next
source append rejected `source_count=8/core_count=0`. Restore the same
transaction boundary: require aligned counts before an eligible rewind and
decrement SourceDraft's instruction-type count with the Core counts. Preserve
the original exact suffix/SSA and same-local proof; do not relax ASM admission.
Release `minic-cc`, the RV32 target gate (including `rv32_irq_save_legacy`)
and the 2026-10-29 five-ISA review batch all pass after the correction. The
workflow's unrelated AArch64 ACLE Stage3 unsupported GCC pragma and hosted
KVM CPUID VMX blocker remain separate from this fix.

### IR-P17 strict CI fixture correction — GNU extern-inline declaration-only

The next strict native step had a pre-existing contradictory test: its only
GNU `extern inline` declaration legally emits an existing but zero-byte `.s`,
yet the fixture required a nonempty file. Both the pre-migration compiler and
the new SourceDraft compiler produce the same successful zero-byte output.
Check file existence instead of nonemptiness, retaining the stronger negative
assertion that no strong `prefix_extern_inline_identity` symbol appears and the
unrelated invalid `no_sanitize` source must be rejected. The complete native
step's function-metadata, fixed-register, five-target inline ASM, empty ASM
and constant-switch regressions pass after the test-only correction.

## IR-P18 — Separate pure Core construction from C source ingress (2026-11-03)

The published Core schema was made C-type-free in P17, but its implementation
still mixed source-only `MinicType` admission, enum identity, source signatures,
SourceDraft disposal and one-shot import with pure SSA/CFG construction. Read
the complete ingress, append, snapshot, failure-retry and destruction chains
before moving them together into `core_ir_source.c`. Keep `core_ir.c` and its
verifier independently buildable without the frontend type implementation.
The C adapter registers a temporary draft cleanup callback; successful type
publication and ordinary destruction both release it exactly once. The pure
instruction append APIs no longer alter a caller's pretyped Core IDs based on
the presence of a source draft: only explicit source append wrappers create
invalid pre-snapshot type placeholders. Shared array growth and string copying
retain a single C-free implementation.
The C-only private header is now `core_ir_source_internal.h`; the old mixed
`core_ir_internal.h` is removed and the independent Core verifier imports
only the public C-free Core schema. Existing source-ingress consumers are
migrated together, including standalone test and bootstrap compile lists.

**Verification:** Debug `make -s MODE=debug -j8 check-fast`, Release
`minic-cc` build, strict target-boundary script and `git diff --check` all
PASS. Core type snapshot/import and Core shape native plus ASan/UBSan tests
PASS; standalone Core literal-branch folding and RV root-alignment binaries
compile and run without linking any frontend type implementation.

This is a source-ownership boundary, not a new IR layer or runtime/ISA feature.
GNU operand source qualification remains before publication. The five machine
writers' remaining borrowed `MinicC0Program` is a **separate**, later
five-architecture migration, and neither this split nor a green native gate
implies five-architecture Linux boot certification.

## IR-P19 — One sealed Module-only machine output boundary across five ISAs (2026-11-03)

Read the full C source-to-machine chain before changing the target handoff:
driver/TargetInfo → final ModuleIR linkage and file-ASM snapshot → shared
function/global visitor and Core facts/storage plan → target admission and
machine instructions → staged assembly transaction. Remove the old Program
parameter and source fallback **together** from the production backend API,
the five writer entrypoints, common global/function visitation, PIC and ELF
undefined-symbol analysis, and x86-64/AArch64/LoongArch64 physical emitters.
There is no Core/Target writer path that can reinterpret a C declaration when
the sealed Module disagrees; a missing or nonfinalized Module rejects output.
The common transaction still preserves the last successful output on failure,
including a target finalizer or census scan failure.

The machine side now receives only ModuleIR, Core functions, prepared facts,
target feature/ABI policy and explicit callbacks. RISC-V file-ASM strict-I
markers and native ISA admission remain target-owned. This phase changes no
Core instruction representation and adds no Machine IR. The separate GNU ASM
*source qualification* stage still correctly handles exact C type ranks and
operands before publication. Historical standalone legacy source-C ABI/NEON
fixtures remain separate cleanup work; their presence must not be mistaken
for an active production C Program machine-output fallback.

**Verification:** Debug compiler and assembler build succeeded, strict
`check-minic-target-boundaries.sh` passed, and `make -s MODE=debug -j8
check-fast` exited zero (including staged writer rollback and five-target
linkage/ABI/ASM probes). Check-fast is native/source-level evidence only;
five-architecture Linux GNU-link and QEMU runtime acceptance remain NOT RUN
for this refactor. Record independent acceptance before declaring a Linux pass.

## IR-P20 — Retire parallel C machine emitters and name the Module owner (2026-11-03)

Read all five writers through `MinicBackend.emit_module`, the common
function/global visitation, RISC-V relocation and PIC symbol decisions, and
the independent ABI/NEON tests. P19 sealed the production Module-only API;
P20 removes the *unused alternative implementations* instead of preserving
two competing models indefinitely:

- **RV32/RV64 and Common globals:** delete the old `MinicC0Program`/FILE
  global serialization, C relocation walkers, and compatibility ELF alias
  wrappers together. Keep the active `MinicCoreModule` fragment, relocation,
  symbol and section writers. Drop the no-call C aggregate storage-shape
  helper; retain the Core nominal-record shape calculation.
- **x86-64:** delete unused C AST/type ABI classification, record return
  adaptation, and duplicate C global output. Keep the CoreType SysV ABI
  classifier and Module global writer. The native Core ABI shape probe still
  tests one/two eightbytes, unaligned aggregates and hidden results.
- **RV64:** remove C Program-dependent standalone function emission,
  duplicate C ABI cursor/placement logic and dead C function-symbol aliases.
  The production emitter requires a finalized Module when PIC is enabled;
  Core-only standalone fixtures cannot pretend to know ELF preemption.
  Rewrite the ABI fixture against a constructed CoreTypeContext and remove
  its unused frontend/DataLayout link dependencies.
- **AArch64:** remove legacy C-type NEON selection wrappers. Test sealed
  Core vector128 machine operations and malformed-shape failures directly.
  Qualifier stripping for the machine vector shape remains distinct from C
  source inline-ASM type qualification.
- **Shared names and wiring:** rename five ISA writer entrypoints to
  `write_core_module`, the direct writer to `direct_write_module`, and its
  configuration to `DirectModuleConfig`; update backend dispatch and strict
  ownership checks together. These are renames, not an added adapter layer.

Do **not** remove source C types in GNU inline-ASM constraint qualification:
those interfaces operate before Core publication and must still apply C
rank, lvalue and operand compatibility rules. The target-machine paths no
longer expose the retired C global/ABI/function-output alternatives.

**Verification:** strict backend boundary and native AArch64 Core NEON
contract tests PASS; RV64 Core ABI fixture PASS without frontend linkage;
debug `make check-fast` PASS. This is compiler cleanup and native/source-
level regression evidence, **not** a claim that five Linux images boot under
QEMU. Cross-toolchain GNU assembly/link and five-architecture runtime
certification remain outside this refactor's execution evidence.

## IR-P21 — Read-only five-target ownership audit (2026-11-03)

Re-read the entire parse/inline specialization/Core lowering/reachability/
Module sealing/machine emit chain alongside each ISA writer. The remaining
machine paths are all Core + finalized Module; the two function-pruning passes
are distinct stages in a function/static-global/relocation-root fixed point,
not duplicate emitters. Identify three unconsumed Core/Module C metadata
copies, a missing CoreFunction/Module type-arena identity check, and target
GNU ASM source qualifiers interleaved with physical emitters. Also identify
the LoongArch64 builtin machine-form query that reconstructs C types merely
to learn a machine mnemonic. This phase was strictly read-only.

## IR-P22 — Seal type-arena ownership and separate Source ASM from machine ISA (2026-11-03)

Address the coupled classes from the P21 inventory across the actual owning
layers, without adding a new IR, cloning opcode lists or altering ABI policy:

- **Core/Module publication:** remove the unused Core record anonymous-member
  marker, Module global `force_emit` copy and Module function `is_noreturn`
  copy. Keep the actively consumed C source fields. All five machine writers
  now share a fail-closed Backend admission check: a live Core definition's
  type arena must be *identical* to the sealed Module's arena, and it must not
  retain an unpublished SourceDraft. Declarations and pruned internal slots
  remain legal without a lowered CoreFunction. The new real-writer unit gate
  verifies both admitted output and mismatched-arena/no-partial-output rejects.
- **Source/Target GNU ASM ownership:** x86 KVM MOVDQA C vector-type
  qualification moves out of `raid6_simd.c` into x86 Source ASM admission;
  the shared machine file stays Core/ISA-only. LoongArch64 LSX/LASX likewise
  separates C operand admission and post-normalization seal into
  `simd_source_policy.c`, retaining the one exact machine-template parser and
  Core machine emitter in `simd_asm.c`. Source and machine share the parsed
  instruction effects, not duplicated recognizers. The LP64 Linux static-key
  source qualifier moves to `linux_jump_label_source.h`, while the Core side
  retains a C-free template/relocation contract.
- **Loong machine builtins:** source GNU aliases/signatures and native CALL
  form lookup project from the same 24-entry target descriptor. The native
  emitter queries the sealed unspellable Core callee directly, never rebuilds
  the source GNU builtin name or constructs and discards a `MinicType`. Keep
  Core call signature, GPR ABI and immediate validation. Add alias, invalid
  internal identity and cross-target negative checks.
- **Header boundaries:** `DataLayout` and `Backend` machine headers expose
  only C AST *opaque* declaration handles when necessary; target Core emit
  headers across RV32, RV64, x86-64, AArch64, LoongArch64 and Common stop
  importing the full C AST transitively. Move the shared call-frame-address
  semantic enum into `common` and give source `.c` files explicit AST imports.
  A standalone include-only machine-header gate ensures this separation
  cannot regress. Remove six function-name-specific `GENL_*` diagnostic
  prints; preserve CFG/constant-fact behavior and supported error tracing.

**Verification:** `make MODE=debug -j8 check-fast` PASS (including CoreType
snapshot, Module emission, x86 typed ASM, AArch64 and LoongArch64 normalized
seal, Source/Target descriptor tests); `make MODE=release CFLAGS=-Werror -j8
build/release/bin/minic-cc` PASS; dedicated Backend type-arena negative test
PASS; standalone machine-header AST isolation PASS. Target-specific host/native
SIMD and GNU source tests exercised the unchanged acceptance/rejection
contracts. Cross-GNU/QEMU full Linux five-architecture runtime qualification
was **not** performed; these native gates do not prove Linux boot.

## IR-P23 — Separate C target semantics, seal vector representation, isolate machine text passes (2026-11-03)

Read the whole source/target admission → CoreType import and vector lowering →
five-target machine emission → LoongArch64 native text postprocessing path.
GCC's GENERIC/GIMPLE versus RTL and LLVM's IR versus TargetLowering provide
the reference for *ownership*, not a demand to clone either compiler's full
intermediate representations. This batch keeps the existing Core/Module IR,
the same target registries, and unchanged supported ISA/ABI contracts.

- **Target C semantics:** move integer rank, promotion, literal typing,
  `sizeof`, `char`/`wchar_t`, `va_list` source identities, invocation-local
  C options, GNU attributes/pragma/inline-ASM source constraints and builtin
  source signatures together from `target_info.c` to
  `source_policy/language_semantics.c`. The original public API remains
  stable. `TargetInfo` owns the five target descriptors, DataLayout pointers,
  machine capability/ABI facts and finalized Module ELF symbol queries;
  it no longer includes or dereferences the C AST.
- **Frontend declaration roles:** replace arch-named vector type helpers
  with generic source builtin declaration-role and fixed-vector storage
  queries. Parser and AST verifier share one exact unqualified-operand
  identity check for the admitted typed vector XOR. The target SourcePolicy
  still admits the known AArch64 spellings; this does not authorize GNU
  vector ALU, other targets' NEON, or a vector argument/return ABI.
- **Core vector representation:** replace the source-language-derived
  `vector.is_builtin` flag with explicit `STORAGE_ONLY` or `REGISTER_VALUE`
  Core representation, and generalize the storage-copy opcode to
  `VECTOR_STORAGE_COPY`. Import, intern, lower, verify, value census, dump,
  x86 storage copy, AArch64 NEON and shared value slot consumers use the
  same sealed Core property. Machine code rejects unsupported Core shape
  instead of re-querying C type declaration permission.
- **Loong machine text ownership:** extract both existing postprocessing
  passes out of `loongarch64/codegen.c` into `machine_text_opt.c`: one-use
  spill forwarding followed by native frame/branch compaction. Preserve
  the exact order, sign extension, address/alias barriers, source inline-ASM
  opaque regions and shared Core-use census. Standalone native test covers
  14 positive/negative scenarios; no optimization algorithm redesign.
- **Build and review inventory:** update the compiler build, standalone
  SourcePolicy tests, bootstrap file inventory and strict source boundaries.
  Index previously newly introduced P22 owners alongside the P23 source
  units as `inventory`, without falsely transferring old blob reviews or
  declaring GNU/QEMU Linux acceptance from a host-native check.

**Verification:** Debug `make MODE=debug -j8 check-fast` PASS, including
CoreType native/ASAN-UBSAN, source parser/verifier, x86 typed GNU ASM,
AArch64 vector contract and LoongArch64 14-case machine text optimizer;
Release `make MODE=release CFLAGS=-Werror -j8 build/release/bin/minic-cc`
PASS. Strict boundary and review-inventory checks run separately. GNU cross
assembly/link, five-ISA Linux runtime and QEMU are **NOT RUN** for P23.

## IR-P24 — Seal local-symbol identity and split machine/source target interfaces (2026-11-03)

Remove the two remaining *published-interface* paths that could still borrow
source-language identity. The prior P23 split changed implementation owners;
P24 also changes the actual public headers and target registration layout.
GCC's front-end trees versus machine RTL and Clang's source TargetInfo versus
LLVM's TargetMachine informed the ownership boundary, without inventing MIR
or changing the existing five-target ISA/ABI contract.

- **Target ownership:** `target_info.h` becomes machine/ABI/ISA/ELF-only;
  an opaque `source_info` refers to C language types, integer ranks,
  builtins, GNU attributes, `va_list` and invocation-specific C options in
  `source_policy/source_info.h`. Canonical target registration and a mutable
  invocation copy both maintain separate source and machine descriptors.
  `data_layout.h` now owns only the ABI byte/align table; the source adapter
  `data_layout_source.h` owns C record/object/relocation layout queries and
  statically checks the source-rank table width. Three register-name-only
  admission queries remain visible to Core machine consumers because they
  inspect *sealed register names*, not C nominal types.
- **Three architecture-wide ASM headers:** x86-64, AArch64, LoongArch64
  move the C/Program GNU ASM source qualifier declarations from
  `asm_contract.h` to their explicit `asm_contract_source.h`. Common and
  machine header translation units compile without transitive Frontend type
  or AST headers. Source qualifier implementations remain target-owned;
  the existing ISA form parsers and machine emitters are not duplicated.
- **Core/Module publication:** replace block `source_label_id`, deferred
  GNU asm `source_inline_asm_id`, and Module LABEL relocation IDs with
  owned `local_label_symbol`, `deferred_immediate_symbol` and
  `relocation_symbol`. Source ingress formats one canonical `.Luser_N` or
  `__minic_deferred_asm_immediate_N_0` identity; the five machine emitters
  only print it after strict format validation, with no source table lookup.
  Core owns free/reachability roots/duplicate-label checks and rejects
  forged assembler instructions, leading zeros and non-goto deferred forms.
  Cloned specializations still suppress duplicate local alias definitions.
- **Source label lifetime:** GNU `__label__` allocates an anchor *before*
  its definition. Resolve ordinary goto and GNU asm-goto together at the
  actual definition, so neither forward/backward nor local label references
  bypass cleanup-context checks. Ordinary goto continues to materialize
  source cleanup on permitted exits; asm-goto cross-cleanup taken edges are
  rejected until Core represents cleanup-on-edge. The AST verifier independently
  rechecks this invariant against forged/externally built source trees.
- **Validation and review boundaries:** add Core symbol identity native and
  ASAN/UBSan tests, machine symbol negative tests, GNU source CFG lifetime
  positives/negatives and standalone no-C machine-header compilation.
  New target source headers are recorded as `inventory`, not falsely
  certified as fully reviewed or Linux/QEMU accepted.

**Verification:** Debug `make MODE=debug -s -j8 check-fast` PASS; Release
`make MODE=release CFLAGS=-Werror -s -j8 build/release/bin/minic-cc` PASS;
`check-minic-target-boundaries.sh` PASS; machine-only headers PASS; Core
types/symbol ownership native 4/4 and ASAN/UBSAN 4/4 PASS; `asm goto`
source lifetime positive 3/3 and negative 5/5 PASS; same-scope and
ordinary goto cleanup differential PASS; `check-four-target-model-m0`
five-target SourceInfo/invocation/ABI tests PASS. Full GNU cross-assembler,
linker and QEMU Linux runtime acceptance is **NOT RUN** for this refactor.

## IR-P25 — Source array-object read convergence across Frontend/Core/DataLayout (2026-11-03)

**Baseline:** IR-P24 remote `6f387248` on PR #674. This is a single batch
across all five source-to-machine paths, not a declaration that the old C
array representation has been deleted or that a new MIR exists. There are two
active producer shapes today: an outer declarator dimension carried by
`MinicLocal` / `MinicRecordField`, and a fully materialized `MinicArrayType`
often introduced by a typedef. Both must yield the same C object semantics.

### Completed read-side ownership

- Frontend provides `minic_c0_local_array_object_info`,
  `minic_c0_record_field_array_info` and the ID-based field variant. They
  distinguish legacy outer repetitions, materialized array type, GNU complete
  zero arrays, incomplete flexible members, and runtime-sized VLAs. The
  expression-level query additionally uses the **qualified expression view**:
  `const struct` member arrays must not lose propagated `const` by reading an
  unqualified field declaration.
- Frontend expression, local initializer, builtin `sizeof` / `offsetof`,
  record/global initializer slot walkers, Core lowering/type import/ASM and
  local facts, common Global Payload planning, AArch64 fixed-register source
  checks, and DataLayout record/relocation paths now use these queries.
  Legacy arrays repeat the element type once per outer declarator bound;
  materialized arrays traverse their full type **once**, without multiplying
  an already materialized dimension twice. Bitfield and relocation guards
  continue to reject malformed representations.
- New executable test covers nested typedef arrays, record-member indexing,
  GNU zero/flexible members, VLA bound evaluation once, compound literals,
  inferred arrays, 25 negative target cases and absence of partial `.s` files.
  DataLayout's standalone probe compares legacy and materialized nested-array
  layout, offsets and global-relocation addends, with undefined-behavior
  sanitizer enabled. The test is wired into `check-fast`.

### Regression fixed during batch integration

The first `check-fast` found a **real** x86 M1 regression: compiling a
`static const struct` containing a nested `int items[3]` failed in Core
subscript lowering. A separate five-target GNU zero/nested record probe failed
at the same point. The new expression query had initially substituted the
field's unqualified declared element type for the qualified expression type.
After restoring the expression-qualified view, x86 M1 (including
`zero_record_bitfields` exit 42), the GNU zero/nested five-target probe and a
materialized typedef array under `const struct` all pass. No Core-specific
workaround was added.

### Explicitly retained producer seam and verification limit

**The producer representation is still dual:** 29 direct array-metadata
assignment sites remain in parser declaration/expression/initializer paths,
with the static-initializer expansion protocol still owning legacy repetition.
Do not delete `is_array` / `element_count` until those producers, expression
type/decay, initializer slots, Core storage and ABI import can move together.
This batch is **read-side convergence**, not complete C type elimination.

| Gate | Result |
| --- | --- |
| Debug `make ... check-fast` | **PASS**, exit 0 (254 PASS/SKIP lines; individual skips are not runtime certification) |
| Release `-Werror` compiler build; `git diff --check` | **PASS** |
| Five-target `.i → .s`; 25 negative programs, no partial `.s` | **PASS** |
| x86 GNU and MiniC→GNU AS/LD native semantics | **PASS** |
| x86 M1, GNU zero/nested 5 ISA, const-typedef record | **PASS** |
| DataLayout UBSan, x86 four record ABI differentials, flexible members | **PASS** |
| Backend review manifest strict check | **FAIL, preexisting inventory drift**: `backend_source.c/.h` missing from manifest and many older fingerprints stale. This batch did not update that unrelated review inventory. |
| Backend structural boundary checks (same script, omitting only the stale-manifest invocation) | **PASS**, including native and sanitizer Core/Module and five-target ownership probes |
| Four-target GNU cross ABI / five-target Linux QEMU | **NOT RUN / missing cross GCC and QEMU tools** |

## IR-P26 — Materialize parser-produced array objects and RecordFields (2026-11-03)

**Baseline:** PR #674 remote `64e127f` (IR-P25). IR-P25 made the two
declaration representations readable through one interface. This batch moves
the main **producers** to real `MinicArrayType` rather than leaving every
consumer to reconstruct a legacy declarator dimension.

### One producer/type/initializer/Core transaction

- Fixed, inferred and GNU zero-length *automatic locals* now publish
  `local.type=ArrayType`, `element_count=1`, `is_array=false`. Inferred string
  and brace initialization completes the **same** incomplete array descriptor
  after determining its extent. Array compound literals and their backing
  locals use the same full type, while the Core initializer block runs once.
  The GNU complete zero array has `sizeof==0`, yet Core retains the old
  one-element **storage sentinel** to preserve address materialization;
  OBJECT_ADDRESS uses the backing type and a validated pointer bitcast.
- Record declarations now publish full arrays for fixed, multidimensional,
  typedef, GNU `[0]` and flexible `[]` members. The parser preserves the
  established field-attribute attachment order before wrapping the outer
  dimension. Parsed fields have `element_count=1`, `is_array=false`, while
  GNU zero/flexible semantic flags remain explicit. The built-in `va_list`
  state record's byte array likewise uses a complete ArrayType.
- Static initialization's five linked algorithms—zero expansion, nested
  designators, union overlays, backward slot rewrites and record/union
  positional walking—now share one field-shape and fixed-slot policy.
  This removes roughly 56 spread-out legacy shape reads. Runtime record
  initialization accepts GNU zero-array empty `{}` without fabricating an
  element write; excess elements are rejected.
- Core FIELD_ADDRESS uses `EXACT` for a materialized array **field type**,
  reserving `ARRAY_ELEMENT` for hand-authored legacy repeated fields. Core
  record import obtains a flexible incomplete array's alignment through its
  complete element type without losing the sealed incomplete CoreType.
  The unchanged Core verifier enforces the resulting typed pointer contract.
- AST verification recognizes an incomplete by-value ArrayType **only** as a
  properly flagged flexible member, accepts full-array compound literals,
  and validates the canonical `va_list` field through its array query.
  Expression array queries propagate `const`/`volatile` recursively to
  materialized array elements, so a `const struct` member cannot be written
  through a subscript.

### Deliberate remaining exceptions and evidence

VLA locals still own a single-evaluated runtime bound and address pair: they
are **not** incorrectly transformed into a compile-time fixed ArrayType.
Manually built AST/white-box fixtures may still publish the old repeated
field shape through `minic_c0_record_add_field`; the shared query continues
to accept it until those public callers migrate. Parser-produced fixed array
locals and record fields no longer need that fallback. Some GNU typedef-array
initializer forms outside this batch's accepted domain remain separate
feature gaps, not newly claimed behavior.

The first integration exposed two real regressions, both fixed before the
final gate: materialized flexible arrays initially lacked an incomplete-type
semantic owner in the AST verifier; and materialized GNU zero record members
initially had bound 0 where the runtime initializer expected the legacy
sentinel 1. The fixes restore precise array source semantics, not machine
special cases.

| Gate | Result |
| --- | --- |
| Complete Debug `check-fast` | **PASS**, exit 0 |
| Release full compiler `-Werror` | **PASS** |
| New local array gate: five targets, GNU/MiniC x86 native 42, negative/rollback | **5/5, native 42/42, 80/80** |
| New static typedef-field gate: five targets, GNU/MiniC native 42, negative/rollback | **5/5, native 42/42, 40/40** |
| FAM / zero nested / Record ABI / AArch64 va_list source contracts | **PASS** at focused local gates |
| Prior machine `.s` A/B | Local-only phase byte-identical on five targets; **full RecordField phase differs** in temporary spill/order. Native semantic results pass, byte equality is **not** claimed. |
| Strict backend review manifest | Previous IR-P25 historical stale-inventory failure persists; this batch does not rewrite unrelated fingerprints. |
| GNU cross ELF/QEMU and five-ISA Linux runtime | **NOT RUN**, cross GNU/QEMU unavailable here |

## IR-P27 — Retire duplicate source array metadata across Frontend/Core/Layout (2026-11-03)

**Baseline:** PR #674 remote `5fba0cc` (IR-P26). P26 materialized parser-produced
arrays but still had a second, legacy field/count representation available to
hand-constructed C Program objects. This batch removes that split end-to-end.

### One source-object representation, with an explicit VLA exception

- `MinicLocal` no longer stores `is_array`, `element_count` or
  `is_zero_length_array`. Fixed, inferred, typedef and GNU zero local extents
  live exclusively in `MinicArrayType`. Declaration-time bounds/flags remain
  temporary parser state and never enter the persisted AST. An automatic VLA
  keeps its **single-evaluated** runtime-bound and allocated-address hidden
  local IDs; the paired IDs, not a forged static array length, identify the
  runtime-sized object. The verifier rejects partial/invalid hidden-ID pairs.
- `MinicRecordField` no longer stores `is_array`, `element_count` or
  `is_zero_length_array`. The low-level `minic_c0_record_add_field` builder now
  materializes its count>1 input as a full `ArrayType`, like parser-produced
  members. Static/runtime initialization, global payloads, source DataLayout,
  Core record import and AST verification read the same typed shape. Tests
  which explicitly built the old representation now construct typed fields.
- The sole retained C record-array marker is `is_flexible_array`: declaration
  legality and the semantic ownership of an incomplete *tail* array cannot
  be inferred from zero extent alone. Core's own imported flexible/zero-size
  metadata remains independent of this source-frontend representation.
- Core local object lowering and compound-literal addresses no longer have
  dead branches to manufacture a second repetition dimension from C metadata.
  Fixed-array Core storage owns one complete typed object; GNU `[0]` retains
  a one-element addressable backing sentinel while its source `sizeof` is 0.
  VLA addresses still come from runtime allocation, not static Core objects.

### Verification boundary

The source cleanup is not a five-target Linux runtime completion claim.
Machine code and linker behavior were not rewritten in this batch. The
existing tool environment does not provide the full cross-GNU/QEMU matrix.

| Gate | Result |
| --- | --- |
| Debug `minic-cc` and Release `-Werror` builds | **PASS** |
| Array-shape convergence: five targets, GNU/MiniC x86 native, bound-once, 25 negative programs | **PASS** |
| Materialized locals: five targets, native 42/42, invalid/rollback | **PASS, 80/80** |
| Static record typedef arrays: five targets, native 42/42, invalid/rollback | **PASS, 40/40** |
| Record, RV64 layout, AST contract, Core type import + sanitizers, DataLayout zero/relocation | **PASS** |
| Full Debug `check-fast` | **PASS**, exit 0; 367 PASS lines, 3 environment SKIPs |
| Synthetic AST negative contracts: partial/forged VLA pair, illegal union FAM | **PASS**, reject at AST verification |
| Strict backend manifest | **Pre-existing stale inventory**; not updated in this source-model batch |
| Five-target Linux QEMU runtime | **NOT RUN**; no cross-GNU/QEMU toolchain in this environment |

## IR-P28 — Retire duplicate Core object extent and field-element access (2026-11-03)

**Baseline:** PR #674 remote `864c18f` (IR-P27). P27 removed the source AST's
extra declarator array extent; this batch follows the same logical ownership
through pure Core storage and the five-machine shared ABI/layout path.

### One typed Core object, one layout owner

- A published `MinicCoreObject` owns exactly one `MinicCoreTypeId` and an
  optional explicit object alignment. Its `element_count` repetition scalar
  and `minic_core_function_add_repeated_object` adapter have been retired.
  Production lowering already emitted only complete typed objects after P27;
  legacy Core-only tests now construct real `MINIC_CORE_TYPE_ARRAY` values to
  exercise two-/three-element arrays and their layout boundaries.
- Core object layout and Common Target storage planning use `CoreType.size`
  directly, without multiplying by a second count or permitting an extra
  shape to disagree with CoreType. ABI call/result admission, frame intervals,
  RV64 record snapshots and pointer/storage preflight remove dead `count==1`
  special cases while retaining actual type and alignment checks.
- The obsolete `MINIC_CORE_FIELD_ACCESS_ARRAY_ELEMENT` compatibility form
  had no possible source producer once RecordFields gained full ArrayType.
  Core FIELD_ADDRESS now names the exact field type or a bit-field storage
  carrier; the former numeric form remains invalid under Core verification.
- Core array type registration checks complete/zero/incomplete state and
  checked element-size multiplication, catching malformed standalone Core
  producers without consulting source C DataLayout. Existing VLA runtime
  allocation and GNU zero-length backing semantics remain unchanged.

### Architectural ownership result and certification limit

Read-through confirms the **sealed Core Function/Module already has no
`MinicType` or AST identity**: Frontend's `MinicCoreSourceDraft` exists during
lowering, then `snapshot_types()` imports typed IDs and destroys the draft.
Backends fail closed if the draft is still live. Moving source lowering files
between directories is a later physical-ownership change, not grounds to
rebuild the Core IR model. Core ABI facts (calling convention, memory access,
transparent union and field layout) are intentionally retained.

| Gate | Result |
| --- | --- |
| Core types, verifier, shape tests, native/ASan/UBSan | **PASS**, 4 native + 4 sanitizer and malformed CoreArray contracts |
| Core object storage ownership/resolved, frame/call, RISC-V alignment/RV64 admission | **PASS**, scoped native checks |
| Array/source record gates: five targets, GNU/MiniC x86 native, negative+rollback | **PASS**, 25 + 80 + 40 cases |
| Full Debug `check-fast` | **PASS**, exit 0; 367 PASS lines, 3 environment SKIPs |
| Release `-Werror` compiler | **PASS**, `MODE=release BUILD_DIR=build/p28-final CFLAGS=-Werror` |
| Five-target GNU ELF/QEMU Linux runtime | **NOT RUN**; cross runtime tools unavailable in this environment |

## IR-P30 — One C-to-Core ingress owner and physical Source/Machine separation (2026-11-03)

**Baseline:** PR #674 remote `8d8065b` (P28 source plus a CI trigger). The
premature exact-head Linux campaign did **not** recertify this compiler: its
seven-way C owner shards failed before new QEMU proof. This is an architectural
ownership batch; no full Linux rebuild is part of its acceptance gate.

### Source transaction is no longer a CoreFunction field

- C-only `MinicCoreSourceBuilder` owns the unsealed CoreFunction **and** the
  parallel `MinicType` source draft. Source lowering, enum metadata, typed
  SSA construction and source-ingress verifier take this explicit builder.
  All Core-only constructors/verifiers/consumers still take `MinicCoreFunction`.
- A successful source snapshot atomically installs CoreType IDs and frees
  the source draft. Failed imports preserve the original draft for retry;
  builder destruction frees partial Core and source state without requiring
  the pure Core destructor to link frontend/type.c. A sealed-only `take_core`
  moves the Core function into Compiler's function set; failures never
  publish an unsealed object.
- Remove both `source_draft` and its language-aware release callback from
  `MinicCoreFunction`, its general destructor, verifier and machine-backend
  entry check. There is **no** `void *` hidden compatibility owner or second
  path that bypasses C ingress. `CoreTypeContext` remains borrowed from
  the Compiler-owned Module/FunctionSet transaction.
- Move **all 45 C/AST-dependent Core lowering and type-import files** into
  `src/compiler/c_lower/` with Git move history preserved. The **13 files
  remaining under `src/core/`** contain no C AST, `MinicType`, `MinicC0Program`
  or Frontend include; no old-path forwarding headers survive.
- The C AST and its input buffer are now destroyed **before** machine backend
  emission, following sealed FunctionSet/Module publication. Codegen traces
  take function counts from the sealed Core set instead of the dead Program.
- Checked integer overflow's last C-only enum eligibility bit is retired
  from published Core IR. Frontend, AST verification, C lowering and source
  ingress reject enum destinations before publishing any Core instruction;
  pure machine numeric shape checks now see only executable Core types.

### Five ISA source policy and machine codegen no longer cohabit an object

- `target/data_layout.c` now keeps only five machine ABI descriptors;
  `source_policy/data_layout_source.c` owns nominal C records, objects,
  arrays and initializer layout (original algorithm moved unchanged).
- `target/target_info.c` now owns machine target descriptors. Separate
  `source_policy/target_source_info.c` owns C integer/wchar models and
  target-language policy, exposing an opaque lookup without copying or
  reinterpreting architecture machine facts.
- x86-64, AArch64 and LoongArch64 `asm_contract.c` implementations are now
  machine-only. Their C source qualification and operands move to matching
  `asm_contract_source.c` files; one private `asm_contract_internal.h` per
  target shares **one** ISA form recognizer implementation and typed form
  metadata instead of duplicating template parsers. Existing target-specific
  machine emitters, register/ABI distinctions and accepted source forms stay
  separate. RISC-V source/machine splitting was already present.
- Live Makefile/bootstrap/test-workflow source inventories and boundary
  assertions track the new owners. Archived pinned patch scripts that
  deliberately target older source trees are not alternate production paths.

### Verification and remaining architectural work

| Gate | P30 result |
| --- | --- |
| Full Release `MODE=release CFLAGS=-Werror all` (including toolchain binaries) | **PASS** |
| CoreType snapshot/rollback, Core shapes, Core verifier, native + sanitizers | **PASS** |
| Core Module rollback/fragments, machine header isolation, five-target source registration, sealed symbols | **PASS** |
| Source shapes five target `.s`; Loong typed/forgery/normalized seal; A64 seal | **PASS** |
| Two GNU-preprocessed `.i` fixtures × five targets: before/after `.s` byte comparison | **10/10 identical** |
| Full Debug `check-fast` (including the MiniAS assembly contract) | **PASS**, exit 0; 574 matching PASS lines, 3 environment SKIPs |
| Overflow enum source negative / pure Core numeric positive, native + sanitizers | **PASS**, dedicated gates |
| RV64 historical Core emitter tests using sealed SourceBuilder/FunctionSet | **PASS**, 187 / 858 assembly lines; no QEMU claim |
| Full five-ISA Linux Kbuild/GNU ELF/QEMU | **NOT RUN**: architecture batch; historical QEMU results do not certify P30 |

This closes the C draft *ownership* leak and mixed source/machine translation
units; it does **not** claim that backend generic algorithm deduplication,
register/frame ABI modeling, Machine IR, object-format work, or five-target
runtime recertification is finished. These remain distinct architectural
work with separate evidence requirements.

The next *whole-owner* refactors are specifically known, not fixed in P30:
`MinicTargetInfo` still carries an opaque `source_info` pointer; removing it
requires switching the complete Parser/SourcePolicy/C lowering family to a
per-compilation Source+Machine invocation without losing signed-char/wchar
overrides. Separately, the three post-lowering Core reachability passes still
mutate C Program function/global emission flags; their complete root set
(alias, initializer relocation, file/inline ASM, owner, clone and entry)
must be sealed into ModuleIR and closed in one Core+Module reachability pass.
Neither deserves a transitional C fallback in pure Core or machine Target.

## IR-P31 — Invocation-owned C semantics and machine-only TargetInfo (2026-11-03)

**Baseline:** P30 remote commit `3226fb0`. This batch closes the entire
SourcePolicy/MachineTarget identity seam. It is not a five-ISA Linux runtime
recertification and does not close the separate Core reachability ownership
problem.

- `MinicTargetInfo` now carries **only** machine ISA, ABI, codegen, byte-order
  and register facts; delete its last `source_info` backlink. A fresh
  `MinicTargetInvocation` owns the selected machine descriptor, immutable
  source-language profile copy and per-compilation C integer/wchar overrides.
  It is explicitly non-copyable after initialization because its source model
  borrows the integer model embedded in that same invocation.
- Frontend's complete Parser/AST/constant-evaluation family (30 files),
  Compiler and C lowering/type-import/global payload family (32 files),
  driver and C-specific test callers now pass one Invocation through their
  C semantic queries. Every Core/ISA/ABI machine consumer explicitly takes
  `&invocation.machine`; there is no old overloaded TargetInfo C interface.
- Remove C integer-size/alignment rank arrays from machine DataLayout and
  place them in the source-language profile. All eight C record/array/object
  layout interfaces now take Invocation. Pure machine layouts retain only
  ABI storage, endian, pointer, scalar, vector physical properties.
- Five ISA-specific machine register recognition modules own the actual
  fixed-register grammar. C SourcePolicy and machine TargetInfo register the
  **same functions**, without a duplicate parser, canonical target roundtrip
  or C-oriented fallback in machine code.
- Complete the SourceBackend callbacks as a whole: x86-64/AArch64/LoongArch64
  source ASM qualify/seal/asm-goto and LoongArch64 SIMD sealing receive the
  active Invocation. In particular, architecture-specific ASM qualifiers no
  longer assume default machine layouts or independently look up canonical
  architecture descriptors when performing C type/layout checks. RISC-V
  sources follow the same explicit SourcePolicy boundary.
- The build/bootstrap inventories include all five machine-register units;
  the runtime workflow path trigger covers the new x86/AArch64/Loong owners.

| Gate | P31 result |
| --- | --- |
| Full Release `MODE=release BUILD_DIR=build/p31-strict CFLAGS=-Werror all` | **PASS**, exit 0 |
| Frontend (30 files), Compiler/C Lower (32 files), Target (19 files), affected tests strict C11 `-Werror -fsyntax-only` | **PASS** |
| Five-ISA target selection, C language ABI options and machine ISA/ABI options | **3/3 PASS**, 0 failures |
| Whole Target boundary contract (`check-minic-target-boundaries.sh`) with source/machine ownership and native probes | **PASS**, exit 0 |
| Four-target model M0 (including Invocation ABI, C va cursor and RV32 ILP32) | **PASS**, 14 subresults |
| Source DataLayout/AST contract, pure Core types and verifier (including sanitizers) | **PASS**, CoreTypes native 4/4 + ASAN/UBSAN 4/4 |
| Two historical RV64 Core emitters with sealed SourceBuilder | **PASS**, 187 and 287 assembly lines |
| C DataLayout zero-size relocations/aggregates, UBSan + native + five-ISA ASM | **PASS**, both dedicated gates |
| Loong typed descriptor, A64 source-normalized ASM seal, five-target SourceBackend registration | **PASS**, direct fixture linking against P31 strict objects |
| GNU-preprocessed `.i` fixtures × five targets, P30 vs P31 `.s` byte equality | **10/10 identical** |
| Five-ISA Linux Kbuild/GNU ELF/QEMU, full Debug regression | **NOT RUN** for this architecture batch |

**Next whole-owner class:** seal post-lowering C root facts into Core
FunctionSet/ModuleIR and perform one function/global reachability closure;
no Core-stage writeback to C AST or compatibility path is permitted.

## IR-P32 — Core/ModuleIR joint reachability with no post-Core C writeback (2026-11-03)

**Baseline:** P31 remote `b86bbc8`. This batch retires all three separate
post-lowering Core reachability/pruning passes, rather than keeping a C AST
bridge to resolve Core function/object liveness.

- The **single** Core `MinicCoreModule` is prepared before function lowering:
  owned linker-visible function/global identities, source root evidence,
  alias edges, initializer relocation targets, source-local-static ownership,
  specialization clone ancestry, and typed GNU ASM operand roots. No global
  initializer bytes are materialized during this identity stage.
- Core FunctionSet lowering uses the source ingress root facts to retain
  canonical static FunctionBodies needed by an ASM symbolic immediate or
  local-label relocation even when ordinary C reference specialization removed
  their lexical reference. After lowering, one joint Core+Module function-
  object fixed-point closure follows CALL, FUNCTION_ADDRESS, GLOBAL_ADDRESS,
  aliases, global relocs, clone-owner storage and source-label ownership.
  Every liveness decision is published **atomically in ModuleIR**, never
  written back into `MinicFunction` or `MinicGlobalObject`.
- Only after the closure is complete does Compiler materialize initializer
  byte/relocation fragments for ModuleIR globals marked emittable; Core and
  machine backends never reconsult the C AST. The one ModuleIR is sealed and
  verified before the C Program is destroyed, and only sealed ModuleIR is
  available to machine emission. No replacement sidecar or old prune-call
  compatibility path remains.
- The same change fixes two recorded correctness cases: MC-064 GNU ASM
  symbolic immediate retaining a static function body (formerly RV64 symbol
  relocation without a definition) and MC-072 `static inline` specialization
  with function-local `&&label` initializer relocations (formerly four ISA
  `.Luser_*` references without corresponding definitions).
- MC-073 canonical Core CFG now treats `BLOCK_ADDRESS` produced in a reachable
  block as an address-escape edge, even without source `local_label_symbol`.
  Dead address producers stay dead; malformed indices are rejected. All
  machine CFG emission and Module closure share the same Core block marker.
- The first joint-closure implementation incorrectly treated a parser-only
  `is_referenced` flag combined with section/asm-name/visibility as a stable
  function root. An independent P32 review and a five-ISA dead-helper/GNU
  native-link negative reproduced the resulting dead-function leak. Stable
  roots now exclude these parser-only hints; real Core/alias/ASM roots remain.
  ModuleIR verifier also rejects malformed specialization ancestry and
  local-label relocations to noncanonical or mismatched owner functions.

| Gate | P32 result |
| --- | --- |
| Strict Release full compiler/toolchain | **PASS** |
| Final full Debug `check-fast` (including new joint closure gate) | **PASS**, exit 0; 617 matching PASS lines, 3 environment SKIPs |
| Joint static global graph/owner, aliases, Core conditional reachability, CFG source labels, x86 GNU/native | **PASS** |
| MC-064 RV64 static function ASM immediate | **PASS**: static function definition now exists in `.s` |
| MC-072 RV32/RV64/AArch64/LoongArch64 label relocations | **PASS**: 2 references / 2 definitions on each ISA (previously 2 / 0) |
| Two GNU-preprocessed fixtures × five ISAs, P31 vs P32 `.s` byte equality | **10/10 identical** |
| One `check-core-module-reachability-closure` acceptance (static globals, aliases, MC-064/072/073, dead-source-metadata regression) | **PASS**, exit 0; P31 baseline fails as expected on MC-064 |
| ModuleIR clone provenance + owner-label forged inputs | **PASS**, 9 malformed cases rejected, native and ASan/UBSan |
| Independent architecture/root/transaction/finalization diff review | **CLEAR**, no outstanding P32 correctness blocker |
| Five ISA GNU ELF/QEMU Linux runtime recertification | **NOT RUN**: cross GNU and QEMU executables unavailable here |

This seals the *post-Core C ownership* boundary, not the separate work on
generic backend algorithm convergence, Machine IR and full cross-runtime
correctness.

## IR-P33 — One compiled Common Program Visitor and emission transaction (2026-11-03)

**Baseline:** PR #674 `820d9b9` (P32). The five backends already shared one
semantic FunctionPlan and ProgramEmit/ProgramVisit algorithm, but the latter
two lived as ~500 lines of `static inline` production logic in public headers.
This batch moves the complete implementations to independently built Common
`program_visit.c` and `program_emit.c`, with headers now exposing only types,
callbacks and declared entry points. There is no copied compatibility body.

- `program_visit.c` is the single owner of sealed ModuleIR global/function
  selection and each selected function's one prepared CoreFunctionPlan
  acquire/visit/destroy lifecycle. All five targets retain their actual
  ISA-specific function and global callbacks.
- `program_emit.c` is the single owner of reachable Core/Module symbol-use
  collection, inline/file ASM token checks, undefined ELF weak/visibility
  binding, and adjacent-file `mkstemp`/close/finalize/atomic-rename rollback.
  The internal symbol-set data structures and scanners are no longer
  published to every machine backend. No ISA/GNU syntax or output ordering
  was changed.
- The standalone output probe now checks weak/hidden/unused names through
  actual emitted assembly rather than reaching into private symbol scans.
  The visitor probe still uses test-owned fake FunctionPlan callbacks, linked
  against the real compiled visitor. The Makefile, B0 source inventory and
  structural/negative boundary gates changed in the same owner batch.
- Separately registered the six P31 machine register source/header files in
  the existing blob review manifest after targeted source inspection. The
  manifest's older stale hashes remain intentionally stale: updating them
  would falsely imply a new full source review.

| Gate | P33 result |
| --- | --- |
| Release MiniC compiler `-Werror` | **PASS**, exit 0 |
| Full Debug `check-fast` | **PASS**, exit 0; GNU/QEMU-dependent SKIPs remain SKIPs |
| Whole target boundary (including five-target output transaction and Module visitor) | **PASS**, output failure rollback=5, prior output preserved=5, census=1 |
| Review ledger `--check` (including both newly compiled Common units) | **PASS**, inventory=251, new/removed/invalid=0, stale=202 (not recertified) |
| Two GNU-preprocessed fixtures × five ISA, P32 vs P33 assembly byte equality | **10/10 identical** |
| Five ISA Linux original GNU ELF/QEMU complete runtime | **NOT RUN**; no new Image/P1 or software correctness claim |

The latest P30/P31/P32 remote CI failures were independently compared and
remain non-green: x86-64 cmpxchg128, RV32 Zstd Core lowering, AArch64 GNU
pragma/NEON, RV32 P2 virtio-gpu, and environment-blocked x86 KVM. P33 does
not relabel these as architectural regressions or success. The next stage
must judge remaining target Common versus ISA-specific machine responsibilities
from complete producers/consumers, not introduce a second MachineIR model
before the actual machine-pass requirements are clear.

## IR-P34 — One prepared Core CFG/use-view owner across five machines (2026-11-03)

**Baseline:** P33 PR #674 `cf0c271`. Following the entire production
ProgramVisitor → CoreFunctionPlan → physical function plan → block emission
chain confirmed that the five backends already share semantic CFG walking,
but repeatedly allocate and recompute two *identical* Core-only analyses.

- `CoreFunctionPlan` now owns one source-label-aware reachable-block bitmap
  and one semantic SSA value-use census for each admitted, verified function.
  Both are immutable, function-identity-checked borrowed views in
  `MinicCoreFunctionFacts`. The plan destroys each allocation *after* its
  machine callback returns; targets never free visitor-owned memory.
- x86-64 uses both views for machine block selection, early kernel head
  address analysis and GP dead-writeback; RV32 uses the same reachability
  roots; RV64's strict ISA preflight and block walker share the visitor's
  bitmap rather than letting the physical frame allocate it a second time.
  RV64's public standalone emitters still acquire and own their fallback
  bitmap. The frame has distinct `owned_reachable_blocks` and borrowed
  read-only `reachable_blocks` fields to prevent double-free.
- The direct AArch64/LoongArch64 emitter receives the same borrowed views
  through its existing BlockContext; its SSA GP forwarding and target-
  certified immediate folding retain the *original* all-lexical-block
  value-use census. LoongArch64's subsequent single-use machine text pass
  also borrows that census rather than recomputing it. Actual physical
  ISA/ABI register, stack, call and terminator implementations are unchanged.
- The ProgramEmit ELF symbol scanner must still find live assembly symbol
  uses *before* invoking any function visitors, so it intentionally keeps
  its own earlier reachability allocation. Merging these two lifetimes
  would change module emission ordering/transaction semantics. A standalone
  transaction probe now explicitly links the Common census implementation.
- No new Machine IR, parallel machine callback dispatcher, transitional C
  metadata fallback, or generic five-ISA physical instruction abstraction
  was introduced.

| Gate | P34 result |
| --- | --- |
| Strict Release MiniC `-Werror` | **PASS**, exit 0 |
| Full Debug `check-fast` | **PASS**, exit 0 |
| GNU preprocessed six fixtures × five ISAs: exact P33 vs P34 `.s` | **30/30 byte-identical** |
| Core architecture boundary and host-native probes | **PASS**, including prepared function-view lifecycle, CFG traversal, Direct LP64 dispatch, atomic output rollback |
| Prepared plan / output transaction host ASan+UBSan | **PASS**, LeakSanitizer unavailable under host ptrace; run with `detect_leaks=0` |
| Review ledger inventory | **PASS**, 251 entries, no new/removed/invalid; existing stale entries not recertified |
| Five-ISA original Linux GNU ELF/QEMU runtime | **NOT RUN** in this architectural owner batch |

## IR-P35 — Seal the C-to-Core type ABI facts in one batch (2026-11-04)

**Baseline:** P34 PR #674 `e4eb19b`. Read the C Type/record importer, Core
TypeContext registration/completion/verifier, x86 SysV integer aggregate
classifier, Common LP64 GP/HFA classifiers and RV64 zero-sized global gate as
one owner chain, together with their source-layout and Core probe consumers.

- Core no longer carries source-only `is_transparent_union`: GNU transparent
  union admission and argument conversion remain in the C frontend/lowering.
- Record fields no longer duplicate flexible-array or GNU zero-length-array
  declaration flags. Their referenced sealed Core ARRAY shape (complete vs
  incomplete, count, extent and layout) now owns the distinction. The C ingress
  checks that the materialized source field array matches the imported Core
  array before publishing a completed record; x86 SysV and Common LP64 keep
  rejecting unsized/zero tails for register classification.
- Core ARRAY no longer duplicates the zero-length flag: `is_complete` with
  `count == 0` is a valid, zero-sized GNU array; `!is_complete` with `count == 0`
  is an incomplete array with no storage. Registration and RV64 global gates
  now consume that normalized representation.
- Packed record, pack alignment and explicit record alignment policies now
  enter Core as one **target-neutral nondefault-layout fact**; packed fields
  carry the same independent field-local fact. Common LP64 interprets these
  facts conservatively in its homogeneous-FP classifier; Core does not encode
  that ABI decision. Exact physical size/alignment, member offsets, bitfields
  and qualifiers remain intact. Source DataLayout still validates the policy
  and owns its numerical values. The Core alignment override is named for its
  effective ABI property, and x86 by-value ABI equality still checks it. No
  transitional old fields or machine-to-C metadata fallback were retained.
- Negative Core ABI probes now explicitly synthesize complete zero extent,
  incomplete extent and field-local homogeneous-FP restrictions. Architecture
  boundary gates forbid a regression to the removed Core/source shadow flags.

| Gate | P35 result |
| --- | --- |
| Strict Release MiniC `-Werror` | **PASS**, exit 0 |
| Full Debug `check-fast` (CoreType, C transparent union, zero array, target ABI) | **PASS**, exit 0 |
| GNU preprocessed six fixtures × five ISAs: exact P34 vs P35 `.s` | **30/30 byte-identical** |
| GNU C-origin probes (transparent union, array identity/local, packed record) × five ISAs | **20/20 byte-identical** |
| Core architecture boundary/native probes (incl. newly forged ABI tails) | **PASS** |
| Five-ISA original Linux GNU ELF/QEMU runtime | **NOT RUN** in this type-owner batch |

## IR-P36 — One canonical Module symbol/alias owner (2026-11-04)

**Baseline:** P35 PR #674 `fc54cf9`. Trace source symbol aliases from
`module_ir_build` through Module verification, Common source-symbol use
closure, ELF alias serialization, Direct AArch64/LoongArch64 and the three
independent x86/RV32/RV64 global emitters as one ownership family.

- The alias relationship in published Module IR is represented by exactly one
  `alias_target_index` (or `SIZE_MAX` when absent) and the canonical target
  symbol name already owned by the referenced module entry. Remove the second
  alias-name allocation and the object's independent `is_alias` boolean.
  Reject out-of-range, self-referential, chained, tentative, undefined, and
  invalid-definition aliases before emitting any target-specific ELF text.
- Remove global `explicit_alignment`, which was copied from the source but
  never consumed after the effective `storage_alignment` was sealed. Preserve
  `MinicCoreModuleFunctionSymbol.explicit_alignment`, which is consumed by
  function entry emission.
- Remove function-symbol `is_variadic`, an unused copy of the semantic Core
  function signature. Preserve CoreFunction's variadic ABI flag. Preserve the
  function-symbol `is_source_label_relocation_owner` flag and its pre-lowering
  retention check: initializer `&&label` can require the canonical owner even
  when ordinary C lexical call references were pruned.
- The two-stage reference and payload storage is intentional. Source-free
  `MinicCoreModuleReference` edges must be available for static reachability
  closure before live initializer fragments are materialized; folding the
  structures would import otherwise unreachable unsupported C data.
- Synthetic Core Module tests explicitly initialize non-alias entries with
  `SIZE_MAX`, and exercise malformed object/function alias indices, owner
  definition status and emitted `.set` canonical target spellings.

| Gate | P36 result |
| --- | --- |
| Strict Release MiniC `-Werror` | **PASS**, exit 0 |
| Full Debug `check-fast` | **PASS**, exit 0 |
| Core Module alias-index verifier and Common output transaction | **PASS**, including 14 alias-index cases, rollback and five-target output transactions |
| Five-ISA P35 vs P36 GNU `.i` byte parity | **65/65 byte-identical**: ten C-origin fixtures plus three real function-alias fixtures × five ISAs; all 15 aliases emit `.set alias_fn, target` |
| Architecture boundary and backend-review manifest | **PASS** (`inventory=251`, no invalid/new/removed; pre-existing stale entries reported) |
| Five-ISA original Linux GNU ELF/QEMU runtime | **NOT RUN** in this Module-owner batch |

## IR-P37 — One type identity and ABI fact per owner (2026-11-04)

**Baseline:** P36 PR #674 `e4facb9`. Read Core indirect calls, C-to-Core
type import, target source builtin registration, record DataLayout, and
`va_copy` from their producers through all consumers before changing their
representation. Migrate the complete related call sites in one batch:

- The indirect `MinicCoreCallSignature` owns only a sealed Core FUNCTION
  `TypeId`. Core verification, common call view and machine backends read
  return type, parameter types/count, variadic flag and calling convention
  from that one immutable function type. C lowering rejects mismatched source
  signature facts before publishing the snapshot, preserving rollback.
  Direct callees remain a separate Core fact and are not conflated with
  indirect calls.
- Replace nine AArch64-specific generic C base-type enum values with a
  `TARGET_BUILTIN` type carrying target-owned identity and declaration role.
  AArch64 SourcePolicy registers the builtin names, admission capabilities,
  Core import forms and optional vector scalar types. Source semantics,
  AST verifier and C-to-Core importer use that descriptor rather than
  per-target generic switches. Header-only builtin types do not gain a
  synthetic Core representation; opaque SVE identities and NEON vector
  identities retain their intended distinction. Foreign or forged target
  builtin identities are rejected.
- The Source DataLayout record-field storage alignment result is now the
  authority for C-to-Core bitfield import, including packed records and
  zero-width barriers. Remove the second local alignment algorithm.
- Parser and AST verifier share one SourcePolicy `va_copy` cursor-type
  compatibility query; machine ABI specifics stay behind the target policy.

The target registry is checked as a whole before lookup: IDs and spellings
must be unique, declaration role/capability/Core import/vector element must
agree, and forged record, array, function, enum, integer or GNU-vector fields
cannot piggyback on a target builtin identity. Legal pointer/CV qualifiers
remain supported. Invalid registry and forged-type cases are negative tests.

This is an owner/boundary change, not an announcement of new instruction,
cross-target ABI, or runtime support. No temporary legacy-field adapter was
retained. Source/test changes prior to this documentation record: **35 files,
+752/-391 lines (net +361)**.

| Gate | P37 result |
| --- | --- |
| Strict Release MiniC `-Werror` | **PASS**, exit 0 |
| Debug MiniC compiler build | **PASS** |
| Full Debug `check-fast` | **PASS**, exit 0; optional unavailable cross GNU/QEMU paths explicitly skipped |
| Source/Core/AST contract, call signature, target builtin, record bitfield and `va_copy` probes | **PASS** on available native tools |
| Five-ISA P36 vs P37 targeted `.i` differential | **255/255 matched outcomes**: 187 byte-identical `.s`, 68 both rejected, 0 differing |
| Five-ISA original Linux GNU ELF/QEMU runtime | **NOT RUN** in this owner batch; absent cross toolchains are not counted as passes |

## IR-P38 — Canonical Core direct-call type and Module assembler-name closure (2026-11-04)

**Baseline:** P37 PR #674 `b0520b7`. This batch follows two related
source-independent ownership chains end to end, without adding an ISA feature
or changing the five target ABI policies.

- Direct `MinicCoreCallee` now owns its emitted symbol name and exactly one
  nominal Core `FUNCTION` TypeId. Remove its duplicate return TypeId,
  parameter TypeId allocation/count, variadic bit and calling convention.
  C-only call signature facts remain in the SourceDraft until snapshot;
  snapshot registers the canonical Core FUNCTION shape. Core verifier,
  CallView, dump and x86/LoongArch machine consumers all read that one type.
  Failed partial snapshots clear published call TypeIds for safe retry;
  synthetic compiler libcalls do not need a C Program FunctionId.
- ModuleIR owns a sorted, borrowed **assembler-symbol name → complete function
  index range** query. C names are not substituted for GNU `asm("name")`
  spellings. Calls, function-address edges, global object-to-function
  initializer relocations and GNU asm operand roots use every matching
  assembler definition; an extern declaration must not hide an internal
  same-name definition. A candidate static body is lowered before closure
  when its assembler name is shared, while ordinary dead static lowering is
  still skipped. Core closure prunes unreferenced candidates before emission.
  The Module verifier checks the **entire emitted ELF symbol namespace**:
  live function bodies, function aliases, emitted object definitions and
  object aliases cannot collide. Extern-only declarations and pruned internal
  bodies do not count as emitted symbol owners.
- Canonical local-label/alias owners retain their exact FunctionId because
  these references name an owner, not an arbitrary equal-spelled function.
  Remove the redundant Module `is_specialization` boolean; the existing
  `specialization_source_index != SIZE_MAX` is the sole specialization fact.
  x86 `.head.text` early-address call closure reuses the Module name index
  and scans only emitted function bodies, avoiding dead candidates.

Before updating this record: **33 production/test files**, including one new
real GNU-assembler collision fixture; production **18 C/H files,
+423/-197 lines (net +226)**, tests **+465/-58** (including new 48-line
fixture). No compatibility shadow fields or duplicate x86 name index remain.

| Gate | P38 result |
| --- | --- |
| Strict Release MiniC `-Werror` | **PASS**, exit 0 |
| Full Debug `check-fast` | **PASS**, exit 0, after final emitted-symbol namespace and arena fixture edits |
| Core type snapshot and call tests, native + ASAN/UBSAN | **4/4 native + 4/4 sanitizer PASS**; synthetic `__addtf3` → `__trunctfdf2`, nominal identities, failure/retry |
| Module symbol reachability and object initializer, `asm` roots | **PASS**; five-ISA assembler generation and x86 MiniC/GNU native execution |
| x86 kernel `.head.text` section and GNU link/address relocation | **PASS**; unchanged early-boot `R_X86_64_32S`/`PC32` contracts |
| Target-boundary checker, Core Module fragment, rollback and backend arena | **PASS**; 18 additional emitted-ELF-namespace cases covering cross-kind aliases and dead-vs-live definitions; hand-built Core module fixture sets specialization sentinel explicitly |
| Five-ISA P37 vs P38 GNU-preprocessed `.i` differential | **255/255 matching outcomes**: 187 `.s` byte-identical, 68 both rejected, 0 differing |
| Five-ISA original Linux GNU ELF/QEMU runtime | **NOT RUN** in this type/symbol-owner batch |

The next independent architecture family is the three Core inline
specialization producers, which still duplicate local-body analysis and
clone-state ownership; do not mistake this P38 symbol closure for their
full migration.

## IR-P39 — One AST inline-specialization owner and transitive call worklist (2026-11-04)

**Baseline:** P38 PR #674 `a5c3d7c`. Read the three full AST specialization
passes (`inline_integer.c`, `inline_symbolic.c`, `inline_boolean.c`) together
with FunctionBody source ownership, the Core call selectors, target lowerers,
section closure and original-ELF test fixtures. Consolidate **four shared
families in one change** while preserving each pass's domain-specific proof:

- New `inline_specialize_common.[ch]` owns one AST-only fact key (integer
  known/bits, relocatable symbolic expression, Boolean `[0,1]` known),
  mask-selectable candidate comparison and variant lookup. The three passes
  retain their own constant/symbolic/range proof, GNU static eligibility,
  clone quotas, source `FunctionBody` and Core callsite revalidation.
- The shared `MinicInlineInitialOwners` owns original `ExpressionId →`
  **canonical source FunctionId** discovery when section names exist. It
  fails closed on an unavailable required index, and bounds every lookup by
  the **captured original function limit**: a former ambiguous-owner sentinel
  must not become a new valid FunctionId when specialization appends clones.
- The shared optional `MinicInlineCallWorklist` groups stable CALL IDs by
  canonical owner, combines the current source's bucket with the ambiguous
  bucket in ascending expression order, then scans dynamically appended
  expressions. If indexing cannot be allocated or owners cannot be computed,
  its live full scan remains valid. No transitive pass rewrites a shared
  source AST CALL to guess which clone will execute.
- Remove all three duplicated variant predicates, repeated source-owner
  arrays, repeated CALL-index allocation/ordering and the old cross-pass
  section helper declarations. Preserve existing diagnostics/perf counters;
  in a representative Boolean section fixture, candidate scans fall from
  **198 to 9** with the exact same direct/transitive clones.

**Code/build/test batch:** 9 paths, **+869/−747 lines (net +122)**, plus this
ledger entry (10 total paths, +923/−747, net +176). The six
production C/H files are **+609/−747 (net −138)**, including the new 401-line
shared implementation/header; Makefile +12; two new test files +248.
The existing three domain implementation files alone are **+208/−740
(net −532)**. There is no backward-compatibility alias for a removed helper.

| Gate | P39 result |
| --- | --- |
| Strict Release `minic-cc`, Debug compiler and full Debug `check-fast` | **PASS**, exit 0; optional absent cross GNU/QEMU paths remain SKIP |
| Shared AST Key/Section/Owner/CALL test | **PASS**, including ambiguous ID after clone, stable merged bucket order, appended calls, optional OOM/owner fallback |
| Integer/Symbolic/Boolean targeted GNU and MiniC native, x86 `.head.text`, MC001 arena, kmalloc/CFG guard | **PASS**, including 90 MC001 five-ISA assembly variants and 6 native cases |
| Full target-boundary checker | **PASS** |
| P38 vs P39 inline/section GNU-preprocessed `.i` differential | **80/80 matching outcomes**: 65 `.s` byte-identical, 15 both rejected, 0 different |
| P38 vs P39 five-ISA broad Core execution/type `.i` differential | **255/255 matching outcomes**: 187 `.s` byte-identical, 68 both rejected, 0 different |
| Five-ISA original Linux GNU ELF/QEMU runtime | **NOT RUN** in this AST refactor; cannot infer Linux end-to-end correctness from `.s` parity |

**P40 correctness follow-up (not claimed fixed by P39):** MC-058 symbolic
pointer-arithmetic closure is still incomplete for transitive `key + 1`
GNU `i` operands; the Core asm symbolic reducer currently emits an unscaled
`key_slots+1` for `int key_slots[4] + 1`, whereas GCC relocates +4 bytes.
Next batch must unify pointer-stride and transitive symbolic fact handling
across the Core reducer, AST specialization and exact Core callsite selector,
with both commuted addition and subtraction checked against DataLayout.

## IR-P40 — MC-058 symbolic byte offsets and live specialization facts (2026-11-04)

**Baseline:** P39 PR #674 `4222b2c`. This is a correctness batch grounded in
actual GCC/MiniC counterexamples, not an ISA-support expansion or an unrelated
architecture redesign. It closes both the symbolic address chain **and** the
parameter-fact liveness chain across AST, Core call selection and GNU asm.

- **Byte-accurate symbolic relocations.** GNU `i` text now derives a signed
  byte addend from the *pointer operand's* C pointee stride, using the same
  language-semantic element-size query as ordinary Core `POINTER_OFFSET`.
  Handle `p+n`, `n+p`, `p-n`, nested arithmetic, explicit casts that alter
  stride, zero-size GNU complete-object stride and integer-cast byte addition.
  Check signed scale, negate and addend overflow; preserve the earlier
  conservative rejection of invalid nonzero zero-size indexed addresses.
  Expose the normalizer as a Core `symbol name + byte addend` fact query.
- **Complete symbolic transitive closure.** The AST Symbolic pass can close
  pointer parameters inside constant pointer arithmetic with the original
  parameter pointer type retained, including two-hop nesting. Structurally
  reuse synthesized expressions and never rewrite a source-FunctionBody CALL
  on behalf of one clone. The late Core selector compares normalized symbol
  and byte addend, not incidental AST ExpressionId, and it revalidates the
  exact active caller. Integer/Symbolic/Boolean selectors cannot choose a
  candidate carrying additional unproved facts from a later selector.
- **No stale ingress facts after mutation.** One linear Program-wide
  source-LocalId stability bitmap rejects speculative reuse of parameter
  facts after assignments, compound assignments, ++/--, taking an address,
  GNU asm output or top-level `volatile`. Integer, Symbolic and Boolean AST
  passes consume this source fact, including initial volatile formal checks.
  Core local CFG facts now record writes/escapes for *all* specialization
  families, rather than only exact integer constants. The Boolean range,
  function-pointer symbolic identity, late Core selectors, GNU asm numeric/
  symbolic/name fallbacks and loop mutation barrier all honor live state.
  A newly assigned, genuinely known constant remains usable; a stale entry
  constant or pointer symbol does not.

**Real failures before P40:** `int key_slots[4] + 1` emitted an unsafe +1
instead of +4 relocation; transitive `mid(key) → leaf(key+1)` failed to
resolve GNU `i`; Boolean `mid(int x){x=2;return leaf(x);}` with incoming
`(_Bool)z` returned the wrong native value; reassigned pointer parameters
still emitted the previous global symbol; an overwritten integer parameter
could incorrectly supply an `i` operand from its old entry constant.

**Code/build/test batch:** 27 paths, **+1147/−129 (net +1018)** before this
ledger paragraph. The 11 production C/H files are **+617/−129 (net +488)**;
Makefile +8; 15 new test/probe files +522. No fallback compatibility fields
or new target-specific ISA feature were introduced.

| Gate | P40 result |
| --- | --- |
| Strict Release compiler and full Debug `check-fast` | **PASS**, exit 0; optional absent GNU cross/QEMU runtime remains SKIP |
| New MC-058 integrated target (`check-mc058-specialization-correctness`) | **PASS**: RV32/RV64 eight GCC-equal typed relocation offsets, zero stride, illegal index and signed overflow, four transitive call routes/two canonical addends |
| Parameter mutability and volatile regression | **PASS**: overwritten dynamic integer and volatile GNU `i` are correctly rejected; fresh `x=2` emits `.byte 2`; stale Symbolic is conservatively rejected rather than misrelocated |
| Boolean/Integer native, shared-body and MC001 | **PASS**: Boolean five-ISA compilation + GCC/MiniC x86 execution; integer transitive five-ISA + native; Symbolic MC001 90 five-ISA assemblies and 6 native cases |
| P39 vs P40 broad five-ISA `.i` differential | **255/255 same outcomes**: 187 byte-identical assembly, 68 both rejected, 0 differing |
| P39 vs P40 inline/section `.i` differential | **55 byte-identical, 15 both rejected, 10 intentional assembly differences** out of 80; differences are two symbolic fixture families selecting preexisting equivalent clones, with GNU/MiniC native probes PASS |
| Target admission for structured symbolic `.quad` immediate on x86/AArch64/LoongArch | **3 explicit SKIP**; cannot infer these ISA ports implement that GNU asm form |
| Five-ISA Linux GNU ELF/QEMU runtime | **NOT RUN** in this focused correctness batch; an ABI/Linux all-pass claim would be false |

**Remaining supportedness gap:** A reassigned symbolic pointer that is now a
different static global may still be conservatively **rejected** by MiniC
where GCC proves the new `i` operand. This is safer than emitting a stale ELF
relocation, but does not yet establish GCC acceptance parity. The source
static-initializer pointer relocation evaluator remains separate from GNU asm
normalization, and these two sources of layout semantics need differential
coverage when further acceptance work is scheduled.

## IR-P41 — Source member CV and typed/live GNU ASM facts (2026-11-04)

**Baseline:** P40 PR #674 `1c7619e`. Five independently reading tracks
followed source type construction through verifier, AST inline-specialization,
Core local facts, target-aware immediate evaluation and all five ASM emitters.
Three tracks found live, reproducible wrong-code and closed their complete
consumer chains together. Two exploratory tracks **did not** introduce
unproven architecture changes.

- **One source type rule for record members.** A member selected from
  `volatile struct/union` had lost the enclosing `volatile`, despite already
  inheriting `const`; `_Generic(&p->field)` returned a different value from
  GCC and Core could omit one of two required volatile loads. New
  `minic_type_record_member()` propagates both qualifiers. Pointer and
  rvalue member parsing, synthesized runtime record initializer MEMBER nodes
  and AST verifier share the same rule; field pointee qualifiers remain
  untouched. Coverage includes anonymous/nested records, arrays, pointers,
  bitfields, two reads, stores and const-volatile members.
- **One typed integer evaluator for specialized GNU `i`.** Previously the
  Core walker ignored C casts and result widths, so
  `(unsigned char)(value+1U)` with specialized `value=258` emitted `.long 259`
  instead of GCC's `.long 3`. The evaluator now uses the existing target
  integer conversion/representability API for casts and each unary/binary
  result, including sign extension, unsigned wrap, `_Bool`, promotions,
  shifts and overflow guards. The pointer-arithmetic relocation path is
  tested with a narrowed index and verifies `symbol+12`.
- **Bounded-OR writes revoke entry specialization.** In `if(y) x |= 2`, the
  Core bit-set facts legitimately stayed useful, but an old specialization
  `x=0` remained usable as GNU `i` after CFG joins; MiniC emitted `.byte 0`
  where GCC rejected the dynamic operand. Separate the current-value bitset
  lattice from the immutability of the caller's entry fact. All bounded-OR
  paths mark `specialization_modified`; valid straight-line `x|=2` still
  produces the fresh `.byte 2`. ASM output writes, loops, native runtime and
  five-ISA Core negative admission are covered.

**Independent architectural restraint:** GNU aligned pointer typedefs and
reduced-alignment record value loads show safe admission gaps, not confirmed
wrong-code; no per-pointer-level alignment representation was invented.
P38 Source ASM operand roots currently prevent MC-064's historical missing
static-function link, but over-retain symbols in dead ASM. Replacing them
requires complete typed per-instance Core ASM relocation edges across every
target-qualified/structured/generic form and a reachable-block consumer;
no unsafe partial deletion or borrowed Source identity was introduced.
MC-043 clone quota also still needs genuine always-inline/caller-context
semantics, not a blind optional-fallback `continue`.

**Batch size:** 22 paths, **+774/−41**; the nine production
C/H files contribute **+182/−37**. New GNU/native/five-ISA regression C and
shell files are kept together with one `check-fast` Makefile target and the
corrected historical issue statuses, rather than split into tiny commits.

| Gate | P41 result |
| --- | --- |
| Strict Release compiler and full Debug `check-fast` | **PASS**; source and new P41 gates together, exit 0 |
| `check-p41-typed-asm-and-live-facts` | **PASS**: typed GNU `i` 12 integer + 1 pointer relocation cases per RV32/RV64, GCC assembly parity; 5-ISA dynamic immediate safely rejected; live known value `.byte 2` preserved |
| Volatile member semantics | **PASS**: 5/5 target `-S`, GNU and MiniC x86 native runtime, five `_Generic` qualifier forms, two volatile loads and bitfield |
| Local fact control flow | **PASS**: five-ISA non-ASM generation, GNU/MiniC native branches/loops, ASM output negative; 3 ISA structured-ASM admission **explicit SKIP** |
| P39 vs P41 broad five-ISA `.i` differential | **255/255 same outcomes**: 187 byte-identical `.s`, 68 both rejected, 0 differing (P40 gave identical broad results) |
| P39 vs P41 symbolic specialization differential | **55 byte-identical, 15 both rejected, 10 expected changes** out of 80, exactly the two P40 symbolic clone-selection fixture families; P41 adds no further variant differences |
| Five-ISA original Linux GNU ELF/QEMU | **NOT RUN**; original Linux end-to-end remains separate, no result invented |

## IR-P42 — ELF Module linkage facts have one object-format owner (2026-11-04)

**Baseline:** P41 PR #674 `86a9f48`. Read the complete sealed Module symbol
graph, emitted-symbol verifier, TargetInfo's six linkage predicates and all
five machine consumers before changing the interface. Core and machine
emitters already have **zero** direct `MinicType`/`MinicC0Program` reads;
the legitimate SourcePolicy and C-to-Core ingress are deliberately untouched.

- **One ELF facts query.** `target/common/elf_module_symbol_info.[ch]` owns a
  source-independent object/function lookup producing TU-local,
  nonpreemptible, and undefined-weak facts in one pass. Remove all six
  duplicate scanners and their misplaced declarations from `TargetInfo`.
  The physical ELF-specific decision does not pollute format-neutral Core IR
  or become a generic ISA capability. Five machine consumers use the sealed
  Module fact while retaining their original, distinct ISA instructions:
  RV `lla/la`, x86 RIP/GOT, AArch64 ADRP/GOT and BL/BLR, LoongArch `la.local`
  versus `la.global`.
- **Actual emitted owner beats a prototype.** An extern-only weak declaration
  sharing a GNU assembler name with an emitted definition can no longer
  shadow the definition through a first-match array lookup. Emitted object
  and function aliases are not undefined weak references, even when the
  source declaration is `weak`. Internal definitions pruned by Core closure
  cannot claim TU-local linkage; actual emitted local aliases can. Hidden
  extern-only declarations still remain nonpreemptible without becoming
  TU-owned definitions. Weak-only declarations are aggregated independently
  of source order and lose their undefined status when a real owner exists.
- **One verification family.** Existing 20 native ELF linkage cases are
  expanded to 31 with extern/definition order reversal, emitted weak/local
  aliases, pruned definitions, and invalid symbol-kind admission. Update the
  Target Boundary's API ownership check and standalone probe link list.
  Add both Common files to the backend-review source inventory without
  claiming a complete deep review of otherwise untouched targets.

**Batch size before this ledger entry:** 14 paths, **+267/−204 lines**;
ten production C/H paths **+152/−144 (net +8)**. This is a whole-owner
migration, not six compatibility wrappers or a new ISA/ABI feature.

| Gate | P42 result |
| --- | --- |
| Native ELF Module linkage fact test | **PASS**, 31/31 |
| Strict Release `-Werror` full toolchain `all` and full Debug `check-fast` | **PASS**, both exit 0; optional unavailable cross GNU/QEMU tests remain SKIP |
| Target Boundary and backend review inventory | **PASS**, 253 C/H files, zero new/missing/invalid inventory entries |
| Exact P41 vs P42 five-ISA `.i` broad differential | **255/255 same outcomes**, 187 byte-identical `.s`, 68 both rejected, zero different |
| Exact P41 vs P42 Inline/Section differential | **80/80 same outcomes**, 65 byte-identical `.s`, 15 both rejected, zero different |
| Five-ISA original Linux GNU ELF/QEMU | **NOT RUN** in this architecture batch; historical or gate-only passes are not current-head certification |

## IR-P43 — Canonical source CALL, one contextual Core resolver (2026-11-04)

**Baseline:** P42 PR #674 (local `0ebf0c3`). Reviewed the complete AST
Integer/Symbolic/Boolean variant-producer chain, Core direct scalar/record
call consumers, function-reference retirement, and Module reachability before
editing. This batch owns the *whole* caller-to-callee identity boundary, not
one target-specific code generator or another compatibility layer.

- **Immutable source CALL.** The three AST specialization passes no longer
  overwrite the shared `Expression.value.call.function_id`. A compiler-owned,
  append-safe `MinicInlineCallPlan` holds *lexical planning* selections across
  all three passes, including original-owner section closure. It is destroyed
  **before** Core lowering. Cloned FunctionBodies can therefore share their
  original canonical CALL without accidentally sharing one active caller's
  target.
- **One contextual Core choice.** Replace four staged candidate selectors
  with one callsite-proven scan over integer constants, normalized symbolic
  name/byte-addend, Boolean ranges, and the exact caller section. Never use
  facts invalidated by writes or unproved by the current context. For equal
  symbolic facts, prefer the exact source argument ExpressionId so two
  distinct inline GNU `asm volatile` callsites retain their separate emitted
  clone identity (`mc058_words+4` remains present twice).
- **One direct call producer.** Scalar, void, and Record-returning direct
  calls now share contextual callee selection, left-to-right argument value
  and Record-object snapshots, callee signature, and Core CALL append.
  Preserve the separate indirect path, empty-void-helper elision, Record
  variadic admission boundary, and machine ABI responsibility.
- **Retire AST-pruning finalizer.** Delete the entire 227-line
  `inline_reference.c` and its invocation/inventory. Clone creation inherits
  initial source reference status, while the already-existing P32 Core/Module
  closure determines final live emission. Move the still-needed opaque GNU
  asm name scanners to the existing specialization common module, retaining
  both compiler and ModuleIR consumers; no parallel reachability algorithm.
- **A verified GCC counterexample.** Prior P42 emitted five-ISA assembly for
  a section-qualified, multi-fact, direct Record-return call test, but its
  x86 GNU-linked executable returned **11** while GCC `-O2` returned **0**.
  P43's single Core path makes the MiniC result **0**. New identity unit
  tests verify plan growth, three sequential selections and canonical source
  CALL IDs after plan teardown.

**Batch size before this size paragraph:** 17 paths, **+781/−881**
(net **−100**); ten production paths **+414/−879** (net **−465**).
The four new integration/unit test files add 295 lines; this deliberately
replaces duplicate selection code and deletes an obsolete 227-line source.

**Verification:** strict Release `-Werror` build; Debug `check-fast` including
the new GNU/native and plan-identity gates; MC058 full relocations and stale
facts; exact P42/P43 255-case five-target broad differential and 80-case
inline/section differential. New P43 five-target `-S` plus x86 GNU AS/LD,
native return and ELF `.head.text`/`.init.text` checks are distinct from
five-target Linux/QEMU *runtime* verification. Optional cross-toolchain
paths may report SKIP when unavailable; do not count them as passing.

**Still open:** MC-007/008 mixed-fact counterexample coverage, MC-043 clone
budget semantics and MC-068 GNU always-inline return-address effects. The
direct Core selector currently scans all function variants linearly; a
measured per-source variant index is an optimization candidate, not a
correctness gate. The historical `b0-sources.txt` manifest was already out
of sync with more than this deleted source in P42, so no Stage4 self-host
claim is made here. Next full-owner batch: live typed GNU-ASM relocation
edges and truly const AST traversal, then five-target source/machine ASM
FormId contracts and shared machine dispatcher ownership. These are separate
from the original five-ISA Linux GNU ELF/QEMU acceptance campaign.

## IR-P44 — Live Core ASM symbol edges and genuinely read-only AST (2026-11-04)

**Baseline:** P43 local `b631d99`, PR #674. Read as one connected
ownership path: Source GNU ASM operands and specialization facts → seven Core
ASM admission handlers → six Core ASM instruction forms → reachable Core
blocks → Module function/object closure → common five-ISA ELF declarations.
Also read the AST CALL child walk together with its read-only and mutating
consumers. GCC's extended-ASM operand contract and LLVM's distinction between
function inline ASM and module-level ASM informed the ownership boundary;
neither reference grants MiniC the ability to parse arbitrary GAS text.

- **Seal live ASM facts in Core.** Each `MinicCoreInlineAsm` owns a verified,
  de-duplicated list of function/object assembler names and signed *byte*
  addends. Snapshot symbolic GNU `i` inputs at dispatcher entry, before
  output lvalue lowering can invalidate source-local specialization facts;
  publish them only on successful, matching Core ASM effects. The Core
  verifier, destructor and six ASM instruction kinds are migrated as one
  unit. No Source `ExpressionId` or C type leaks into the Core symbol edge.
- **Liveness is no longer source-global.** Remove the scan that promoted
  *every* Source inline-ASM operand into a Module root, including dead
  functions and folded dead branches. Only reachable Core ASM instructions
  now enqueue the names of needed definitions. Raw template spellings retain
  a narrowly scoped, live-ASM conservative fallback. Actual file-level GNU
  ASM is always emitted, so its lexical function/object roots are sealed
  separately at Module ingestion. Drop the producerless Module-wide
  `preserve_all_static_objects` flag; opaque nested operand storage retains
  a per-live-Core-ASM fallback instead. Preserve assembler-name aliases and
  emitted static-local ownership when a live ASM edge requires them.
- **One output fact.** Five targets continue to share the existing common
  staged output transaction. It uses emitted Core ASM's typed references
  to decide undefined `.weak` / `.hidden` declarations, and only scans raw
  templates of actually emitted Core effects. File-ASM roots use Module facts.
  No architecture gains another source scanner or a new machine layer.
- **Correct GNU `%cN` for the existing register-output/`i` form.** Complete
  the one-register-output immediate specialization to accept `%c1`–`%c9`,
  which emits a bare symbolic immediate. Keep `%0` as the register output
  and reject `%c0`. This exposes a genuine case where lowering `+r(*(&p))`
  invalidates the local fact *after* a symbol was rendered; the early Core
  snapshot preserves that live relocation.
- **No mutating read visitor.** Replace the historical child traversal with
  one shared child enumerator behind two distinct APIs: a `const` read-only
  visitor and an explicit remapper. Migrate FunctionBody, Core constant facts
  and normalization together. The 64-argument CALL test covers overflow
  storage, indirect CALL, exact source identity and actual remapping without
  writing through a shallow copy during a read.
- **Build dependency correction.** Include the missing generated `.d` files
  for frontend semantic-snapshot, function-diagnostic and backend Module
  arena test objects. A changed Module header otherwise left a stale test
  object containing the previous structure layout and caused a false
  `BACKEND_MODULE_ARENA` failure despite the updated compiler being valid.

**New GNU oracle:** `p44_live_asm_reachability.c` keeps live function/
object references and nested `+4` / `+8` symbolic byte-addends; GCC `-O2`
produces four `R_X86_64_64` relocations and drops dead function, dead object,
and `if (0)` ASM references. RV32/RV64 MiniC match the desired live/dead
symbol identities and emit the exact addends. The separate
`p44_inline_asm_mutating_symbolic.c` gives one GCC ELF relocation and a
GNU-native zero exit; RV32/RV64 retain the same symbol through a `+r`
output-lvalue escape. The x86/AArch64/LoongArch structured-immediate cases
are explicitly **SKIP** when their form is unadmitted; no cross-toolchain
runtime result is invented. An old duplicate-assembler-name test had to
change because GCC also prunes unused ASM and its unused static target.

| Gate | P44 result |
|---|---|
| Debug full `check-fast`, strict Release `-Werror` | **PASS** after regenerating the stale backend-arena test object |
| Core/Module MC064 + MC072 + MC073, x86 vDSO hidden unused | **PASS**; MC064 RV64 symbol pairing 3 relocations, MC072 four-ISA source checks |
| GNU `-O2` ELF/native oracle and P44 live/mutating ASM | **PASS**; GNU ELF 4+1 relocations, RV32/RV64 2/2 admissible forms in each probe, other three explicit SKIP |
| Exact P43/P44 new deterministic broad cohort | **255 outcomes unchanged**; 122 byte-identical, 133 both rejected, 0 differing (51 successfully preprocessed source cases × five ISA) |
| Exact P43/P44 new ASM/inline/section cohort | **80 outcomes unchanged**; 59 byte-identical, 21 both rejected, 0 differing (16 successfully preprocessed cases × five ISA) |
| Original five-ISA Linux GNU ELF/QEMU execution | **NOT RUN** in P44; source/ELF-level green gates do not certify whole Linux runtime |

**Still open:** raw live ASM needs conservative lexical matching, which can
cost `O(live ASM × Module symbols × template length)` on very large translation
units; profile Linux first and introduce a shared token/name index only if
that shows meaningful cost. The remaining Source/Target ASM FormId admission,
machine dispatcher C-descriptor boundary, MIR policy and the independent
five-ISA Linux GNU/QEMU correctness campaign are subsequent work, not silently
certified by this batch.

## IR-P45 — Whole-family GNU ASM machine-identity ownership (2026-11-04)

**Baseline:** P44 local `cdf1efd`, same-tree PR #674. Independently reread
five-target Source→Core→Machine ASM and the GCC extended-ASM / LLVM inline-ASM
ownership models before changing code. Machine emitters already consume
Core/Module/TargetInfo without reopening C AST or C types; the real gap was
that many Linux source-approved forms had no ISA-owned machine identity and
were reclassified solely by raw machine-stage template text. This batch does
**not** add MIR, new GNU syntax, ISA extensions, or broaden the C admission set.

### One connected Source/Core/Machine migration

- **x86-64:** preserve the ordered 111-entry C/GNU source qualifier table and
  its exact type, constraint, clobber and operand-count contracts. Add **69**
  FormIds to its previous **42**: **111/111 table entries now have a nonzero
  machine family identity**. All 111 IDs have matching target-owned native
  template checks and machine dispatch. The old 69 raw-machine branches now
  require their own exact family ID; a different/unknown ID never reaches
  another family's emitter. Existing source qualifiers for `nospec`, `percpu`
  and generic arithmetic/CPUID below the table still legitimately use ID0.
  The new 69 IDs seal **families**, not every sub-mode: their variant remains
  zero and the original machine-native matcher validates width/opcode/mode.
  No fabricated nonzero variant can claim one of those families.
- **AArch64:** census **29 named source qualifiers plus 23 direct branches**;
  reconcile these 52 admission paths with **51 machine FormIds** (the old four
  plus 47; two C `MRS` branches use one machine identity). The existing four
  strict LDAR/STLR/RAW_IO seals remain unchanged. All other source-approved
  forms must have matching original and normalized machine-family ID/variant
  before their C owner disappears. One target-owned classifier reuses the
  preexisting native matchers; machine emission independently rechecks the
  normalized form and its `TARGET_QUALIFIED` owner before any instruction.
  This includes LSE, HYP_SPIN, SMCCC, TLBI, alternatives and system registers.
- **ID0 is a real, narrow contract**, not a way around FormId: on both
  targets it requires variant zero and a valid owner, and unknown typed IDs
  fail closed. AArch64 forbids ID0 for its 51 sealed families except the
  existing exact generic zero-operand barrier; its full KEXEC crash-register
  snapshot remains a separately verified **GENERIC_STRUCTURED + ID0** form.
  x86 keeps the existing Core generic GPR/memory snapshot and generic
  arithmetic/CPUID shapes through their own exact native checks. Rejecting
  these blanketly would have regressed real Linux and prior Core consumers.
  Core IR data layout and the 32/64-bit RISC-V and Loong ISA ownership remain
  unchanged. `MINIC_ASM_OPERAND_PLAN_TRACE` now prints FormId and variant so
  original GNU C admission and later Core machine identity can be audited.

The new direct-machine adversarial test deliberately manufactures invalid
FormId, variant, owner, normalized template, Core pointee/type and SSA ID,
and requires **zero emitted bytes**. It also checks forbidden ID0 re-entry
into x86/AArch64 target-qualified ISA forms while protecting legitimate ID0
consumers. That test found and drove fixes for an x86 generic regression,
x86 nonzero ID0 variant, and AArch64 bad owner/ID0 fallback; the previous
hand-built `typed_asm_core_poison` fixture was updated to use legally owned
Core identities instead of treating owner=0 as a positive example.

### Exact evidence, including negative results

| Gate | P45 result |
|---|---|
| Full Debug `make -s -j8 MODE=debug check-fast`, strict Release `MODE=release all`, `git diff --check` | **PASS** |
| `check-p45-machine-form-boundary` and old `check-typed-asm-core-poison` | **PASS**; source→machine positives, forbidden IDs/owners/variants, poisoned Core types/SSA, no partial output |
| x86 futex GNU AS/LD/native + AArch64 hyp spin + AArch64 KEXEC crash snapshot | **PASS**; x86 native exit 42, exception table nine entries, A64 15 GPR pairs/large-frame negative cases; AArch64 cross-GNU GAS **SKIP** |
| Same GNU-preprocessed 22 true Linux ASM fixtures against P44 | **20/20 byte-identical `.s`**, **2/2 both rejected** at prior unrelated Core lowering; 244 source-qualified ASM instances, 243 with nonzero FormId and one intentional generic x86 ID0, **0 changed** |
| P44/P45 51-case five-ISA source cohort | **255 outcomes identical**; 122 byte-identical, 133 both rejected, 0 changed; 4 preprocessing skips |
| P44/P45 16-case five-ISA ASM/section cohort | **80 outcomes identical**; 59 byte-identical, 21 both rejected, 0 changed; 2 preprocessing skips |
| RV64 strict I/ABI + opcode-origin audit; x86 GNU file ASM | **PASS**; compiler I=61/non-I=0, Source ASM M/A/CSR/Zifencei retained; x86 ELF alias/native |
| Five-ISA Linux GNU ELF + QEMU kernel runtime | **NOT RUN**; these source/host oracles do **not** certify five Linux boots |

**Remaining independent architecture owners after P45 (not deferred patches of
this same GNU ASM change):** LoongArch's machine-text reparse/stack-spill
forwarding after GNU `.s` exists; x86 head physical-VMA use-role side tables
(MC-076); common Module global payload versus target-local ELF section
classification; duplicated C `symbol + byte_addend` evaluation for static
initializers and GNU `i`; x86 multiple bytewise aggregate-copy emitters
(MC-084's 4 KiB sample emitted 8,223 lines versus GCC's 65). GCC's
GIMPLE→RTL and LLVM's IR→MachineInstr layering reinforce **machine decisions
before rendering text** as the next ownership target. Do not reopen already
finished P38/P39/P42/P43/P44 type, clone, linkage or reachability work, and
do not confuse architectural ownership improvements with five-ISA Linux/QEMU
runtime correctness.

### P45.1 — Restore machine-observable AArch64 per-CPU LL/SC variants

After publishing P45, the **exact new PR head** uncovered one previously
uncovered change in the four-architecture GNU runtime CI: the 16-variant
original Linux arm64 per-CPU LL/SC sample failed Core lowering at
`percpu_8_add_return`, whereas P44's identical CI step succeeded. This was
**a new P45 regression**, not one of the inherited, still-red CI jobs.

The root cause was ownership, not an unsupported ISA operation: GNU's named
`[tmp]` versus `[ret]` output templates denote the same bare LL/SC machine
text after Core normalization replaces both with `%1`. P45 had incorrectly
encoded that erased Source name in `PERCPU_RMW`'s machine variant. Keep exact
Source named-operand admission, but now seal only the independently visible
machine **width and ALU operation** in its variant. Original and normalized
form IDs/variants then agree for all four widths × add/andnot/or/add_return.
An actual LSE `.altinstructions` replacement remains distinguishable by its
`ldadd` versus `stadd` opcode and the original, unchanged emitter/parser.

The host-only `check-p451-a64-percpu-normalized` gate executes the literal
two fixture-generation blocks from the **existing** cross GNU/QEMU oracle
(`probe-gnu-file-asm-multitarget.sh`), rather than maintaining second-source
templates. It checks the 16 LL/SC emissions, one original per-CPU **offset**
EL1/EL2 alternative (not the LL/SC LSE replacement),
17/17 nonzero FormIds, and **nine** invalid GNU template/operand cases;
when `P44_MINIC_CC` is available, it compares the resulting full `.s` bytes.
Exact P44/P45.1 byte diffs are **identical** for both fixtures (SHA256 prefixes
`39e402bc` and `bfb32d0a`), with no cross toolchain or QEMU claim. The
broader Debug `check-fast` and Release `-Werror` also pass locally.

PR #674 P45 head `a204fde` still had inherited P44 red jobs: x86 KVM guest
CPUID VMX missing, Native Strict isolated link missing
`minic_core_asm_names_symbol`, AArch64 NEON GCC pragma, RV32 full Zstd Core
lowering, and original x86 CMPXCHG128 Core instruction. Their P44 predecessor
steps failed for the **same concrete reasons**; none was silently treated as
this AArch64 regression. A new exact-head four-architecture CI run, not a
host source-only probe, must establish that the repaired arm64 LL/SC cross
GNU/QEMU step has recovered. Five-ISA complete Linux boot certification
remains separate.

The exact P45.1 four-architecture CI run `41781854184` subsequently
confirmed the AArch64 GNU file-scope ASM step **PASS**. The RV64,
AArch64, and LoongArch64 jobs completed successfully; x86-64 remained
red at the same preexisting original Linux CMPXCHG16B Core case. The
strict three-ISA GNU LD/QEMU Linux P0/P1 workflow also succeeded at
that head. This evidence closes the specific P45-induced arm64 regression,
not the independent five-ISA full Linux runtime qualification.

## IR-P46 — Coherent machine-text transport, aggregate copy, head address roles (2026-11-04)

**Provenance:** based on the exact P45.1 PR head `c9dcfa7`. An earlier
uncommitted P46 implementation passed the tests below but its transient
worktree disappeared after the executor disconnected. Both local snapshots
and remote refs were searched; no P46 patch survived. This P46 is an
independently **reconstructed and reverified** batch on a fresh clone of
P45.1, not a claim that the lost files were recovered. Work remained on
the same `refactor/minic-target-language-boundary` branch and PR #674.

### One related machine-output cleanup, without a new MIR

- **LoongArch64:** remove `single_use_generated` and a second full-function
  `open_memstream`. The old single-use spill forwarding produces complete
  lines for a stateful compactor directly; the latter stores native GPR,
  frame-slot and 32-bit sign-extension facts, no dangling reference to a
  previous text line. Original two public passes remain as differential
  reference APIs. Exact source-owned GNU inline-ASM begin/end barriers
  prevent transforming user instructions. This removes redundant full-text
  materialization, **not** all post-assembly-text optimization: early
  structured MachineIR ownership remains a separate future change.
- **x86-64:** unify four independent record/aggregate byte transports
  (Core RECORD_LOAD, RECORD_COPY, incoming parameter object and outgoing
  CALL memory argument) into one ISA-owned `x86_emit_record_move_bytes`.
  Small transfers retain the exact previous byte or qword/tail operations;
  large transfers use bounded-text `rep movsb`, preserving `rdi/rsi/rcx`,
  correcting outgoing `%rsp` relative offsets after three pushes and
  leaving overlapping-copy direction and exact byte extent unchanged.
- **x86 .head.text:** an independent same-source GCC GNU AS/LD and physically
  displaced native runtime oracle found a **real existing wrongcode**.
  RECORD_LOAD/COPY (and the analogous VECTOR_STORAGE_COPY) escaped the
  early-head physical-memory address census; two aggregate pointer operands
  were emitted as unrebased link-VMAs. Extend the same owner-owned role
  census to one/two addresses and rebase each emitted `r10`/`r11` address
  independently with `rax` scratch. Preserve numeric pointer→integer casts
  as link-VMAs and preserve the existing scalar path. Do not generalize
  provenance through pointer LOAD/PHI, opaque aliases or helper effects;
  those remain the unresolved MC-076 gap.

### Reconstructed direct correctness evidence

| Gate | Rebuilt P46 result |
|---|---|
| Loong machine-text standalone, preserving original two-step output | **17/17 PASS**, including user ASM barriers |
| Full `tests/compiler/c0/*.c` Loong P45.1/P46 differential | **1,052 cases**: 680 byte-identical `.s`, 372 both rejected, **0 changed**; 61 preprocessing skips |
| Nine aggregate sizes 1,3,7,8,9,2048,4096,4097,16384, five-ISA `-S` | **PASS**, x86 GNU AS/LD and same-source GCC/MiniC native PASS; byte guards, source mutation, self-copy |
| Nine-size x86 ASM text size with identical native result | P45.1 **2,649,716** bytes → P46 **98,696** bytes, **96.28% less** on this fixture |
| x86 early-head physical role, previous wrongcode versus GCC | Reconstructed GNU/MiniC displacement **3/3 PASS**: direct record copy, cast round-trip, record load; ELF PC32 vs integer link-VMA 32S and PIE admission PASS |
| Original MC-076 scalar, x86 M1, MC082 GNU/MiniC SysV mixed caller/callee | **PASS**, four mixed ABI modes |
| Full Debug `make -s -j8 MODE=debug check-fast`, strict Release `MODE=release all`, `git diff --check` | **PASS** after combining all reconstructed P46 production and tests |
| Five-ISA complete Linux GNU ELF/QEMU kernel runtime | **NOT RUN** in P46; above host-oracle evidence is not five Linux boot certification |

The supporting host gates are `check-p46-aggregate-and-loong-oracle`,
`check-p46-x86-head-aggregate` and the extended existing Loong text-opt test.
Their same-source GNU C producer and GNU GAS/LD runtime checks are independent
of the tiny optimized assembly size; they guard semantics, not code volume.

**Separate remaining architecture owners from the global audit:** RV32/RV64,
x86 and Direct A64/Loong have duplicate ELF default-section/output policy
even though P42 already sealed symbol linkage and Module fragments. Their
ABI/`.bss` assembler dialects differ and require one target-flavored ELF
output plan, not a blind identical switch. Static initializer `symbol+
byte_addend` keeps member paths and strict C storage rules, whereas GNU
`i` evaluates specialization-local facts and GNU zero-size/void strides;
share only checked byte arithmetic and sealed facts, not C walkers. These
are P47 candidates, not silently patched into this machine P46 batch.

## IR-P47 — Five-ISA ModuleIR ELF global-output plan (2026-11-04)

**Status:** one coherent implementation batch on
`refactor/minic-target-language-boundary` / PR #674, **local focused gates
verified; cross GNU ELF/QEMU Linux execution NOT RUN**.
P47 continues the completed P42 Core Module payload/symbol ownership and
P46's explicitly deferred global-output boundary. The exact submitted
commit must be recorded after integration; the local source/host oracles
below are completed evidence, **not** a five-ISA Linux execution claim.

### One output decision with target-specific ELF dialects

- **Core/Module boundary stays sealed:** `src/core/module_ir.h` continues
  to own symbol identity, visibility, linkage, default-section intent,
  layout, initializer fragments and relocations. No `MinicType`, C AST
  initializer or parser symbol ID is read by the ELF object writers.
- **Common ELF object plan:** `src/target/common/module_global_emit.[ch]`
  introduces `MinicModuleElfGlobalPlan` and a checked builder that resolves
  default/explicit section, `PROGBITS` versus `NOBITS`, section flags,
  binding, visibility, data-directive callback, signed literal treatment
  and relocation field width *before writing output*. The same Common
  emitter serializes the planned section, binding, visibility and payload.
  Keep function-symbol visibility as its separate public primitive; this
  plan is for global **objects**, not an unrequested whole-linker redesign.
- **Five ISAs, four writer families:** `target/riscv32/rv32_global_emit.c`,
  `target/riscv64/global_emit.c`, `target/x86_64/global_emit.c` and
  `target/common/direct_program_emit.c` (shared by AArch64/LoongArch64)
  consume that one plan rather than each reconstructing the Common policy.
  The target may still choose a legacy default-section fallback, assembler
  directive spelling, relocation width and explicit `.bss[.*]` section
  convention. In particular RV32/RV64's explicit `@progbits` behavior is
  intentionally **not** rewritten to x86/Direct's `@nobits` convention.
- **One Core-only scalar shape query:** the identical RV32/RV64 imported
  Core type unwrap/width classification moves into the Common module
  global emitter; each RISC-V target still owns admission for Record/Union,
  zero extent, weak-zero storage and its machine/ABI-specific directives.
- **Removed the superseded second decision:** old Common standalone global
  `emit_elf_section` and `emit_elf_binding` entry points and duplicated
  target-local policy switches are removed in this same batch. Keep
  existing `minic_multi_module_emit_global_fragments` as the single
  checked, contiguous storage/relocation serializer. Do **not** add MIR,
  move C constant evaluation into Target or change the five backends' ABI.

The architecture comparison is deliberately narrow: GCC separates C
initialization/decl semantics from assembler/linker section and symbol
decisions; LLVM IR separates global linkage/initializer facts from ELF
object-file lowering. MiniC's equivalent seam is **C ingress → sealed
Core Module → one target-flavored ELF output plan → GNU `.s`**. GNU GAS/ELF
dialect differences remain data/policy choices inside the individual
writer, not reasons for four more copies of the symbol algorithm.

### Verified focused gates and explicit non-claims

| Gate | P47 observed result |
|---|---|
| Core Module global-fragment standalone probe, migrated to the new ELF plan interface | **PASS** (local standalone); this is not a GNU cross-target ELF execution |
| Pre-P47 compiler versus new Debug RV32/RV64 204-case source/output differential | **PASS**, **98** byte-identical `.s`, **106** both rejected, **0** changed; this is 204 target cases, not a claim that all five ISAs were covered |
| `check-p47-five-isa-output-smoke` (six dependent groups) | **PASS**, includes both P46 x86 GNU AS/LD/native gates, nine-object x86 ELF ABI, RV32 and RV64 source ABI, and five-ISA section/weak/sparse/relocation/injection source probes |
| Five-ISA default/explicit section, weak binding, sparse storage, relocation and malformed-input source evidence | **PASS** under the focused smoke gate; this is `-S` source-level coverage for non-host targets |
| RV32/RV64 legacy scalar directives and weak-zero, x86/Direct explicit NOBITS policy | **PASS** in the respective source ABI/section probes; deliberately different GNU dialects retained |
| `tests/compiler/c0/check-minic-target-boundaries.sh` | **PASS**, no reintroduced C AST/Type dependencies below Module ingress |
| Debug `make -s -j8 MODE=debug check-fast` | **PASS**, local exit 0 after the Common plan and target-writer changes |
| Strict Release `make -s -j8 MODE=release CFLAGS=-Werror all` | **PASS**, local exit 0 |
| x86 nine-object global ABI GNU AS/LD/native oracle | **PASS**, GNU ELF/native execution on host; does not imply remaining targets' GNU ELF/QEMU success |
| YAML syntax, Bash syntax and `git diff --check` | **PASS** in local focused verification |
| Cross GNU AS/ELF, GNU LD and QEMU execution for RV32/RV64/AArch64/LoongArch64 at this P47 head | **NOT RUN** — source/host passes cannot substitute for cross-target object and runtime certification |
| Four-architecture GNU runtime CI and three-ISA Linux GNU/QEMU P0/P1 workflows at this P47 head | **NOT RUN** — historical predecessor workflow successes do not certify this head |
| Complete five-ISA Linux GNU ELF/QEMU kernel runtime | **NOT RUN** in P47; source `.s` and host GNU ELF checks are not Linux boot evidence |

**Next independent owners after P47:** the unresolved x86 `.head.text`
physical-VMA provenance through pointer LOAD/PHI/alias (MC-076), and
duplicated checked 64-bit arithmetic in static initializer relocation,
GNU `i` specialization and bounded Core constant facts. The latter should
share safe arithmetic primitives while preserving the distinct C static
initializer member paths and GNU ASM specialisation/zero-stride admission.
No P38–P46 type, clone, ASM symbol-reachability or aggregate-copy migration
should be reopened merely because this ELF owner is being consolidated.

## IR-P48 — x86 early-head physical/VMA provenance through Core local memory (2026-11-04)

**Status:** one x86-address-provenance mechanism batch, on the same
`refactor/minic-target-language-boundary` branch / PR #674. The local host
and GNU ELF proof below was observed; full x86 Linux pre-MMU QEMU and full
five-ISA Linux runtime remain **NOT RUN** at this head. Keep MC-076 `OPEN`
until independent opaque alias/cross-function proofs and real boot evidence.

### The complete owner, not per-opcode bandages

- **Actual prior wrongcode:** the P47 `x86_head_address_global` def-walk
  stopped on Core pointer `LOAD`. A pointer stored into a local Core object
  and reloaded could retain the *link VMA* while used as a displaced physical
  address. New `.head.text` + `.head.data` same-displacement native tests
  reproduced eight failures on the old host binary, including independent
  direct global data traffic poisoning a conflicting local pointer's numeric
  VMA return. These are real displaced execution failures, not solely text
  mismatches.
- **One x86 plan:** the `x86_64/codegen.c` HeadAddressPlan derives facts
  from frozen Core SSA and `ObjectId`, not C AST or frontend symbol text.
  Every derived local address must prove an exact `(object_id, byte_offset)`
  through object/field/constant offset forms; 8-byte pointer/integer slots
  have a Core type and extent check. The **entire address-use graph** is
  scanned for escapes, indirect/opaque uses, aggregate/parameter effects and
  unproven offsets. Those invalidate private memory provenance.
- **Cross-block transfer is real memory flow:** local `STORE` updates the
  slot's `{global_id, crossed_vma_cast}`; local `LOAD` uses the current slot
  fact; predecessor joins retain only agreed origins, including variable
  field offsets at the *global* consumer. The fixed point checks both slot
  state and SSA origin convergence, including reversed block order/loops.
  Core currently has no PHI opcode; do not invent PHI handling.
- **One producer of the three machine roles:** the completed address plan
  fills the existing physical direct-global, VMA scalar-cast, and
  memory-rebase arrays. `machine_emit.c` remains target owner of actual
  RIP/absolute selection. Numeric VMA casts are a consumer effect, so an
  SSA value can still feed real displaced memory and a link-VMA computation.
  The same `x86_head_memory_addresses` census now includes aggregate/vector
  copies, `INTEGER_OVERFLOW.result_address`, `VARIADIC_NEXT_SCALAR` and both
  sides of `VARIADIC_COPY`. Machine rebasing preserves live arithmetic
  registers and flags (overflow paths reserve `%rcx` scratch).
- **Proven versus unknown:** an exact private pointer slot, dynamic global
  field offset and same-origin branch join are supported. Conflicting or
  escaped aliases which could inherit another direct use's physical global
  representation cause an explicit head-provenance compile rejection,
  rather than a guessed binding or silently wrong executable. Opaque
  cross-function pointer effects remain beyond this local proof.
- **No unrelated redesign:** non-head `.text`, PIC/PIE global addressing,
  Core language types, other four target backends, GNU assembler and linker
  stages are unchanged. P46's record-copy owner and P47's global-output
  plan remain independent and fully exercised.

### Verified targeted host/GNU evidence

| Independent gate | P48 observed result |
|---|---|
| P47 Debug prior to fix, physically displaced `.head.text` native oracle | **8/11 FAIL** in the strengthened pre-fix fixture; real physical data and link-VMA wrongcode confirmed |
| P48 same-source GNU-AS/LD ELF and physically relocated MiniC execution | **9/9 PASS**: direct controls, local pointer, two numeric/pointer roundtrips, two agreed branch paths, pointer-containing Record field, overflow result address |
| Ambiguous conflicting global and escaped alias with independent direct memory role | **2/2 expected rejection** with explicit diagnostic and no partial output; these are intentionally **not** claimed as executable physical positive controls |
| GCC `-O2` same-source displaced controls | **2/2 PASS**; GNU `-O0` local pointer storage does not reliably produce a displaced-physical oracle |
| Original MC-076 scalar and P46 aggregate GNU ELF/physically displaced native | **PASS**, separately rerun |
| Ordinary `.text`, PIC/PIE, GNU link/relocations and x86 host execution | **PASS** in the P48 focused gate |
| Debug `make -s -j8 MODE=debug check-fast` and Release `make -s -j8 MODE=release CFLAGS=-Werror all` | **PASS**, complete local exit 0 after combining P48 production, one-gate fixtures and review records |
| `bash tests/compiler/c0/check-minic-target-boundaries.sh`, `git diff --check` and P48 Bash syntax | **PASS**, source/target ownership unchanged |
| Full x86 GNU Linux `Image` pre-MMU under QEMU for P48 exact head | **NOT RUN**, required before declaring MC-076 Linux-safe |
| Full five-ISA Linux GNU ELF/QEMU | **NOT RUN**; x86 host ELF does not certify other architectures |

The complete new evidence is one `make check-p48-x86-head-local-provenance`
target (included by `check-fast`) and one native strict CI step, backed by
`tests/compiler/c0/probe-p48-head-local-provenance.sh` and its same-source C,
displaced native driver, and separate deliberately uncertain negative fixture.

## IR-P49 — x86 `CMPXCHG16B` GNU 128-bit memory shape (2026-11-04)

**Owner:** one complete x86 structured-ASM machine admission correction and
its *already-existing* two-source GNU/native/ELF differential gate. No new
generic Core atomic opcode, second ABI algorithm, or 16-byte LP64 register
scalar path. The original in-flight P48 exact-head CI failed this pre-existing
test, including at its previous P47 parent.

### End-to-end source/Core/machine proof

- The x86 source qualifier already admits exactly two distinct original
  Linux template forms: a locked native 16-byte CAS with `=@cce` condition
  output and `.smp_locks` relocation, and a GS per-CPU alternative with an
  old kernel emulation call plus replacement instruction. Each carries a
  single 16-byte `+m` value and four exact x86 register scalar roles. Source
  lowers all operand effects to one structured Core ASM plan; the verifier
  accepts the typed pointer object and effect. None of this needed changing.
- The machine `emit_linux_cmpxchg128` previously used the ordinary scalar
  width predicate to validate the **16-byte memory object**. Its Common
  LP64 integer classifier, correctly limited to 1/2/4/8-byte GPR scalars,
  necessarily rejects 128-bit objects. Before the fix, the original native
  script emitted `function cmpxchg128, kind 43` unsupported Core diagnostic.
- P49 adds a target-only CoreType memory predicate requiring `INTEGER`,
  `bit_width==128`, full 16-byte layout and at least 16-byte alignment, and
  applies it solely to the CMPXCHG128 memory binding. The other source-
  qualified flags, registers, GNU clobbers, machine template, ELF section
  and emitted x86 instruction remain under their existing exact proof.
  In particular ordinary register operands still cannot become 16-byte
  scalar GPRs just because CMPXCHG16B accepts a 128-bit *memory* operand.

### Current exact-working-tree verification

| Gate | P49 observed result |
|---|---|
| GNU C source and MiniC `-S` through GNU AS/LD native hardware | **PASS**, both CAS success/failure routes, independent GNU/MiniC executables each exit **42** |
| x86 machine sequence | **PASS**, `lock; cmpxchg16b (%r11)` and immediate `sete %r10b` |
| Kernel ALT Linux ELF metadata | **PASS**, 14-byte `.altinstructions` record, feature **141**, branch lengths **8/5**, two PC32 ELF relocations; replacement **not executed** |
| Debug `make -s -j8 MODE=debug all` | **PASS**, exit 0 |
| Full five-ISA GNU Linux/QEMU and latest-head x86 original kernel boot | **NOT RUN**, targeted host/ELF proof is not full Linux acceptance |

The unified permanent gate is
`tests/compiler/c0/probe-x86-linux-cmpxchg16b-native.sh`; it exercises both
Linux original atomic and ALT sources. No empty placeholder source or
standalone smoke was substituted for actual native/ELF verification.

## IR-P50 — One RISC-V incoming-stack-anchored variadic ABI (2026-11-04)

**Scope:** MC-014 RV32 ILP32 and MC-015 RV64 LP64 together. The independent
frame emitters had the same physical ABI defect: a 64-byte-aligned local
forces machine frame base S = alignDown(E − frame_size, 64), but only the
original incoming E is contiguous with the caller's spilled arguments.
Saving remaining a-registers at S + varargs_offset instead of immediately
below E misplaces the register-to-stack cursor by a runtime phase of
0/16/32/48 bytes. RV32 additionally used false S + frame_size when no
GPR remained; RV64 already recovered saved E there.

- Both machine emitters now make E the source of truth whenever dynamic
  frame alignment is active. For V = (8 − named_gprs) × XLEN/8, remaining
  incoming register words occupy exactly [E − V, E), and va_start begins
  at E − V. No C type, parser hint, ABI plan clone, extra Core opcode
  or redundant xlen-specific source analysis was introduced.
- When V=0, RV32 now derives the variadic cursor as E plus named stack
  slots times 4, not a distance from the realigned frame. RV64's
  existing E-based zero-GPR path and both architectures' ordinary
  16-byte aligned frames remain unchanged.
- Frame storage remains legal: both frame planners already reserve V
  bytes above fixed objects and preserve E in a private frame slot;
  the actual realignment adds nonnegative slack. For each permitted E
  mod64 phase, E−V stays above the saved entry-SP slot and below the
  caller's first stack argument. Physical register spills and va_start
  now use that same interval, including moving alloca stack functions.
- One permanent GNU C source exercises one/seven/eight/nine named
  arguments plus moving alloca, all with a live 64-byte-aligned local.
  The ninth named argument exercises the nonzero named stack-slot offset.
  The source/ISA
  probe checks actual RV32/RV64 register offsets, the exact saved
  entry-SP reload in each function, and four E mod64 phases
  algebraically; it rejects old positive realigned-SP spill offsets.

| P50 focused verification | Observed |
|---|---|
| Debug MiniC compiler build after both target changes | **PASS** |
| GNU C host semantic/preprocessor source accepted by MiniC RV32I/ILP32 and RV64I/LP64 | **PASS** |
| RV32 source assembly: one/seven/eight/nine named arguments, moving alloca | **5/5 PASS**, each **4/4** entry-SP phases |
| RV64 source assembly: same five cases | **5/5 PASS**, each **4/4** entry-SP phases |
| Genuine GNU GAS/LD ELF and QEMU for RV32/RV64 | **NOT RUN locally** (cross GCC, binutils and QEMU unavailable); execution remains mandatory |
| Complete five-ISA Linux boot/runtime | **NOT RUN** in this ABI-focused batch |

The focused target is make check-p50-riscv-overaligned-varargs, included
in check-fast. This paragraph records the initial local-only gate; the
subsequent exact-head remote GNU/ELF/QEMU results follow.

**P50 actual machine evidence (2026-11-04):**

| Original GNU/MiniC binary execution | Exact-head CI verdict |
|---|---|
| RV32 GNU GAS/LD ELF32 QEMU, four E mod64 phases × five functions | [run #41830221934](https://github.com/yituanxing/minic-toolchain/actions/runs/41830221934) **GNU 20/20 PASS; MiniC 20/20 PASS** (later unrelated Zstd stage failed) |
| RV64 GNU GAS/LD ELF64 QEMU, four E mod64 phases × five functions | [run #41830222237](https://github.com/yituanxing/minic-toolchain/actions/runs/41830222237) **GNU 20/20 PASS; MiniC 20/20 PASS**, full four-ISA runtime matrix 4/4 SUCCESS |
| x86 original Linux 6.6.143 frozen 2758 `.i` units, seven independent producers | [run #41830221931](https://github.com/yituanxing/minic-toolchain/actions/runs/41830221931) **653/2758 MiniC C units compile**, seven producers blocked; linker/QEMU receiver **NOT RUN** (no seven complete candidate artifacts). This is not an x86 guest kernel failure. |

MC-014 and MC-015 now `FIXED-UNVERIFIED` with true GNU ELF/QEMU
focused machine evidence; complete multi-ISA Linux remains a separate gate.

## IR-P51 — One qualified GNU anonymous-record declaration contract (2026-11-04)

**Single shared-language fix, not a 2030-file special case.** The exact-head
seven-shard x86 Linux 6.6.143 campaign
[#41830221931](https://github.com/yituanxing/minic-toolchain/actions/runs/41830221931)
compiled 653 of 2758 frozen original GNU `.i` owner files. Its 2105 first
failures comprise 2030 `record has no such member`, 27 module-global snapshot,
24 Core lowering, 16 Core unsupported, seven typed inline ASM, one AST
contract. In 44 retained original `.i` diagnostics from the affected
family, the member errors point to Linux's `vma_end_read(vmf->vma)` inline. This
is a single common source declaration contract, not independent errors in
drivers, net, fs or arch.

`parse_record_field` previously sampled `current == KW_STRUCT/KW_UNION`
*before* `minic_parser_parse_type_specifiers`, which consumes `const` /
`volatile`. GNU's valid `const struct { T *vma; ... };` is therefore
silently omitted from the enclosing record. The common existing anonymous
member field-path search, type qualifier propagation and Core layout
already own downstream semantics correctly. P51 changes only direct
record-specifier classification to use a lexer-only non-mutating lookahead
across qualifiers / GNU `__extension__`. It preserves true tagged or
typedef-only bare declarations, whose meaning is *no* nested member.

The permanent `check-anonymous-qualified-record-members` proof compares
GNU and MiniC on the same preprocessed C source, x86 native runtime and
five target `-S` outputs; it also rejects modification of promoted
const members and rechecks tagged/typedef nonmembers. Unmodified original
Linux `arch/x86/entry/vdso/extable.i` (40,716 lines) now proceeds through
MiniC machine assembly and GNU AS to ELF64 relocatable `.o`. Unmodified
Linux `arch/x86/events/core.i` (2,900,614 bytes, SHA-256
`2a1835a5577862cb7b4a1ae7c57d8680a7f45b6d903927219491494a1ba93578`)
no longer fails at `:23797:21` on `vmf->vma`; with its original GNU Kbuild
flags it reaches a different later blocker, Core lowering for
`perf_events_lapic_init`. A later failure is **not** a full original `.i`
PASS. A complete **56/56** preserved frozen `.i` sample (eight authentic
first-failure owners per shard, seven shards) was recompiled by fixed MiniC
with the original x86 effective flags: **16/56 successfully emitted `.s`**;
the other 40 advanced to new failures (**19** Core lowering, **13** Core
unsupported, **7** ModuleIR global snapshots and **1** structured inline
ASM), and **0/56** retained the old `record has no such member` error.
All 2758 owners and linker/QEMU remain to be reverified separately;
2030 cannot be credited as fixed-to-completion from first-failure census.

**P51 exact-head full requalification:** [run #41832840551](https://github.com/yituanxing/minic-toolchain/actions/runs/41832840551) used the corrected MiniC and the same 2758 SHA-locked original Linux `.i` owners, 394/shard × 7 shards. Results were respectively **199, 206, 201, 210, 206, 209, 207** successful MiniC/GNU AS objects: **1438/2758 = 52.14%**, up from **653/2758 = 23.68%** (**+785 verified complete C units**). The old member-not-found error disappeared across the entire corpus. Remaining 1320 first failures: **1019 Core lowering**, **156 Core Module globals**, **72 AST contract**, **52 Core unsupported**, **21 x86 machine ASM kind43**. Seven runners still failed to furnish complete object sets; receiver final GNU LD and QEMU **NOT RUN**. This is an important real improvement, not a Linux Boot PASS.

## IR-P52 — x86 Linux SMP XADDq independent 64-bit integer rank admission (2026-11-04)

The existing typed x86 Linux SMP LOCK XADD lowering recognized the exact
template, memory clobber, `+r`/`+m`, machine width and `.smp_locks` section,
but its source contract rejected the legal `signed long long` operand used
by GNU Linux's `atomic64_t.counter` (source `__s64`) because it demanded
only `MINIC_INTEGER_RANK_LONG` for width eight. The correction handles the
complete **8-byte XADDq operand class** once: independent signed/unsigned
`long` and `long long` C scalar ranks on both read/write operands, with
old width 1/2/4 rank admission and existing x86 machine emission unchanged.
No Linux function-name whitelist, no new Core opcode, and no source-to-machine
ABI bypass.

The existing `probe-x86-linux-abi-carry-xadd.sh` now exercises actual
GNU/MiniC native atomic `lock xaddq` hardware for eight cases, including
64-bit signed/unsigned/mixed ranks and kernel-shaped `atomic64_add_return`;
both compilers agree on returned values and updated memory, illegal widths
fail closed. Focused x86 native PASS **8/8** and GNU/MiniC reference
PASS; Debug `check-fast` and Release `-Werror` PASS. Current complete
Linux P51 21 kind43 failures include nine `arch_atomic64_add_return`
cases plus related `arch_atomic64_fetch_add`/`local_add_return`. Exact
original all-file requalification of P52 is still mandatory.

## IR-P53 — Nominal incomplete-enum CoreType without false integer ABI (2026-11-04)

After P51, authentic x86 Linux original GNU `.i` samples exposed a shared
**Core type import** failure with two diagnostic faces: ModuleIR globals
`cannot snapshot Core module globals` and function `Core IR lowering failed`
(even when the function's CFG lowering had actually succeeded). The common
trigger is a forward `enum` in a transitive function-pointer signature:
`fwnode_operations.device_get_dma_attr` returns incomplete
`enum dev_dma_attr` beneath `extern platform_notify`, and `con_build_attr`
takes incomplete `enum vc_intensity` beneath `conswitchp`. No storage or
calling ABI is needed merely to retain these function-pointer types, yet
`import_type` incorrectly forced incomplete enums through C's effective
integer type (which legitimately refuses incomplete layout).

P53 extends **one existing nominal opaque CoreType cache**, keyed by source
identity plus incomplete-enum vs target-builtin class. Truly incomplete
C enums become distinct `MINIC_CORE_TYPE_OPAQUE` IDs with `has_layout=0`;
complete enums still become their actual integer representation. No Target
or C language identity is enumerated in the Core IR, and by-value incomplete
objects are still rejected. New importer unit tests assert two nominal
incomplete enums differ, retain no layout, and survive pointer wrapping,
while complete enums have integer ABI. The GNU C fixture uses both actual
public extern function-pointer globals and native complete-enum callback,
with five-target `-S`, GNU/MiniC native, negative incomplete-object, SAN
and `check-fast` all PASS.

**Original Linux evidence:** seven of seven initial frozen ModuleIR globals
failures advance to complete `.s`, with `apic/probe_64.i` assembled into
GNU ELF64. Authentic `events/core.i` moves past `perf_events_lapic_init`
function CoreType snapshot to a later `wrmsr` reachability problem. Comparing
56 current frozen P51 failure `.i` samples before/after P53 shows
**0→20 complete `.s`**, Core lowering **24→2**, Module globals **12→0**;
remaining 34 moved to later blockers or existing unchanged categories.
This is source-verified evidence, **not** proof that all 1019+156 original
owners pass; fresh seven-way full corpus remains mandatory.

## IR-P54 — Core/ModuleIR reachability-driven late lowering fixed point (2026-11-04)

The P53 incomplete enum repair exposed that many genuine Linux 6.6.143
`Core IR does not yet support function 'wrmsr'` (also `cpuid` / `rdtsc`)
were not ISA operation gaps at all. `minic_prepare_core_function_set`
intentionally skipped initially C-unreferenced internal function bodies,
recording the default `MINIC_CORE_LOWER_UNSUPPORTED`. The later Core/Module
symbol closure marked some of those definitions live through a real Core
call, inline ASM symbol or object relocation, but the compiler never
revisited their body. The final validation mistook *never tried* for
*unsupported* and rejected the translation unit.

P54 adds a **compiler-owned per-FunctionId attempted ledger** and extracts
the initial C-to-Core lowering into one transactional lower-one path (one
SourceBuilder, type snapshot, Core verifier, take and fold). A single
closure-completion stage runs existing source-independent Core/Module
reachability, lowers just newly live, still-unattempted definitions,
then recloses until the live graph stabilizes. A nonfinal round consumes
at least one function, making it bounded; dead bodies still avoid Core
lowering and genuine failures still fail closed. No new ISA instruction,
target-specific heuristic or duplicated Core lowering path was added.

**Authentic Linux evidence:** 19 prior original `Core lowering failed`
owners were rebuilt with this P53+P54 compiler: **16/19** emit `.s`, two
EFI Core verifier problems remain, one `arch_static_branch` has a true
remaining operand constraint. For the complete 56 SHA-locked P51 failure
sample, P53 alone emitted **20/56** `.s`; P53+P54 emits **50/56**, and
**all 50/50** assemble via GNU GAS into true `ELF64` x86-64 relocatable
objects. Authentic `arch/x86/events/core.i` emits ~1.18MB `.s`, GNU
relocatable `.o` ~302KB with one late-live function (`wrmsr`) lowered.
One permanent Core closure test covers transitive late-live symbolic
retention and all five source targets; full Linux 2758 owner and QEMU
remain separately required.

## IR-P55 — AST array-object qualifier agreement for GNU C (2026-11-04)

P51's exact seven-shard census contained **72** `invalid expression
contract` first failures, **65** in networking. One authentic GNU
`net/mac80211/agg-tx.i` failed at `(const void *)&he_oper->optional`:
`he_oper` points to a `const struct ieee80211_he_operation` whose
`optional` field is a flexible array. The AST producer correctly promotes
the enclosing lvalue's const/volatile to the array elements, while
`ast_verifier.c::array_object_type_matches` compared the expression's
qualified element against the raw unqualified interned ArrayType element.
The verifier rejected a valid, already represented language type.

P55 compares the **effective** cv-qualified materialized element to
the actual expression element, preserving original element identity,
incomplete/flexible/zero/fixed array extent and object ownership checks.
The permanent GNU/MiniC probe covers fixed, GNU zero-length, flexible and
nested array members, five-ISA `-S`, x86 native execution and negative
const-write / wrong-element-pointer cases. Exact original GNU Linux `.i`
with their pinned x86 Kbuild flags from `net/mac80211`, `net/wireless`,
`net/netfilter` and `fs/lockd` (all **11/11** retained shard0 AST-failed
owners) proceed through MiniC `-S` and GNU GAS to real ELF64 relocatable `.o`. This
confirms a shared C language verifier bug; no 65/72 full-corpus claim
without a fresh seven-shard run.

**P52–P55 exact-head full requalification:** [run #41837374647](https://github.com/yituanxing/minic-toolchain/actions/runs/41837374647), remote commit `d7b9d22f832b162de350c4a23452d4587c883125`, reused the same SHA-locked 2758 original GNU Linux 6.6.143 `.i` owners (394 per shard). The seven independently compiled-and-assembled success counts are **378, 380, 378, 385, 381, 387, 383**, totaling **2672/2758 = 96.88%**, up **1234** fully assembled objects over P51's 1438, and **2019** over the original 653. The remaining **86** first errors are **65 Core IR lowering**, **13 x86 machine-ASM kind 43**, **8 Core unsupported**. This census reports **zero** leftover GNU anonymous-record, ModuleIR global snapshot or AST qualifier errors. All seven producer jobs are necessarily incomplete because of those 86 missing objects; the final GNU linker and QEMU Linux receiver **did not run**. This is a full *compile+assemble* gain, not a claim of Linux boot, and subsequent fixes must be requalified against the exact frozen source again.

## IR-P56/P57 — Coherent Core type ingress, cleanup control flow and x86 descriptor ASM (2026-11-04)

The P55 86-first-error corpus was reclassified without regenerating or editing GNU `.i`: 65 Core lowering, 13 machine ASM kind43 and eight unsupported Core functions. All **41** frozen `.i` samples retained from the 65 Core failures were traced at the original Core verifier: 21 `FIELD_ADDRESS`, 14 `RECORD_COPY`, four `STORE`, two `PARAMETER_OBJECT`. The common Core defect was losing a record's ABI alignment/qualifier wrapper when comparing its nominal stored value; qualified EFI GUID records exposed it in parameter objects, copies, calls and returns. A second related ingress defect duplicated otherwise *identical* C function-pointer execution signatures under separate source declarator IDs, making record field addressing appear inconsistent. C→Core importer now canonicalizes only matching return type, exact parameter list, varargs and calling convention, while direct Core producers retain nominal function identity and indirect-call ID checks. Source-verified `FIELD_ADDRESS` from `drivers/md/dm.i` has matching complete signatures; this is not blanket pointer compatibility. `STORE` also correctly discards top-level scalar value qualifiers, without admitting unrelated nested pointer types. The 41 original GNU Linux Core verifier owner samples now have **37/41 MiniC `-S` PASS**: record copies **14/14**, parameter objects **2/2**, field addresses **20/21** and stores **1/4**. Four nested pointer-qualification cases remain explicitly fail-closed; follow-on STORE barriers may emerge after preceding cases unlock.

Independently, GNU `cleanup` scope metadata was incorrectly treated as a lifetime-exiting edge for ordinary inline ASM; only actual return/break/goto or scope exit has this behavior. The shared Core lowering rule now permits same-lifetime ASM (including already-validated same-lifetime `asm goto`) and preserves cross-cleanup goto rejection. Frozen original `kernel/jump_label.i` advances from `static_key_slow_inc_cpuslocked` unsupported to **MiniC `.s` + GNU AS `ELF64 ET_REL` PASS**. Another true target gap was a valid *10-byte packed record* operand to `lidt/lgdt/sgdt/sidt`: the x86 scalar-width admission gate incorrectly rejected it. One strict descriptor-table Core record check now covers all four, with size/layout negative tests; frozen `arch/x86/boot/compressed/idt_64.i` and `arch/x86/kernel/cpu/common.i` both pass MiniC→GNU GAS ELF64. Privileged instructions were **not** executed in userspace. New probes cover the relevant five-ISA C→Core import, x86 native GNU differential/ELF, cleanup lifetime and rejection cases; the fast test gate includes the probes.

Finally, the seven-runner diagnostic exporter now stratifies its unchanged **8 owners per shard** original-`.i` budget by actual first-error family, retaining uncommon machine-ASM and Core unsupported cases that alphabetic first-eight selection previously hid. Successful object acceptance, full owner count, cached inputs, GNU toolchain and QEMU gates are unchanged. The previous seven JSON manifests prove the next run will retain missing `net_namespace.i`, `sock_diag.i` and `ring_buffer.i` failures. **This P56/P57 local replay is not a new 2758-owner qualification**; the next exact-head full seven-runner MiniC/GNU AS, linker and QEMU result remains pending.

**P56/P57 exact-head seven-way requalification:** [run #41842988218](https://github.com/yituanxing/minic-toolchain/actions/runs/41842988218), remote commit `fba099626135c97e2bf284ee4d9227b20b9fc0d4`. The SHA-locked original GNU Linux 6.6.143 corpus was unchanged: seven shards of 394 distinct original `.i` owners each. MiniC generated assembly and GNU GAS generated authentic x86-64 ELF objects for **391, 389, 389, 393, 391, 392, 391**, totaling **2736/2758 = 99.20%**. Relative to P55 this is **+64** assembled objects; relative to the initial 653 it is **+2083**. All **22** remaining first errors were preserved and independently replayed with the exact frozen compiler: **11 Core `STORE` type mismatches, 3 Core `LOAD` mismatches, 1 Core `FIELD_ADDRESS` mismatch, 4 x86 structured inline-ASM kind43, and 3 GNU `asm goto` `arch_static_branch` immediate-specialization failures**. The receiver correctly rejected the partial seven-object set before GNU Kbuild relink. Neither candidate GNU LD nor candidate QEMU Linux P0/P1 ran in this invocation. The exact first-error census, not a projected local sample success, defines the next fix batch.

## IR-P58 — Close the 22 exact original x86 Linux compilation blockers by shared ownership (2026-11-04)

The 22 P56 first failures split into three *shared semantic defects*, not 22 filename-specific workarounds. For the 15 Core verifier `STORE`/`LOAD`/`FIELD_ADDRESS` failures, C type compatibility intentionally ignores `explicit_alignment`, whereas Core retains alignment-qualified ABI type identity. A scalar conversion previously skipped the required Core retyping when C types compared equal; an object-aligned integer LOAD incorrectly carried storage alignment into the scalar SSA value; const-qualified nested pointer import incorrectly erased the **pointee's** explicit ABI alignment. The fix retains strict Core verification, explicitly converts just the otherwise-C-compatible pointer view at the typed STORE boundary, preserves nested pointer ABI views on import, strips object-only alignment from nonpointer loaded scalar values, and converts aligned integer CALL snapshot reloads to the callee's exact declared ABI view. It does not globally treat equal-width pointers as interchangeable. All **15/15** exact frozen `.i` failures now complete MiniC `-S` **and GNU AS ELF64 ET_REL** in the isolated clean implementation, including `slub`, `virtio_ring` and EFI runtime wrappers.

The four remaining machine-ASM errors all used the same valid `xaddq` form with an eight-byte `long` register and an eight-byte `long long` memory operand. A single strict x86 source-size predicate now validates actual width independently across SMP, LOCAL and GS/percpu XADD; the target machine contract still rechecks operands. All **4/4** frozen `.i` compile and assemble to GNU ELF64. The permanent source differential probe runs **5100** native GCC/MiniC comparison pairs, four negative constraints, plus the existing SMP and GS/percpu probes. The three `arch_static_branch` immediate failures arose when the symbolic static-key clone pass could not recover a proven literal branch integer from an initially unreferenced helper. Integer and symbolic specializers now share a stable-entry integer fact oracle, preserving mutable/volatile rejection and clone budgets (the historical MC043 2048/2049 limit remains enforced). All **3/3** frozen original `.i` compile and assemble via GNU GAS with authentic `__jump_table` relocation sections; dynamic `i(key)` remains rejected by both GNU and MiniC, and EFI uses its genuine `-fshort-wchar` Kbuild ABI option.

Thus the **22/22 first-error owners pass local exact-source MiniC plus GNU GAS**; Debug `check-fast` and Release builds also pass on the integrated code. This is not yet a new seven-shard 2758/2758 receipt: newly exposed later failures or final GNU Kbuild relocation/undefined symbols may still occur. Only an exact-head seven-runner receiver may assert completed C ownership, candidate GNU LD and QEMU P0/P1.
## IR-P59 — One sealed Core FUNCTION signature for definitions (2026-11-04)

The Core call graph already represented direct and indirect call contracts by
`core_function_type_id`, but a definition still maintained five independent
facts (`core_return_type_id`, `core_parameter_type_ids`, `parameter_count`,
`calling_convention` and `is_variadic`). The verifier, common ABI planners
and all five machine backends read those parallel definition fields. This
was a real source of semantic drift, not a missing ISA instruction or a reason
to introduce MIR.

P59 physically removes the parallel fields. One nominal Core FUNCTION type
now owns each definition's return, parameter list, arity, convention and
variadic rule. The C-to-Core builder keeps unfinished source facts only in
its private `MinicCoreSourceDraft`; snapshot publication registers a complete
FUNCTION type transactionally. Pure Core producers use the explicit
definition-signature constructor. Core verifiers, dumpers, Common call/ABI
plans and all five target emitters resolve the sealed type through the same
checked Core API, without reopening C declarations. Small independent ABI
test binaries use header-local Core accessors without a new link dependency.

`ModuleFunctionSymbol` still independently owns assembler symbol spelling,
visibility, aliasing and reachability: those are linker facts, not duplicate
function-type signatures. No old fields, compatibility shadow signature,
new optimization or MIR stage was kept.

## IR-P60 — Module relocation references and emitted fragments agree (2026-11-04)

Module construction necessarily first records symbolic `references[]` for
Core/Module reachability and only then materializes live global initializer
fragments. Previously `relocation_slot_count` duplicated `reference_count`,
while neither count was checked against the eventual relocation fragments.
P60 removes that redundant scalar. The finalized Module verifier now checks
each emittable non-extern global's relocation fragments against the preserved
reference edges, in order, for count, object/function/owner-label kind and
canonical target identity; poisoned mismatches fail closed before ELF output.
RV32/RV64 admission uses the single reference count rather than another C
snapshot. Dead static and extern-only declarations retain their preclosure
edges without being forced to materialize payloads. The separate
`initializer_slot_count` and `payload_kind` source-shape evidence remains for
strict target admission; a byte stream alone cannot reconstruct those rules.

**P59/P60 combined local evidence (before merging remote P58):** Debug `all` and `check-fast` PASS;
Release `-Werror all` PASS; Core Module fragment/rollback/reachability
contracts PASS, including forged count/kind/symbol/label negatives and dead or
extern positives; five-target global/section source smoke, RV32/RV64 global
plans, x86 GNU AS/LD/native and P46 output oracle PASS; `git diff --check`
PASS. Cross GNU/QEMU targets not provided locally remain SKIP, not PASS.

**Independent Linux boundary:** prior exact-head P57 seven-runner
[run #41843210178](https://github.com/yituanxing/minic-toolchain/actions/runs/41843210178)
assembled **2736/2758** original x86 Linux 6.6.143 C owners (99.20%),
with 22 failed originals and therefore no complete GNU LD/Image/QEMU boot.
The 22 original `.i` and `.o.cmd` effective flags were replayed against the
separate P59/P60 local binary **before** the new upstream P58 fix was
integrated: its 22 first errors remained unchanged, as expected for an IR
ownership refactor. The upstream P58 ledger above separately records its
own 22/22 GNU AS local fix. Neither local result is a current integrated
full-corpus or Linux P1 recertification; same-head acceptance remains required.

**Integrated exact-source P58+P59/P60 replay:** after fast-forwarding the
same PR branch to `95513f3` and applying both structure batches, all **22/22**
frozen P57 original GNU Linux `.i` owners compile with the new Debug MiniC
under their original `.o.cmd` C flags (`-fshort-wchar`, `-funsigned-char`,
`-mno-sse`) and all **22/22** assemble using the real matching GNU AS flags
to ELF64 x86-64 relocatable objects (`readelf` machine verified). No GNU C
fallback or stale candidate object was used. The combined Debug `check-fast`
and strict Release `-Werror all` also pass; the seven independent Linux
candidate producer shards and final GNU LD/QEMU receivers have not yet been
rerun at this integrated tree and are **not** counted as certified.

**Strict CI boundary maintenance:** the first P59/P60 remote Native Strict
job stopped at a stale source-text assertion requiring the physically deleted
`fn->core_return_type_id` in Direct LP64 profile code. The checker now
requires the actual sealed `minic_core_function_return_type(fn)` consumer
and rejects reintroduced parallel CoreFunction return/parameter fields.
The standalone `check-minic-target-boundaries.sh` passes locally, including
its negative/ownership probes. This is a test-contract update, not a new ABI
fallback or a claim that the subsequent remote rerun is already green.

**P58 exact-head seven-way requalification:** [run #41848009482](https://github.com/yituanxing/minic-toolchain/actions/runs/41848009482), remote commit `95513f3e5bb84033456e25baa8ac1cfe188f5c78`. This run actually built all seven 394-owner shards from the authenticated GNU `.i` corpus, with x86-64 MiniC `-S` and GNU GAS ELF acceptance counts **393, 394, 394, 394, 394, 394, 394** = **2757/2758 = 99.96%**. The P56 22 previously failing owners were independently replayed under one integrated Release MiniC binary with their exact Kbuild ABI flags and all **22/22** produced x86-64 ELF64 relocatable files. One *newly exposed* machine-ASM kind43 error remains at `arch/x86/kernel/nmi.o` (`exc_nmi`) in shard0; no other original owner failed. Six shard jobs passed completely, the one partial shard retained its exact source and first diagnostic. The receiver correctly reported `THREE_ISA_SEVEN_RUNTIME=FAIL missing or extra seven candidate artifacts for x86_64` at preflight **before GNU LD or candidate QEMU**. A host KVM nested-VMX preflight red light is separately classified as environment-blocked; the AArch64 NEON ACLE `#pragma GCC aarch64` red light is an unchanged earlier scope gap, neither is this x86 Linux candidate receiver failure.

## IR-P59 — Preserve complete-enum GS/percpu XADD source operands (2026-11-04)

The P58 sole first failure, authentic `arch/x86/kernel/nmi.i`/`nmi.o`, revealed a real regression in P58's new shared x86 XADD *source* rank gate. `exc_nmi` uses `xaddl %0, %%gs:%1` with a 4-byte complete `enum nmi_states` memory lvalue. The previous per-CPU source contract intentionally admitted integral `+m` enum objects but the shared `x86_linux_xadd_integer_width` restricted every operand to `MINIC_TYPE_BASE_INT`, erroneously dropping this legal 4-byte enum into the unsupported kind43 form. This is not a missing GS/percpu machine instruction. Only the per-CPU `+m` enum case now uses the existing source DataLayout to validate the **complete enum's actual target byte width** against the exact `xaddl` template; incomplete or wrong-width enums remain rejected. The register, SMP and LOCAL machine-form contracts retain their original strict operand policies and the backend rechecks Core integer width. The exact retained `nmi.i` with its original x86 Linux Kbuild flags has locally completed MiniC `-S` followed by GNU GAS `ELF64 ET_REL`; another exact-head seven-runner and real GNU LD/QEMU receiver proof remain mandatory.
## IR-P61 — Retire LoongArch post-serialization machine rewrites (2026-11-04)

The Direct LP64 emitters already select every native instruction, stack slot,
ABI register and inline-ASM form from sealed Core facts. LoongArch alone still
buffered every emitted function with `open_memstream`, parsed the resulting GNU
assembly text and then rewrote SSA spill/reloads, parameter reloads, branches,
frame loads and sign extensions. That text parser depended on synthetic SSA and
inline-ASM comment markers. These were semantic decisions *after* assembly
serialization and had no equivalent in the other four machine paths.

P61 makes LoongArch emit directly to the owning module stream, as AArch64
already does. The target/Core/ABI emitters still make the original instruction
and alignment decisions; function-local outlined unaligned memory helpers
still follow the function body. The entire 505-line post-text optimizer, its
interface, staging buffer, redundant use census, markers and optimizer-only
test harness are removed together with their source inventories. No MIR,
compatibility pass or ISA extension is introduced. This deliberately retires
text peephole optimizations until there is an owned, structured machine stage.

**P61 local evidence:** Debug `minic-cc` rebuild PASS;
`check-minic-target-boundaries.sh` PASS; Loong direct frame-fold PASS
(`work_lines=75`, under the existing 150-line budget), Core dead-SSA writeback
PASS (`dead_lines=24`, under the existing 37-line budget), Core GP forward PASS,
Loong immediate folding PASS, P46 five-target assembly oracle PASS and five-ISA
spill-pressure PASS (Loong assembly 51,088 bytes). These verify source and
assembly contracts only: Loong GNU AS/QEMU are unavailable locally, so native
runtime remains **unverified**. The existing x86 Linux seven-runner for
concurrent remote `2dad513` was still running when this local work started;
P61 is not a current Linux Image/P1 certification.
## IR-P62 — One product Driver invocation across parse and pipeline (2026-11-04)

The product `minic` previously kept command-line parsing, ISA/ABI admission,
RV64-specific GNU macro exposure, process execution, temporary-file lifecycle,
preprocessing, assembling and linking in one nearly thousand-line `main.c`.
P62 moves ownership into `driver_internal.h` (the validated invocation),
`driver_cli.c` (parse, catalog and fail-closed mode/target admission), and
`driver_pipeline.c` (tool paths, subprocesses, CPP/CC/AS/LD and cleanup).
`main.c` now only parses, dispatches and returns the final status. The Makefile
has a single `MINIC_DRIVER_SOURCES` inventory from which product objects are
derived; the Stage2 toolchain script consumes that same inventory instead of
silently self-hosting only `main.c`. The runtime profile cache fingerprints all
driver C/headers so future edits cannot reuse stale compiled tool artifacts.
Strict architecture assertions now inspect their actual owning units.

This is a **behavior-preserving ownership refactor**, not newly qualified ISA
support: five registered backends still accept already-preprocessed `.i` under
`-S`; only RV64 has the product MiniPP→MiniC-CC→MiniAS `-c` and MiniLD final
link pipeline. Target-sensitive GNU preprocessing, external GNU AS/LD, ABI
sysroot/runtime selection and source-vs-machine capability negotiation remain
necessary before exposing five-architecture `-c/link`; merely flipping the
TargetCatalog stage flag would silently run RV64 tools on other targets.

**P62 local evidence:** strict Debug `-Werror` build PASS; five-target `-S`
catalog and byte parity PASS, non-RV64 `-c` fail-closed PASS, five-target
language ABI option PASS, RV64 GNU CPP predefine PASS, strict target-boundary
suite PASS, Stage2 eight-tool source plan PASS (`minic` now four translation
units), runtime cache component-isolation selftest PASS. Driver RV64 ELF
readelf/link preflight and negative controls ran, but local QEMU and
cross-GNU LD availability prevented full driver runtime checks; these stages
are **unverified**, not incorrectly counted as passing. A separate historical
compiler B1 source manifest currently disagrees with Makefile (`202` vs `192`
entries); it was already stale and is not silently re-baselined as part of
product Driver ownership.

**Concurrent Linux full-corpus receipt at remote `2dad513`:**
[run #41850988524](https://github.com/yituanxing/minic-toolchain/actions/runs/41850988524)
completed all seven fresh x86-64 GNU Linux 6.6.143 Kbuild C-owner producer
shards successfully: **2758/2758 MiniC `-S` plus real GNU AS ET_REL objects**.
The receiver then attempted genuine GNU Kbuild linking and failed on exactly
one unresolved compile-time assertion, `__compiletime_assert_161`, retained
in the candidate `mm/memory.o` under `encode_page(flags > 3ul)` in the pinned
original GNU `.i`. No final candidate Image or QEMU P0/P1 was produced.
The next corrective work is Core/source constant-fact propagation through
the real inlining/call chain, not a filename-specific assertion suppression.
This receipt predates local P61/P62 and is not credited to those changes.

## IR-P63 — Propagate verified mutable Boolean ranges through inline GNU compile-time guards (2026-11-04)

The last x86 GNU LD symbol comes from `include/linux/mm_types.h`'s always-inline `encode_page(page, flags)`, called through `__tlb_remove_page` in `mm/memory.c`'s `zap_pte_range`. The caller's `unsigned int delay_rmap` is assigned only `0` or `1`; therefore `flags > 3ul` is always false. GCC proves it after inlining. MiniC already creates Boolean-domain variants but its Core callsite selector accepted only `_Bool` expressions or immutable Boolean-specialized parameters, losing the mutable caller's existing **CFG-merged possible-one-bits `{0,1}`** fact. The unspecialized generic `__tlb_remove_page → encode_page` emitted a conditional call to the external `__compiletime_assert_161` and failed linking.

This coherent three-part correction uses existing Core facts without relaxing any source rules: a stable local's may-one-bit mask now supplies a conservative nonnegative interval `[0,mask]` (rejecting signed sign-bit uncertainty, volatile and escaped locals), Core callsite selection may admit the established `{0,1}` clone only when that interval is wholly `<=1`, and the existing false-only bounded comparison proof now also handles strict `>` and `<=` with mathematically correct inequalities. The selected clone's immutable entry contract supplies `[0,1]` to the nested `encode_page` guard. The original predicate remains lowered before discarding its impossible diagnostic branch, preserving runtime effects; a local with possible value `4`, an address-escaped local, volatile reads, and arbitrary runtime parameters retain the unresolved guard. No weak definition, GNU-C fallback, source-string-specific rule, or suppress-all-asserts shortcut is introduced.

**Local original owner proof:** the SHA-authenticated `mm/memory.i` and real `.o.cmd` `-mno-sse -funsigned-char -fshort-wchar` ABI options now compile to MiniC `.s` and GNU GAS ELF64 ET_REL; `nm -u` contains **zero `__compiletime_assert_161`** references. A five-ISA source regression with a nested always-inline caller, mutable 0/1 local and observable runtime condition passes five-target assembly, native GNU/MiniC exit42 and **4/4 must-retain negative variants**. Isolated Debug and Release `check-fast` both **PASS**. This does **not** constitute a newly linked MiniC Linux Image or QEMU P0/P1 proof; only the next exact-head seven-runner GNU LD and QEMU receiver can establish that.

## IR-P64 — Effectful source expressions cannot consume pre-evaluation Core-local ranges (2026-11-04)

**Reason:** P63's new `{0,1}` callsite specialization exposed a C semantic
miscompile: a caller with `flags=0` passed `mutate_condition(&flags) ? flags :
flags` to an always-inline Boolean-range clone. MiniC chose the clone from
the *pre-expression* local fact before the call changed `flags` to `4`,
returning `1` where native GCC returned `42`. The same stale local fact
could suppress a true `>3` branch or `&4` bitmask test; a sibling argument
could also mutate a fact used by an earlier argument. Pure literal arms
still permit false-only guards despite observable condition mutations.

**Generic barrier:** Core direct-call specialization checks the *entire
source argument group* for effects before using any caller-local range,
known integer or symbolic fact. Effectful groups permit only intrinsic
source constants and type-level `_Bool` bounds; all arguments retain their
real evaluation. `core_cfg_nonnegative_range_impl`, its possible-one-bits
evaluator, bounded comparisons, bitmask proofs and subtraction RHS constant
fallback share the whole-expression admission: an effectful expression
cannot consume its pre-execution local facts. Source-only literal/bitmask
and conditional-arm shape proofs remain available. The generic condition
branch constant bypass requires proven purity, as does the second constant
switch dispatch after selector evaluation.

**GNU block-owned locals:** GNU `({ ... })` from Linux `min_t` initialize
private local temporaries, and can be discarded if their whole integer
result is constant. A shared, fail-closed observational-purity certificate
accepts straight-line `MINIC_STATEMENT_ASSIGN` writes *only* to nonvolatile
locals whose parser-recorded `has_declaration_block` owner exactly matches
the GNU expression block, with recursively pure RHS and result expressions.
The local-constant interpreter now reuses that certificate: it cannot
interpret writes to an enclosing local as effect-free constants. External
assignments, volatile writes, unrecognized control flow, calls that cannot
be proved pure, and reentry are rejected. No C facts or ownership metadata
enter Core IR or TargetInfo; no filename or assertion is special-cased.

**P64 local evidence (not a Linux full-corpus claim):** Debug and strict
Release `-Werror` builds PASS. `check-core-cfg-effectful-boolean-fact-barrier`
passes five Linux target `-S` configurations and executable GNU/MiniC x86
native differential (`42/42`), covering effectful ternary, comma,
assignment, sibling call arguments, bounded compare, disjoint bitmask,
subtraction RHS, `if`, `switch` and GNU statement expressions that write
an enclosing local. The existing Linux `encode_page` source probe passes
five-ISA assembly, native `42/42` and 4/4 must-retain diagnostic guards;
`check-gnu-if-condition-local-owner` also passes, with zero surviving RV64
`__compiletime_assert_645` references from `min_t`. Optional SHA-authenticated
original `mm/memory.i` replay was absent from this local environment and
needs the Linux receiver. The new probe is a short dependency of `check-fast`;
no new Linux Image/QEMU test was launched for P64.

## IR-P65 — Publish only sealed global fragments and linker facts (2026-11-04)

**Source/Core ownership:** The ten-way `payload_kind` (C scalar/array/record
initializer taxonomy) and `initializer_slot_count` have both been removed
from public `MinicCoreModuleGlobal`, its builder, verifier and target
consumers. The ten-way source-private `MinicSourceGlobalPayloadKind` remains
in Compiler's `global_payload` serializer only, where it is still needed to
consume C initializer shapes and construct deterministic ZERO/BITS/RELOCATION
fragments. No copied Core field, compatibility enum or shadow slot count
replaces either removed field. Common trace reports fragment count; the
target-boundary gate rejects a recurrence of the source classifiers in
published Core/Module, common output or RV32/RV64 owners.

**RV32/RV64 equivalence:** Both targets make admission and ELF section/data
directive decisions from sealed CoreType, exact fragment coverage and
reference edges, generic definition facts (`is_tentative`,
`is_zero_initialized`), weak/linkage/section policy and ABI widths. RV32's
weak tentative-zero `.bss` case remains distinct from explicitly initialized
weak all-zero records/arrays in `.data` although the serialized bytes may be
identical; it requires a Core zero-definition fact plus one complete ZERO
fragment. RV32 recursive/record shared style was already implied by a typed
record base or references, so its three source-kind OR terms were removed
together. RV64 preserves its previous zero-extent and typed scalar/aggregate
admission gates without accepting unmaterialized nonzero definitions.

**P65 local evidence:** isolated Debug `-Werror` eight-tool build PASS,
`check-minic-target-boundaries.sh` PASS, source-private and hand-built
`check-core-module-fragment-contract` PASS (three deliberately identical
ZERO-fragment weak globals with different generic definition facts),
RV32/RV64 GlobalPlan PASS including six weak and three nested shapes each,
`check-p47-five-isa-output-smoke` PASS including the x86 GNU-native
global ABI gate. The nine-shape frozen P64 `.s` oracle is byte-identical to
the P65 output for **both** RV32 and RV64, covering weak tentative,
explicit 0, empty and `{0}` arrays/records, recursive scalar arrays and
nested record arrays. GNU's scalar `weak int x = {};` is rejected at the
MiniC source parser on both P64 and P65 and is **not** counted as an
accepted-code ELF regression. Optional RV cross-GNU ELF and QEMU runtime
stages were not run; P65 is **not** a new Linux Image/P1 certificate.

**P66 follow-up, outside P65 scope:** The general Core Module verifier should
strengthen its type-to-storage extent invariant for independent producers
(e.g. a complete fixed array with 16-byte CoreType layout but a global
storage size of zero), and decide the target-neutral legality of contradictory
zero/tentative facts with nonzero BITS or relocations. These preexisting
producer-contract questions are deliberately not papered over by new C
cardinality fields or a P65-only target-specific compatibility layer. Their
implementation needs a separate five-ISA negative producer matrix.

## IR-P66 — Seal live Module global layout and definition semantics once (2026-11-04)

**Source-independent Core contract:** `minic_core_module_verify` now requires
every materialized non-extern global to occupy exactly its declared CoreType
extent with at least that type's alignment. The one legal larger extent is a
complete non-union record whose **last Core field** is an incomplete array:
the additional byte length must be a whole number of complete, positive-size
element extents. This retains GNU flexible-record static initializers (an
8-byte record with three four-byte tail elements emits a 20-byte object),
without accepting a forged complete `int[4]` CoreType of 16 bytes as a
zero-byte definition. Complete zero-length arrays and empty records retain
genuine zero-byte definitions. Qualified Core type wrappers are peeled only
to inspect the underlying field shape; declared alignment remains effective.
Dead/pruned objects, extern declarations, aliases, incomplete declarations,
and TLS target capability policy are not rewritten as initialized storage.

**Definition/ELF facts:** Tentative and explicit-zero definition flags are
mutually exclusive; tentative storage has no relocation references and no
nonzero materialized numeric bits. `is_zero_initialized` permits symbolic
RELOCATION fragments: `int *p=&x`, a pointer-sized integer cast of `&x`, and
string-literal addresses intentionally have zero source numeric slots plus
a real linker relocation. Only their numeric BITS fragments must serialize
to zero. The Module default BSS section independently requires zero numeric
bytes and no relocation, even for explicit `{0}` records whose generic
zero-initialization flag is false. This rule does **not** infer ELF section
type from `section_name`: RV32/RV64 accept explicitly named `.bss.*` with
nonzero scalar bytes or relocations as GNU `@progbits`, whereas LP64 direct
targets separately enforce their `@nobits` admission. Fragment byte coverage
and ordered relocation/reference identities are verified before these new
semantic facts, with no source C cardinality, per-architecture shadow verifier,
or ELF directive introduced into Core.

**Direct emitter trust boundary:** The production backend entry verifies
the entire sealed Module before dispatching into the five target writers.
RV64's independently callable `minic_riscv64_emit_module_global` accepts a
`finalized` flag without re-running that global check; its existing narrow
duplicate zero/tentative admission guard remains as direct-entry defense in
depth. Deleting that guard would expand direct-API admission for a forged
finalized Module. Simplifying it is a separate interface-contract decision,
not part of this source-independent verifier migration.

**P66 focused evidence:** A new independent Core Module definition matrix
exercises matching/forged array and scalar extents, underalignment,
nonmultiple flexible tails, complete zero extents, weak tentative and zero
definitions, zero-tagged symbolic relocation, nonzero numeric payload,
default BSS contradictions, explicit `.bss.*` target ownership, extern
incomplete type and dead objects. The Core Module contract compiles and runs
under strict `-Werror`; the existing five-target output-transaction probe
passes its own strict compile and runtime. RV32/RV64 GlobalPlan source
probes, x86 GNU-native GlobalPlan, and five-target Linux global section
source smoke all PASS. The frozen nine-shape P64 RV32/RV64 GNU assembly
oracles match the P66 compiler output byte-for-byte. This is **not** a
full Linux compile, GNU ELF32/ELF64 cross-architecture link or QEMU P1
recertification; those remain separate target runtime milestones.

## IR-P67 — ELF NOBITS admission consumes sealed Core bytes (2026-11-04)

**Remaining cross-layer shadow removed:** P65 deleted C initializer shape
from the public Core Module; P66 validated physically materialized live
definitions. A separate `common_elf_bss_admitted` Boolean still remained in
`MinicCoreModuleGlobal`. Compiler ingress computed it from C source
`initializer_values[]` and `relocation_count` before the byte serializer had
produced the actual fragments; x86/AArch64/LoongArch64 writers trusted this
unverified field and x86 independently repeated a partial fragment check.
Even a valid source-free producer could not emit an ordinary `.data` Core
global if its unrelated default-initialized Boolean was false. Conversely a
forged true flag could ask the two Direct LP64 writers to emit nonzero data
or a relocation into ELF `SHT_NOBITS` without validating actual contents.

The single Common ELF global-plan builder now checks physical `BITS`, `ZERO`
and `RELOCATION` fragments whenever the caller selects **explicit `.bss` or
`.bss.*` as `@nobits`**. It rejects relocations and nonzero *visible* bytes;
bits above a 1/2/4-byte scalar's serialized width are correctly masked.
This is an output-format admission check, not C constant evaluation, not a
Core ISA rule and not a new architecture-specific ABI algorithm. Compiler
ingress no longer generates a Boolean, Core Module does not publish one,
and x86 no longer has a second independently evolving checker. The
production Core Module verifier retains P66's separate *default* BSS
zero/relo contract for all targets.

**Five-target dialect policy retained:** x86_64, AArch64 and LoongArch64
pass `explicit_bss_nobits=true`, so `.bss.*` remains GNU-style NOBITS and
rejects physical nonzero/relocation payload. RV32 and RV64 pass false and
retain their previously accepted explicit `.bss.* @progbits` bytes; `.bssish`
is never misclassified as `.bss`. In particular, the change can admit a
source spelling such as `unsigned char b = 256` in an explicit `.bss.*`
section after its C conversion serializes a real zero byte; this agrees
with GNU GCC's `@nobits` output (which warns about truncation) and avoids
rejecting a physically valid ELF definition based solely on an untruncated
C initializer value. This is intentional, not a relaxation for nonzero
physical ELF contents.

**P67 evidence:** The standalone Common ELF plan probe now rejects its old
invalid `.bss.test` plus label relocation, admits it under the RV-style
PROGBITS switch, rejects visible nonzero bits, admits a masked-width numeric
zero and a genuinely ZERO fragment. The five ISA global section probe
checks explicit NOBITS/PROGBITS dialects, zero/narrowed-zero payloads,
`.bssish` data, and rejection or retained source admission of nonzero and
symbol relocation payloads. The x86 GNU native global ABI gate independently
assembles both GCC and MiniC narrowed-zero cases and observes the same
ELF SHT_NOBITS section. Existing RV32/RV64
GlobalPlan and strict five-target boundaries are the minimal regression
set. This scope does **not** claim current-head Linux linking or guest
runtime certification.

## IR-P68 — One verified Core admission boundary for independently callable machine writers (2026-11-05)

**Whole-program ownership:** The production `minic_backend_emit_assembly_with_module`
already checked a complete finalized Core Module and the type-arena identity
of every selected live CoreFunction. However, the independently callable
RV32, RV64 and x86 full-module writers previously entered machine planning
with only a `finalized` flag; the x86 head-address call closure ran before
the common output transaction. AArch64 and LoongArch64 shared a different
full-Module check but did not share the live function-arena preflight.
One Common admission function now owns the exact Module + live-function
ownership contract, and all five program writers and the production facade
invoke it before target planning or file output. The generic output
transaction retains its independent, source-free, temporary-file commit
role; it is not made responsible for semantic Core verification.

**Per-global ownership without quadratic scans:** Three cross-translation-unit
global emitters (RV32, RV64 and x86) were independently callable with
forged `finalized=true` Modules. The Core Module verifier now shares its
one-object validation body with `minic_core_module_verify_global`, which
checks safe root/table preconditions and the requested object's CoreType,
byte extent, fragment coverage, definition state and reference identity.
Public per-global writers fail closed on invalid/dead objects before emitting
assembly, but do not rescan all other globals or the Module's namespace.
Complete writers still perform full Module verification exactly once per
entry. RV64's duplicate zero/tentative check was removed: unlike the Core
physical-byte test, it treated masked-off high bits as nonzero and could
reject an otherwise legal serialized byte. RISC-V explicit `.bss.*`
PROGBITS and x86 NOBITS output policies remain target-owned.

**RV64 per-function ownership:** The only independent function writer
consuming a Core Module previously accepted externally supplied prepared
facts whose pointer-identity flag could be forged. Its checked interface
now rebuilds canonical CoreFunction facts and object storage via the common
CoreFunctionPlan after verifying Module and matching type arenas. Its
production callback uses an explicitly internal, already-admitted trusted
variant to reuse the common visitor's prepared plan without repeating a
whole-Module scan per function. Two additional standalone RV64 CoreFunction
entrypoints verify Core before performing facts analysis. ISA instruction
helpers and block emitters are internal prepared-machine operations, not
additional complete-Module admission owners.

**Targeted evidence and limits:** Independent source-free Core-producer
tests should reject forged extents, missing reference tables, mismatched
type arenas, inconsistent zero/BSS/relocation data, and invalid dead-object
emission; positive cases retain zero-size objects, flexible records,
symbolic relocations, width-masked zeros and both ELF `.bss` dialects.
Shared preflight and all five target emitter paths are tested under strict
`-Werror`, with no new C initializer shadows or per-architecture verifier
algorithms. The exact-head full Linux/QEMU runtime qualification remains a
separate, explicit requirement, not implied by these structural probes.
