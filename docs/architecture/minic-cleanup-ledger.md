# MiniC 纯清理流水账：先通读、补合同，再重构

> 持续维护；以 [架构入口](README.md)、[函数契约](minic-function-contract-register.md) 和
> [MC 问题台账](minic-issue-register.md) 为事实来源，不复制 Bug 的永久编号与状态。
> **清理**仅允许：删有调用链证明的死路、收窄私有接口、去同一操作的局部重复计算、
> 修正文档漂移。不得在本阶段移动目录、拆出新层、统一不同 ABI、改 IR 合同，
> 或把 Bug 修复混入“净删行数”。

## 工作边界与统计口径

- 2026-11-01 起始源码基线：`refactor/minic-target-language-boundary` / `6934c9e`。
  本轮复核 `rg --files src` 的 C/H 实际共 **339 个**：Frontend 69、
  Compiler 11、Core 48、Target 211，另有两个 CLI。最初流水账误记
  Target 208/总数 336，C10 收官时已纠正；前序 F8 的 337 个是冻结的
  旧输入集合，不能当本轮分母。
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
  而当前 HEAD 上可逐文件追溯的“完整深读”清单仍待重建，暂不宣称 339/339 结束。

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

**源基线：** `fa68367`，2026-11-01；**逐文件全文阅读：22/339 个生产 C/H**
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
（源码原始约 **9,355 行**）。累计全文深读 **38/339**，其中 Frontend
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
共 4,000 行**。累计全文深读 **48/339**，Frontend **48/69**，
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
C/H，原始源码约 8,613 行**；截至本批累计全文深读 **57/339**，
Frontend **57/69**，仍有 Frontend 12 个、其他目录 270 个生产文件未在
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
`parser_local_declarations.c/.h`。累计全文深读 **62/339**，Frontend
**62/69**；尚未读完的 Frontend 7 个文件均为 Parser 大型主体/初始化器，
Compiler/Core/Target 270 个文件本轮尚未全文读完。

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

## 批次 C8：完整表达式归属与 `return` 不可达旧分支

**基线：** `9d6e559`，2026-11-01。完整通读另外 **3 个生产 C/H、
约 3,019 行**：`src/frontend/parser_expression.c`、
`parser_runtime_initializer.h`、`parser_static_initializer.h`。
累计全文深读 **65/339**，Frontend **65/69**；未全文读完的四个 Frontend
大型源文件是 `parser_builtins.c`、`parser_function.c`、
`parser_runtime_initializer.c` 和 `parser_static_initializer.c`。
此前为调查 Bug 而抽读其片段不折算为全文通读。

| 审读接缝 | 实际调用闭包与决定 |
| --- | --- |
| `parser_expression.c` | 整数常量表达式、GNU compound literal、cast、前后缀、成员/数组和指针运算按 precedence 与 TargetInfo 建立 AST；普通/复合赋值已经由 `minic_parser_expression_parse_internal(..., minimum_precedence=0)` 消费，逗号表达式由 `parse_comma_expression_tail` 完成。**KEEP(source semantics)**：不把不同优先级、值类别、目标宽度的分支当成机械重复。 |
| `parser_statement.c` 的 `parse_return` | 返回值调用 `minic_parser_parse_full_expression`，其入口是 `parse_comma_expression → minic_parser_expression_parse_internal(..., 0U, ...)`；`=` 分支直接构建 `MINIC_EXPRESSION_ASSIGNMENT` 并递归消费 RHS。故原 `parse_return` 后续再检查 `current.kind == MINIC_TOKEN_EQUAL` 的 37 行旧式 statement assignment 路径，对完整表达式的合法返回入口不可达。C7 的 `NEEDS_PROOF` 在本批由完整源码调用闭包、独立五目标 ASan 15/15 和 gcov 零命中交叉确认；**DELETE(dead branch)**，仅移除该路径，不动表达式 parser。 |
| 两个 initializer `.h` | runtime/static initializer 的声明仍由 Parser 调用，头部内容短但各有独立所有权/效果合同。**KEEP(interface)**；实现文件只做过专项审查，尚未完成逐行阅读。 |

**C8 生产源码：** `parser_statement.c` **+0/-37，净 -37 行**。
累计纯清理 **净 -173 行**（C1 -69、C3 -21、C4 -19、C6 -14、
C7 -13、C8 -37；C2/C5 零）；任何独立 MC 缺陷修复仍不计入。

**验证 PASS：** Release `-Werror` 增量全编译、`check-ast-contract`、
`check-expression-statements`、新增 `check-return-assignment-expression`。
新增固定回归核对 GNU/C11 语法、MiniC 五目标 `-S`、GNU/MiniC x86_64
原生运行（exit 0），覆盖 `return a = b`、嵌套链 `a = b = c`、
赋值与三元表达式结合；`const int` 非法赋值在 GNU/MiniC 两侧都拒绝。
`check-fast` 纳入此测试。未声称完成五架构 ELF/运行或 MiniAS/MiniLD 联合验收。

## 批次 C9：Builtin 语法分派与顶层函数声明事务

**基线：** `508622d`，2026-11-01。本批全文通读 **2 个生产 C 文件、
原始 4,843 行**：`src/frontend/parser_builtins.c` 与
`src/frontend/parser_function.c`。累计全文深读 **67/339**，Frontend
**67/69**；剩余两个 Frontend 文件是 runtime/static initializer 的大型实现。

