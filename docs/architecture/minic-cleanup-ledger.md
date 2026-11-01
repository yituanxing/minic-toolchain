# MiniC 纯清理流水账：先通读、补合同，再重构

> 持续维护；以 [架构入口](README.md)、[函数契约](minic-function-contract-register.md) 和
> [MC 问题台账](minic-issue-register.md) 为事实来源，不复制 Bug 的永久编号与状态。
> **清理**仅允许：删有调用链证明的死路、收窄私有接口、去同一操作的局部重复计算、
> 修正文档漂移。不得在本阶段移动目录、拆出新层、统一不同 ABI、改 IR 合同，
> 或把 Bug 修复混入“净删行数”。

## 工作边界与统计口径

- 2026-11-01 起始源码基线：`refactor/minic-target-language-boundary` / `6934c9e`。
  当前 `src/` 共 **336 个生产 C/H**：Frontend 69、Compiler 11、Core 48、Target 208，
  另有两个 CLI。前序 F8 的 337 个是冻结的旧输入集合，不能拿来当本轮分母。
- 第十一轮 `A11-01～50` 是**有优先级的线索池，不是 50 个已可删除项目**。
  `minic-function-contract-register.md` 的 212 项是历史函数/函数组审读记录；
  它们并不自动证明当前 HEAD 的源码已经重新逐函数深读。
- 旧审查 manifest 的源指纹检查报告 `inventory=211, drift=141`；这是**待复核的
  历史证据漂移信号**，不等于 141 个缺陷，也不意味着先前 211 项全失效。
  今后复审应按当前 HEAD 重核 producer、consumer、测试与接口合同，区分
  `已扫路径`、`已追调用链`、`已实际清理`、`已动态验证`，不得将其中一个冒充另一个。
- 这轮不是 MiniAS/MiniLD 联合验收，完整 Linux 多架构矩阵延后；`-S`、GNU GAS/LD、
  原生运行、目标 QEMU、源码静态推导分别标明，不以单一探针声称 P2 全过。
- **逐文件阅读统计另算：** C1 的 6 个被改动源码文件只可记为“相关入口、调用者和
  删除闭包已核”，**不是**这 6 个完整文件的逐行通读。历史 F11 完成 336 路径扫描，
  而当前 HEAD 上可逐文件追溯的“完整深读”清单仍待重建，暂不宣称 336/336 结束。

## 批次 C1：高确定性的局部死路与重复计算

**源码基线：** `6934c9e`；**范围：** 四个 `A11` 接缝，6 个生产 C/H 文件。
源码变更 **+21 / -90 行，净 -69 行**；文档单独统计。每项均事先核对现役消费者，
仅退役最小可证明路径。

| 接缝 | 本批实际动作 | 保留的活路径 / 证据限制 |
| --- | --- | --- |
| `A11-03` FunctionBody ownership | 删零调用的 `minic_c0_function_body_owns_local` 和头声明（共 9 行） | 保留 Body view、root block、整体 ownership validator、遍历和发射策略；没有将 Body 模块整体判死。 |
| `A11-06` declaration Sema | 两个只在 `.c` 内调用的 `*_valid` 改成 `static`，删头文件暴露（4 行声明） | 实际数组 suffix 与外部对象属性校验仍运行；没有改变其合法输入集合。 |
| `A11-19` Core local constant facts | 删除重复的后置 GNU statement-expression 分支 54 行 | 同函数较早的 `MINIC_EXPRESSION_STATEMENT` 路径所有分支最终 `return`，因此后段不可达；保留前段完整表达式/副作用/局部事实算法。 |
| `A11-40` x86 Linux ASM legacy dispatch | 各缓存一次 `boot_flags_form`、`bit_scan_form` 分类结果 | dispatch 的顺序、8 操作数特例、volatile/operand 资格和实际 emitter 不变；两个 matcher 仍是现役入口。 |

### C1 验证与未覆盖

