# MiniC RISC-V RV32/RV64 收尾待修改清单

> 目标：**只整理 MiniC**。巩固 RV32I / RV64I 正确性，消除真正重复的机器算法，
> 保持源内联/文件级汇编可交给目标汇编器；给未来 M、C 等可选优化保留小型指令选择入口。
> 本清单不授权修改 MiniAS、MiniLD、MiniPP；不引入通用 Machine IR，也不为所有 ISA 扩展一次性造齐发射器。

**基准**：2026-10-26，PR #651 `refactor/minic-target-language-boundary`（持续更新 HEAD）。
**证据约定**：`源码确认`只表示静态代码中能定位；`运行确认`必须具有对应 HEAD 的 GNU/QEMU/CI 结果。
**状态**：`TODO` / `IN_PROGRESS` / `CODE_FIXED` / `VERIFIED` / `DEFERRED`；
**关闭条件**：代码、正反例、原有回归测试均通过才能填 `VERIFIED`。
**优先级**：P0 = 可能产生错误机器结果/ABI 失配；P1 = 阻碍正确扩展或审计；P2 = 非阻断性代码清理。

## P0 — 正确性：先修再整理

| ID | 状态 | 问题及源码证据 | 修复策略（最小范围） | 必须验证 |
| --- | --- | --- | --- | --- |
| **R01** | CODE_FIXED（等待 exact-head 运行验证） | **RV32 `-fsigned-char` 的机器符号判断错误**。[`rv32_scalar.c`](../../src/target/riscv32/rv32_scalar.c) `rv32_scalar()` 把 `is_plain_char` 一概判成 unsigned，忽略 invocation 生效后的 `integer_sign`。 | 使用 **生效后的** `effective.integer_sign`，不因 `is_plain_char` 强制 unsigned；保留 `_Bool` 等特殊类型的原有规则；仅改 RV32 机器符号判断。 | `-fsigned-char`：`volatile char c=-1; c<0`、`(int)c`、指针取值；`-funsigned-char` 反向检查。RV32 GNU/QEMU 值比较，重新跑五目标选项门禁。 |
| **R02** | CODE_FIXED（等待 exact-head 运行验证） | **RV64 Linux 默认 `wchar_t` 模型错误**。`target_info.c` 设为 `unsigned short`，当前选项脚本还把这个历史值当正确默认值。RISC-V **Linux psABI Appendix A** 指定所有 ILP32/LP64 `wchar_t` 为 **signed 4 字节、4 字节对齐**。 | 只将 RV64 Linux 默认改成 `int`，保留显式 `-fshort-wchar`→16 位及恢复目标默认的 `-fno-short-wchar`；同步修复依赖旧期望的测试，检查 CPP/头文件宏的对应关系。 | RV32/RV64 默认 `sizeof(wchar_t)==4`、`sizeof(L\"X\")==8`、`(wchar_t)-1<0`；显式 `-fshort-wchar` 为 16 位；与 GNU 预处理/运行差分；已有 Linux 与自举回归不退化。 |
| **R03** | CODE_FIXED（独立提交 0e362e1；运行待验） | **RV64IM 的某些 `__int128` 比较反而比 RV64I 支持得少**。`riscv64/isa.c` 在 M 可用时拒绝两字比较，而 `scalar_emit.c` 中 Base-I 路径有 `__cmpti2/__ucmpti2`。另 `runtime_libcall.c` 按 `M` 全局判断是否有 libcall，可能漏掉保存 `ra`。 | 将 **“有 M”** 与 **“这一条 Core 指令选哪种算法”** 分开；先保持两字比较走可用软件调用，确保函数级 `ra` 保存检查根据实际选中 libcall，而不是仅看 `M`。先修退化，再考虑优化。 | 同一 `__int128` 有符号/无符号比较在 `rv64i/lp64` 和 `rv64im/lp64` 上都链接运行；比较边界、连环调用、返回地址保存；审计 I 分支不出 M。 |
| **R04** | CODE_FIXED（等待真实 GNU/QEMU 差分） | **RV32 binary32 算术**：`rv32_emit.c` 的浮点分派没有 `FLOAT_ADD/SUBTRACT/MULTIPLY/DIVIDE`，`rv32_fp_emit.c` 也未见对应 `__*sf3` 分支。编译普通 `float a+b` 的实际 Core 路径需验证。 | 用现有 `riscv/instruction_select.h` 的 `__addsf3` 等符号查找；RV32I ILP32 使用整数寄存器位传递，不另写浮点算法，也不声称 F 扩展。仅补目标 machine lowering 缺口。 | 四个 binary32 运算、`NaN`、带符号零、有限边界；GNU+compiler-rt/libgcc+QEMU 差分。排除浮点常量折叠假阳性。 |