| 组件与事实 | 清理决定 |
| --- | --- |
| `parser_builtins.c` 早/晚分派 | `va_*`、`offsetof`、`_Generic`、GNU `choose_expr`、`constant_p`、`object_size`、溢出与 target builtin 共享入口但结果类型、是否真正求值、TargetInfo 准入、Core 表达式归属完全不同；Target builtin 会增加 Program functions，`va_start` 已在递归前快照 parameter_count。**KEEP(source/target policies)**；不抽统一 builtin 表或更改成功/失败路径。 |
| `__builtin_offsetof` 判名 | 文件私有 `current_is_builtin_offsetof` 按 token kind、lexeme 长度及字节逐一比对，与已在 `parser_core.c` 实现的 `minic_parser_current_identifier_lexeme_equals_cstr` 完全等价，只有一个站内 caller，外部脚本无该文本锚点。**DELETE(duplicate matcher)**，改直接调用共享 lexer helper。 |
| `_Generic` / `__builtin_types_compatible_p` | 文件私有 `generic_types_compatible` 仅原样转发 `minic_c0_types_compatible`，无额外 source policy/转换/诊断；两处调用点直接使用原函数。**DELETE(pass-through)**；association 选择与 `_Generic` 控制表达式不求值的合同保留。 |
| `parser_function.c` 声明属性与对象归属 | `parse_function` 同时决定 file-scope 对象、typed function declarator、prototype、definition、weak/alias/visibility/asm label、constructor、TLS、GNU extern inline，先 collect/defer 属性再根据 entity 定归属；alias 仅 TU 结束解析、诊断字符串先保留 span 再 Program-owned。**KEEP(transaction/metadata)**。`typed_is_variadic` 从同一 FunctionType 连续赋值两次，第二次无写入间隔。**DELETE(identical assignment)**。 |
| `parser_function.c` 其他判名 | `function_identifier_is` 与 top-level `__extension__` 判名表面也可使用共享 helper，但 `tools/dev/pr75-gnu-function-attributes.py`、`pr77-gnu-format-cold-function-attributes.py`、`pr77-gnu-extension-prefix.py` 等含原函数/调用的字符串锚点。仍未确立这些开发脚本已退役，**NEEDS_PROOF(tooling consumer)**，暂不删；函数声明前后两个 prefix scanner 的 token 准入、属性捕获也不同。 |

**C9 生产源码：** `parser_builtins.c` **+3/-21**，`parser_function.c`
**+0/-1**，合计 **净 -19 行**；累计纯清理 **净 -192 行**。
以上仍不包含任何独立 MC 修 Bug 的增删行。

**验证 PASS：** Release 全源 `-Werror` 编译、`check-static-functions`、
`check-global-objects`、`check-type`、`check-record`；
`run-function-typed-declarator.sh`、`run-gnu-function-attributes.sh`、
`run-gnu-function-copy-alias.sh`、`run-gnu-prefix-function-attributes.sh`。
新 `check-builtin-lexeme-contract` 用现有三份 GNU C fixture 做 GNU 编译
与 MiniC `-S`、GNU/MiniC x86_64 原生执行对照（均 exit 0）：
`offsetof` 的硬编码断言仅适用四个 LP64 目标；`_Generic`/
`__builtin_types_compatible_p` 和 GNU `choose_expr` 覆盖五个目标。
测试已接入 `check-fast`。

**旧测试的边界：** `run-builtin-offsetof.sh`、
`run-gnu-compiletime-selection-builtins.sh`、`run-gnu-choose-expr-bitfield.sh`、
`run-typeof-generic.sh` 的历史汇编文本/符号留存断言在当前优化输出下
exit 1，不计 PASS，也不能据此宣称新清理造成运行语义回归；首次未设
`MINIC` 时还发生默认 debug executable 不存在的 exit 127，重设 Release
路径后上述断言仍不满足。新跨目标汇编/本机运行探针专门覆盖本批触及的
判名与类型兼容合同。未运行完整五架构 ELF/QEMU，也不启动 MiniAS/MiniLD
联合验收。

## 批次 C10：Runtime/Static Initializer 双实现完整深读，Frontend 收官

**基线：** 03417d6，2026-11-01。本批全文通读最后 **2 个 Frontend
生产 C 文件、原始 5,356 行**：parser_runtime_initializer.c（1,776 行）
与 parser_static_initializer.c（3,580 行）。至此本轮 **Frontend 69/69
全部全文深读**，全生产树 **69/339**；Compiler/Core/Target/Driver 等
**270 个文件尚未按本轮标准全文深读**。阅读覆盖数不等于联合验收。

| 两条 initializer owner | 实际合同、耦合与决定 |
| --- | --- |
| Runtime scalar array | MinicArrayInitializerPlan 接收 designated/positional 写入，Resolved 保存最终 owner；先补未填零槽，再按 action 顺序 lower；GNU range 最终多 owner 通过临时 Local 确保表达式只求值一次，全部覆盖的旧 action 可消除效果。**KEEP(evaluation order/side effects)**，不能与静态字节布局路径合并。 |
| Runtime record / nested array | local/record lvalue 按稳定 ExpressionId 构造，先做类型转换再发 Statement；记录数组可单次初始化后 record-copy，nested array descriptor 在递归前快照防 arena 扩容。已修 MC-039/056；另有 MC-035 的表达式借用、MC-038 的 designated 恢复字段索引，MC-003 旧 Record 悬空借用也已由 MC-056 同根修复覆盖并新增定点回归。**KEEP(lifetime, track risk)**。 |
| Static scalar / symbolic slot | 常量 bits、pointer/integer relocation 与覆盖/追加必须维持同一 slot identity；CORE_FAST_TRACE 有调试定位消费者，未确认退役前不删。**KEEP(relocation/debug compatibility)**。 |
| Static aggregate arrays | action payload 保存 scalar bits、relative relocation 和 union selection；capture 撤回 GlobalObject 临时写入，Resolve 后重放并重基址。链式设计符重复覆盖只替换选中子槽，其余保留旧 action。与 Runtime 的求值顺序合同不同。**KEEP(transaction)**。 |
| Static record/union/backward | 匿名 path、非 canonical union member/active span、GNU zero-length/FAM、前向补零与后向覆盖对应不同布局及拒绝出口；MC-023/049 已有定点快照修复。**KEEP(DataLayout/rollback)**。 |
| Runtime 重复声明 | grow_runtime_array_action_values 和 lower_runtime_scalar_array_plan 在同一 C 文件上半部已有完整定义，后面又重复出现相同静态原型共 12 行；无工具文本消费者。**DELETE(redundant declarations)**，无公开接口变动。 |