| 类别 | 结果 | 精确边界 |
| --- | --- | --- |
| 全量 `-Werror` MiniC Release 构建 | **PASS** | `make MODE=release CFLAGS=-Werror BUILD_DIR=build/cleanup-werror -j7 build/cleanup-werror/bin/minic-cc`；含 MiniC Driver 构建。 |
| x86 boot flags 与 tied bit scan | **PASS** | `probe-x86-linux-boot-flags-native.sh`；`probe-x86-linux-tied-bit-scan.sh`（3 族 / 33 用例及负例）。 |
| Core GNU statement-expr 局部事实 | **PASS** | `probe-core-inline-statement-constant-guard.sh`：五目标 GNU `-O2` 编译、GNU AS/LD、两例 x86 native 运行与负例。 |
| Frontend 静态合同 | **PASS** | `make ... check-type check-ast-contract check-expression-statements`。 |
| GNU statement-expression 通用脚本 | **基线脚本断言未通过，不计回归** | `run-gnu-statement-expression.sh` 要求输出 `.Lwhile_condition_` 标签；基线编译器与 C1 的同输入 `.s` 字节一致，均无该标签。该脚本的断言需单独审理。 |
| RISC-V statement-expression 运行测试 | **NOT_TESTED（环境）** | `run-core-statement-expression.sh` 所需 `riscv64-linux-gnu-gcc` 交叉 libc 头 `bits/libc-header-start.h` 缺失；不能将其记为产品 FAIL 或 PASS。 |
| 完整五架构 Linux / 联合验收 | **NOT_RUN（按计划推迟）** | C1 无全目标 ELF/QEMU、Linux 大程序集成闭环。 |

## 暂不清理：已核实有外部消费者或语义不等价

| 接缝 | 暂留原因 | 下一步证明 |
| --- | --- | --- |
| `A11-01` `ast_global` 八个旧 wrapper | `src/` 仅靠零调用判断为死路不够；历史 Python materialize/PR 脚本仍通过函数名作文本锚点，且头文件对外声明。 | 先清算脚本是否现行、头文件 API 合同，再退役；不能把脚本弄坏而称“纯删”。 |
| `A11-02` AST 参数计数 | Linux perf CI 的源码补丁脚本仍以其文本为锚点。 | 先迁当前 CI 的字符串匹配，验证精确补丁。 |
| `A11-12` Core Reachability policy | 五个已注册 Backend 都设 `emit_reachable_core_blocks_only=true`，false 路径当前不启用；但现行 Linux workflow 仍使用关联的历史补丁脚本，且 all-block 与 source-label 根追踪有契约历史。 | 先检查三个 CI 脚本在 pinned commit 上的实际 patch 行为，并证明所有 emitter 的 CFG 根合同，再删虚假策略分支；`minic_enqueue_core_block` 仍有现役 CFG 消费者。 |
| `A8-15` `GENL_COND_*`/`GENL_IF_PATH` 日志 | Core 中无环境开关的日志虽碍眼，现行 Linux profile 构建仍应用 `apply-netlink-if-control-trace-v0.py`，cohort bisect 脚本实际抽取 `GENL_IF_DECISION/GENL_COND_EMIT`。 | **KEEP(tooling)**；先把现行诊断消费者迁成稳定的可控日志合同并验证 workflow，再决定是否退役硬编码输出。不能按“debug print”直接删。 |
| `M194_SMALL_LOOP_PROBE` Core 诊断 | `cfg_constant_calls.c` 中约 70 行 debug/probe 代码被 `apply-core-unreachable-reentry-v0.py` 检查，且可被 `apply-small-constant-loop-probe-v0.py` 重新注入；`linux-runtime-earlycon-profile-probe-v1.sh` 为现行 focused workflow 使用。 | **KEEP(tooling)**，待迁移诊断脚本/工作流，再考虑退役，不能把当前 `src/` 调用数当作唯一依据。 |
| `CORE_SWITCH_DETAIL` stderr 路径 | `core_lower_switch.c` 分散约 25 行细节诊断无已知 CI 文本消费者，但可能是合法源程序拒绝时的用户可见错误合同。 | **NEEDS_PROOF(diagnostics)**；先做 GNU 正反例与诊断快照、确认错误原因归属，不能随意移除。 |
| `A11-25` Core alignment validation | Core verifier 的 object 循环未检查 `explicit_alignment`，Common storage planner 在后端检查且现有 native boundary gate 真实使用。 | **KEEP(validation hole)**；是验证职责缺口，不是死代码，留到后续合同重构或独立修复，不能计清理。 |

### C2：源函数/CI 消费者证据筛查（无源码删除）