标准：<https://docs.riscv.org/reference/abi/riscv-cc-linux-specific-abi.html>。

## P1 — 可靠扩展、来源与重构

| ID | 状态 | 问题/目标 | 修复策略 | 必须验证 |
| --- | --- | --- | --- | --- |
| **R05** | CODE_FIXED（已分离可用 ISA / codegen ISA；同 HEAD 验收中） | **目标允许 ISA 与实际 codegen ISA 仍耦合**。当前 `riscv/instruction_select.h` 已有单 XLEN 的 optional-M/软调用选择接口，是正确起点。 | 保留原入口，但将“机器允许 M”和“编译器本次**选用** M”解耦为轻量策略；未显式启用 codegen 优化时 I 路径始终可选。不新增 Machine IR/大量配置层。 | RV32I/RV64I 保持原本 Base-I，支持 M 的目标同时可强制 Base-I；在相同 ABI 下，开启可选扩展不减少已支持 Core 语义。 |
| **R06** | CODE_FIXED（先支持 2 个明确来源汇编超集；其它待逐项核实） | **`-march` 限定为几个精确拼写**，例如 Linux 汇编所需 `rv64ima_zicsr_zifencei` 不能仅因 MiniC 不主动生成 A/CSR 就在 `-S` 前拒绝。 | 只在 MiniC 内整理目标 ISA 的解析和能力读取；保持 XLEN/ABI 的严格判断，不承诺 MiniC 生成目标允许的全部扩展。预定义宏如进入本批范围必须与机器事实一致；不要重写 MiniPP/AS/LD。 | `-march=rv64ima_zicsr_zifencei -mabi=lp64 -S` 成功且 compiler-origin 仍符合 I；错误 XLEN/ABI 失败；源码 A/CSR 模板通过来源追踪，最终交 GNU AS 检查。 |
| **R07** | CODE_FIXED（禁止来源汇编伪造审计标记，待 exact-head CI） | **内联/文件级源码 ASM 与编译器 ASM 的来源审计**。RV64 已有标记和扫描；RV32 有来源注释，但证明手段不同。 | 保持原有内联汇编约束和发射器，不要重写；让审计区分 compiler-origin 与 source-origin，静态审计必须拒绝被源码 marker/指令伪装绕过的情况，GNU AS/ELF 是最终裁判。 | 源码 `amoadd`、CSR、`fence.i` 不误判编译器违反 I；在 compiler 区域人为注入 M 必须失败；覆盖文件汇编、结构化 asm、asm goto、错误 clobber。 |
| **R08** | CODE_FIXED（RV64 F/D 可用时也能选择 LP64；同 HEAD 验收中） | **ISA/ABI 仍是一张一对一 profile 表**，如 `rv64imafd/lp64` 被测试列为 invalid，而此配置符合 RISC-V ABI（硬件 F/D 可用、软浮点调用 ABI）。 | 在 MiniC 解析时分别确定 XLEN、扩展与调用 ABI，再根据实际 ABI 支持能力做检查；禁止把 `lp64d` 误按 `lp64` 发射。超出已实现的软浮点 ABI 部分可以明确拒绝，不能谎称通过。 | `rv64imafd/lp64` 对已具备 LP64 integer-only 的 C 用例可工作；浮点签名根据真实 ABI 支持状况验收；`rv64i/lp64d` 必须拒绝。 |
| **R09** | CODE_FIXED（公共 ABI/ELF header 已归位；等 exact-head CI） | **RISC-V 共用代码仍放在 `riscv64` 命名空间**，如 `riscv64/function_symbol.h` 被 RV32 直接 include；`target_info.h` 也跨层依赖 `riscv64/isa.h`。 | 已证实共用的符号/ISA 事实移动到 `target/riscv/`；调用两端复用同一实现，删除旧路径，保留真正 RV32/64 ABI、寄存器和帧差异。 | 严格边界门禁、五目标构建，符号弱/别名/可见性、ILP32/LP64 差分；旧文件/重复实现真的消失。 |
| **R10** | TODO（待量化） | **RV32/RV64 重复算法剩余盘点**：`rv32_integer_emit.c`、`rv32_memory_emit.c`、`rv64/scalar_emit.c`、`rv64/memory_emit.c`、`rv64/legalize.c`。 | 先按同一 Core 算法、不同 XLEN/寄存器/ABI 做成对审计；只合并算法/判定/辅助函数符号；不强制合并完整 Backend 或栈帧。每抽取一项立刻删除旧算法。 | 所有实际被触达的发射路径保持 GNU/QEMU 输出行为；公共算法无“旧路径+新包装”并存；性能不出现高代价重复预检。 |