**C10 生产源码：** parser_runtime_initializer.c **+0/-12，净 -12 行**；
累计 C1～C10 纯清理 **净 -204 行**，完全排除并行 MC 修复的增删行。
两类 initializer 仍共享 AST/arena，但清理期只输出映射与问题，
不提前抽新的 Initializer 架构层。

**验证 PASS：** Release 全源 -Werror 编译；MC-039/056/023/049/061/002
六个已合入的容量门禁（五目标 -S、GNU/MiniC x86 native）；
run-runtime-local-array-initializer.sh、run-runtime-record-array-initializers.sh、
run-runtime-record-zero-shorthand.sh、run-static-union-zero-overlay.sh、
run-static-nested-record-designator.sh、
run-static-record-nested-designator-string-overlay.sh、
run-static-record-array-member-designator.sh、
run-static-storage-scalar-array-owner.sh、
run-static-multidimensional-array-relocation.sh（五 ISA 源汇编）。
static_aggregate_array_designators.c 直接 GNU/MiniC x86 本机编译运行均 exit 0。

**旧脚本不冒报：** run-static-aggregate-array-designators.sh 在
“.size sparse_records, 32” 保留符号文本检查处 exit 1，
当前优化输出无该静态符号；对应 fixture GNU/MiniC 原生执行核对通过，
旧脚本本身仍 **NOT PASS**。run-static-multidimensional-array-relocation.sh
首次未设置 MINIC_CC 而寻找不存在的 debug 编译器（exit 127），
更正路径后 PASS。完整 ELF/QEMU 与 MiniAS/MiniLD 联合验收继续推迟。

**独立 P0 小批次（不计入 -204 行）：** C10 期间另外合入 MC-002
原函数指针 Record UAF 的独立容量回归（03417d6），确认与已合入 MC-061
同根；MC-001 Symbolic Closure Expression arena UAF 的稳定 ID
重取修复和容量测试（7656565）；MC-003 原 tagged Record UAF 的独立
容量回归（e720381），确认已被已合入 MC-056 同根修复覆盖。
三项均有旧版 ASan 证据与新版五目标 -S/本机专项验证，
仍统一保持 FIXED-UNVERIFIED，未达到完整 ELF/QEMU/联合验收门槛。

## 批次 C11：Compiler 全目录深读，清退符号特化中的无效旧路

**基线：** `d4026e3`，2026-11-01。本批逐行通读 Compiler **11/11**
生产 C/H 文件、原始共 **4,667 行**：compiler.c、core_function_set.c/.h、
core_reachability.c/.h、inline_boolean.c、inline_integer.c、
inline_reference.c、inline_specialize.h、inline_specialize_internal.h、
inline_symbolic.c。本轮累计 **Frontend 69/69 + Compiler 11/11 = 80/339**
全文深读；Core **0/48**、Target **0/211**。剩余 **259/339**
未经本轮逐行审读。只审阅本阶段生产树，不把历史路径扫描冒充全文深读。

| 路径 / 候选 | 合同与判断 |
| --- | --- |
| Compiler orchestration | Parser/normalized AST/owner 三重前置验证、四类 inline specialization、Core lower 与 DataLayout 的顺序有依赖；`MINIC_BOOTSTRAP_TRACE` 和失败 census 有外部诊断消费者。**KEEP(pipeline contract)**。 |
| Core function / static object reachability | 静态对象裁剪会改变回调函数的活跃根，因此函数裁剪必须前后做两次；GNU file ASM、alias、relocation 和残留 AST 引用保守保活。Core block reachability 由 backend 配置。**KEEP(two-pass reachability, fallback)**，旧回调与段符号问题另按 MC 台账核验。 |
| Integer / symbolic / Boolean inline | 三次特化的来源、section、共享 FunctionBody 与调用方事实不是同一种 identity；布尔 Local domain 的线性旧查询是缓存分配失败时的备用逻辑，符号传递 CALL 按 owner 分桶但共享/不明 owner 仍必扫。**KEEP(OOM fallback, shared AST contract)**；MC-006/007/008/043/058 独立排查，不能用清理名义处理。 |
| Symbolic static expression | `MINIC_EXPRESSION_LVALUE_READ` 分支只返回 `operand != NULL && ... && false`，恒为 false；后面的默认分支已覆盖该 kind。**DELETE(dead predicate + redundant AST lookup)**。 |
| Boolean/symbolic unused casts | Boolean cache builder 中 `(void)minic_inline_local_is_parameter` 指向本文件真实调用过的函数，symbolic transitive loop 的 `(void)original_function_count` 指向本函数多次实际使用的局部变量；二者都是无效旧占位。**DELETE(no-op casts)**。 |
| Reference finalization | Core 层尚未能覆盖所有 GNU file ASM / 源级 label identity，AST 残余与 sub-string ASM 引用保守保留；仅靠 Core 图会漏真实外部引用。**KEEP(conservative roots)**。 |

**C11 生产源码：** inline_symbolic.c **+0/-6**、inline_boolean.c
**+0/-1**，合计 **净 -7 行**；累计 C1～C11 纯清理 **净 -211 行**。
所有 P0/P1 独立修复增删不计入该数字。本批仅移除恒假分支与无效语句，
未改变公开接口、优化预算、ABI、FunctionBody owner 或代码生成时序。