已对 `src/` 内 `static` 函数引用、条件编译/常假分支、Core stderr/诊断字符串，
以及 `.github/workflows`、`tools/ci` 的脚本锚点做一轮交叉扫描。
在排除当前测试、历史脚本和潜在公开 API 后，**未再找到两项可独立证明
“直接删至少 10 行、且没有外部合同”的新目标**；因此 C2 不为了净减行数继续动源码。
下一步转向按子系统通读整个调用链：Frontend→Compiler→Core→Common Target→五目标，
逐文件登记输入、owner、失败退出、测试和历史脚本引用，再从证据中选 C3 删除项。

## 批次 C3：Frontend 基础层全文审读与两处现成实现复用

**源基线：** `fa68367`，2026-11-01；**逐文件全文阅读：22/336 个生产 C/H**
（Frontend **22/69**，本批源码约 **4,854 行**）。这次数字的含义是已从头至尾读取以下
当前文件并核对关键外部消费者，**不**代表每个行为已有跨架构动态证明；其他
314 个文件暂不计入本轮全文阅读。C1 的局部调用链阅读也不回填进全文计数。

| 全文阅读路径（均在 `src/frontend/`） | 生产者、消费者和本批结论 |
| --- | --- |
| `token.c/.h`、`lexer.c/.h` | Token 编号→诊断名，Lexer 借用 `source/path`，在 Parser 和词法测试中消费；GNU 预处理行标记和未预处理指令显式分流。**KEEP**，无死扫描器。 |
| `type.c/.h` | C 类型身份、pointer qualifiers、GNU vector/source builtin、构造与相容性；TargetInfo/Parser/Verifier 消费。**DELETE duplicate:** `minic_type_integer` 原来重复初始化 14 个字段，现调用已有 `minic_type_scalar`，再写 sign/rank，保持所有字段值。 |
| `attribute.c/.h` | GNU 属性拼写→语义级别→允许的声明位置/参数数；Parser 多个入口共享。别名拼写都是现役语法数据，不因数据行重复删；**KEEP**。 |
| `ast_traversal.c/.h`、`cast_normalization.c/.h` | child/external ID 遍历与先验后改写；Cast 归一化独立构造新 Expression arena，成功后移交 owned CALL overflow、失败时不替换旧 owner。公开 external visitor 虽无仓内调用仍有头文件合同；**KEEP(contract/transaction)**。 |
| `expression_semantics.c/.h` | Source ASM value/constraint、GNU null-pointer ICE、条件类型、pointer arithmetic stride；Parser 与 AST verifier 的共享准入，TargetInfo 供目标事实；**KEEP**，不把源许可冒充机器码能力。 |
| `initializer.c/.h` | 数组指定初始化的 last-write-wins 行为和逆序 skip-set；static/runtime 两类消费者共享，失败时清理暂态分配；**KEEP**，不是可删除的额外索引。 |
| `parser_builtin_memory.c/.h`、`parser_labels.c/.h`、`parser_static_assert.c` | builtin libc signature 归 Frontend、标签/asm-goto 回填和 GNU cleanup lifetime、静态断言常量值诊断；**KEEP**，保留独立错误与合法语法条件。 |
| `ast_function.c` | 函数 symbol/section/visibility/alias/constructor metadata 的 owner；两个 immutable owned-string setter 原各复制一次分配/字节拷贝/同名重声明校验，改为一个 `.c` 私有 `set_function_owned_name`；`error/warning` 可替换诊断的不同所有权保持原实现。 |

**C3 实际源码：2 个 `.c`，`+35/-56`，净 **-21 行**。不移动目录、
不改变 AST API、Type 结构体字段、GNU 语义准入、错误诊断或 Target ABI。