## P2 — 结束条件、交接 MiniAS

| ID | 状态 | 工作 | 验收 |
| --- | --- | --- | --- |
| **R11** | TODO | 整理 `riscv64` 专属名字用于公共代码的命名、旧注释、过时实验开关；仅限本轮触达文件。 | `rg`/边界脚本不发现误导性公共依赖，旧函数/重复计算已删。 |
| **R12** | TODO | **MiniC 最终验收**：RV32I、RV64I、M 可选回退、原版 Linux 七分片、四架构不回退。 | 必须在**同一 exact HEAD**保留 CI/workflow URL 与机器结果；`-S` 的成功只证明生成汇编，不等于 ELF/QEMU/Linux 通过。 |
| **R13** | DEFERRED | MiniAS 的 I/M/A/C/Z 编码、重定位、伪指令、压缩、ELF 属性/ABI 兼容性。 | **本清单只记录交接**，MiniAS 的实际开发留到 MiniC R12 通过后另外规划。 |

## 现有实现：保留且复用

- `src/target/riscv/word_operations.c`：两字加减/位运算/移位/扫描，已在 RV32/RV64 共用。
- `src/target/riscv/gpr_abi_cursor.h`：共享 8 个 GPR 的抽象游标；**分类、参数栈布局依旧由各自 ABI 所有**。
- `src/target/riscv/record_copy_bytes.c`：Base-I 聚合字节复制。
- `src/target/riscv/instruction_select.h`：Base-I opcode、soft libcall、基础 optional-M 策略（后续增强，而非重写）。
- `src/target/common/core_object_reuse.c` 等：共享 Core 生存期证明，RV32/RV64 保留各自栈帧。

**明确不做**：给 M/A/C/V 所有操作造空壳 API、创建第二套指令分类器、强行统一 ILP32/LP64D、移动 MiniAS/LD/PP 逻辑、把 `-S` 等同于真实程序运行成功。

## 实施顺序与 CI 策略

1. **P0 一批修复、一次回归**：R01、R02，再验证/修复 R03、R04。每个 defect 增加反例或 GNU/QEMU 差分用例。旧五目标配置门禁更新为正确 ABI 期望，而不是继续保留错误历史基准。
2. **P1 一批重构**：复用现有策略入口，R05、R06、R08；同步做 R07。再做 R09/R10 的有证据的公共算法去重，不做文件搬迁式重构。
3. **收尾**：R11、R12 的 exact-head 实际验收完成才将 MiniC 转入 MiniAS；尚缺任何一项就标注缺失证据，而不是写 PASS。
4. **CI 优先外部 runner**：本地只做低成本静态检查；原有 native/five-arch、RV32 GNU/QEMU、七分片 Linux 作为最终证据。执行前检查缓存与 owner 路由，不重复造完整流水线。

### 变更记录