**验证 PASS：** `make MODE=release CFLAGS=-Werror
BUILD_DIR=build/cleanup-werror` 完成 `minic-cc` 链接；MC-001
symbolic arena 稳定容量回归（双变体、五目标 90 次 `-S`、GNU/MiniC
原生六次对照）；symbolic shared body 传递克隆与 GNU 原生对照；
symbolic two callers 五目标 `-S`、GNU/MiniC x86 原生对照；
GNU Linux kmalloc Boolean guard（GAS/LD、GNU/MiniC 原生 exit 42、
不安全路径负向检查）；GNU dead static object callback 图（GNU AS、
`nm -u`、存活/死根差分）。

**部分验证、不作 PASS 冒报：** `run-static-local-owner-reachability.sh`
修正入口变量为 `MINIC` 后，x86_64 GNU/MiniC ELF/原生对照 PASS，
AArch64/LoongArch64 仅 `.s` PASS（缺 GNU 交叉工具链），RV64 源汇编
与 GNU AS 已通过，但宿主缺 RISC-V `crt1.o`/`-lc` 导致静态
链接 exit 1；该脚本的全程结果仍为 **NOT PASS(host toolchain)**。
最初误用 `sh`（脚本要求 bash）、误用 `MINIC_CC`（实际读 `MINIC`）
两次调用无效，未纳入验证结果。MiniAS/MiniLD/五目标完整联合验收仍延后。

## 批次 C12：Core SSA 引用、CFG 重入、局部事实与合成 Loop 形状

**基线：** `dec0155`，2026-11-01。全文深读 Core **8/48** 文件，
原始合计 **1,564 行**：value_uses.c/.h、reentry_index.c/.h、
local_facts.c/.h、source_loop_shapes.c/.h。本轮总计
**88/339**（Frontend **69/69**、Compiler **11/11**、Core **8/48**、
Target **0/211**）；尚有 Core 40、Target 211，**251/339 未深读**。

| 代码和消费者 | 本轮清理判断 |
| --- | --- |
| `value_uses` | 遍历每个 Core Instruction、call 参数与 terminator 的真实 SSA operand，重复 operand 按两次引用记账；未知 op/value ID 和计数溢出 fail-closed。此处无 target stack/register 合同。**KEEP(enum completeness/unknown rejection)**。 |
| `reentry_index` | 先完整验证 goto、asm goto、label-address，再发布索引；不存在或 OOM 时退回 AST 全表扫描，不能把“索引未命中”当无 CFG 入边。`if` 丢弃、loop label、case/default 的查询强度不同。**KEEP(negative proof fallback)**。 |
| `local_facts` | known bits、specialization facts、nonnegative range、escape 和 possible-one bits 是不同保守事实；source write/increment 要 invalidate，未知 CFG merge 要 meet 或丢弃，trace 被 Linux 诊断脚本消费。**KEEP(invalidation + trace)**；MC-033 旧错编已由现有生产代码修复，另由独立小批次增加永久验证。 |
| `source_loop_shapes` | Parser synthetic `do/while` 的 0/true 识别、`for` continue/update tail、cleanup context 和源位置全部参与 CFG 证明。两个看似相同的 `do/while` matcher 合同不同，**KEEP(provenance)**。源位置三字段相等在 while-label 中重复手写，**DELETE(duplicate comparison)**，改复用同文件已存在的 `source_position_equal`，不改变判定次序与字段。 |

**C12 生产源码：** source_loop_shapes.c **+6/-10，净 -4 行**；
累计 C1～C12 纯清理 **净 -215 行**，不含 MC-033/035 等 Bug
修复、专项测试或诊断文档。本批没有拆 Core 层、改 SSA 枚举或改变
任何 source CFG 判活条件。

**验证 PASS：** Release `-Werror` 编译链接；Core typed reentry
indexed/fallback、AST source-subtree membership、两例五 ISA `-S`
以及 GNU/MiniC x86 native；SSA value-use-count 单元测试
（重复 operand、terminator、死结果、异常输入）；runtime IF
local-effect-facts GNU/MiniC 原生差分、RV64 `-S`。完整 Linux
QEMU/跨目标 GNU ELF 与联合验收仍待后续阶段执行。

## 批次 C13：Core IR 构造、Verify、Dump 全路径审读与重复调用输出

**基线：** `5942dcf`，2026-11-01。本批全文深读 Core **5/48**
生产文件、原始 **3,776 行**：core_ir.c/.h、core_ir_internal.h、
core_ir_verify.c、core_ir_dump.c。累计全文深读 **93/339**：
Frontend **69/69**、Compiler **11/11**、Core **13/48**、Target
**0/211**；Core 仍剩 **35** 个、Target **211** 个，合计
**246/339** 个生产文件待通读。

| 文件/调用关系 | 本批决定与后续重构约束 |
| --- | --- |
| `core_ir.h` + `core_ir.c` | Value/Instruction/Block/Object/FunctionSymbol 等 ID 使用固定 `uint32_t` 身份、哨兵值与 `size_t` 容量；builder 先保留空间再提交，指针不能跨 `realloc` 借用。function-address 与 direct callee 不同，Record Call 值/对象是两种存储。**KEEP(stable IDs + append commit)**。 |
| `core_ir_verify.c` | 每个 block 内验证 SSA 值定义顺序，跨 block 不凭空继承 `available_values`；直接 CALL 的变参 record 与间接 CALL 的变参 scalar 接纳范围不同，不能共享完整参数校验。GNU structured ASM 的 source/target provenance、寄存器早破坏、memory clobber 等也互不等价。**KEEP(fail-closed admission)**。MC-055 不完整 auxiliary shape preflight 保持独立调查。 |
| Core IR 语言耦合 | `MinicType`、`MinicRecordId`、`MinicEnumId`、`MinicFunctionTypeId`、source label / span 仍被构造器、验证、backend 访问；这是未来目标无关 IR 的边界债，**NEEDS_PROOF(language boundary)**，当前清理不删除承载真实身份的字段。 |
| `core_ir.c` 私有 callee 比较 | `minic_core_function_add_callee` 已用相同 name_length/memcmp 找到候选，再调 `callee_signature_equal` 反复判断完全相同名字；只有一个内部调用者。**DELETE(redundant name comparison)**，保留返回类型、定参类型、变参与调用约定的比较。 |
| `core_ir_dump.c` CALL 输出 | 直接/间接 CALL 的参数打印循环完全同构：VALUE `%n`、OBJECT `%on`、逗号分隔与 fprintf 失败传播均相同，差别仅在前缀与 signature 尾缀。**DELETE(two copies)**，改复用唯一私有 `dump_call_arguments`；不改任何 Core 文本格式。 |

