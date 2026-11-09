# MiniC Core CFG / 可达性 / 局部事实 / 源效果跨文件审计（第三轮之后）

本文件只对 `src/core/` 通用 lowering 分区建模；GNU ASM 解析/规范化、Core IR 结构与 verifier 分别由其他并行工作区负责。`reentry_index`、`source_loop_shapes` 与 C0 AST 都是 target-independent，任何下面的整改不得引入五种目标的枚举。源码行号以 2026-10-29 共享树为准。

## 一、现存多个权威源的完整调用关系

| 边界 / 入口 | 输入来源 | 决策函数 | 后续消费者 | 隐患及必须保留的不变量 |
| --- | --- | --- | --- | --- |
| 外部标签进入的全程序图 | C0 `GOTO`、`asm goto`、`&&label` | `reentry_index.c:48-159` 构建一次 incoming linked edges | `core_lower_if.c:99-124`、`core_lower_switch.c:223-280`、`core_lower.c:1148-1230` | 索引不完整时不可证明**没有**外来边。下层 fallback 在各入口用不同扫描细节；必须明确 `ready(index)` 与无索引的 fail-closed 策略。 |
| 丢弃恒值 IF 的另一侧 | AST 控制流/cleanup 上下文 | `core_lower_if.c` 的 `core_block_contains_reentry_label` + `core_zero_bool_cfg_condition` + const eval + source effect | `core_lower_if.c:519-644` 的源码单臂下降 | 只有表达式无可观察副作用、丢弃子树无外部重入，才能删除。value-known 与 pure/effect-free **不是同一证明**。 |
| while/for/do 源形式规范化 | parser 生成 label、update、scope、cleanup | `source_loop_shapes.c:1-321` | `core_lower_loop.c:77-370`、`core_lower.c:1272-1335` | “synthetic continue label”身份需唯一，普通用户 label 和 goto/asm goto 不得被当作 parser 噪声删除；`reentry_index` 提供引用信息。 |
| switch case dispatch/尾段 reentry | case/default/nested switch/direct goto/asm goto/computed goto | `core_lower_switch.c:223-280,626-675,1040-1300` | `core_lower.c` 普通 LABEL 和嵌套 CASE lowering；local facts merge | dispatch、前一 case 真正 fallthrough、函数标签 reentry 是**不同 CFG 前驱**，必须 meet 而不是按 AST 顺序抄事实。 |
| 普通 straight-line local facts | `MinicCoreLocalIntegerConstant` `(known,value,escaped,specialization,program_zero)` | `local_facts.c` snapshot/restore/clear/meet；`core_lower_const_facts.c` 的 local constant set/eval | `core_lower_if`、`core_lower_expr_flow`、`core_lower_switch`、`core_lower_loop` | `known` 是局部条件事实，不可跨 goto/case、条件的源副作用、循环回边直接复用；`program_zero_stable` 是更强的整个函数 invariant，要有独立证明规则。 |
| 整个函数恒零 `_Bool`（两套） | local 的 entry direct zero initializer、所有语句/表达式写、地址逃逸、asm output | `core_lower_bool.c:15-198` 的严格 CFG-proof；`core_lower_const_facts.c:86-355` 的 program-zero stable proof | IF 直接恒值分支；`local_facts.c` 的 clear-known 重新建立恒零值 | **规则不同，不可机械合并**：program-zero 额外能排除有证明的 `return` 后死写，严格 proof 不排除。两者调用入口/失败语义需单独测试；相同 source-writes 分类器可共享。 |
| 某个局部的 may-write / may-escape | 复杂左值地址表达式、`++`、assign、condition、GNU statement-expression tail、call args、inline asm | `core_lower_local_effects.c:1-266` recursive source walker | `core_lower_if.c:187-224` 每 local 条件复核；`core_lower_loop.c:398-454` 每 local 的 invalidate 和 restore | 现总工作量近似 `O(local_count×AST)`，且循环会对同一 subtree 做两遍；应只有**一个保守 source-effects collector**，批量填 local mask 并复用。未知/深度限制要写 all-modified，而不能当没有修改。 |
| 初始 vs 最终函数可达性 | parser `is_referenced`，Core 已下降的 live call/address，global relocation roots | `src/compiler/core_function_set.c`→`core_reachability.c`→`compiler.c` | 七路 Linux probe 中 `core_lower_traces` 与 post-prune `core_frontiers` | 初次 unsupported 的 static helper 可被最终 prune；trace 应保留 pre-prune provenance，不能直接作为 live error。动态最小五 ISA 正反例见 `run-core-if-snapshot-and-prune.sh`。 |

## 二、实施顺序与验证阈值