**本地验证：** Release 全源 `-Werror` 增量编译、`check-lexer`、`check-type`、
`check-type-alias`、`check-ast-contract`、`check-function-diagnostic-attribute`，
以及 `run-gnu-section-symbol-attribute.sh`、`run-gnu-function-visibility.sh` 均 PASS。
`run-gnu-function-asm-labels.sh` 的 `grep '  la a0, __real_renamed_api'` 在修改前
的独立旧编译器上**同样失败**；当前真实汇编仍有 `call __real_renamed_api`、
`lla t0, __real_renamed_api`，旧脚本的寄存器/助记符形状断言已漂移。
应单独修测试 oracle，不能把该脚本 FAIL 算作 C3 回归或声称它通过。
| `A11-04` AST external traversal | 上层函数虽零内部调用，但底层递归与 remap 现役，可能存在公开 API 合同。 | 明确 ABI / 外部消费者后才删最上层 wrapper。 |
| `A11-05` semantic snapshot wrappers | 生产无调用但白盒单测使用。 | 若要合并，先迁测试且保存回滚/ownership 断言。 |
| `A11-21` Core scalar predicate | 两个同形判断的持有者分别属于 memory 和 CALL；合并收益极小，邻近的 global-addressable predicate 对 `void` 的语义不同。 | 等输入合同证明和实际简化收益，不为了“统一”引入层级。 |
| `A11-28/30/32/38/39/47` 目标后端 | RV ELF 分类需要目标 GNU AS/ELF，RV64 destructor 有独立单测，x86 Record cap 与 Common 不同，legacy ASM 与 Loong SIMD 有机端二次防伪消费者。 | 逐族 Oracle / ABI 证明、先迁消费者再删旧实现；不跨 ISA 机械去重。 |

## 批次 C4：Frontend 声明与静态数据路径逐文件通读

**基线：** `b0846c7`，2026-11-01；本批从头到尾逐文件读完以下 **16 个生产 C/H**
（源码原始约 **9,355 行**）。累计全文深读 **38/336**，其中 Frontend
**38/69**，Compiler/Core/Target 仍为 **0** 个本轮完整深读；过去的路径扫描和
关联调用链抽查均不冒充全文读完。清理前代码有两处严格局部的冗余，
实际生产源码 **+33/-52，净 -19 行**，文档不计。

| 全文阅读文件（均在 `src/frontend/`） | 归属与决定 |
| --- | --- |
| `declaration_sema.c/.h` | 外部对象类型兼容的 snapshot→preflight→commit、tentative 数组复合和 TLS 合同有各自失败出口；不能合成无回滚的直接修改。**KEEP(transaction)**。 |
| `parser_global_lookup.c`、`parser_typedef.c`、`parser_enum.c` | 全局对象、typedef 与枚举常量查找的索引具有不同 first/newest、局部遮蔽语义，并保留索引分配失败时线性回退；表面相似不能强行共享。**KEEP(scope/fallback)**。 |
| `parser_declarator.c`、`parser_type.c`、`parser_function_parameters.c` | 直接 declarator、类型名抽象 declarator、函数参数 array/function adjustment 共享类型事实，保留各自括号绑定和临时数组 descriptor 的 owner。**DELETE duplicate:** `parser_type.c` 的函数指针/数组指针分类各复制的完整 `(* const ... *)` token 前缀扫描归并成一个 `.c` 私有探针，两个 suffix 判断和诊断路径仍各自运行；`parser_function_parameters.c` 删掉只赋值从不读取的 `has_outer_bound` 标记及其假使用。 |
| `parser_attribute.c`、`parser_pragma.c` | GNU attributes 的 parse-only/target layout/ABI 是不同准入等级；`#pragma GCC target`、visibility、pack 的堆栈均改变后续声明环境。**KEEP(policy)**，空白/括号扫描相似不足以跨 owner 抽新层。 |
| `parser_external_object.c/.h` | 旧 extern/tentative 合并需要保留 canonical array descriptor，成功后退役 parser 临时 descriptor；静态初始化器和 symbol metadata 有不同 commit 时序。内部 `CORE_FAST_TRACE` 输出仍可能有脚本消费者。**KEEP(ownership/tooling)**。 |
| `parser_record.c` | record 内部类型属性、字段属性、bit-field、匿名成员、灵活数组和 pragma pack 独立参与 DataLayout。发现的 `MC-061` 悬空借用、`MC-063` tagged bare member、`MC-067` attributed forward 均已在 MC 台账，**不在清理提交顺手修语义**。 |
| `parser_string.c`、`parser_static_constants.c/.h` | 字符串 length probe 与 append/replace 的 owner 不同；静态指针可为整数绝对值、global/member 地址、function 或 label relocation，并为复合 initializer 做一致性判定。**KEEP(relocation/ownership)**。 |

### C4 定点验证与风险边界