**C13 生产源码：** core_ir.c **+2/-7（净 -5）**、
core_ir_dump.c **+24/-44（净 -20）**，本批 **净 -25 行**；
C1～C13 纯清理累计 **净 -240 行**。新 Core API 单元测试、
shell gate 与 Makefile 入口不纳入生产删行统计。

**验证 PASS：** Release `-Werror` 重编 Core IR 并链接 minic-cc；
新 `check-core-call-dump` 构造含 `VALUE + OBJECT` 两类实参的
direct/indirect CALL，逐字节比对 Core 文本与预期格式；
Core literal branch fold 及 malformed fail-closed 单元测试；
Core reentry indexed/fallback + subtree、五 ISA `-S`、GNU/MiniC
x86 原生；GNU Linux Boolean guard GNU AS/LD/原生差分和负向检查。
没有执行全量 CI、五 ISA ELF/QEMU 或 MiniAS/MiniLD 联合验收。

## 批次 C14：Core source-to-IR 私有边界与地址/事实扫描（仅记录）

**基线：** `394385b`，2026-11-01。本批全文深读 Core **6**
生产文件、原始 **1,836 行**：core_lower_internal.h（360）、
core_lower_types.c（185）、core_lower_metadata.c（115）、
core_lower_bool.c（199）、core_lower_local_effects.c（371）、
core_lower_address.c（606）。累计 **99/339** 全文深读：
Frontend **69/69**、Compiler **11/11**、Core **19/48**、Target
**0/211**。Core 余 **29** 个，Target **211** 个，合计 **240/339**
待全文深读；接口消费关系核对不能替代五目标运行。

| 主路径 | 合同与清理决定 |
| --- | --- |
| `core_lower_internal.h` | 私有接口仍按 assignment、record、varargs、ASM、CFG、fact 等 source-to-Core 语义家族分治；`MinicCoreLowerContext` 借用 FunctionBody 和重入索引，不持有 Target ABI 寄存器。**KEEP(current consumers)**，后续边界重构需把 API 依赖从源语言事实逐步隔离。 |
| `core_lower_types` 与 `core_ir` | memory scalar transport 与 call scalar transport 当前恰好接受同类标量，但未来 target ABI、GNU `extern void` 对象取址的合同不同；源码 addressable global 不包括普通 void，Core IR symbol table 可承载特殊外部 void。**KEEP(distinct admission contracts)**，不因为当前谓词相似就统一。 |
| `core_lower_metadata` | Enum 的有效整数 representation 从 Parser snapshot 到 Core、固定寄存器 binding 从 Frontend owner 复制到 Core；不允许 backend 重新读取 C AST 兼容类型。**KEEP(semantic metadata)**。 |
| `core_lower_bool` | Whole-source 零值 `_Bool` 不变量依赖入口赋零、全部写入纯零、无逃逸/RMW/asm 输出及后续 CFG 重入；每次全程序扫描虽贵，但缺审计证据不能缓存错误阴性结论。**KEEP(fail-closed scan)**。 |
| `core_lower_local_effects` | AST 子树批量 effect mask 对未知表达式/过深递归保守 poison 全部 local；GNU `__builtin_constant_p` 不执行操作数，不能误报写副作用。更新类 unary 目前以 `operator_kind > MINIC_UNARY_BITWISE_NOT` 区分，依赖历史 4～7 运算符编号；**NEEDS_PROOF(enum ordinal coupling)**，还不是已证明真实 Bug。 |
| `core_lower_address` | 三种 compound literal 地址都暴露 storage，但数组旧表示、标量 temporary 和 Record snapshot 初始化合同各异；`*&` 快捷路径还必须逃逸 local fact，subscript 对静态 global+纯 0 下标保留 relocation 身份。**KEEP(sequence/provenance)**，不合并为一个地址 emitter。 |

**C14 生产源码 +0/-0，净 0 行**；累计 C1～C14 纯清理仍为
**净 -240 行**。本批候选 6 组中 **KEEP 5 / NEEDS_PROOF 1 /
DELETE 0**；明确记录不应删除的逻辑，避免之后凭重复外观破坏语义。
另有 MC-036 Record 条件表达式两臂事实隔离已由独立 Bug 分支修复，
复用现有 scalar/void fork/meet 规则而未迁移公共层；重构阶段再审其重复协议。

**验证 PASS：** 当前 Release `-Werror` minic-cc 全源链接；
`run-core-local-effect-summary.sh` 的 AST 白盒单元、GNU/MiniC
x86 原生与五 ISA `-S`；`run-gnu-runtime-if-effect-facts.sh`
的 x86 GNU/MiniC 原生差分和 RV64 `-S`。无实际生产改动，
完整五 ISA ELF/QEMU 与联合验收仍推迟。

## 批次 C15：Core 函数入口、源 CFG 与逻辑值落盘（清理 + 审读）