- 2026-10-26：依据 PR #651 静态代码审计建立清单。此前绿灯只属于之前对应的 exact HEAD。
- 2026-10-26：R01 修复 RV32 机器符号判断并添加 signed/unsigned char 生成汇编门禁；R02 修复 RV64 Linux wchar_t 默认并更新选项、target-descriptor 测试和架构文档。状态标为 CODE_FIXED，等待对应 CI/GNU/QEMU 执行证据，不宣称 VERIFIED。
- 2026-10-26：R03 检查到独立提交 `0e362e1` 已删除“开 M 禁用 i128 比较”的错误限制，并在函数帧预检中预留 libcall RA，标记 CODE_FIXED（仍待同 HEAD 真实运行）。R04 增加 RV32 binary32 加减乘除 soft-float ILP32 机器发射和四个 `__*sf3` C→S 门禁；真实运行差分仍未完成。

- 2026-10-26：R05/R06/R08 已进入代码修复阶段：`MinicRiscvIsa` 拆分 `features`（目标允许指令）/`codegen_features`（MiniC 选用指令），两个 Linux `-march` 超集限定仅 `-S`、机器仍发射 Base-I；`rv64imafd` 可选 LP64/LP64D；预定义、Driver/CC 和严格 ISA 正反例同步。相关 exact-head CI 与外部 ELF/QEMU 结果必须分别核实，不能凭 `-S` 通过关单。

- 2026-10-26：R09 先修一个确定的依赖方向：`target_info.h` 只包含公共 `riscv/isa_features.h`，编译器专用的 `riscv64/isa.h` 由 `minic-cc` 显式引用，并用架构边界脚本防回退。RV32 与 RV64 共用的 function-symbol header 尚未迁移，故 R09 保持 IN_PROGRESS。

- 2026-10-26：R09 完成 MiniC 公共头文件的所有权清理：通用 `TargetInfo` 仅依赖 `riscv/isa_features.h`；RV32/RV64 共用 ELF function-symbol 实现已移至 `riscv/function_symbol.h`，旧 `riscv64/function_symbol.h` 删除且四处生产 include 已更新；新增边界门禁检查旧实现不再恢复。待当前 HEAD CI 后再标 VERIFIED。

- 2026-10-26：R07 静态审查发现：RV64 严格 ISA 分析直接相信可打印的来源注释边界，源码内联或文件级 ASM 若含相同标记会干扰审计。已在源 ASM 发射前统一拒绝内部预留标记，并新增 inline/file ASM 负例；普通来源 ASM 仍按原路径交给外部汇编器。此修复属于 MiniC 来源所有权，不改变 MiniAS。

- 2026-10-26：复审 #652 的 3352 TUs exact-head 七片记录：3306 compiled，46 failures 全部为 indirect call 参数声明不匹配。首批失败上下文为 EFI 的 `get_var(L"SecureBoot", ...)`；原版 Linux Kbuild 显式使用 `-fshort-wchar` 和 `-funsigned-char`，但此前 `corpus_replay.py` 只传 `-march/-mabi`，导致原始 ABI 选项遗失。此次修正 replay C 选项复现、加入 EFI 间接调用正反例。MiniC 默认 RISC-V Linux `wchar_t` 保持标准 32 位，不放松合法的函数指针调用检查，也不改 MiniAS。需以此 exact HEAD 再次汇总 3352 条验收。

- 2026-10-26（七分片根因复查）：HEAD `a710ccd` 的 3352 个 Linux TU 中 3306 编译通过、46 个全部报“indirect call argument type does not match declaration”；检索实际失败上下文发现所有错误均落于 EFI `get_var(L"SecureBoot", ...)`。核对 Linux v6.6.143 顶层 Makefile：KBUILD_CFLAGS 明确设置 `-fshort-wchar -funsigned-char`，但 `tests/external/linux/corpus_replay.py` 把预处理后的 .i 用 `-S -march=rv64i -mabi=lp64` 重放，丢了 Kbuild 的语言 ABI 选项。R02 把 RV64 默认 wchar 修正为正常 32-bit int 是正确的，不能回滚。已给 **Linux corpus replay** 恢复上述两个 Kbuild 语言选项，并加 EFI `efi_char16_t*` 间接调用正反例（16-bit 成功、32-bit 仍严格拒绝）。当前状态为 CODE_FIXED，需同 SHA 七分片验证后才能记 3352/3352。