- **PASS：** `make MODE=release CFLAGS=-Werror BUILD_DIR=build/cleanup-werror
  -j7 build/cleanup-werror/bin/minic-cc`；增量 `check-record`、`check-type-alias`、
  `check-static-functions`、`check-global-objects`、`check-pointer-object-const`。
- **PASS：** `check-cast-expressions`、`check-pointer-arithmetic`、
  `check-pointer-subscripts`；脚本 `run-function-parameter-adjustment.sh`、
  `run-gnu-typeof-array-type-name.sh`、`run-gnu-typeof-array-bound-cast.sh`、
  `run-gnu-typeof-local-declaration.sh`、`run-record-multi-declarators.sh`、
  `run-gnu-packed-record-field.sh` 和 `run-gnu-function-attributes.sh`。
  脚本均显式设置本批 Release `MINIC`/`BUILD_DIR`。
- **基线脚本断言漂移：** `run-typeof-generic.sh` 的 `grep '  li a0, 7'`
  仍 FAIL；修改前的独立编译器运行同一脚本同样失败，不能计为本批回归，
  也不能声称此脚本 PASS。首次不带 `MINIC` 环境执行三个脚本时因默认
  `build/debug/bin/minic` 不存在而 exit 127，已用正确构建产物重测 PASS。
- **未做：** MiniAS/MiniLD 联合验收、全五架构 Linux ELF/运行矩阵。
  清理净删行只计上述两处，不把小弟的独立 MC 修复混入 C4。

## 批次 C5：Parser 状态与控制流合同全文审读（零生产改动）

**基线：** `3bf0290`，2026-11-01；完整阅读另外 **10 个生产 C/H，
共 4,000 行**。累计全文深读 **48/336**，Frontend **48/69**，
Compiler/Core/Target 本轮全文深读仍为 0。本批检出多处“外表可重复、合同却
不同”的路径，没有达成安全的完整删除闭包，**生产源码 +0/-0**；
不因追求删行数字而混入新结构或降低输入准入。

| 本批全文阅读（均在 `src/frontend/`） | 决定与保留依据 |
| --- | --- |
| `parser.h`、`parser_internal.h` | 公共 parser 入口要求选定 TargetInfo 贯穿后续代码生成；内部 Parser 浅拷贝用于 lookahead，`cache_owner` 将缓存所有权钉在原 Parser，局部标签、tag/ordinary 两命名空间和 cleanup 状态分离。**KEEP(interface/ownership)**。 |
| `parser_core.c` | scope 退出只倒退可见 binding 数、不释放 Program-owned 类型实体；函数名 hash 早声明胜出、allocator 故障可线性回退；`CORE_FAST_TRACE`、`MINIC_TYPEDEF_INDEX_PERF` 属现行诊断兼容面，未迁消费前不删。**KEEP(scope/fallback/tooling)**。 |
| `parser_member.c`、`parser_postfix.c` | 匿名 Record field path、rvalue member materialize、数组到指针衰减、函数指针实参 scratch 的生命周期跨表达式构建；`MC-034` 的 Record 快照逆映射不是可清理死路，须独立修。**KEEP(AST/owner)**。 |
| `parser_constant.c` | `int` 与 `uint64_t` 的整数文本读取虽然有循环重复，但溢出与允许范围不同；字符 escape 与字符串 escape 的输入宽度/报错也不同。**KEEP(validation)**。 |
| `parser_gnu_inline_asm.c/.h` | GNU output/input/clobber/goto 保留源身份；约束 `I` 可推迟到 Core 特化后判 ISA；`inline_asm_identifier_is` 虽与一般 lexeme helper 同义，但 `tools/dev/pr77-gnu-inline-asm.py` 仍含该 helper 的文本锚点。**NEEDS_PROOF(tooling)**，先确认工具生命周期再考虑合并；ASM-only function identity 脱落另属 `MC-064`。 |
| `parser_statement_control.c/.h` | `do-while` 通过 WHILE+条件断路构造，`continue` 目标/cleanup 路径必须精确；switch case 的有符号范围交叠检查、for 初始化 scope、延迟 update 均被 Core 消费。**KEEP(CFG/lifetime)**。 |

**C5 验证性质：** 只有只读审查和外部工具文本消费者 `rg` 核对，没有生产
变更，因此不冒称本批有新的 ELF/运行验收；沿用 C4 和独立 Bug 的各自明确
边界。下一批先完成剩余 Frontend **21/69** 个生产源文件的逐文件深读，
再审 Compiler→Core→Target，不提前抽出共用层。