**基线：** 远端 C14 同树提交 `e57f2a0`，叠加独立 MC-009/042/051
定点修复 `cbc439a` / `3f6cd4f` / `a0232f0`，2026-11-01。
本批全文深读 Core **4** 个生产文件、原始 **3,087 行**：
`core_lower.h`（22）、`core_lower.c`（1,639）、
`core_lower_expr_flow.c`（747）、`core_lower_assignments.c`（679）。
累计 **103/339**：Frontend **69/69**、Compiler **11/11**、Core
**23/48**、Target **0/211**；Core 余 **25**、Target 余 **211**，
合计 **236/339** 文件待全文深读。

| 语义/所有权边界 | 清理判断与后续重构提醒 |
| --- | --- |
| `core_lower.h` / `core_lower.c` | `FunctionBodyView` 传入源函数、`TargetInfo` 只读布局、Core builder 接受固定 ID；带索引与不带索引两个入口共享主体。参数入口要先把定义局部类型规范化到 IR signature，再 materialize stack object；不能把 frontend 兼容类型误当 Core 严格类型身份。**KEEP(source/Core signature normalization)**。 |
| `core_lower.c` 的 Return/Cleanup | GNU return 表达式必须先求值再执行 cleanup；标量返回 spill/reload 跨清理调用，Record 返回带 cleanup 尚未建模，应保持 fail-closed。后半部的 unreachable tail、parser synthetic while label、外部 goto/asm goto/label-address 用 reentry membership 证明，不能把字符串/行号近似当 CFG 入边。**KEEP(evaluation order + reentry proof)**。 |
| `core_lower_assignments.c` | 普通赋值是 RHS 求值并 spill → 目标地址求值 → reload/store；GNU vector16 先 snapshot 16B 后才评估目标，指针 `+=` 先保留旧地址/指针值后才执行可能分裂 CFG 的 RHS；bit-field 和固定寄存器另有精确 owner。赋值后清除/重建 local facts 的顺序是真正数据流约束。**KEEP(distinct lvalue owners / fact invalidation)**。 |
| `core_lower_expr_flow.c` | void/scalar/GNU `a ?: b` 走不同结果表示，但互斥臂共用 snapshot/restore/meet；不能为减少重复行数混同 GNU omitted-middle 的一次求值语义。`&&/||` 物化真/假结果时，两臂的 INTEGER_CONSTANT → OBJECT_ADDRESS → STORE 三条 Core 指令完全同构。**DELETE(duplicate logical result store)**，抽为该文件私有 helper，保持 0/1、类型、span、volatile=false 和分支顺序不变。 |
| `GENL_COND_*` 与 `CORE_ASSIGN_STAGE` | 部分失败和 Linux 定点 trace 是无开关 stderr，现列入 MC-020 的诊断债。尚未证明下游脚本/用户诊断不消费，**NEEDS_PROOF(diagnostic consumers)**；本次不删。 |

**C15 生产代码：** `core_lower_expr_flow.c` **+52/-63，净 -11 行**；
C1～C15 纯清理累计 **净 -251 行**。三项 MC Bug 修复、专项测试
和本文档变更不纳入纯清理删行；联合验收仍按约定后移。

**验证 PASS：** 合并后 Release `-Werror` 完整 minic-cc 构建；
`check-semantic-snapshot`、`check-packed-zero-width-bitfield-barrier`
（9 组 GNU layout/五 ISA `-S`/x86 native）、`check-layout`、
`check-record`、真实 MC-009 12 参数 CALL 的五 ISA `-S` + x86
GNU/MiniC native exit7；MC-051 `long double` 取负跨 TU 的 x86
GNU/MiniC 原生 `failure_mask=0x0`；Core CALL dump exact-text、runtime IF facts
和 GNU IF condition-local owner 的 x86 GNU/MiniC 原生差分及 RV64
静态测试。旧 `core_short_circuit_or` 用例追加 x86 GNU/MiniC 原生
stdout 完全一致 `7 0 3 1`，五 ISA `-S` 全过。AND 用例在 RV32/RV64
`-S` 通过，A64/Loong 的旧 Core 限制及 x86 源内 RV fence 不作为
新失败归因；原两个 RV64 native/QEMU shell 脚本因交叉 libc 缺
`bits/libc-header-start.h` 未执行完成，**不计 PASS**。五目标完整
ELF/QEMU、MiniAS/MiniLD 和联合验收均未进行。

## 批次 C16：Core 表达式总分发、标量比较与跨块值快照

**基线：** C15 远端提交 `6d21e85`，2026-11-01。本批全文深读
Core **3** 个生产文件、原始 **3,693 行**：
`core_lower_expression.c`（1,360）、`core_lower_expr_scalar.c`（1,360）、
`core_lower_scalar_values.c`（973）。累计 **106/339** 全文深读：
Frontend **69/69**、Compiler **11/11**、Core **26/48**、Target
**0/211**。Core 余 **22** 个、Target **211** 个，合计 **233/339**
生产文件待全文深读。

