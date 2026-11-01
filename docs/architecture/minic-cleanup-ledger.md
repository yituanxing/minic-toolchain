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

## 后续批次进度规则

每批发布：当前 HEAD、源文件逐目录覆盖（明确深读与只扫的区别）、
候选数与 `KEEP/NEEDS_PROOF/DELETED` 判定、删改行数、验证矩阵、未解决的耦合，
以及 Git commit。一个批次累计多项再提交；不为单个 helper 刷大量小提交。

遇到真实错编、合法程序拒编或跨 ISA 约束冲突，进入唯一 MC 台账，
由独立 Bug 修复批次处理；清理账只记录依赖，不把 Bug 修复计入 C1 净减行数。