## 批次 C6：AST 实体、验证边界与函数/常量只读分析

**起点：** `ede361e`，2026-11-01。本批另从头至尾逐文件阅读 **9 个生产
C/H，原始源码约 8,613 行**；截至本批累计全文深读 **57/336**，
Frontend **57/69**，仍有 Frontend 12 个、其他目录 267 个生产文件未在
本轮全文深读。不是把调用链抽查、自动搜索或先前 Bug 分析折算成通读。

| 全文阅读（`src/frontend/`） | 所有权、消费者与处理决定 |
| --- | --- |
| `ast.h`、`ast.c` | Program 含 Expression/Local/Statement/Block/Function/Record/ArrayType/FunctionType/GlobalObject 等可扩容 arena，实体 ID 稳定、借用指针会因 realloc 失效；CALL overflow、函数名及签名、内联 ASM、全局重定位均有独立释放/拷贝合同。**CLEAN duplicate:** `minic_c0_expression_array_object_info` 内针对已物化 ArrayType 的三份同字段提取归并成一个 `.c` 私有 helper；本地/成员的旧形式优先级、零长度与未完成数组、失败返回均保持原样。 |
| `ast_verifier.c/.h` | PARSED/NORMALIZED 表达式、源语言 TargetInfo 合法性、整数/指针与 record 值类别、ASM 输入输出、全局重定位、完整 Program arena 与详细诊断分阶段校验。未完成数组 owner 的索引为可失败加速器，仍有线性回退；恶意构造 AST 预检查次序另见既有 `MC-032`。**KEEP(validation/fallback)**，不能移掉失败出口或把机器码支持混为源准入。 |
| `function_body.c/.h` | 稀疏函数 body view、嵌套块/表达式/ASM 的 ownership 验证、静态 inline reachability 与 perf owner 索引各有不同合同；Expression 子节点遍历的浅拷贝可能写共享 CALL overflow，既有 `MC-030`，**KEEP(borrowed state)**。 |
| `semantic_snapshot.h` | 多种 parser 计数快照与仅类型/仅表达式复位虽样式近似，但不意味着回滚可释放已有 owned payload；对应既有 `MC-009`。**KEEP(transaction)**。 |
| `const_eval.c/.h` | C 整数常量表达式的有符号溢出、目标宽度、位运算、指针零/成员偏移、builtin overflow、浮点到整数的定义域分别约束；**KEEP(source semantics)**，不随意折叠与 runtime fold 似同的分支。 |

**C6 源码改动：** `ast.c` **+24/-38，净 -14 行**；仅消除三份机械复制的
materialized array descriptor 读取，不修改类型表示、公开 AST API 或 Parser。
至 C6 纯清理累计源码净 **-123 行**（C1 -69、C3 -21、C4 -19、C6 -14；
C2/C5 零），独立 MC Bug 修复不在内。

**验证 PASS：** Release 全源 `-Werror` 编译，`check-type`、
`check-ast-contract`、`check-pointer-arithmetic`、`check-pointer-subscripts`、
`check-record`、`check-global-objects`、`check-expression-statements`；
`run-gnu-zero-length-array.sh` 与 `run-compatible-incomplete-array-pointer.sh`
（GNU syntax oracle、x86 native、x86/aarch64/loongarch 代码生成、正负例）。
后一个脚本必须显式 `MINIC_CC=build/cleanup-werror/bin/minic-cc`，首次仅设
`MINIC` 导致默认 `build/debug/bin/minic-cc` 不存在而 exit 127；改正路径后 PASS，
不算产品失败。Linux 全架构/与 MiniAS MiniLD 联合验收继续推迟。

**结构性清理结论：** 多个 P0 的共同耦合不是“重复函数”，而是递归 Parser
跨 `Program` arena 增长操作保留 `Record*`、`ArrayType*`、`RecordField*`
借用指针，所有权写在 `ast.h` 却靠各调用现场自觉遵守。
本阶段只记录并由独立 MC 小批次按 stable ID/按值快照修补，**不**先实施
arena/Parser 架构重构，也不将 Bug 行数冲入清理成绩。

## 批次 C7：全局符号/重定位与语句/局部声明合同