| 消费链/重复外观 | 本批清理决定 |
| --- | --- |
| `lower_expression` 分发次序 | Bit-field read、record member、fixed register、vector/普通 lvalue、`discard`、statement/conditional/sequenced、Call、更新类 Unary、arithmetic 是有重叠前提的有序语义 owner。`ADDRESS_OF(function)` 的函数设计符指针和 `ADDRESS_OF(local)` 的存储地址不能共用路径；丢弃非 volatile lvalue 不需要读值，但仍须计算带副作用的地址。**KEEP(admission + evaluation order)**。 |
| `minic_core_lower_internal_lower_expression_statement` | Direct Record-return CALL 已在上方的 `record_materialized_address` 分支 **无条件返回**（包括失败状态）；下方普通 CALL 分支内再次检测完全相同的 direct Record-return 谓词，其 `minic_core_lower_direct_record_call_object` 调用永远不可达。**DELETE(unreachable duplicate handler)**，一并去掉该分支内多余作用域，不改变任何真正执行的 Call。 |
| `core_lower_expr_scalar.c` | popcount 由目标宽度 32/64 的 SWAR 常数运算实现，`ffsll` 必须按 64 位 unsigned 计算；bswap 在做多次移位前只求值一次。整数、指针、binary64 与 binary128 关系比较各自的转换/NaN/交换规则不同，尤其 `long double` 比较的 helper 返回条件不能与整数 `LESS` 机械统一。**KEEP(typed comparison owners)**。 |
| `core_lower_scalar_values.c` | `core_scalar_rhs_stays_in_block` 是**不产生新 Core basic block**的保守结构证明，不是纯度检查；证明为真时可免左值 spill，但仍在 RHS lowering 后复核 block ID。未知 AST、过深递归及 short circuit 均 fail-closed；浮点二元、指针运算、Runtime helper 需保留先计算 LHS → spill → RHS → reload 的 Core SSA 语义。**KEEP(block-local proof + snapshot)**。 |
| 隐含结构耦合 | `core_scalar_rhs_stays_in_block` 依赖 `MINIC_UNARY_BITWISE_NOT` 运算符排序认定纯 Unary，呼应 C14 的 `core_lower_local_effects` ordinal 线索。**NEEDS_PROOF(enum ordinal coupling)**；枚举排序改变前需补不变量/探针，但当前不擅自替换为假设相同的判断。`MINIC_OPT_PURE_DISCARDED_ADDRESS_EQ` 仍是显式 opt-in，不视为可无条件开启的死开关。 |

**C16 生产源码：** `core_lower_expression.c` **+32/-42，净 -10 行**；
C1～C16 纯清理累计 **净 -261 行**。没有改表达式静态可接受集合、
Call/Record ABI、Core IR enum、Target 规则或 Linux 诊断。独立 MC
修复不计入本次删行。

**验证 PASS：** Release `-Werror` minic-cc、minic 完整链接；
`run-small-record-copy-return.sh`（真实 source → Core → RV64 `-S`）；
`run-record-conditional-materialization.sh`（Record 条件值、直接 Record
CALL 实参一次求值、x86 GNU AS/LD 原生和 RV64 GAS 静态检查）。
未运行全五 ISA ELF/QEMU、MiniAS/MiniLD 联合验收；新旧 Core
表达式分支的动态等价性只以现有定点回归和静态不可达证明为界。

## 批次 C17：Core Call 实参快照、Record 物化与算术/复合赋值

**基线：** C16 和独立 MC-038/044 问题记录远端同树 HEAD `4478f67`，
2026-11-01。本批全文深读 Core **3** 个生产文件、原始
**3,093 行**：`core_lower_calls.c`（1,205）、
`core_lower_record.c`（975）、`core_lower_arithmetic.c`（913）。
累计 **109/339** 全文深读：Frontend **69/69**、Compiler
**11/11**、Core **29/48**、Target **0/211**；Core 余 **19**、
Target 余 **211**，总计 **230/339** 待全文深读。

| 路径与形似代码 | 清理/重构边界判断 |
| --- | --- |
| `core_lower_calls.c` | Direct/indirect Call 的签名资格、参值转换、Record by-value 私有对象、scalar spill/reload 和间接 callee 的求值时序不是一项 ABI 泛化规则。透明 Union 由 Frontend/Sema 判兼容、Core 只把选中指针成员物化为声明类型的 OBJECT，Target 独自决定寄存器/栈传递。**KEEP(source legality vs ABI placement + snapshot order)**。 |
| 透明 Union 成员选择 | `field` 初始化 NULL，只在 `assignment_compatible` 的命中分支写入；本地 `found` 布尔值与 `field != NULL` 完全等价，`field_index` 的循环外赋零又立刻由 `for` 初始化覆盖。**DELETE(redundant duplicate state)**，未改变成员遍历、拒绝条件或物化时序。 |
| `core_lower_record.c` | Record 的 lvalue 地址、rvalue materialization、conditional 两臂私有快照、MEMBER 字段投影及 copy/load 受不同源语法约束。Record-call 作为值生产者时必须先固定返回对象，再计算目标地址；`?:` 两臂不可同时执行，更不可从一个臂错误继承局部 fact。**KEEP(record provenance + CFG fork/meet)**，此处不抽出共享 ABI 层。 |
| `core_lower_arithmetic.c` | 整数 promotion、两种 shift promotion、指针之外的 floating helper 和 `float/double/long double` 运算有不同 IR 类型/精度契约；复合赋值先存左侧地址与旧值，求 RHS 后 reload 再 store，volatile 只体现在 load/store。**KEEP(typed arithmetic + LHS snapshot)**。 |
| 三个 arithmetic probe 尾部 | 函数入口均已把 `*handled = false`，所有将其置 true 的分支都立即 return；末尾重复清 false 不可能改变结果。**DELETE(three dead tail assignments)**，probe 顺序与失败状态原样保留。 |
| 其他可疑点 | 用 GNU `__addtf3` 等名称承载语言 `long double`、透明 Union 与 ABI fixed-call qualifier 强关联，是**NEEDS_PROOF(source/ABI boundary)**；本批只记录、不把辅助函数搬到 Target。MC-089 的 x87 有符号 helper 错编单独由 Bug 修复提交处理，不计入纯清理。 |

**C17 生产源码：** `core_lower_calls.c` **+1/-5（净 -4）**，
`core_lower_arithmetic.c` **+0/-3（净 -3）**；合计
**+1/-8，净 -7 行**。C1～C17 纯清理累计 **净 -268 行**。
仅记录 `core_lower_record.c` 的真实所有权合同，未更改实现。