1. **P1 Core effects 批量化与证明语义**：先把 `core_lower_local_effects.c` 的唯一分类语义抽成可填全 local mask 的 collector，scalar query 也用同一 collector；再使 IF/LOOP 消费一次性摘要。否定样例必须有 `a[index++]` 左值地址、GNU detached stmt-expression tail、nested if/switch、goto、asm output/unknown、64+ 深度保守性；原来全局 effect/branch/switch tests 不应回退。
2. **P1 zero-bool 证明级别**：分别固定 strict zero-only 与 dead-after-return tolerant 两级 oracle 的正/反例，不能因为代码相似就抹除其中一种。若共享入门 initializer 或修改域，要保留特殊 `reentry` 守卫。
3. **P1 CFG entry 唯一权威**：在 stable effect/domain 后再讨论共享 source CFG graph；禁止仅以 `statement->kind`、source text、标签字面名推出所有前驱。索引缺失/损坏路径必须 fail closed。
4. **P2 调试与统计来源**：`core_lower_if` / `local_facts` / `core_lower_const_facts` 还有 Linux 函数名定制 trace；切换统一 TraceSink 前，核实精确 CI 使用者，保留活函数 lowering 失败诊断。

## 三、本轮 P1 统一 source-effect collector 批量实施结果

**已整改（Core 局部、target-independent）：**

1. `src/core/core_lower_local_effects.c` 的标量递归扫描改为 **唯一的 AST kind 分类器** `CoreSourceLocalEffectScan`：一次遍历可设置整个源函数的 `unsigned char modified[local_count]`。旧 `core_expression_may_modify_local` / `core_block_may_modify_local` API 仍可用，但由同一个 collector 在 1-local mask 上计算，不再保留另一套分类判断。
2. `src/core/core_lower_internal.h` 新增 append-only `core_expression_collect_modified_locals` 和 `core_block_collect_modified_locals`，明确未知类型、缺失 AST 节点、cleanup 不匹配、ASM 语句、深度超过64等均将**全部 local 标记 may-modify**；标记零代表正向证明的 no-effect，不代表 scanner 没扫描到。
3. `src/core/core_lower_if.c` 对 condition 只扫描一次并为每 local 判断位图；以前最热的 `for each local -> recursive expression scan` 已移除。保留「local 源声明在 condition 之后」的作用域门禁；位图 calloc 失败时仅保守丢弃事实，不会虚构 no-effect。
4. `src/core/core_lower_loop.c` 只扫描一次 condition + body，将同一个 summary 同时用于 entry specialization invalidation、entry/exit invariant restore；不再重走两遍整个 loop subtree。在 calloc 失败时全局事实降级为未知；`normalized do-while(0)` single pass 沿用原策略。
5. `tests/core/core_local_effect_summary_test.c` 白盒新增 `modified[3]` 全 local mask、同 scalar oracle 一致性、ASM mark-all、深度溢出 mark-all。保留原 `a[index++]` 左值地址和 GNU detached stmt-expression tail 检查。`tests/compiler/c0/run-core-local-effect-summary.sh` 仍执行五 ISA `-S`、GNU vs MiniC native 动态差分。

**本批统计：** 5 个已跟踪生产/测试文件（`core_lower_local_effects.c`、`core_lower_internal.h`、`core_lower_if.c`、`core_lower_loop.c`、`core_local_effect_summary_test.c`）合计 `+333 / -208` 行，约 541 行修改；此统计独立于其他代理共享工作区的变更和本文件新增说明。

**动态验证（本批新编译器）：** 全工具链 `-Werror` 编译 PASS；`run-core-local-effect-summary.sh`（AST 单测、五 ISA `-S`、GNU/MiniC x86 native）PASS；`probe-core-expr-branch-local-facts.sh` **25/25** + 五 ISA PASS；`run-core-switch-case-fact-isolation.sh` **x86 native 9/9** PASS；`run-core-cfg-fold-effectful-conditions.sh` 两条 PASS；`run-core-if-snapshot-and-prune.sh` pre/post prune 五 ISA + x86 native PASS；`run-for-loops.sh` 与 `run-unbounded-for-break.sh` PASS。**未证明**五 ISA QEMU runtime 或七路 Linux 全量逐文件无回归。

**最终严格门禁：** 在专属 `BUILD_DIR=build/core-effect-batch/build` 先运行 `make -j8 CFLAGS=-Werror all` **EXIT0**，再运行 `make -j8 CFLAGS=-Werror check-fast` **EXIT0**。第一次试跑 `check-fast` 因该目录此前只构建 `minic-cc`、缺 `minic-as` 而在浮点辅助脚本出现 Error127；并非测试不通过，已通过完整 `all` 补齐工具并重新全量运行至 EXIT0。工具缺失已解决。个别 RV64 GNU/QEMU 专项因当前执行环境未安装而显示 SKIP，未将其当作通过。

**仍待 P1：** 两级 zero-bool proof（strict vs dead-tail-aware）暂不合并，维护不同 proof strength 但建议共享 lexical entry-zero/assignment shape；reentry index 与 fallback 仍有三个入口（IF、SWITCH、generic LABEL/LOOP），需下一轮用 external goto/asm-goto/label-address 动态对照验证后，才能做结构性收敛；循环已不再重复扫描源 AST，却仍会为每次循环 query 分配一份局部位图，未来可测量缓存带来的收益，不能提前宣称性能倍数。

验收仅声明已实测的层级。`-S` 五 ISA PASS 只是机器汇编生成，不是五 ISA 实机/QEMU 运行；x86 native GNU/MiniC 差分单独记录。`-Werror` 和既有 guard-page bitfield / switch / local facts 回归是 Core 修改的必要最小保护，不替代 exact-head 七路 Linux 大矩阵。