**源码改动基线：** `a4b8b71`；本批另外全文读完 **5 个生产 C/H、原始 5,885 行**：
`src/frontend/ast_global.c`、`parser_global.c`、`parser_statement.c`、
`parser_local_declarations.c/.h`。累计全文深读 **62/336**，Frontend
**62/69**；尚未读完的 Frontend 7 个文件均为 Parser 大型主体/初始化器，
Compiler/Core/Target 267 个文件本轮尚未全文读完。

| 审读接缝 | 证据与判定 |
| --- | --- |
| `ast_global.c` | 统一 GlobalObject 的 extern/tentative/definition、按字段/数组槽定位的重定位、union 选择、flexible tail、按 function owner/pruned 状态发射；重定位 binary search 维持有序/不重叠，失败的 owned-map 可退回慢查询。`minic_c0_global_initializer_slot_count` 看似仅包一层，但仍属 `ast.h` 公开调用合同；新建 static relocation helper 可能破坏当前外部脚本锚点。**KEEP(relocation/API/fallback)**。 |
| `parser_global.c` | static/extern 复合数组边界、声明符链、属性附着与 canonical array descriptor 的生命周期并不同步；已有成功后显式丢弃临时 descriptor 的逻辑。**KEEP(tentative transaction)**，不把 declaration merging 当成重复入口删。 |
| `parser_statement.c` | GNU statement-expression 移除最后一条 value-statement，把结果变 AST 值；`return` 类型在递归解析前做 snapshot，GNU 属性 lookahead 用 Parser probe，赋值链要求稳定 lvalue。`parse_return` 中旧式 `current == '='` 后处理分支看似有悬空借用风险，但独立五目标 ASan/GNU 对照未复现，覆盖显示当前 assignment 已在 `parse_full_expression` 里吞掉，真分支零命中。**NEEDS_PROOF(dead branch)**：等 C8 全读 `parser_expression.c` 并确认所有入口语法后再考虑纯删除，不误报新的 P0。 |
| `parser_local_declarations.c/.h` | local VLA `alloca` count/address 隐藏局部、GNU cleanup context、`__auto_type` 先初始化后 bind 名、block extern、static local 的 symbol owner 与 section 均现役。**CLEAN duplicate:** 六种 static-local 构造路径使用完全相同的 `snprintf("__minic_static_local_%zu_%zu")` 与截断判定，统一成文件私有 `format_static_local_symbol_name`；各路径仍保留原报错/array/record/relocation/section 决策。 |

**C7 生产源码：** 仅 `parser_local_declarations.c` **+24/-37，净 -13 行**。
累计纯清理 **净 -136 行**（C1 -69、C3 -21、C4 -19、C6 -14、C7 -13），
不混入此间独立合入的 MC-061/039/056/049/023 等 P0 修复。

**验证 PASS：** `-Werror` Release 全编译；12 个 static-local focused 脚本：
`run-static-local-{scalars,arrays,record-initializers,pointer-arrays,inferred-arrays,fixed-arrays,self-reference,string-arrays}.sh`、
`run-gnu-static-local-{implicit-zero,interleaved-attribute}.sh`、
`run-static-local-prefix-attributes.sh`、`run-gnu-efi-static-local-wide-arrays.sh`。
`run-static-local-owner-reachability.sh` 在 GNU RV64 链接阶段因缺少交叉
`crt1.o`/`-lc` 失败，属于 **NOT_TESTED（运行环境）**；其前置编译/汇编已通过。
首用 `sh` 调用两个 `bash -o pipefail` 脚本报 shell option 错，改用 `bash`
后 EFI wide 测试 PASS；不将 invocation 错误当成源码回归。
MiniAS/MiniLD 联合验收和完整五架构 Linux 运行继续推迟。

## 后续批次进度规则

每批发布：当前 HEAD、源文件逐目录覆盖（明确深读与只扫的区别）、
候选数与 `KEEP/NEEDS_PROOF/DELETED` 判定、删改行数、验证矩阵、未解决的耦合，
以及 Git commit。一个批次累计多项再提交；不为单个 helper 刷大量小提交。

遇到真实错编、合法程序拒编或跨 ISA 约束冲突，进入唯一 MC 台账，
由独立 Bug 修复批次处理；清理账只记录依赖，不把 Bug 修复计入 C1 净减行数。
