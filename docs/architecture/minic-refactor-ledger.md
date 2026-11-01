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