**验证 PASS：** Release `-Werror` 重新编译 Core Call、arithmetic
及 x86 helper 并链接 minic-cc/minic；透明 Union 的正反例、x86 GNU
SysV 单 INTEGER chunk 的跨编译器双向原生互操作（包含 fixed/variadic
Record 与 transparent union）GNU AS/LD；Record conditional 物化
及原生 effects；小 Record copy/return 和 MC-089 比较 helper
六运算符/NaN/正负零的 GNU/MiniC x86 原生定点回归，MC-089
另通过四目标 `-S`。五目标完整 ELF/QEMU、MiniAS/MiniLD
联合验收仍按用户决定推迟。

## 批次 C18：Core IF/Loop/Switch 控制流、重入和不可达终结符

**基线：** C17、MC-041/089/052 已推远端并完成同树核对的
`9c3e6b8`，2026-11-01。本批全文深读 Core **3** 个生产文件，
原始 **2,666 行**：`core_lower_if.c`（788）、
`core_lower_loop.c`（523）、`core_lower_switch.c`（1,355）。
累计 **112/339**：Frontend **69/69**、Compiler **11/11**、
Core **32/48**、Target **0/211**；Core 余 **16**、Target
**211**，合计 **227/339** 待全文深读。

| 来源 CFG / 事实边界 | 审读决定 |
| --- | --- |
| `core_lower_if.c` | 谓词先后求值与两臂**推测式先构造**不是源语言提前执行；进入 condition block 时必须恢复 incoming 或 effect-filtered branch-entry facts，两个 exit 再 meet，不能继承一臂结束状态。跳过常量假/真分支前还须检查 C label、case/default、asm-goto、label address 的外部重入闭包。**KEEP(condition dominance + reentry)**。 |
| `core_lower_if.c` 的诊断轨迹 | `MINIC_DMA_IF_TRACE`、`MINIC_SHMEM_IF_TRACE`、固定函数名 `GENL_IF_PATH` 以及无开关 `CORE_FAST_TRACE` 并存，且首项 DMA 的 `\\n` 文本与 SHMEM 真换行不同；看似重复但下游日志消费者和原始调试语义未证明可删。**NEEDS_PROOF(trace/diagnostics consumers)**，与 MC-020 关联，不做偷偷移除。 |
| `core_lower_loop.c` | Parser 把 GNU `for` / `do while` 归一为 `WHILE`，`continue` 可能必须先执行 update block，零次/常真回边的 exit 是否可达由**真实 Core 前驱**证明而非语法关键字。局部事实的 loop-body 修改掩码 OOM 时保守 invalidate；对无副作用 one-pass `do while(0)` 单独扁平化。**KEEP(parser provenance + backedge facts)**。 |
| `core_lower_switch.c` | GNU `case lo ... hi` 是严格按 selector 整数类型比较；内嵌 case 的所有权属于最近的 switch，普通标签的 function-level goto 可越过 case dispatch 进入 dead-prefix 或 `break` 后尾部。常量 selector 的 prefix/suffix pruning 需要上述全子树重入的反证，case fallthrough 的事实 meet 不能借 selector 入口事实替代。**KEEP(nested labels + reentry/fallthrough)**。 |
| 三处 `UNREACHABLE` | Nested switch 的无前驱 exit、主 switch 的被常量证明死掉的 case segment、普通 switch 的无前驱/所有路径终止 exit，都清零构造相同的 `MINIC_CORE_TERMINATOR_UNREACHABLE`，span 和 block ID 为唯一参数。**DELETE(three duplicate terminator builds)**，复用单个本文件私有构造函数，原返回错误与落点不变。 |
| `MINIC_CORE_SWITCH_LABEL_LIMIT=512` | 固定 nested/flat label 缓冲和递归 scan 上限仍可能拒绝合法超大 C，但本批没有小步等价替代且会触及临时 arena/内存上限设计。**NEEDS_PROOF(capacity policy)**，不在纯清理中擅改。 |

**C18 生产源码：** `core_lower_switch.c` **+20/-27，净 -7 行**，
C1～C18 纯清理累计 **净 -275 行**。无 IF 或 Loop 源行为改动；
独立 MC-048 RV32 指针扩展修复和本批源码减行分别记账。

**验证 PASS：** Release `-Werror` 重编 Core Switch 并链接
minic/minic-cc；`run-core-switch-case-fact-isolation.sh` 的 GNU/MiniC
x86 原生对照；`run-core-constant-switch-dead-cases.sh` 五目标 `-S`
与外部 goto/死分支覆盖；`probe-gnu-linux-zoned-constant-switch.sh`
的 GNU AS/nm 对照；`run-gnu-switch-case-range.sh`；
`probe-gnu-nested-switch-record-return-5isa.sh` 五 ISA `-S`、
x86 GNU/MiniC 原生及负例；`run-for-loops.sh`、
`run-gnu-runtime-if-effect-facts.sh`、`run-core-reentry-query-contract.sh`。
另独立 MC-048 在合入后通过五 ISA `-S` 与 GNU/MiniC 双 ELF32
`qemu-riscv32` native exit7。**不计 PASS：** 老
`run-switch-control-flow.sh` 首个断言仍要求历史
`.Lswitch_case_`（现 Core label namespace），实际 `-S` 成功后在
文本 `grep` 退出1；`run-core-switch-m27.sh` 因缺 RV64 `crt1.o/-lc`
无法链接。需要单独改造旧 oracle 或补 cross libc 才能复验。
完整联合验收仍推迟。

## 后续批次进度规则

每批发布：当前 HEAD、源文件逐目录覆盖（明确深读与只扫的区别）、
候选数与 `KEEP/NEEDS_PROOF/DELETED` 判定、删改行数、验证矩阵、未解决的耦合，
以及 Git commit。一个批次累计多项再提交；不为单个 helper 刷大量小提交。

遇到真实错编、合法程序拒编或跨 ISA 约束冲突，进入唯一 MC 台账，
由独立 Bug 修复批次处理；清理账只记录依赖，不把 Bug 修复计入 C1 净减行数。
