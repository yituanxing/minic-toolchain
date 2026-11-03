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
   GNU overflow 对 enum 目的地的源语义禁入仍由 Core 上已封存
   `source_overflow_destination_ineligible` 证明，不能误把 enum
   降到普通 i8 后重新放行。
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
