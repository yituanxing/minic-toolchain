# MiniC 缺陷、风险与架构债台账

> **长期维护文档，不是立即修复清单。** 发现问题先记录、核实、分类；是否进入某一批重构另行决定。
>
> 首次建档：2026-10-30；第五轮审计起点：`b0870345b`（`refactor/minic-target-language-boundary`）；之前第四轮基线为 `400f4a611`。
> 范围：MiniC 的 Frontend、Compiler、Core、Target、Driver 和验证流程；不包含 MiniAS、MiniPP、MiniLD、NaSeed。

配套阅读：[全局架构与所有权图谱](minic-global-contract-map.md) · [函数契约审计台账](minic-function-contract-register.md)。

## 使用约定

- **编号永久不复用。** 新发现分配新的 `MC-xxx`；同一个根因涉及多个可独立触发的位置，可以分别登记，并注明共同根因。
- **状态与优先级独立。** `OPEN`（确认待处理）、`INVESTIGATING`（证据不完整）、`DEBT`（已证实的维护债，未必有功能错误）、`FIXED-UNVERIFIED`、`VERIFIED`、`CLOSED-NOT-A-BUG`。`P0` 表示影响可能严重，不代表已经动态复现。
- **证据等级独立。** `ASAN`＝运行时内存错误堆栈；`ASAN-POINTER-PAIR`＝开启 `pointer-subtract` 专项插桩后确认跨对象指针算术；`GCC-DIFF`＝与有效 GCC 输入/生成物对照；`GNU-NATIVE-DIFF`＝GNU C 本机执行与 MiniC x86 `-S`→GNU AS/link→本机执行差分；`LOCAL-COMPILE-DIFF`＝GNU C 合法性 + 当前 MiniC `-S` 成败对照（不等于同目标 GCC/GAS）；`ELF-ORACLE`＝真实目标文件差分；`ASM-ONLY`＝生成汇编及地址/指令关系；`SOURCE-TRACE`＝分配、所有权、调用链证明；`CI-LOG`＝实际工作流/脚本结果；`DESIGN-REVIEW`＝设计与消费者核对；`SUSPICION`＝尚缺反例。
- **不以代码行数、长函数、两个数组或两个实现的表象判 bug。** 查清生产者、身份不变式、持有者、消费者、ABI/能力边界，再决定修法。
- **不因修补代码即关闭。** 修复后填写提交、精确 HEAD 回归结果及适用架构；只有证据达到验收条件才能改成 `VERIFIED`。未验证前保持 `FIXED-UNVERIFIED`。
- 审查时先查本台账，避免把已经排除的误判再次作为重构理由。复现材料目前有部分在临时工作区，**尚未纳入仓库的探针须在修复批次中转成永久回归测试**。

## 索引：确认的缺陷与缺口

| ID | 优先级 | 状态 | 证据 | 归属 | 简述 |
| --- | --- | --- | --- | --- | --- |
| [MC-001](#mc-001-符号闭包读取失效的-expression-指针) | P0 | OPEN | ASAN | Compiler | 符号闭包递归后读取扩容前的 Expression 指针 |
| [MC-002](#mc-002-record-字段声明中的悬空指针) | P0 | OPEN | ASAN | Frontend | Record 字段声明递归后引用旧 Record 地址 |
| [MC-003](#mc-003-运行时-record-初始化器中的悬空指针) | P0 | OPEN | ASAN | Frontend | 运行时记录初始化器跨字段复用旧 Record 地址 |
| [MC-004](#mc-004-属性探测使-parser-tags-失效) | P0 | OPEN | ASAN | Frontend | `probe=*parser` 的 tags 容器所有权错误 |
| [MC-005](#mc-005-属性探测使-parser-enum_constants-失效) | P0 | OPEN | ASAN | Frontend | 同一浅拷贝使 enum_constants 容器失效 |
| [MC-006](#mc-006-传递特化污染共享-call) | P0 | OPEN | ELF-ORACLE | Compiler/Core | 调用实例的 callee 被写进共享 AST CALL |
| [MC-007](#mc-007-整数与符号特化无法组合) | P1 | OPEN | GCC-DIFF | Compiler/Core | 特化阶段间丢失调用实例的整数事实 |
| [MC-008](#mc-008-符号特化过度要求整数参数常量化) | P1 | OPEN | GCC-DIFF | Compiler | 符号立即量与运行时整数无法共存 |
| [MC-009](#mc-009-表达式探测回滚遗漏-owned-call-参数) | P1 | OPEN | SOURCE-TRACE | Frontend | `expression_count` 回滚遗失 CALL 溢出参数 |
| [MC-010](#mc-010-loongarch-特殊-asm-写回绕过对齐保护) | P1 | OPEN | ASM-ONLY | LoongArch64 | 13 个特殊结果写回槽未走安全 store helper |
| [MC-011](#mc-011-source-only-的-qemu-结果报告不实) | P1 | OPEN | CI-LOG | CI | Source-only 仍报告 `GNU_QEMU=2` |
| [MC-012](#mc-012-source-only-的-elf-结果报告不实) | P1 | OPEN | CI-LOG | CI | `.s` 字符串检查被表述为 ELF reloc 验证 |
| [MC-013](#mc-013-native-gate-的-测试触发白名单缺口) | P2 | OPEN | SOURCE-TRACE | CI | 修改 review-batch 编排未必触发 native gate |
| [MC-014](#mc-014-rv32-变参与过对齐栈帧交界) | P0 | OPEN | ASM-ONLY | RV32 | 合法 aligned(64) 变参函数：保存区与 caller stack 不连续；零剩余 GPR 的首址也错位 |
| [MC-015](#mc-015-rv64-变参与过对齐栈帧交界) | P0 | OPEN | ASM-ONLY | RV64 | 合法 aligned(64) 变参函数：保存区跨向 caller stack 的物理地址错位 |
| [MC-028](#mc-028-rv64-的-int128-移位计数类型限制) | P2 | OPEN | LOCAL-COMPILE-DIFF | RV64 | 合法 GNU C 的宽移位计数导致 MiniC `-S` 拒绝，窄计数可过 |
| [MC-033](#mc-033-core-独立自增自减语句保留过时常量事实) | P0 | OPEN | GNU-NATIVE-DIFF | Core | `x++`/`x--` 语句捷径漏失效化 local fact，实测错误分支 |
| [MC-034](#mc-034-静态-record-设计符查找对无关对象做指针相减) | P1 | OPEN | ASAN-POINTER-PAIR | Frontend | 合法静态 `.x` initializer 触发栈 Record 与 heap records 无关指针相减 |
| [MC-035](#mc-035-运行时范围初始化器借用-expression-跨扩容) | P0 | OPEN | ASAN | Frontend | 合法 GNU range initializer 在 Program Expression 扩容时真实 UAF |
| [MC-036](#mc-036-record-条件表达式的-local-facts-分支合流缺口) | P1 | OPEN | GNU-NATIVE-DIFF | Core | 结构体条件式缺 fact fork/meet，实测 `f(1)` 返回值错 10 |
| [MC-037](#mc-037-loong-特殊-asm-源端与机器端限定符不一致) | P2 | OPEN | LOCAL-COMPILE-DIFF | LoongArch64 | 合法 volatile cpucfg 输出仅因限定符在机器层拒绝 |
| [MC-038](#mc-038-匿名字段设计符后的位置初始化游标错误) | P1 | OPEN | LOCAL-COMPILE-DIFF | Frontend | 匿名字段 `.b=2,3` GCC 接受、MiniC 错报过多位置初始化 |
| [MC-039](#mc-039-运行时嵌套数组递归跨-array-type-扩容借用) | P0 | OPEN | ASAN | Frontend | 合法嵌套数组 `sizeof` 扩 ArrayType 时读失效借用指针 |
| [MC-041](#mc-041-long-double-字面量提前按-binary64-舍入) | P0 | OPEN | GNU-NATIVE-DIFF | Frontend/Target | `1.0000000000000001L` 提前按 double 舍入；x86 执行得到错误比较结果 |
| [MC-042](#mc-042-packed-零宽位域丢失对齐栅栏) | P1 | OPEN | GNU-NATIVE-DIFF | DataLayout | packed Record 的 `int:0` 不再强制下一个字段对齐，`offsetof`/`sizeof` 错误 |
| [MC-043](#mc-043-inline-整数特化预算耗尽直接拒绝合法-c) | P1 | OPEN | LOCAL-COMPILE-DIFF | Compiler | 2048 个 clone 预算由优化上限变成对合法 C 的硬编译失败 |
| [MC-044](#mc-044-函数参数列表中的-record-enum-tag-作用域外泄) | P1 | OPEN | LOCAL-COMPILE-DIFF | Frontend | 原型、定义、函数指针参数里的 tag 泄漏到文件作用域 |
| [MC-045](#mc-045-loong-futex-cmpxchg-覆盖-gnu-可读写输出初始值) | P1 | OPEN | ASM-ONLY | LoongArch64 | GNU `+r` 输入原值未被消费，机器特化无条件写回 0 |
| [MC-048](#mc-048-rv32-指针到-64-位整数的合法转换被拒绝) | P2 | OPEN | LOCAL-COMPILE-DIFF | RV32 | Core 合法 `SCALAR_BITCAST` pointer→u64 在 RV32 目标发射器被拒绝 |
| [MC-023](#mc-023-静态聚合后向覆写跨递归借用-record) | P0 | OPEN | ASAN | Frontend | 原调查项升级：静态 Record 后向覆盖跨 Record arena 扩容 UAF |
| [MC-049](#mc-049-静态链式数组设计符跨-arraytype-扩容借用) | P0 | OPEN | ASAN | Frontend | 静态数组链式设计符解析 `sizeof(int[3])` 后借用 ArrayType 悬空 |
| [MC-050](#mc-050-x86-六组-asm-限定符源端与机器端不一致) | P2 | OPEN | LOCAL-COMPILE-DIFF | x86-64 | 六种合法 GNU ASM 的 volatile 结果被机器端不一致准入拒绝 |
| [MC-051](#mc-051-long-double-一元取负丢失有符号零) | P1 | OPEN | GNU-NATIVE-DIFF | Core | `-0L` 经 `0L - x` 实现，`-(+0L)` 错误变为正零 |
| [MC-052](#mc-052-aarch64-uaccess-窄输出按八字节写回) | P1 | OPEN | ASM-ONLY | AArch64 | LDTRB/H/W 将1/2/4B输出统一按8B `str x` 写回，越界风险 |
| [MC-053](#mc-053-rv64i-lp64-软浮点只覆盖四则) | P2 | OPEN | LOCAL-COMPILE-DIFF | RV64 | LP64 已实现软四则，但比较、取负、转换的 10 个合法函数均无法 `-S` |
| [MC-056](#mc-056-运行时-record-顺序初始化器跨扩容借用) | P0 | OPEN | ASAN | Frontend | 普通 Record 位置初始化 `sizeof(struct {...})` 扩容后使用失效 Record* |
| [MC-057](#mc-057-gnu-空-record-返回及实参-abi-缺口) | P2 | OPEN | LOCAL-COMPILE-DIFF | RV32/RV64/x86 | 零字节 Record ABI 合法；目标 terminator/call 路径显式拒绝不同子集 |
| [MC-058](#mc-058-符号指针加常量偏移的传递特化丢失) | P1 | OPEN | LOCAL-COMPILE-DIFF | Compiler | 唯一 caller 的 static pointer+offset 经 inline wrapper 后无法解析 GNU ASM `i` |
| [MC-059](#mc-059-rv64-lp64d-含浮点成员-union-被误当-fp-aggregate) | P2 | OPEN | LOCAL-COMPILE-DIFF | RV64 ABI | LP64D union 中浮点字段不能触发 FP flatten；具名参数/返回四种 GNU 用例错拒 |
| [MC-060](#mc-060-lp64-两个目标的大聚合-2048-字节硬上限) | P2 | OPEN | LOCAL-COMPILE-DIFF | Common LP64/A64/Loong | 合法 2049B Record 的按值参数/返回在 A64/Loong preflight 错拒，2048B 通过 |
| [MC-061](#mc-061-record-字段声明中-pointer-to-array-跨扩容借用) | P0 | OPEN | ASAN | Frontend | 声明 Record 的 pointer-to-array 字段遇匿名 Record 时 UAF，无需 initializer |
| [MC-062](#mc-062-x86-alt-reloc-的-memory-输入源机约束分裂) | P2 | OPEN | LOCAL-COMPILE-DIFF | x86-64 ASM | Source 允许任意 GNU m lvalue，Machine 硬编码唯一符号，合法替代重定位遭拒 |
| [MC-063](#mc-063-带标签的裸-record-声明被误作匿名成员) | P1 | OPEN | GNU-NATIVE-DIFF | Frontend/DataLayout | GNU 的 `struct Named;` 仅声明不占成员，MiniC 错当匿名成员导致 `sizeof` 错误 |
| [MC-064](#mc-064-inline-asm-仅符号引用的静态函数被错误裁剪) | P1 | OPEN | ASM-ONLY | Core Reachability | `"i"` ASM 引用静态函数被降为文本后丢失函数可达性边，汇编有引用无定义 |
| [MC-066](#mc-066-aarch64-浮点指针字段被误判为浮点聚合) | P2 | OPEN | LOCAL-COMPILE-DIFF | AArch64 ABI | 32B 聚合的 `double*` 被当 FP 成员拒绝，换成 `int*` 即通过 |
| [MC-067](#mc-067-带-gnu-属性的-record-前向声明被错判为定义) | P2 | OPEN | LOCAL-COMPILE-DIFF | Frontend | `struct __attribute__((aligned(16))) Forward;` GCC 合法，MiniC 五目标要求 `{` |
| [MC-069](#mc-069-aarch64-linux-vdso-在-gnu-ld-拒绝动态重定位) | P1 | OPEN | GNU/MiniC-ELF-AB | AArch64 Linux vDSO ELF visibility | 4108 项 MiniC `-S`/GNU AS 通过；GNU 的 HIDDEN 未定义数据符号经 ADR_PREL，而 MiniC 错为 DEFAULT+GOT，GNU LD 拒绝动态重定位 |
| [MC-070](#mc-070-loongarch64-linux-heado-对-start_secondary-的-b26-越界) | P1 | OPEN | GNU/MiniC-ELF-AB | LoongArch64 Linux 链接/体积 | 2723 项 MiniC `-S`/GNU AS 通过，主 GNU LD B26 越界；同输入 C executable ELF 代码约 16.89MB(GNU)→371.97MB(MiniC)，膨胀 22.03× |
| [MC-072](#mc-072-static-inline-的-computed-goto-表指向不存在的-source-label) | P1 | OPEN | GCC-DIFF/ASM-ONLY | Compiler/Core/Target | 有效 GNU `static inline` 的静态 `&&label` 表引用 `.Luser_N`，整数特化克隆发射但原函数被裁剪；源标签别名没有任何定义 |

## 索引：尚待验证的风险

| ID | 优先级 | 状态 | 证据 | 位置/疑点 |
| --- | --- | --- | --- | --- |
| [MC-016](#mc-016-线性-va_list-类型准入过宽) | P2 | INVESTIGATING | SUSPICION | RV32/RV64/Loong 接受任意指针作 `va_list` |
| [MC-017](#mc-017-另外两个-parser-语义探测浅拷贝) | P1 | INVESTIGATING | SOURCE-TRACE | 数组 extent 探测的 Parser 副本 |
| [MC-018](#mc-018-aarch64-短分支距离依据基本块数) | P2 | INVESTIGATING | SUSPICION | `cbnz` 距离仅按 `block_count` 估计 |
| [MC-024](#mc-024-common-core-中的-rk-约束特例) | P2 | INVESTIGATING | DESIGN-REVIEW | Common Core GNU `rK` 方言和 Target policy 分工待证 |
| [MC-025](#mc-025-aarch64-公共全局-object-类型伪操作兼容性) | P2 | INVESTIGATING | SOURCE-TRACE | A64 object suffix 与 Common `.type` 固定格式需实测 GAS |
| [MC-027](#mc-027-targetinfo-惰性初始化的并发契约) | P3 | INVESTIGATING | DESIGN-REVIEW | 静态目标描述符惰性初始化有无多线程 API 义务 |
| [MC-032](#mc-032-ast-verifier-内层容器检查顺序) | P3 | INVESTIGATING | SOURCE-TRACE | 非法内部 Program 可能在嵌套形状验证前被索引构建读取 |
| [MC-047](#mc-047-core-事实求值与可消除副作用的证明边界) | P2 | INVESTIGATING | DESIGN-REVIEW | Core 的“值可知”事实在不同 CFG consumer 中需要明确纯度门槛 |
| [MC-055](#mc-055-core-verifier-内部辅助数组形状先决条件) | P2 | INVESTIGATING | CORE-API-ASAN | Verifier 对人为损坏的 fixed binding 形状崩溃，对公开 API 可构造的缺 enum metadata IR 误接受；未证有效 C 错码 |
| [MC-071](#mc-071-rv64-linux-完整-gnu-链接成功但-qemu-45-秒仅见-opensbi) | P1 | INVESTIGATING | QMP-FAULT+IMAGE-BYTES | 2064 项 MiniC/GNU AS、GNU LD 通过，Image 150491136B；QMP 证实 S 态 load access fault（`lbu`，非法地址 `0xffffffff88620a4c`），P0/P1 未通过 |
| [MC-073](#mc-073-core-block_address-目标缺少-reachable-root-闭包证明) | P2 | INVESTIGATING | SOURCE-TRACE/DESIGN-REVIEW | Core verifier 允许 `BLOCK_ADDRESS` 指向无 `source_label_id` 的合法块，但 Core reachable walker 不从地址引用加入目标；**现有 Frontend 不产生该形态，未复现生产故障** |
| [MC-074](#mc-074-推测-if-条件继承未来局部变量事实导致-x86-linux-拒编) | P0 | FIXED-UNVERIFIED | LOCAL-COMPILE-DIFF/ELF-ORACLE | Core IF 的条件事实快照继承了声明在该条件之后的局部对象事实；原 Linux 同 `.i` 五版本红绿及 first8 GNU AS 证实两行通用生命周期修正 |
| [MC-075](#mc-075-rv64-预mmu-从-got-取出未重定位的-chosen-链接虚址) | P0 | FIXED-UNVERIFIED | SAME-IMAGE/QMP/GOT-BYTES | RV64 2064/2064 与 GNU LD 已通过，Image 中 `/chosen` 的 GOT 表项仍是 `0xffffffff88620a4c`，QEMU satp=0 时 `strlen` 读它触发 load access fault；按 ELF 可抢占性分配 GOT/PCREL 的通用 Target 修复需完整重验 |

已验证非问题：**[MC-026](#mc-026-source-form-id-的实际身份边界)**、**[MC-065](#mc-065-x86-r11-跨调用快照误报排除)**，状态 `CLOSED-NOT-A-BUG`，保留编号和消费者证据，避免重复误报。

## 索引：确实存在但不宜立即改的架构与验证债

| ID | 优先级 | 状态 | 证据 | 内容 |
| --- | --- | --- | --- | --- |
| [MC-019](#mc-019-两套-bool-恒零证明扫描重叠) | P2 | DEBT | DESIGN-REVIEW | 两种保守策略共享了大量全程序扫描 |
| [MC-020](#mc-020-无条件的-genl-调试输出) | P2 | DEBT | SOURCE-TRACE | 六处 Linux 特定诊断无开关输出 |
| [MC-021](#mc-021-历史测试与脚本可达性) | P2 | DEBT | SOURCE-TRACE | 老负例、历史补丁和 inactive 流程需分类 |
| [MC-022](#mc-022-符号特化回归的-clone-身份断言不足) | P1 | DEBT | CI-LOG | 测试有真实语义覆盖，但日志断言不能证明 clone 身份 |
| [MC-029](#mc-029-core-if-的-local-facts-快照重复实现) | P3 | DEBT | SOURCE-TRACE | IF 专用快照与共享快照算法重复、消费者均可辨 |
| [MC-030](#mc-030-functionbody-名义只读遍历的隐式写入) | P2 | DEBT | SOURCE-TRACE | CALL 溢出参数只读遍历经浅拷贝发生同值写回 |
| [MC-031](#mc-031-parser-name-bound-无消费者的旧-api) | P3 | DEBT | SOURCE-TRACE | Parser 两个内部 helper 仅留定义/声明、无实际生产调用 |
| [MC-040](#mc-040-嵌套-switch-labels-重复全量扫描) | P3 | DEBT | SOURCE-TRACE | 同一 nested switch 先检测再构造，两次收集标签 |
| [MC-046](#mc-046-aarch64-两个特殊-asm-路径引用默认-target-layout) | P2 | DEBT | SOURCE-TRACE | A64 特例借用默认 RV64 DataLayout，目前字段相同但接口错误耦合 |
| [MC-054](#mc-054-inline-symbolic-恒假表达式残留) | P3 | DEBT | SOURCE-TRACE | Inline 静态表达式深度函数保留无消费者意义的 `&& false` 分支，周边 fallback 仍活跃 |
| [MC-068](#mc-068-always_inline-未实际展开且外部-clone-造成-init-段生命周期失配) | P1 | DEBT | CI-LOG/SOURCE-TRACE | `always_inline` 只解析未落实强制展开；独立 `.text` clone 引用 `.init.text`，GNU modpost 证实 section mismatch |

## 条目详情与验收门槛

### MC-001 符号闭包读取失效的 Expression 指针

- **位置：** `src/compiler/inline_symbolic.c:135–333`，第一次明确读取在 `:250`；扩容点 `src/frontend/ast.c:516`。
- **证据：** ASan `heap-use-after-free`，`READ of size 208`。递归闭包保存 `program->expressions[id]` 的地址，递归追加表达式引发 `realloc`，返回后仍执行 `closed = *expression`。单、双符号参数和多个 `PAD_COUNT` 扩容边界均可复现。
- **关联风险：** `inline_symbolic.c:748–825` 在实参循环外借用 `nested` 指针，同样可能跨表达式追加，但此位置尚未独立动态复现（内部 UAF 会先触发）。
- **修复门槛：** 使用稳定 `ExpressionId` 保存子节点身份，并在任何可能追加之后重新获取地址；双参数场景另补外层 `nested` 复测。避免递归层层拷贝 208B Expression，顾及 RV64 自举栈预算。

### MC-002 Record 字段声明中的悬空指针

- **位置：** `src/frontend/parser_record.c:385–443`，`record_has_field()` 在 `:443` 读旧地址；`src/frontend/ast.c:1136` 使 Record arena 扩容。
- **证据：** 带函数指针字段、嵌套 `struct Inner { ... }` 参数类型的有效 GNU C 输入；ASan 在 RV32、RV64、LoongArch64 复现 UAF。x86/AArch64 同一容量边界未触发，**不能据此宣称安全**。
- **修复门槛：** 递归解析前后使用稳定 `RecordId` 再取 Record，测试多种记录预置数量及五目标 ASan。

### MC-003 运行时 Record 初始化器中的悬空指针

- **位置：** `src/frontend/parser_runtime_initializer.c:1374–1398`、`src/frontend/ast.c:2191`。
- **证据：** `struct Outer o = { sizeof(struct Inner { int z; }), 22 };` 在处理首字段时扩容 Record arena，第二字段访问旧 `record`；ASan 在 RV32/RV64/LoongArch64 复现。GCC GNU11 接受该构造。
- **修复门槛：** 跨字段按 RecordId 重新解析地址，或采用已在静态初始化器中使用的、语义可证明安全的 Record 快照；五目标扩容矩阵。

### MC-004 属性探测使 Parser tags 失效

- **位置：** `src/frontend/parser_attribute.c:745–751`、`src/frontend/parser_core.c:937,973`。
- **证据：** `MinicParser probe = *parser` 浅拷贝后解析 `aligned(sizeof(struct Probe { int p; }))`，新 tag 的 `realloc` 仅更新副本、释放原 Parser 的旧 `tags`；ASan 五目标均复现。
- **修复门槛：** 完整语义探测应使用唯一拥有动态元数据的 Parser，明确保存/恢复词法游标和 tag 可见性；不能把词法副本当成语义事务。

### MC-005 属性探测使 Parser enum_constants 失效

- **位置：** 同一 `parser_attribute.c:745–751`；释放在 `src/frontend/parser_enum.c:309`，旧地址读取在 `:114`。
- **证据：** `aligned(sizeof(enum ProbeEnum { A16 = 4 }))` 导致 `enum_constants` 在副本中扩容，原 Parser 随后查询既有枚举常量触发 UAF；GCC 接受，ASan 五目标复现。
- **修复门槛：** 与 MC-004 统一修 Parser 事务所有权，检查 `tags`、`enum_constants`、scope/index 等所有可分配字段；不要逐字段零碎补丁。

### MC-006 传递特化污染共享 CALL

- **位置：** `src/compiler/inline_symbolic.c:874–878` 与 `src/core/core_lower_calls.c` 的候选选择。
- **证据：** 两个 caller 通过共享的 `mid` 函数体传入不同静态 key。GCC x86 目标文件的 `__jump_table` 有两种 key 重定位（`keys+2`、`keys+0xa`）；MiniC 只有一种。反转 caller 顺序，MiniC 保留的地址也反转，说明共享 CALL 被后写者覆盖。
- **根因：** `(caller FunctionId, CALL ExpressionId)` 的实例专属 callee 被降为单个共享 `ExpressionId → callee`。
- **修复门槛：** 共享 AST 保留源码静态关系；实例特化事实表以稳定 `(caller_id, call_id)` 为键，Core 在具体调用实例上选目标。正反顺序均应保留两种 ELF 重定位；不能依靠“生成 clone 数量大于零”作为正确性证据。

### MC-007 整数与符号特化无法组合

- **位置：** `src/compiler/inline_integer.c` 传递特化、`inline_symbolic.c:780–814`、`src/core/core_lower_calls.c`。
- **证据：** `mid(key, k) → leaf(key, k)`，两 caller 分别提供不同静态 key 且 `k=1`。整数阶段建出 leaf 的整数 clone，却没有将事实归到具体 CALL 实例；符号阶段读共享原始 CALL，`transitive=0`，最终 Core 报不支持的 leaf clone。修正探针中的 x86 GNU ASM 为合法 `.balign 8` 后，GCC `-O2 -c` 通过而 MiniC `-S` 失败。
- **修复门槛：** 与 MC-006 共用实例事实模型；联合整数＋符号特化不丢任何一方事实。

### MC-008 符号特化过度要求整数参数常量化

- **位置：** `src/compiler/inline_symbolic.c` 的 `minic_inline_symbolic_integer_inputs_are_closed` 等检查。
- **证据：** `leaf(int *key, int k)` 中 GNU ASM 只要求 `key` 为立即量，`k` 正常运行时使用；GCC `-O2 -c` 成功，MiniC `-S` 拒绝 `leaf`。当前算法要求**所有**整数参数已知才准符号特化。
- **修复门槛：** 按实际立即量操作数和已知事实需求判定，不强制无关整数形参常量化；与 MC-006/007 共同回归。

### MC-009 表达式探测回滚遗漏 owned CALL 参数

- **位置：** `src/frontend/parser_static_initializer.c:3165–3178`、`semantic_snapshot.h:167–194`、`ast.c:499–528,107–111`。
- **证据：** 对数组 designator 的 `sizeof(func(12 个实参))` 试探解析会给 CALL 分配 `overflow_arguments`；恢复较小的 `expression_count` 后，析构只遍历当前节点计数，旧分配失去所有者。GCC 与 MiniC 均接受相应合法输入。
- **证据边界：** 源码所有权链确认；此环境 LeakSanitizer 无有效动态泄漏报告。普通值型 ArrayType/FunctionType 计数回滚并没有同类堆所有权，不应一概否定。
- **测试缺口：** `tests/frontend/semantic_snapshot_test.c` 只手工设置/调整 Program 的计数，不构造拥有 `overflow_arguments` 的真实 CALL；它能验证快照数量门槛，不能反证 owned payload 泄漏。`semantic_snapshot.h` 中原本过度概括“安全回滚”的注释已单独更正，**代码问题仍开放**。
- **修复门槛：** 回滚前释放所有被撤销表达式的 owned payload，或采用显式事务；补异常/失败路径和 LSan 测试。

### MC-010 LoongArch 特殊 ASM 写回绕过对齐保护

- **位置：** `src/target/loongarch64/asm_contract.c` 对 `add_unless`(2)、`sub_positive`(2)、futex RMW(2)、futex CMPXCHG(2)、CMPXCHG(1)、masked CMPXCHG(2)、small XCHG(2)，合计 **13 个逻辑结果槽**。
- **证据：** 对可能不对齐的 packed C lvalue 仍直接发出 `st.w/st.d`，未经过 `src/target/loongarch64/memory_emit.c` 的安全 store helper。现有 packed 回归覆盖其他路径且记录 `QEMU=NOT_RUN`；当前仅确认汇编路径缺保护，**未证明硬件异常**。
- **第四轮独立发射样本：** 有效 GNU C 的 `struct __attribute__((packed)) S { char lead; int rc; };` 将 `x->rc` 作为 `add_unless` GNU ASM 的 `=&r` 结果 lvalue，而真正 LL/SC 原子内存使用独立对齐的 `int *aligned_atomic`，避免故意制造未对齐原子操作的假阳性。MiniC LoongArch64 `-S` 接受并生成 `.s`：为结果地址加偏移 `1` 后直接 `st.w ...,0`，未经过对非对齐 `int` 的 byte-scatter helper。证明 MC-010 的结果槽路径确实可被用户源码触发；**尚无 Loong GAS/QEMU 报错或硬件异常**。复现 `.i` 位于工作区 `loong4_add_unless_packed.i`。
- **修复门槛：** 复用现有 `minic_loongarch64_emit_asm_output_store`；确认结果、地址及 scratch 寄存器互不别名；覆盖 packed/对齐两路、交叉 GAS/ELF/QEMU，不改 LL/SC 源原子循环语义。

### MC-011 Source-only 的 QEMU 结果报告不实

- **位置：** `.github/workflows/minic-native-refactor-gate.yml` 以 `MINIC_SOURCE_ONLY=1` 调 LoongArch 脚本；`tests/compiler/c0/probe-loong-linux-{cmpxchg,xchg}-qemu.sh`。
- **证据：** source-only 仅生成 `.s` 并检查助记符，跳过 GNU AS/link/QEMU，却仍输出 `GNU_QEMU=2` 或 `GNU_QEMU_42=2`。独立按相同环境执行确认。
- **修复门槛：** 报告显式标记 `mode=source_only, qemu=NOT_RUN`；四架构 runtime workflow 中实际 QEMU 步骤继续保留，不能误称全项目从未运行 QEMU。

### MC-012 Source-only 的 ELF 结果报告不实

- **位置：** `.github/workflows/minic-native-refactor-gate.yml` 的 `MINIC_SECTION_ONLY=1`；`tests/compiler/c0/probe-linux-global-section-reloc.sh`。
- **证据：** 此模式只检查 `.section/.weak` 等汇编文本，仍报告 `reloc=1`、`relocations=2`，不能解释为已 `readelf -r` 验证。
- **修复门槛：** `source`/`elf` 验证级别进入测试结果，只有真实 ELF 才报告 relocation 验收。

### MC-013 Native gate 的测试触发白名单缺口

- **位置：** `.github/workflows/minic-native-refactor-gate.yml` 的 `paths`；其执行的 `tests/compiler/c0/probe-review-batch-2026-10-29.sh`。
- **证据：** 仅改 review-batch 编排或未列子测试时，不一定命中 native gate 的 path 白名单。
- **修复门槛：** 覆盖该聚合脚本及其测试变更路径，或建立单一可追踪测试触发契约。

### MC-014 RV32 变参与过对齐栈帧交界

- **位置：** `src/target/riscv32/rv32_frame.c:172–185`、`rv32_emit.c:673–679,781–805`。
- **状态更新（第四轮）：** `OPEN` / P0 / `ASM-ONLY`。由合法 GNU C **实际被 MiniC 接受并生成汇编**，并非仅凭对源码分支推想；但尚未运行 RV32 GNU GAS/QEMU，不能宣称机器级动态错编已复现。
- **可重现输入：** `/tmp/minic-riscv-va-frame-probe.c` 含 `va_frame_one(int tag, ...)`（`aligned(64)` 局部 + 连续读取 9 个 `int` 变参）和 `va_frame_eight(int a0,...,int a7, ...)`（无剩余变参 GPR）。GNU `cc -E -P` 预处理后 `build/debug/bin/minic-cc --target=riscv32-linux -S` 通过。
- **汇编/ABI 交叉证据：** `va_frame_one` 的入口为 `E=t2`，帧基址 `S=alignDown(E-160,64)`；保存 `a1..a7` 在 `S+132..156`，变参游标最终走到 `S+160`，而调用方第一个栈参数在原入口 `E`。两者在 `E` 的不同 64B 相位下可相差 16/32/48B。`va_frame_eight` 直接从 `S+128` 而非 `E` 起读，也错位。`rv32_call.c` 的 outgoing stack slot 从 caller 当前 SP 起写，与 callee 入口 SP 的 ABI 身份互相确认。
- **修复门槛：** 正确衔接寄存器溢出到 caller stack、零 GPR 路径，并以实际交叉工具链/QEMU 检验四类入口 SP 相位。

### MC-015 RV64 变参与过对齐栈帧交界

- **位置：** `src/target/riscv64/frame_layout.c`、`src/target/riscv64/core_codegen.c` 的变参保存区/动态对齐路径。
- **状态更新（第四轮）：** `OPEN` / P0 / `ASM-ONLY`；同一个合法预处理输入 RV64 `-S` 成功。`va_frame_one` 中 `E=t0`、`S=alignDown(E-144,64)`，`a1..a7` 保存在 `S+88..136`，第 8 个变参按线性游标指向 `S+144`，但 caller 栈起点为 `E=S+144+δ`，其中 `δ` 可取 0/16/32/48；已在生成汇编和 `rv64/call_emit.c` outgoing 参数布局交叉核对。**还没有** RV64 GNU GAS/QEMU 机器运行差分。
- **明确排除：** 对相同 fixture 的 `va_frame_eight`，RV64 在没有剩余 GPR 时确实从保存的 `entry_sp_offset` 恢复 `E` 初始化 `va_list`；这一子路径正确，不能把 RV32 的额外错误泛化到 RV64。
- **修复门槛：** 解决跨界后的地址连续性，GNU/QEMU 验证；在动态运行确认前保持“汇编级高置信风险”。

### MC-016 线性 va_list 类型准入过宽

- **位置：** `src/target/target_info.c:548–575`。
- **疑点：** RV32/RV64/LoongArch 当前只检查参数是否指针，普通 `int*` 可被当作 `va_list` 调用 GNU va builtins。x86/AArch64 MiniC 和宿主 GCC 拒绝。
- **缺口：** 尚缺对应目标 GCC 的 canonical `va_list` 类型差分；可能是明确的扩展/准入限制，应先查目标 ABI 和语言契约。

### MC-017 另外两个 Parser 语义探测浅拷贝

- **位置：** `src/frontend/parser_static_initializer.c:3163–3275`、`parser_external_object.c:650` 和 `src/frontend/parser_constant.c:380–435` 的语义表达式解析调用链。
- **疑点：** 全局检索 38 处 `MinicParser probe=*parser` 类浅拷贝，35 处属于不修改拥有型表的词法前瞻，MC-004/005 是 1 处完整语义解析触发的两种 UAF；array extent 的 probe 也会解析真实表达式，`sizeof(struct X {...})` 可能引入 tag/Record 等实体，需核浅复制 owner 与快照门槛。
- **实测边界：** 本轮有效 GNU C `int array_probe[] = {[sizeof(struct X {char c;})] = 1};` 经宿主 GNU C 语法检查和 RV64 MiniC `-S` 均成功；RV64 `MODE=sanitize`、`ASAN_OPTIONS=detect_leaks=0` 未报错。单一样本没有扩容边界，**不能排除**容量相关 UAF，也不能升级为 Bug；下一步用多种预置 tag 容量、有效 GNU initializer 与五目标 ASan 差分。

### MC-018 AArch64 短分支距离依据基本块数

- **位置：** `src/target/aarch64/block_emit.c:378–404`。
- **疑点：** `block_count<=256` 并不在逻辑上保证 `cbnz` 的 ±1MiB 范围，但当前构造有效超长单块测试受容量/ASM 资格限制，**没有可复现汇编失败**。
- **下一证据：** 找到可接受的大块合法 Core 输入，或证明布局上界与块数真正相关。

### MC-023 静态聚合后向覆写跨递归借用 Record

- **第五轮升级：** `OPEN`、P0、**ASAN**，原调查已被 GNU C 有效源码及真实 heap-use-after-free 堆栈证实。之前那条单点无报警的输入仅说明当时未命中容量边界，**不能再当成反证**。
- **位置与消费者：** `src/frontend/parser_static_initializer.c:2364–2528`，`overwrite_static_zero_record_value` → `overwrite_static_zero_record_constant`。`parse_static_record_constant` 后向覆写零字段时持有 `program->records[record_id]` 借来的 `MinicRecord *record`，`overwrite_static_zero_field_value` 解析 `sizeof(struct { int z; })` 可追加 Record，`realloc` 释放旧 arena，随后本函数 `:2496` 读取 `record->fields`（ASan READ8）。
- **独立复现：** 持久用例 `tests/compiler/audit/mc023_static_record_backward_uaf.c`（源自 `/tmp/minic-front-static-record-backward-minimal-20261030.c`），GNU `-std=gnu11` 编译成功。预置 14 个 Record 在 16 容量界、126 个 Record 在 256 容量界均得到 MiniC ASan UAF；RV64 sanitize `-S` 堆栈指向 `overwrite_static_zero_record_constant:2496`。GCC 原生与普通 MiniC x86 生成程序在该反例上都 exit **7**，即目前证明**内存不安全**，不声称已独立观察到输出错编。
- **其他链路的正当设计：** 正向 `parse_static_record_constant` / `parse_static_union_constant` 使用值型 Record header snapshot，fields heap 独立存在，可跨 Record arena 扩容；不可为了修后向路径删除它们。
- **修复验收：** 后向路径持有稳定 RecordId 或值型快照，避免任何可扩容后再次解引用 arena pointer；以 16/256 容量和多目标 ASan 验证，再做正向/反向 Record+匿名字段+重定位与 GNU 汇编差分。当前尚未修复。

### MC-024 Common Core 中的 rK 约束特例

- **位置：** `src/core/core_lower_asm_structured.c:204–260` 的 `core_asm_try_immediate_output_csr`，对照 `src/core/core_lower_asm_target.c:10`。
- **疑点：** Common Core 直接识别 Linux RISC-V GNU ASM 的 `rK`、固定 output/input/clobber 形状并优先完成立即量特化。项目已有 Target-owned contract/资格机制，需明确这处 source constraint 语义是否可被目标拒绝、是否只属于受控兼容 fastpath。
- **证据边界：** 未发现跨 ISA 错编实例，Core 也没有直接用 `MINIC_ARCH_*` 枚举发码。需要从 `minic_core_lower_inline_asm` 的 probe 顺序追踪真消费者；**不能按出现 `rK` 就宣告架构泄露 Bug**。
- **下一证据：** 验证五目标 target-required admission、fastpath 输入输出等价与退回路径。若属于安全的旧指令约束兼容，标明正当特例即可。

### MC-025 AArch64 公共全局 object 类型伪操作兼容性

- **位置：** `src/target/aarch64/codegen.c:159` 的目标配置 `"%object"`，及 `src/target/common/global_emit.c:1250` 固定输出 `.type ...,@object` 的 simple-global 路径。
- **调用链已确认：** `src/target/common/direct_program_emit.c:53–70` 对非 extern 的真实全局定义直接调用 `minic_multi_emit_simple_global`，此函数没有获得 `%object` 的目标 dialect；目标配置目前仅影响 alias。两种配置不一致确实会走到，但 AArch64 GNU AS 是否接受 `@object` 仍待确认。
- **证据边界：** 这是静态审查发现的接口差异，尚无合法 C→MiniC→AArch64 GNU AS 的失败样本。当前不作为已确认错码。
- **下一证据：** 现有 `tests/compiler/c0/probe-aarch64-pie-extern-got.sh` 已含 static global + 真实 AArch64 cross GCC/GAS/QEMU 步骤，优先在精确 HEAD 跑它并对照 emitter 路径；必要时补已初始化/未初始化对象的差分。如果 GAS 允许固定格式，关闭。不能把 source-only 结果充当 GAS 通过。

### MC-026 Source form ID 的实际身份边界

- **状态：** `CLOSED-NOT-A-BUG`；2026-10-30 本轮由真实读写调用链复核。保留条目用于防止重新误报。
- **位置：** `src/target/backend.c:minic_backend_asm_source_form_id` 与其 `capture_asm_operand_source`、下游 target qualification。
- **先前疑点：** hash 覆盖模板、ordered constraints、flags、clobbers 等，不直接编码某些符号名称/标签文本，曾担心作为缓存键导致不同 CALL/operand 实例冲突。
- **实际消费者：** 全生产源码检索 `source_form_id`：`backend.c` 生成，`core_lower_asm_operands.c` 保存并在可选诊断中打印，`core_lower_asm.c` 仅生成诊断 signature；**没有实例 cache/key/equality 的生产消费者**。`target_machine_form_id` 单独负责目标机器形式；`tests/core/asm_operand_plan_test.c` 仅要求约束变化时来源哈希变化。
- **关闭依据：** 该字段是故意可跨实例重复的来源指纹，不是每个操作数/label 的唯一 ID；遗漏实例名不违反当前契约。未来若新增以其为 key 的消费者，应重新审查其键语义，不能沿用当前结论。

### MC-027 TargetInfo 惰性初始化的并发契约

- **位置：** `src/target/target_info.c:minic_target_info_for_architecture`。
- **疑点：** 若同进程多个线程首次并发使用静态 lazily-built target descriptor，是否存在初始化竞态，应看 API/driver 的明确线程模型。
- **证据边界：** 当前生产 CLI 每次是独立进程，未证明承诺同进程并发调用或存在可复现数据竞争；不能因此判生产 bug。
- **下一证据：** 查找公开并发使用者、线程安全文档及构造路径；需要时 TSAN/并发探针验证。若定义单线程初始化/预先初始化则记录为设计约束。

### MC-028 RV64 的 int128 移位计数类型限制

- **状态：** `OPEN`（已确认有效 GNU C 输入的 RV64 MiniC `-S` 接受缺口）；证据 `LOCAL-COMPILE-DIFF`。**不是**运行时错码的结论。
- **位置：** `src/target/riscv64/scalar_emit.c:101–122,263–290`，对照 `src/core/core_lower_arithmetic.c:490–540`、`src/core/core_ir_verify.c:123–136` 与 `src/target/riscv32/rv32_wide_emit.c:32–67`。
- **复现（基线 `4f516707d`，2026-10-30）：** 对 `unsigned __int128 shift128_left(unsigned __int128 x, unsigned __int128 n) { return x << n; }` 和对应 `>>`，本机 GNU `cc -std=gnu11 -fsyntax-only -x c` 成功。相同 `.i` 用 `build/debug/bin/minic-cc -S ... --target=riscv64-linux -o ...` 失败：`cannot emit RISC-V function 'shift128_left'`。只改 RHS 为 `unsigned long`，RV64 MiniC `-S` 成功；确认是宽 RHS 的目标能力缺口，而非整个 `__int128` shift 未实现。
- **根因：** 左、右移目标发射器均显式拒绝 `minic_type_is_int128_integer(function->values[right].type)`；Core 按 C shift 语义保留 RHS 整数类型；RV32 同族双字 shift 已提供高位计数处理。
- **证据边界与后续：** GNU **宿主机语法接受**和当前 MiniC 的失败对照足以证明此 `-S` 缺口；**尚未**跑 GNU RV64 `-S`、GNU AS/ELF/QEMU，不能称同目标完整差分或运行时错误。修复前后应补左右移、符号/无符号、过宽 count 和五目标能力边界的回归，勿现在贸然改指令算法。

### MC-032 AST verifier 内层容器检查顺序

- **状态：** `INVESTIGATING`；仅针对可能构造的 malformed 内部 Program，当前没有合法源文件触发路径。
- **位置：** `src/frontend/ast_verifier.c:minic_verify_build_array_owners`（约 1317–1393），与 `minic_c0_program_verify_target_detailed` 的索引建立/Record/Function 容器形状校验顺序。
- **疑点：** 加速 owner 索引可能在详细校验 `record->field_count/capacity`、函数参数数组等嵌套存储前迭代它们。若调用者手工构造非规范 Program，或将来新增会破坏其不变式的生产者，验证器也可能在正常返回 `false` 之前读取非法内存。
- **证据边界：** 当前 Compiler 仅传 Parser 构造的 Program，未找到生产路径违反对应容量/指针规则；因此不作为现存错编 Bug。下一步用最小手工构造 Program + ASan 检验公开 verifier 契约，再决定补 shape preflight 还是显式限定输入为结构合法的 Program。

### MC-033 Core 独立自增自减语句保留过时常量事实

- **状态：** `OPEN`，P0；`GNU-NATIVE-DIFF`。这是已在 x86-64 GNU 工具链完整链接、本机执行中复现的**真实控制流错编**；并非仅靠源码猜测。RV32/RV64/AArch64/Loong 的相同输入也都成功 `-S`，汇编里仅保留错误的返回 99 分支，**未在这些目标上执行**。
- **位置：** `src/core/core_lower_expression.c:1237–1246` 对独立 `i++/--i` 表达式语句直接走 `minic_core_lower_scalar_update`；同文件 `:423–435` 的通用 `lower_expression` 入口先执行 `minic_core_local_fact_invalidate`。`src/core/core_lower_assignments.c:233–435` 的更新器只生成 load/算术/store，不自身处理 fact 失效。
- **独立最小复现（2026-10-30，基线 `4f516707d`）：** `int main(void) { int x=0; x++; if (x==0) return 99; return 7; }`。`build/debug/bin/minic-cc --target=x86_64-linux -S input.i -o minic.s` 成功，`cc -no-pie minic.s -o minic` 成功，运行 `minic` 返回 **99**；GNU `cc -std=gnu11 -O0 -x c input.i -o gnu` 运行返回 **7**。MiniC 汇编确实先把递增值写回，又无条件选择旧事实 `x==0` 的分支。负向例 `--` 的相同错判也在联合探针里观察到。
- **根因判断：** statement-only 优化捷径与普通表达式事实失效策略分叉。下一次修复须审计其他有副作用的快捷路径、合流后的事实，而非只让此单一 probe 通过。
- **验收门槛：** 建永久 `x++/x--/++x/--x`、语句形式和嵌套表达式形式、分支真/假两路的 GNU 输出/exit-code 差分；x86-64 本机和 RV32/RV64/A64/Loong 可用目标回归；复验现存 Core CFG 优化及 `continue/goto` reentry 语义。**本轮先记录，尚未修改 Core 实现。**

### MC-034 静态 Record 设计符查找对无关对象做指针相减

- **状态：** `OPEN`，P1，`ASAN-POINTER-PAIR`；在有效源码上已观察到 ASan `invalid-pointer-pair`，**尚未证明输出错编**。
- **位置与链路：** `src/frontend/parser_static_initializer.c:2698–2699` 把 `Program.records[id]` 的 Record 值型头复制到栈上 `record_snapshot`；`:2747` 在 `.field` 设计符下调用 `parse_static_record_designator_path`（约 `:1438–1480`），后者调用 `minic_parser_find_record_field_path` → `src/frontend/parser_member.c:28:search_record_field_path`。该 helper 无条件计算 `(MinicRecordId)(record - parser->program->records)`，但 `record` 此时位于**不相关的栈对象**。
- **触发输入与检测：** `struct Q { int x; }; struct Q q = { .x = 3 };`；GNU C 合法，MiniC 普通 sanitize `-S` 通过。独立构建 `MODE=sanitize BUILD_DIR=/tmp/minic-audit-20261030/pointer-pairs CFLAGS=-fsanitize=pointer-subtract LDFLAGS=-fsanitize=pointer-subtract` 后设置 `ASAN_OPTIONS=detect_leaks=0:detect_invalid_pointer_pairs=2:halt_on_error=1`，RV64 `-S` 真实报 `AddressSanitizer: invalid-pointer-pair`，栈指向 `search_record_field_path:27` ← `parse_static_record_designator_path:1459` ← `parse_static_record_constant:2747`，地址分别来自 `record_snapshot` 栈与 Program.records heap arena。栈 Record 快照本身是为避免 arena 扩容悬空而设计的，不能因这里有 UB 直接删除快照。
- **语义边界：** 静态初始化路径目前只使用返回的 field indices，不使用该处生成的 RecordId；其余成员/offsetof 消费者传入 Program-owned Record。修复要保证仍使用稳定 RecordId，或让不需要 RecordId 的消费者避免该运算；再以 ASan pointer-pair 和多架构目标回归验证，不能把“编译结果看似正常”当作 UB 修复。

### MC-035 运行时范围初始化器借用 Expression 跨扩容

- **状态：** `OPEN`，P0，`ASAN`；已由合法 GNU C 生产输入真实复现 `heap-use-after-free`，不再只是源码推测。
- **位置：** `src/frontend/parser_runtime_initializer.c:457–514:add_runtime_initializer_once_read` 先缓存 `const MinicExpression *value = minic_c0_program_expression(program,value_id)`；`:489` 的 `minic_parser_statement_local_lvalue` 经 `src/frontend/parser_statement.c:15–35` 追加表达式，可能 `realloc program->expressions`，而 `:496` 与 `:510` 仍读旧 `value->span`。
- **真实 ASan 反例（2026-10-30，基线 `4f516707d`）：** `int testcase(int seed) { seed + 0; seed + 1; int target[16] = {0, [1 ... 15] = seed + 1}; return target[1] + target[15]; }`，由 GNU C 接受。`ASAN_OPTIONS=detect_leaks=0:halt_on_error=1:abort_on_error=1 build/sanitize/bin/minic-cc --target=x86_64-linux -S /tmp/minic-runtime-init-uaf-probe/add_expression_statement_002.i -o ...` 报 `heap-use-after-free`，`READ size 48` 指向本函数 `:496 assignment.span=value->span`；先前 `parser_statement_local_lvalue:33` → `minic_c0_program_add_expression:519` → `minic_grow_array:20` 释放旧 arena。容量探针前置 0..128 个无效效果式时大部分 PASS、特定两个 `seed+i;` 使增长边界准确命中，**PASS 不是安全证明**。
- **下一步：** 完整容量矩阵和五目标 ASan；在有副作用的 append 前按值保存 `MinicSourceSpan`，并复核同函数/同模块其他跨递归/跨增量存储借用，再集中修这一类所有权问题。本轮未改 Parser 实现。

### MC-036 Record 条件表达式的 local facts 分支合流缺口

- **状态：** `OPEN`，P1，`GNU-NATIVE-DIFF`；x86-64 已完成 GNU C 正常执行与 MiniC→GNU AS/link→本机执行的同源双端差分，确认实际错编。
- **位置：** `src/core/core_lower_record.c:582–713:minic_core_lower_record_conditional_object` 构建 true/false/merge 三块，两臂依次操作同一个 `context->local_integer_constants`，当前看不到进入 true 臂前的快照、false 臂恢复和 merge 交集。对照 `src/core/core_lower_expr_flow.c` 的标量/void 条件表达式使用 `CoreExpressionFactFork` 进行 fork/meet。
- **最小差分（2026-10-30，基线 `4f516707d`）：** GNU C：`struct S{int v;}; int f(int c){int x=0;struct S a={1},b={2};struct S r=c?({x=1;a;}):({x=2;b;});if(x==2)return r.v+10;return r.v;}`，独立 harness 调用 `f(1)` 与 `f(0)`。`cc -std=gnu11 -O0` 执行得 `f(1)=1, f(0)=12`，返回成功；`cc -E -P` 冻结 `.i` → `build/debug/bin/minic-cc --target=x86_64-linux -S` → GNU `cc -no-pie` 链接均成功，执行得 **`f(1)=11, f(0)=12`**，harness 返回 1。说明假臂 `x=2` 的 fact 漏回收后污染 merge 处 IF 剪枝。
- **修复边界：** 应复用既有分支 fact fork/meet 协议，入口 facts 快照应在求值 condition 完成后；真臂成功后记录 then-exit、恢复 entry 再编译假臂，merge 时取交集，并明确分配失败清理。连同 break/goto/reentry 和 side-effect expression 一起检查，不应对 Record 类型另设计一套不相容的 fact 规则。本轮尚未改 Core 算法。

### MC-037 Loong 特殊 ASM 源端与机器端限定符不一致

- **状态：** `OPEN`，P2，`LOCAL-COMPILE-DIFF`，限 LoongArch64 特殊 GNU ASM 的**编译接受缺口**，尚无 GNU LoongArch GAS/QEMU 目标运行结果。
- **位置：** `src/target/loongarch64/asm_contract.c:758–823` 的 `cpucfg` source gate 对结果类型先 `minic_type_unqualified`，因此接受 `volatile unsigned int`；机器 emitter 检查 Core 输出地址指针 pointee 时直接与无 cv unsigned int `minic_type_equal`，丢失 qualifier 一致性。类似源/机器配对还存在于 `frametrace` 与 `frametrace_csr`（约 `:2826–2990`），暂以**同类根因一条**跟踪，未独立证明这两个形式的动态失败。
- **复现（2026-10-30，基线 `4f516707d`）：** GNU host `cc -std=gnu11 -Wall -Wextra -Werror -fsyntax-only -x c` 接受 `unsigned int volatile_cpucfg(unsigned int index) { volatile unsigned int cfg; __asm__ volatile("cpucfg %0, %1\\n\\t" : "=&r"(cfg) : "r"(index)); return cfg; }`。MiniC `--target=loongarch64-linux -S` 对 `unsigned int cfg` 通过，只改成 `volatile unsigned int cfg` 就报 `LoongArch64: Core instruction/ABI not yet supported by M1 emitter`。`unsigned long v; __asm__ volatile("csrrd %0, 0x7":"=r"(v));` 与 `volatile unsigned long v` 的对应 GNU CSR source 也测得同一组 MiniC PASS/FAIL；按同类 qualifier 根因归入一项。
- **第四轮独立子型：** `asm_contract.c:loong_linux_fpr_transfer_source` 对 `movfr2gr.d %0, $f0` 的输出 pointee 使用 `minic_type_unqualified`，而 `loong_linux_fpr_transfer_emit` 直接以完整带 cv 类型比较 `unsigned long`。同一 GNU C fixture（`unsigned long out` / `volatile unsigned long out`）前者 MiniC `-S` PASS、后者 FAIL，追踪 `stage=qualified_hardware_asm`；见工作区 `loong4_fpr_{plain,cv}.i`。这是独立 ASM 家族复现、同一根因，不另分配 MC 编号；目标 Loong GNU AS/QEMU 仍未运行。
- **验收门槛：** 对源端 qualifier、Core pointer pointee、机器输出类型定义一个共用类型比对契约；补 cpucfg 和两组 frametrace 的 volatile 与非 volatile 组合，目标 GNU AS/ELF/QEMU 执行后才宣称运行时正确。

### MC-038 匿名字段设计符后的位置初始化游标错误

- **状态：** `OPEN`，P1，`LOCAL-COMPILE-DIFF`；合法 GNU C 在当前 MiniC x86-64 `-S` 错误拒绝，尚无已发射错码证据。
- **最小反例（2026-10-30，基线 `4f516707d`）：** `struct Outer{struct{int a;int b;};int c;}; int f(void){struct Outer x={.b=2,3};return x.b*10+x.c;}`。GNU `cc -std=gnu11` 接受、执行返回 `23`，MiniC `--target=x86_64-linux -S` 报 `too many positional record initializers`。若只初始化 `{.b=2}` 或显式写 `{.b=2,.c=3}`，MiniC `-S` 和 GNU 链接/执行均通过，说明主要缺口是 `.b` 后的**隐式位置续接**，不是匿名成员查找能力。
- **源码候选根因：** `src/frontend/parser_runtime_initializer.c:1513–1579:parse_runtime_record_designator_target` 从提升的最终 `MEMBER` expression 读取 `field_index` 并用作 `top_level_field_index`；但 `src/frontend/parser_member.c:113–195` 对匿名成员返回的最内层 `field_index` 是内层 Record 的索引。`parser_runtime_initializer.c:1658–1679` 将该值 `+1` 作为下一个顶层位置游标，可能跳错位置。
- **验收门槛：** 建带匿名 struct/union、连续/嵌套 designator、隐式位置续接与显式位置设计符的 GNU C 差分；由明确的顶层路径第一段确定游标，不依赖最后一个递归 MEMBER；多目标 `-S` 与可执行语义测试后再关闭。

### MC-039 运行时嵌套数组递归跨 ArrayType 扩容借用

- **状态：** `OPEN`，P0，`ASAN`；已由合法 GNU C 的真实 Parser 路径和精准容量边界测得 UAF。
- **位置：** `src/frontend/parser_runtime_initializer.c:647–742:parse_fixed_runtime_nested_array_initializer` 在 `:668–674` 保存 `const MinicArrayType *nested_array` 指向 `Program.array_types[]`；循环中 `:706–707` 递归 parse 子数组，随后继续读 `nested_array->element_type/element_count`。合法嵌套初始化器表达式如 `sizeof(int[777])` 可以向 `array_types` 追加 descriptor，经 `src/frontend/ast.c:1273–1298` `minic_grow_array` 使旧地址无效。
- **真实 ASan 反例（2026-10-30，基线 `4f516707d`）：** 以下是精确输入，共 14 个前置不同 ArrayType 类型。GNU `cc -std=gnu11 -fsyntax-only -x c` 接受：

  ```c
  int testcase(void) {
      (void)sizeof(int[3]);  (void)sizeof(int[4]);
      (void)sizeof(int[5]);  (void)sizeof(int[6]);
      (void)sizeof(int[7]);  (void)sizeof(int[8]);
      (void)sizeof(int[9]);  (void)sizeof(int[10]);
      (void)sizeof(int[11]); (void)sizeof(int[12]);
      (void)sizeof(int[13]); (void)sizeof(int[14]);
      (void)sizeof(int[15]); (void)sizeof(int[16]);
      int a[2][1] = {{sizeof(int[777])}, {5}};
      return a[0][0] + a[1][0];
  }
  ```

  `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 build/sanitize/bin/minic-cc --target=x86_64-linux -S /tmp/minic-nested-array-uaf-probe/sizeofs_014.i -o ...` 命中 `heap-use-after-free`、`READ size 72`，读取为 `parser_runtime_initializer.c:697:nested_array->element_type`；先前 `sizeof(int[777])` 经 `parser_type.c:890` → `declaration_sema.c:54` → `ast.c:1304` 扩容 `Program.array_types`，由 `minic_grow_array:20` 释放旧 arena。其他前置容量样本 PASS 并不能抵消该反例。
- **后续：** 全五目标 ASan、二层/多层嵌套和包含多种子元素类型的扩容矩阵；递归前按值缓存 ArrayType 描述符或必要的 `element_type`/`element_count`，保持稳定 ArrayTypeId 语义，本轮尚未修复。

### MC-019 两套 Bool 恒零证明扫描重叠

- **位置：** `src/core/core_lower_bool.c:39–198`、`core_lower_const_facts.c:95–394`。
- **事实：** 两者都沿全程序语句、表达式、ASM 输出检查恒零，算法明显重复且均活跃。前者服务 `core_lower_if.c` 的严格 CFG 剪枝，后者允许经过 reentry 检查的 `return` 后不可达写入，以维持 `local_facts`。
- **重构门槛：** 可抽取共同 proof oracle 和可达性/死尾策略，但保留不同的保守程度，不能机械删去任意一路；未发现因此错编。

### MC-020 无条件的 GENL 调试输出

- **位置：** `src/core/core_lower.c`、`src/core/core_lower_if.c` 中六处 `genl_op_from_split` 相关打印。
- **事实：** 普通编译该命名函数可以产生六处不受环境开关控制的诊断输出，影响日志可读性。
- **清理门槛：** 先区分无条件 debug 与 `CORE_FAST_TRACE` 等故障诊断，再统一可开关策略；不应全部删除诊断。

### MC-021 历史测试与脚本可达性

- **位置：** `tests/compiler/c0/run-array-bound-constant-expressions.sh`、`tools/dev`、`tools/ci` 及历史 workflow。
- **事实：** 老 `invalid_runtime_array_bound.c` 实际是合法 VLA，但相关入口仅在 disabled/历史流程中发现；不能误报当前 CI 因此失败。仓库有大量一次性补丁和 PR 阶段脚本，但活跃 workflow 静态可达图不覆盖 Makefile/dynamic 调用，**未被图命中不等于死代码**。
- **清理门槛：** 建立 workflow→Makefile→脚本→fixture 的完整可达性报告，先归档历史证据再删除，禁止批量按文件名前缀清理。

### MC-022 符号特化回归的 clone 身份断言不足

- **位置：** `tests/compiler/c0/probe-inline-symbolic-shared-body.sh`。
- **事实：** x86 native 确实执行多个 caller 并检查不同返回值；**不是纯 grep 假绿**。但脚本只从 `transitive>0` 推出并输出 `transitive_clones_distinct=1`，没有核实各 caller 对应 clone 身份。
- **验收门槛：** 增加 MC-006 的正反顺序 ELF relocation oracle 和 Core callee 身份断言；保留现有动态语义测试。

### MC-029 Core IF 的 local facts 快照重复实现

- **状态：** `DEBT`，不是功能 Bug。`src/core/core_lower_if.c:280–300:core_runtime_if_copy_facts` 与 `src/core/local_facts.c:60–91:minic_core_local_facts_snapshot` 都根据 `source_function->local_count` 分配并 `memcpy` 相同结构，空表/失败均返回 NULL。
- **生产消费者：** IF 专用函数在 `core_lower_if.c` 两个分支直接消费；同一模块另外多处使用共享 snapshot。两条都可达，不是可以直接删除的死函数。
- **重构门槛：** 统一调用共享快照前，核对两处失败时的保守路径、快照生命周期及条件合流；完成分支行为差分后再删旧 helper，避免仅因“几行重复”导致事实传播策略漂移。

### MC-030 FunctionBody 名义只读遍历的隐式写入

- **状态：** `DEBT`，`SOURCE-TRACE`，尚未证明生产错码或现实并发竞态。
- **位置：** `src/frontend/function_body.c:91–104:visit_expression_child_ids` 浅复制 `MinicExpression`，调用 `src/frontend/ast_traversal.c:69–86:minic_c0_expression_visit_child_id_refs`；后者对每个 CALL 参数执行 `minic_c0_call_set_argument`。当参数超过 inline 容量时，copy 的 `overflow_arguments` 指针仍别名 Program owned heap，名义只读的 visitor 会把相同 ID 写入原 buffer。
- **契约边界：** 当前输入可写、值不变，函数在普通单线程 Compiler 中没有证明语义错误；但 `const MinicExpression*` 不保证物理只读，不能未经修改用于共享 AST 的并发只读遍历。
- **后续：** 如果引入并行 read-only pass，先实现真正只读 child visitor，或至少为浅复制的 CALL overflow 提供独立缓冲；不要删除通用可写重映射 visitor，它仍承担 AST normalize 的实际写入任务。

### MC-031 Parser name_bound 无消费者的旧 API

- **状态：** `DEBT`，可延后清理。`src/frontend/parser_core.c:minic_parser_name_bound`（约 697–708）只有 `src/frontend/parser_internal.h:475` 声明，生产/测试仓内无调用者；实际同 scope 冲突检查使用 `_in_current_scope` 版本，普通表达式还通过 `minic_parser_find_nearest_ordinary_binding` 实现遮蔽关系。
- **第三轮新例：** `src/frontend/parser_type_query.c:minic_parser_parse_alignof_type_value` 也只有定义与 `parser_internal.h` 声明，暂无内部生产/测试调用；实际 `src/frontend/parser_expression.c:parse_alignof` 同时支持类型名和表达式这两种 source 形式。两处都是可能的迁移残留，**属于同类“无消费者私有 helper”清理债，不是两项语义 Bug**。
- **后续：** 删除前核对编译目标及可能的外部私有头使用；若无契约消费者，连同声明一并移除。**不应顺手删除** `_in_current_scope`、scoped/global 查询或活跃的 `parse_alignof`，它们仍有真实消费者。

### MC-040 嵌套 switch labels 重复全量扫描

- **状态：** `DEBT`，P3，`SOURCE-TRACE`，不是已证语义错编。
- **位置：** `src/core/core_lower_switch.c:707–725:minic_core_lower_switch` 为识别 nested cases 调用 `core_collect_nested_switch_labels`，随后在 `lower_nested_case_switch`（约 `:431`）再次进行同一类别的收集；两个真实生产消费者均在同一 lowering 时序。
- **建议：** 后续性能/结构整理可把第一次收集结果交给 nested 路径，减少额外遍历，但需保留 nested switch 的 reentry、scope 和合法 fallthrough 规则，不能机械把 nested case lowering 并入普通 switch 分支。

### MC-041 long double 字面量提前按 binary64 舍入

- **状态：** `OPEN`，P0，`GNU-NATIVE-DIFF`；x86-64 同一有效 GNU C 源文件已经完成 **GCC 本机** 与 **MiniC `-S` → GNU 汇编/链接 → 本机运行** 对照，绝不是源级字符串检查。
- **位置/根因：** `src/frontend/parser_expression.c:parse_floating`（约 176–259）识别 `L` 后仍调用 `strtod` 产生 binary64，再由 `minic_binary128_from_binary64` 扩展为内部 binary128；超出 53 位有效精度的尾数在第一步不可逆丢失。`src/target/x86_64/machine_fp.c:minic_x86_64_emit_fp_instruction` 的 x87 80-bit 常量发射明确依赖此前输入来自 binary64 并拒绝更宽的 payload，所以**只修改 Parser 为真正 binary128 而不联动 Target 并不构成完整修复**。
- **独立复现（2026-10-30，基线 `400f4a611`）：** `int main(void){ long double a=1.0000000000000001L,b=1.0L; return a>b?7:99; }`；`cc -std=gnu11 -O0 -no-pie source.c -o gnu` 运行退出 **7**；`cc -E -P source.c -o source.i`、`build/debug/bin/minic-cc --target=x86_64-linux -S source.i -o mini.s`、`cc -no-pie mini.s -o mini` 均通过，运行退出 **99**。本仓库 x86 `long double` 机器 ABI 为 x87 extended/16B slot，因此两端不是不同 ABI 的伪差分。
- **修复门槛：** 先明确各目标 `long double` 语义和内部 literal 精度，解析 decimal/hex、极大/极小与舍入边界时保留目标精度；更新 x87 literal 转码规则和其他四目标消费者，再做原生/目标 GNU 差分。当前未修改语义代码。

### MC-042 packed 零宽位域丢失对齐栅栏

- **状态：** `OPEN`，P1，`GNU-NATIVE-DIFF`；生成错误字段偏移/Record 大小，实际生成目标汇编和本机运行均差异。
- **位置/根因：** `src/target/data_layout.c:minic_data_layout_record_depth` 在所有位域上先调用 `minic_data_layout_effective_field_alignment`；`record->is_packed` 或 `#pragma pack(1)` 将 `int : 0` 的 `alignment_bits` 压成 8，再用它对齐下一个字段。GNU packed 规则下的**未命名零宽位域是对齐栅栏例外**，不能等同普通非零位域缩窄。
- **复现（基线 `400f4a611`）：** `struct __attribute__((packed)) P { char a; int : 0; char b; };`；同源 `__builtin_offsetof(struct P,b)` 与 `sizeof(struct P)`，GCC x86 目标得到 **4 / 5**，MiniC 得到 **1 / 2**；两套汇编均成功链接，按 `(offset==4 && size==5)` 构造的 harness，GCC 原生退出 **0**、MiniC→GNU AS/链接原生退出 **1**。`#pragma pack(push,1)`/`pop` 的等价 fixture 同样差分，已排除只由 `__attribute__((packed))` 语法触发；不含 packed 的同形结构，两端均为 4/5。
- **独立 ELF/跨 TU 反例：** `struct P packed_instance = {.a=1,.b=2};` 由 GCC 或 MiniC 分别发射并用宿主 GNU GAS 组成 `.o`。`readelf -sW` 显示 `packed_instance` 的 `OBJECT Size` 为 **5 / 2**；用 GCC 另行编译的 consumer 链接此对象并访问 `.b`，GNU 定义对象程序退出 0，替换 MiniC 定义对象程序退出 1。证明错误已越过编译期常量，实际破坏 x86 跨 TU 对象布局 ABI。
- **影响与修复门槛：** `offsetof`、`sizeof`、字段读写及全局初始化皆可能受到布局语义污染；复核 zero-width 与非零 bit-field、packed struct/field/pragma 及显式 aligned 的不同优先级。当前只对 x86 真实执行建立证据；共用 DataLayout 逻辑影响其他目标的风险高，但不能据此宣称其他 ISA 的 GNU/QEMU 结果。

### MC-043 Inline 整数特化预算耗尽直接拒绝合法 C

- **状态：** `OPEN`，P1，`LOCAL-COMPILE-DIFF`，不是已发射错码。
- **位置/根因：** `src/compiler/inline_integer.c:minic_specialize_inline_integer_calls` 的两处 `MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT`（2048）检查，在新 clone 超限时打印 `_LIMIT_HIT` 并 `return false`；`compiler.c` 将其升级为整个 TU `cannot specialize inline integer call sites`。这使特化预算变成了通用源程序的硬接受门槛。
- **有效输入差分（基线 `400f4a611`）：** `/tmp/minic_inline_budget_probe.c` 中同一 `static inline __attribute__((always_inline)) int identity(int x){return x;}` 在一个函数里接收 **2050 个互不相同常量**作为 callsite；GNU `cc -std=gnu11 -O2 -c` 通过，MiniC x86 `-S` 明确报 `MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT_HIT count=2048 limit=2048 functions=2050 source=0` 并失败。同一探针删去尾部两个 distinct callsite、保留 **2048** 项，MiniC `-S` 成功（隔离预算边界）。第二个独立 fixture `/tmp/minic_inline_bool_budget_probe.c` 使用 `range_sum((_Bool)(x != 0), n)` 的 1025 种整数常量，GNU `-O2 -c` PASS、MiniC `-S` 因 **布尔范围特化预算 1024** 失败；删去第 1025 项即成功。两个 pass 不同，但都把优化预算升级为 TU 硬失败，归同一类维护问题，不按每个配额另开编号。
- **修复门槛：** 审计整数、符号、布尔三阶段对 clone budget、`always_inline` 与后续 Core 的真实要求；能安全使用原调用/其他保守语义路径时应受控退化，真正不能表示的源语义应给明确目标能力错误。不要盲目无限扩大预算导致 Linux 规模灾难；须同时测内存、栈、自举和回归。

### MC-044 函数参数列表中的 Record/Enum tag 作用域外泄

- **状态：** `OPEN`，P1，`LOCAL-COMPILE-DIFF`；合法 GNU C 的 namespace/scope 规则被 MiniC 前端错拒。
- **位置/调用链：** `src/frontend/parser_declarator.c` 与 `parser_function.c:parse_function` 调 `src/frontend/parser_function_parameters.c:minic_parser_parse_parameter_list` 解析含 `struct/enum` 定义的参数类型；`parser_record.c`/`parser_enum.c` 经 `minic_parser_bind_record_tag` / enum bind 修改**当前** Parser scope。`parser_function.c` 直到函数定义 `{` 后才 `minic_parser_begin_scope`（约 `:2083`），而函数原型、定义参数以及嵌套函数指针形参从未获得正确隔离的 prototype/parameter scope。
- **三类有效反例（2026-10-30）：** `void prototype(struct OnlyInside{int a;} value); struct OnlyInside{char b;};`；`int f(struct OnlyInside{int a;} x){return x.a;} struct OnlyInside{char b;};`；`typedef int(*Callback)(struct HiddenPointer{int a;}*p); struct HiddenPointer{char b;};`。GNU `cc -std=gnu11 -fsyntax-only` 全部 PASS（会警告 prototype-scope tag 不对外可见），MiniC x86 `-S` 全部报 `duplicate record definition`。将第一条 Record 替为 `enum Hidden {P=1}` 后，文件级 `enum Hidden {Q=2}` 也报 `duplicate enum definition`。这些是同一参数作用域所有权错误，不按四个测试变成四个 Bug。
- **反向边界：** 两次声明把 prototype-scope `struct P` 当同一完整类型的无效 C 输入，GCC 拒绝而 MiniC 错接受，印证 tag 身份被错误共享；但这不是有效 GNU C 拒编的主要证据。反过来 `int f(struct BodyTag{int a;} x){return sizeof(struct BodyTag)==sizeof(x);}` 在函数 body 中引用该 tag 的 GNU/MiniC 正例均可通过；不能为消除泄漏把 body 内的正确可见性一起删掉。枚举常量被后续 file-level 同名普通对象遮蔽的单一样本执行正常，不能泛称所有 enum ordinary binding 均错。
- **修复门槛：** 不能简单在参数解析末尾统一 `end_scope`：真正函数定义 body 需要能见参数及相应 tags，嵌套函数指针签名需独立结束 prototype scope；逐层清点 scope begin/end、tag 可见性、参数 ID 与声明兼容规则，覆盖 prototype、definition、typedef、nested pointer 及 enum。

### MC-045 Loong futex CMPXCHG 覆盖 GNU 可读写输出初始值

- **状态：** `OPEN`，P1，`ASM-ONLY`（Source→MiniC `.s`），**没有** LoongArch64 GNU GAS/QEMU 动态对照，不能写成已在真实机器运行复现。
- **位置/根因：** `src/target/loongarch64/asm_contract.c:loong_linux_futex_cmpxchg_source` 对结果 `err` 接受 GNU `+r`（既是输入又是输出）；`loong_linux_futex_cmpxchg_emit` 机器特化却用 `addi.w ...,$zero,0` 创建固定零值并在结尾写回，忽略原 `err` 的入口值。这与 MC-010 的可能不对齐写回是**不同语义缺陷**。
- **独立证据：** `loong4_futex_cmpxchg.c` 使用完整 Linux Loong futex GNU ASM `%z4/%z5`、双 `__ex_table` 原模板，令 `int err=7` 并使用 `"+r"(err)`；GNU host `cc -std=gnu11 -fsyntax-only` 接受，MiniC `--target=loongarch64-linux -S` 也成功。原 GNU 模板正常路径未赋值 `%0`，仅异常表 fault 路径可能改变 err，因此无 fault 时应保留初始 7；MiniC `.s` 中先 `li.w $t0,7`，随后 `addi.w $t6,$zero,0`，最后 `st.w $t6,$t7,0`，生成正常路径会将它变成 0。Linux 现有调用若先将 err 初始化成 0，则暂不触发这一具体错码；但 Source gate 允许的 GNU C 语义范围更广。
- **修复门槛：** 机器特化保留 `+r` 旧值并仅在模板显式修改或异常分支时更新；不得假设 Linux 某条调用恰为零。必须补普通/非零初值及 compare success/fail/fault 分支的目标 GNU ELF/QEMU 差分，另独立守护 MC-010 packed output safety。

### MC-046 AArch64 两个特殊 ASM 路径引用默认 Target Layout

- **状态：** `DEBT`，P2，`SOURCE-TRACE`；没有已证错码。`src/target/aarch64/asm_contract.c:aarch64_stlr_memory_carrier` 与 `aarch64_linux_hyp_spin_type` 两处引用 `minic_default_data_layout()` 而非当前 AArch64 `target->data_layout`。默认布局实际是 RV64；所涉当前整数/Record 字段在 RV64/A64 的值暂相同，而且全局入口已有 A64 layout 验证，**不能把架构味道直接报成 A64 错编**。
- **清理门槛：** 明确传入当前 Target/Layout 或改用调用方已计算的字段，保证未来布局扩充不跨目标偷借默认配置；保持现有 GNU 特例 source/emit 准入行为，经 A64 source + GNU AS/ELF 差分后再迁移。

### MC-047 Core 事实求值与可消除副作用的证明边界

- **状态：** `INVESTIGATING`，P2，`DESIGN-REVIEW`；本轮**没有**动态 wrong-code 反例。
- **位置/风险：** `core_const_eval_integer_with_locals` 给出表达式可证明值，`core_lower_if.c` 消费者另检查 `minic_core_cfg_pure_call_argument`，而通用 `core_lower.c` 的 condition branch 直接根据事实改变执行路径。当前 GNU statement-expression 支持范围保守，尚未证明它会跳过有副作用的求值；但**值已知**不自动推出**表达式求值可被整个省略**。
- **第四轮排误报：** `&&/||` 的真实 RHS lowering 已做 snapshot/meet；switch caller 在前后清 facts；两个有赋值的 GNU statement-expression 和 8 路 Core CFG 有效探针，x86 MiniC→GNU native 与 GCC 一致，RV32/RV64/A64/Loong 对同一 8 路 `.i` 的 `-S` 均通过（其余 4 目标没有目标执行证据）。
- **下一证据：** 只在扩展常量求值能力或修改 branch folding 时，核对所有消费者是否分别需要 `known_value`、`effect_free_to_elide`；如无有效反例持续保留调查状态，不应以此为由立即重写 Core CFG。

### MC-048 RV32 指针到 64 位整数的合法转换被拒绝

- **状态：** `OPEN`，P2，`LOCAL-COMPILE-DIFF`；没有目标 GNU RV32 ELF/QEMU 执行证据，问题是 `-S` 接受缺口而非静默错码。
- **输入与差分：** `typedef unsigned long long u64; u64 ptr_to_u64(const void *p){ return (u64)p; }` 是有效 GNU C；宿主 GCC 语法检查成功，同源 `cc -E -P` 后，MiniC `--target=riscv32-linux -S` 失败，具体记录 `<rv32-core>: ... unqualified Core instruction function=ptr_to_u64 ... kind=19 (SCALAR_BITCAST)`。x86/AArch64/Loong/RV64 对照 `-S` 全部通过，**只有 RV32 失败**。同一 RV32 中反向 `void *u64_to_ptr(u64 x){return (void *)x;}` 也可通过；多加一个中间转换 `(u64)(unsigned long)p`，RV32 `-S` 成功并发出低 32 位值、高 32 位零的成对返回，进一步锁定直接 1→2 word 的 BITCAST 路径。没有因此宣称 GNU RV32 目标运行已通过。
- **源码契约矛盾：** `src/core/core_lower_scalar_values.c` 对合法 pointer→integer cast 形成 Core `SCALAR_BITCAST`；`core_ir_verify.c` 认可这种形状；`src/target/riscv32/rv32_integer_emit.c` 看到 `from_words=1, to_words=2` 时却只准整数扩展而拒绝 BITCAST。ILP32 下 pointer 1 word、`unsigned long long` 2 words，此差异无需假定 RV64 的物理 ABI。
- **修复门槛：** 明确 C 的 pointer→integer 目标表示/高位扩展规则，在 Core 类型转换或 RV32 lowering 中表达而不滥用无类型 bitcast；验证指针往返、带符号/无符号目标、不同宽度、GNU RV32 GAS/运行时结果，再判断是否可消除特例。

### MC-049 静态链式数组设计符跨 ArrayType 扩容借用

- **状态：** `OPEN`，P0，`ASAN`；已证生产源码上的 `heap-use-after-free`，区别于 MC-039 的**运行时嵌套数组**路径。
- **位置/链路：** `src/frontend/parser_static_initializer.c:1120–1197:append_static_chained_array_designator_value` 借 `MinicArrayType *array`；递归调用 `minic_parser_parse_array_designator_component` 可解析 `sizeof(int[3])` 并通过 `src/frontend/ast.c:20` 的 arena grow/realloc 移动 `Program.array_types`；`:1156` 再读取先前的 `array->element_type` 引发 ASan `READ size 72`。
- **反例：** 持久用例 `tests/compiler/audit/mc049_static_chained_array_uaf.c`（源自 `/tmp/minic-front-static-chained-array-borrow-20261030.c`），合法 GNU C 静态前向链式设计符；x86 预置 29 个、RV32/RV64/AArch64/Loong 预置 30 个 typedef 数组类型时可命中容量相位，31 个前置元素可能不触发，**无告警不是安全证明**。ASan 指向上述 read/free 栈。
- **修复/验收：** 跨任何可能 append 的解析保存稳定 TypeId/ArrayTypeId，之后重新取或在安全时按值保存明确头；以五架构多容量扩容矩阵、GNU 静态初始化值/重定位和 ASan 合验，不能只把这处改成临时 `const`。

### MC-050 x86 六组 ASM 限定符源端与机器端不一致

- **状态：** `OPEN`，P2，`LOCAL-COMPILE-DIFF`；是合法 GNU C 的 `MiniC -S` 准入缺口，**不是机器执行 wrong-code 证据**。
- **共同根因：** `src/target/x86_64/asm_contract.c` 对 source 的 GNU output carrier 用 `minic_type_unqualified()` 接受 `volatile`，而 `src/target/x86_64/machine_asm.c` 的配对 `emit_*` 对 Core output pointer pointee 直接用 raw `minic_type_equal()`，从而同一 source contract 后端拒绝。Core 的 `is_volatile=true` 是目标 ASM 副作用建模的有意转换，不属于此 bug。
- **六组独立有效模板：** `UNWIND_CONTEXT`（`x86_linux_unwind_context_source`/`emit_linux_unwind_context`）；`PKRU`（`x86_linux_pkru_source`/`emit_linux_pkru`）；`RDRAND/RDSEED`（`x86_linux_random_seed_source`/`emit_linux_random_seed`）；`FSGSBASE`（`x86_linux_fsgsbase_source`/`emit_linux_fsgsbase`）；`ATOMIC_CC`（`x86_linux_atomic_cc_source`/`emit_linux_atomic_cc`）；`BITOPS`（`x86_linux_bitops_source`/`emit_linux_bitops`）。
- **差分：** 同一 GNU11 C 每组 `cc -O0 -Wall -Wextra -Werror -c` plain/cv 均通过，MiniC `--target=x86_64-linux -S` plain PASS、cv FAIL `kind=43`。工作区复现 `x86_5_{unwind,pkru,rdrand,fsgsbase,atomiccc,bitops}_*.c`；UNWIND、ATOMIC_CC、BITOPS 的 GNU 本机与混合 MiniC plain/native 程序可执行并 exit 0；另外三组因 CPU 特性/特权限制**未执行**。
- **修复/验收：** 沿 source→Core→emitter 汇总所有输出类型与 `const/volatile` 规范化接口，成批消除六组不一致，覆盖 plain/cv/packed/读写组合。别机械替换每个 type_equal：例如 MMIO/MXCSR 已正确去 cv、某些 ASM 故意要求 exact identity。按同输入 GNU 严格 C、MiniC `-S`、GNU AS/link/native 可用项复核。

### MC-051 long double 一元取负丢失有符号零

- **状态：** `OPEN`，P1，`GNU-NATIVE-DIFF`；当前证明 x86-64 本机的合法 C 语义错误，独立于 MC-041 的字面量精度 bug。
- **位置与根因：** `src/core/core_lower_arithmetic.c:12:minic_core_lower_try_arithmetic_unary` 对 `long double -x` 采用 `__subtf3(+0L,x)`。IEEE 浮点有符号零下 `+0L - +0L == +0L`，但 `-(+0L)` 应产生负零；对 `float` 和 `double` 的独立 NEGATE opcode 不受此处影响。
- **可执行差分：** 持久用例 `tests/compiler/audit/mc051_long_double_negzero.c` + `_support.c`，保留 `/tmp/minic_long_double_negzero_probe.c` 的 11 项双 TU 检查。GNU x86 `failure_mask=0x0`、exit0；MiniC x86 `-S`→ GNU `cc` 链接→本机 `failure_mask=0x10`、exit1。只有 `-(+0L)` 不一致；其他正负值、`-(-0L)`、double/float、跨 TU ABI 基线符合预期。此处不能用浮点普通数值比较验证符号位。
- **修复验收：** 实现语义上的 unary sign flip（位符号/有正确负零的 TF helper），检查目标 x87/RV64 binary128 与 softfloat，补 +0/-0、NaN、Infinity、有限值及跨 TU 严格差分；仅将 `__subtf3` 换常量不保证 NaN 语义。

### MC-052 AArch64 uaccess 窄输出按八字节写回

- **状态：** `OPEN`，P1，`ASM-ONLY`；源 C/机器 `.s` 的物理地址与宽度关系已经定位，**未执行 AArch64 GNU GAS/ELF/QEMU**。可能破坏邻接字段，风险严重但不能冒称运行崩溃已复现。
- **位置与调用链：** `src/target/aarch64/asm_contract.c:3417:aarch64_linux_uaccess_source` 准入 `ldtrb/ldtrh/ldtr w10` 的 `=r` 1/2/4B C output 和 8B pointer output；`:3467:aarch64_linux_uaccess_emit` 处理 LDTR 后一律 `str x10,[x12]`（8B）写回保存的 C output address，针对 1/2/4B lvalue 都超过对象宽度。8B pointer output 源端允许但机端 raw 整数类型检查拒绝，属同一家族合同不一致。
- **反例证据：** `/tmp/f5_a64_uaccess_{exact,u16,u32,u64,ptr,packed}.i/.s`；GNU C 语法 + MiniC A64 `-S` 对 1/2/4/8 字节输出均 PASS。1B 例 `.s` 中 `ldtrb w10,[x11]`，目的为 `sp+249` 单字节局部，后续却 `str x10,[x12]`；packed `struct{u8 tag;u8 value;}` 的 value 偏移1，仍 8B store。宿主 GNU x86 `-S` 对 C 8bit output 仅 `movb` 1B；A64 目标未执行。
- **修复验收：** 确定 C output object 的合法物理宽度及符号扩展，分别选择 `strb/strh/str w/str x` 并复验异常表 PC/寄存器保存；统一源端和机端 pointer/const/volatile 准入。目标 A64 GNU GAS/ELF/QEMU 和 packed 周边 guard 值必须实测才可关闭。

### MC-053 RV64I LP64 软浮点只覆盖四则

- **状态：** `OPEN`，P2，`LOCAL-COMPILE-DIFF`；**功能缺口且显式 fail closed**，没有 silent wrong-code 证据。
- **位置与原因：** `src/target/riscv64/isa.c:minic_riscv_isa_admit_core_instruction` 对 LP64 无 F/D 的八类 FLOAT/DOUBLE 四则放行；`src/target/riscv64/fp_emit.c:minic_riscv64_emit_fp_instruction` 的比较、double 取负、转换仍只有硬浮点 opcode；`runtime_libcall.c:minic_riscv64_softfloat_binary_symbol` 只映射四则。`riscv/isa_resolve.c` 正确因 LP64 ABI 屏蔽 codegen F/D；不能直接放开指令资格。
- **10×4 编译差分：** 合法 GNU C 十例 `negd,cmpeq,cmplt,cmple,froms,fromu,tos,tou,ext,truncdf`：RV32I/ILP32 `10/10` -S PASS；RV64I/LP64 `0/10`；RV64IMAFD/LP64 `0/10`；RV64IMAFD/LP64D `10/10`。失败提示 `fadd.d` 占位 F/D 需求，不代表编译器偷偷发了 F/D。
- **对照/验收：** RV64I i128 运算 9/9、binary128 long double 运算 9/9 `.s`，仍非目标运行；RV32I i128 是已知不支持。为剩余 10 类实现软 libgcc conversion/compare/negate 及 LP64 return/args ABI，保存 helper CALL 的 `ra`，重跑真实 GNU RV64 assembler/link/QEMU 及旧 RV64 stage4/Linux 回归。

### MC-054 Inline symbolic 恒假表达式残留

- **状态：** `DEBT`，P3，`SOURCE-TRACE`；`src/compiler/inline_symbolic.c:35–40:minic_inline_symbolic_static_expression_depth` 中 `operand != NULL && operand->kind == MINIC_EXPRESSION_LOCAL && false` 恒假，无可执行消费者分支，属于旧调试/保守判断残留。
- **边界：** 该函数**自身仍被**符号特化调用，周边 legacy fallback 有真实行为，不得以一处 `&& false` 删除函数或重构整个 Inline。单独移除恒假分支需确保注释解释为何静态 LOCAL 深度不被认可，并复跑 clone identity/旧 fallback 测试。

### MC-055 Core verifier 内部辅助数组形状先决条件

- **状态：** `INVESTIGATING`，P2，`CORE-API-ASAN`（仅内部 API 与人为构造）——已有真实错误访问证据，但尚未找到任何合法 C→正常 Core producer 触发，不声称现有 GNU C wrong-code。
- **位置：** `src/core/core_ir_verify.c:1138–1168:minic_core_function_verify` 对 globals/callees/call_signatures/inline_asms/call_arguments/objects/values/instructions/blocks 检查 `storage_shape_is_valid`，却遗漏 `fixed_register_bindings`、`enum_types`、`function_symbols`。`instruction_is_valid` 对 FIXED_REGISTER 与 FUNCTION_ADDRESS 等 opcode 之后可能按未认证形状索引前两类指针（enum metadata 另被 Core accessor 使用）。
- **ASan 证据：** `/tmp/minic_core_verify_shape_probe.c` 直接链接四个官方 Core/Frontend `.c`，`api-valid-fixed` 与 `api-valid-enum` 使用公开 add/set/verify API 都 `verified=1 count=1 capacity=8`；人为将 `binding_count=1` 且 pointer=NULL 的 `malformed-fixed` 在 verifier:325 ASan SEGV；人为令 enum_count>capacity 的 `malformed-enum` 被 verifier 错误接受、后续 `minic_core_function_effective_integer_type` ASan heap-buffer-overflow。
- **纯 API 元数据完整性：** 不用手改数组内部形状，仅通过公开 add 指令 API 给 INTEGER_CONSTANT 结果设置 `minic_type_enum(123)` 而不调用 add_enum_type，Core verifier 返回 true；相反未注册 fixed binding 的 READ 按 ID 正确被拒绝。这说明 enum metadata 资格需明确 API 先决条件，但尚未证明 Parser/Core 正常 lowering 会遗漏它。
- **下一步：** 把 `function_symbols` 形状负例一起跑，明确公开 Core 构造 API 是否保证其所有类型 metadata 都已显式登记；若承诺 malformed 只返回 false，应补三数组 preflight 和 enum identity 校验；以原生 ASan、Core verifier API 和真实 GNU C 回归后再改状态。

### MC-056 运行时 Record 顺序初始化器跨扩容借用

- **状态：** `OPEN`，P0，`ASAN`，已在**五目标**合法 GNU C 上按容量相位重现 `heap-use-after-free`；当前证明的是内存安全问题，非已证运行值错编。
- **位置/链路：** `src/frontend/parser_runtime_initializer.c:1362–1496:parse_positional_runtime_record_initializer_from` 开头借 `program->records[id]` 的 `MinicRecord *record`，解析首字段 `sizeof(struct { int z; })` 在 `:1432` 经 `add_anonymous_record`→`minic_grow_array` 扩容 Record arena，下一字段循环 `:1398 minic_c0_record_field(record,field_index)` 读取旧 header（`ast.c:2194` ASan READ8）。
- **持久反例与容量矩阵：** `tests/compiler/audit/mc056_runtime_record_positional_uaf.c` 具有 `R1..R15` 前置 Record 和普通 `struct Pair pair={sizeof(struct{int z;}),3}`。GCC GNU11 syntax PASS；三种前置数量 14/15/16 的五目标 sanitize `-S`：x86/AArch64 为 **UAF/PASS/PASS**；RV32/RV64/Loong 为 **PASS/UAF/PASS**。相位依赖 `records` capacity，非命中 PASS 不代表安全。默认 15 数量下 GCC native 与 MiniC x86 native 都退出 7，尚未独立观察到输出值差异。
- **不同于 MC-023：** 本项是**运行时局部自动对象、普通顺序字段初始化**，不依赖 static backward zero overwrite、重复 designator 或 GNU diagnostic。两处根因可归为借用指针跨递归扩容，必须在修复批次一起检查，但应分别保留回归。
- **验收：** 从 RecordId 跨 parse 重取、或在证明字段数组生命周期后以值型 header snapshot；全量检查同类 runtime record parser 的字段步进/递归/错误出口；五目标 14/15/16 + 额外扩容周期 ASan、GNU native/ELF/QEMU 再决定关闭。

### MC-057 GNU 空 Record 返回及实参 ABI 缺口

- **状态：** `OPEN`，P2，`LOCAL-COMPILE-DIFF`；是 GNU C extension 的目标 ABI **显式编译拒绝**，非已证静默 wrong-code。
- **有效输入：** `tests/compiler/audit/mc057_gnu_empty_record_abi.c`，分别 `-DTEST_RETURN`（定义空结构体返回函数）、`-DTEST_PARAM`（定义空结构体参数函数）、`-DTEST_CALL_RETURN`（调用空 Record 返回者）、`-DTEST_CALL_PARAM`（调用空 Record 参数者）；四个 case 均通过 `cc -std=gnu11 -fsyntax-only` 和预处理。RISC-V GNU psABI 对支持此扩展的 C 编译器明确规定 empty struct/union 参数和返回在物理 ABI 中被忽略（[psABI 规范](https://riscv-non-isa.github.io/riscv-elf-psabi-doc/)），不是需要凭空分配一个物理返回寄存器。
- **`-S` 六目标×四 case：** RV32I = FAIL/FAIL/PASS/PASS；RV64I/LP64、RV64IMAFD/LP64D、x86-64 均 FAIL×4；AArch64、LoongArch64 均 PASS×4。RV32 的调用侧两项能通过，不能概括为 RV32 全部不支持。
- **结构原因：** `riscv32/rv32_abi_plan.c:rv32_plan_shape` 正确识别 `words=0`，但 `rv32_emit.c:rv32_term` 的返回路径仅在 `record_return_size!=0` 时接管 Record，否则误进入 scalar-only 检查而失败。RV64 `abi.c` 将 empty Record 归为 `MINIC_RISCV64_ABI_VALUE_IGNORE`，而 `core_codegen.c:emit_terminator` 只处理 aggregate/indirect，IGNORE 未实现；x86 的独立拒绝路径仍需逐函数关闭定位。
- **验收：** 将 ABI `IGNORE` 明确映射到无物理参数/返回、无隐藏指针；分别验证定义和调用，兼顾 GNU 空对象的 C 层身份与 Core record 值，追加 GNU GCC 汇编与目标 GNU AS/ELF/QEMU ABI 差分。不要在 Common aggregate shape 中硬造1字或让 x86/RV64 共用机器返回算法。

### MC-058 符号指针加常量偏移的传递特化丢失

- **状态：** `OPEN`，P1，`LOCAL-COMPILE-DIFF`；是 MiniC 生成汇编前的合法 GNU C 接受缺口，尚无该条 MiniC 机器码输出可供执行比较。
- **位置/现象：** `src/compiler/inline_symbolic.c:335–413` 的 transitive symbolic argument closure 识别直接 LOCAL 静态参数事实，但没有表示“symbolic base + C 常量 pointer offset”的可传播事实。`mid_key(int *key){ return leaf_key(key+1); }` 传入的 `key` 已由唯一 caller 专门化为静态全局 `key_slots`，callee 内联 GNU `"i"(key)` 却得不到 `key_slots+4`，因而 `immediate_resolved=0`、Core 拒绝。
- **严格隔离：** `tests/compiler/audit/mc058_inline_symbolic_pointer_offset_direct.c` 与 `_transitive.c` 的 `leaf_key` 完全相同，都是 GNU `asm volatile(".byte 0" : : "i"(key))`；仅直达 `leaf(key_slots+1)` vs 经 `mid(key_slots)`/`leaf(key+1)` 不同。GNU x86 `gcc -std=gnu11 -O2 -c` 两者 PASS；相同 `.i` 的 MiniC `--target=riscv64-linux -S` 直接 case PASS (`direct=1`)，传递 case FAIL (`direct=1 transitive=0 immediate_resolved=0`)。另完整 `.quad %c0` GCC source 可以生成 `.data+4/.data+8/.data+c` relocs，但 MiniC x86 的 opaque-ASM backend 另有失配，**不得把 `.quad` x86 MiniC FAIL 当本问题的单一因果**。
- **独立性/修复门槛：** 仅一个 caller、没有整数形参事实或共享 CALL 的多 caller 污染，故区别 MC-006（跨实例污染）与 MC-008（整数运行时参数禁止符号特化）。需由实例事实承载 base symbol + byte offset，保证指针加法的元素大小/type normalization，保留 caller identity，回归 offsets 0/1/2、两层传递、不同全局及 GNU ASM `i` 的目标 GAS/relocs；不要直接污染共享 AST CALL。

### MC-059 RV64 LP64D 含浮点成员 Union 被误当 FP Aggregate

- **状态：** `OPEN`，P2，`LOCAL-COMPILE-DIFF`；RV64 目标以两种明确 ABI/ISA profile 的 `-S` 对照检测到的可复现实例；目前表现为**合法 GNU C 的显式拒绝**，非静默 ABI 运行错误。
- **规则：** [RISC-V ratified psABI，Procedure Calling Convention](https://docs.riscv.org/reference/abi/v1.0/riscv-cc-procedure-calling-convention.html)明确规定 union **永不 flatten**，始终按整数调用约定传递；不能因为 union 任一字段是 `double`/`float` 就当作可 flatten 的 FP struct。
- **根因：** `src/target/riscv64/abi.c:minic_riscv64_classify_abi_value_profile` 的 `size<=16` Record 分支在 LP64D 使用 `minic_common_aggregate_integral_tree`，后者在 `src/target/common/aggregate_shape.c` 对含 FP 字段的 union 递归返回 false；具名参数、返回、direct call 都在真正机器 ABI placement 前被挡住。无名变参却明确按 integer convention，实测可 `-S`。
- **永久反例：** `tests/compiler/audit/mc059_rv64_lp64d_union_float_abi.c`，独立宏 `PROBE_RETURN/PARAM/FLOAT32/CALL/CONTROL`。GNU11 所有 case 合法且可编译；MiniC `--target=riscv64-linux -march=rv64imafd -mabi=lp64d -S` 前四项 FAIL、纯 integer union CONTROL PASS；同输入 `-march=rv64i -mabi=lp64` **5/5 PASS**。原始更广探针还对比 RV32I/ILP32。
- **区分：** `struct { double d; }` 的 LP64D FP flatten 属另一 ABI 分支，当前未全面实现属于既有能力边界；不可为了开放 union 一概放行所有含浮点的 Record。修复需明确 `is_union` 语义，回归具名/变参、参数/返回/嵌套调用、整数 vs FP 物理寄存器、GNU 目标 GAS/ELF/QEMU。

### MC-060 LP64 两个目标的大聚合 2048 字节硬上限

- **状态：** `OPEN`，P2，`LOCAL-COMPILE-DIFF`；这是能由大小边界单独触发的 source acceptance gap，非已证内存破坏。
- **源/消费链：** `src/target/common/lp64_aggregate.c:minic_multi_indirect_record` 在 Record `shape.bytes > 2048U` 返回 false；AArch64 `abi.c:minic_aarch64_indirect_record`、LoongArch64 `codegen.c` ABI policy 使用该共同判据；`direct_preflight.c:minic_multi_direct_lp64_function_preflight` 与 `direct_call_plan.c` 在发射前拒绝；A64/Loong 的 indirect return machine helper 另有 2048B 上限。这里是真实**共用策略误加源语言大小限制**，并不是 ABI 规定 2KiB 以上对象不合法。
- **边界差分：** `tests/compiler/audit/mc060_lp64_large_record_abi_cap.c` 含 `struct LargeRecord { unsigned char bytes[N]; }` 的按值输入/返回/调用。GNU `-std=gnu11 -Wall -Wextra -Werror -fsyntax-only` 对 N=2048/2049 均通过。MiniC AArch64/LoongArch64 `-S`：2048 **PASS**、2049 **FAIL** `reason=return_abi`；同一 2049B case 的 x86、RV32、RV64 `-S` 均 PASS。这是确认的精确阈值，不是凭代码目测。
- **验收：** 先将“机器是否有动态/计数复制策略”和“ABI 是否接受 Record”拆为不同契约；检查参数/返回/递归调用与 caller-owned hidden return pointer、堆栈对齐、缓冲区长度 overflow；用多个尺寸（16/17/2048/2049/更大）及 GNU 目标 GAS/link/QEMU 验证后才能解除。

### MC-061 Record 字段声明中 Pointer-to-Array 跨扩容借用

- **状态：** `OPEN`，P0，`ASAN`；合法 GNU C **Record 定义阶段**即能触发 `heap-use-after-free`，完全不需局部或静态 initializer，与 MC-023/MC-056 不同入口。
- **失效链：** `src/frontend/parser_record.c:parse_record_field_declarator` 在开头保存借用 `MinicRecord *record`；字段 `int (*field)[sizeof(struct {int z;})]` 经 `parse_pointer_to_array_field_declarator`→`parse_array_declarator_suffix`→`parse_sizeof`→`minic_c0_program_add_anonymous_record` 增长 `Program.records`；返回后 `parser_record.c:443:record_has_field(parser, record, name_span)` 读失效 header（ASan READ8）。
- **持久反例：** `tests/compiler/audit/mc061_record_field_pointer_array_uaf.c`；GNU11 `-fsyntax-only` 与原生可执行控制通过；五目标 ASan `-S` 的前置类型数量 14/15/16：x86/AArch64 = **UAF/PASS/PASS**，RV32/RV64/Loong = **PASS/UAF/PASS**，证明与 arena capacity 相位相关。未触发相位上的 PASS 不代表该借用安全。
- **修复原则：** RecordId 在声明、字段重复名验证、属性/位域解析之间保持稳定；任何调用可 append Record 后必须重新取或短期值型快照，不能把 `const Record*` 当生命周期保证。修复后连同 MC-002/003/023/056 以多目标多容量 ASan 和合法 GNU 类型解析对照验收。

### MC-062 x86 ALT Reloc 的 Memory 输入源机约束分裂

- **状态：** `OPEN`，P2，`LOCAL-COMPILE-DIFF`（另有 GNU ELF relocation 实证）；**不是**内核 alternative 的运行时执行差异。
- **Source 与 Machine 不一致：** `src/target/x86_64/asm_contract.c:x86_linux_alt_reloc_selftest_source` 仅检查 GNU `"m"` memory input 是可寻址 lvalue；`src/target/x86_64/machine_asm.c:emit_linux_alt_reloc_selftest` 却额外要求 Core 定义为 `GLOBAL_ADDRESS` 且符号名固定 `__alt_reloc_selftest_addr`，机器替换段硬编码 `leaq __alt_reloc_selftest_addr(%rip),%rdi`。因此 source contract 接受的合法其他符号无法兑现。
- **差分：** `tests/compiler/audit/mc062_x86_alt_reloc_memory_operand.c` 默认 `other_alt_reloc_addr` 和 `-DALT_GOOD` 固定符号正控均通过 `cc -std=gnu11 -Wall -Wextra -Werror -c`；GNU `readelf -r` 的 `.rela.altinstr_replacement` 对应 `R_X86_64_PC32` 从 `__alt_reloc_selftest_addr` 变为 `other_alt_reloc_addr`，是实际 operand 重定位语义。MiniC GOOD `-S` PASS，BAD `-S` FAIL `kind=43`，没有坏例可执行文件。
- **后续设计：** 明确该特化仅承诺 Linux 固定符号还是泛化 GNU memory-input lvalue：前者应在 Source gate 精准限制且交给符合 ABI 的其他路径，后者应从 Core operand 解析动态 symbol+offset 且保留 replacement PC-relative relocation。不能只在 Machine 中硬编码后把 Source 接收范围当正确。

### MC-063 带标签的裸 Record 声明被误作匿名成员

- **状态：** `OPEN`，P1，`GNU-NATIVE-DIFF`；与仅有 `-S` 差分的问题不同，已有本机实际执行结果错误。
- **位置与语义：** `src/frontend/parser_record.c:parse_record_field` 在 `minic_type_is_record(base_type) && current==SEMICOLON` 时统一向外层 Record 添加匿名字段；但 GNU C `struct Named {int x;}; struct Wrapped {struct Named; int y;};` 的带标签裸类型声明**不声明匿名成员**，GCC `-Wall` 提示 `declaration does not declare anything`。相对地 `struct {int x;};` 是可用的 GNU 匿名 Record 成员，二者必须区分。
- **持久反例：** `tests/compiler/audit/mc063_tagged_bare_record_field_layout.c` 同时检查两种语法。GCC GNU11 x86 native 得到 `sizeof(Wrapped)=4`、匿名 Record 组合大小8，整程序 exit **48**；同一输入 MiniC x86 `-S`→GNU AS/cc→native exit **88**，两个结构都被算作8；五目标 `-S` 都生成错误的 tagged 结构大小8。GCC tagged case 有警告而非错误，不能称为 ISO C 保证。
- **修复与验收：** 区分 tagless anonymous definition、tagged bare declaration、record-tag 类型别名，核对字段布局/访问/初始化/bitfield 和 GCC GNU11 本机 ABI；对不同目标 target DataLayout 另补 GCC 对照，避免直接对后端修正 offset。

### MC-064 Inline ASM 仅符号引用的静态函数被错误裁剪

- **状态：** `OPEN`，P1，`ASM-ONLY`；已证**缺失本地函数定义的生成汇编**，当前无 RISC-V 目标 GNU AS/link/QEMU 验收，不能宣称已经实际链接失败。
- **链路：** `src/core/core_lower_asm_operands.c:core_inline_asm_immediate_text` / `core_inline_asm_specialize_immediates` 将 GNU `"i"(asm_only_target)` 的静态函数引用转成裸字符串；`src/core/core_lower_asm_scalar_forms.c` 形成 `OPAQUE_INLINE_ASM`。`src/compiler/core_reachability.c:minic_walk_reachable_core_edges` 只跟 `CALL`、`FUNCTION_ADDRESS`，`minic_prune_inline_specializations_from_core` 把缺少普通 C 引用的静态函数标记不发射。前端/Lower 已通过，缺失发生于可达性身份转换。
- **持久反例：** `tests/compiler/audit/mc064_asm_only_static_function_reachability.c`。GNU x86 `-O0/-O2` 对象中都保留 LOCAL FUNC `asm_only_target`；MiniC RV64I/LP64 `-S` 输出 `.quad asm_only_target` 但无 `asm_only_target:`。定义宏 `MC064_DIRECT_CALL` 后 MiniC 同时输出 C 调用和函数定义；仅以 ASM 引用的**静态全局变量**也有独立正控，故不能泛化为所有 ASM 符号缺失。
- **修复与验收：** 将 ASM 中 function/object relocation source identity 保持在 Core 结构化元数据中并加入 reachability roots，不能用全文本符号子串不安全地推测身份；验证有/无普通 C 引用、函数别名、unused/used、多个目标 GNU AS/ELF relocation 与真实链接。

### MC-065 x86 R11 跨调用快照误报排除

- **状态：** `CLOSED-NOT-A-BUG`；编号永久占用，不计入 OPEN 缺陷数量。
- **候选：** 一个 x86 原生对照让内联汇编写 R11，另一次内联汇编无显式输入依赖而读取 R11，出现 GCC/MiniC 数值差异。
- **排除依据：** 两段 ASM 间包含 C 函数调用，x86-64 ABI 的 R11 是 caller-saved；读取它的 ASM 没有声明相应输入或保活依赖。此测试不能对 R11 的原值建立有效语义约束，因而不能判为 MiniC wrong-code。只有构造出遵守寄存器约束和调用约定的独立反例才能以**新证据**重开调查。

### MC-066 AArch64 浮点指针字段被误判为浮点聚合

- **状态：** `OPEN`，P2，`LOCAL-COMPILE-DIFF`；是合法 GNU C 的显式拒绝，不是已证 AArch64 运行时错误。
- **源/消费链：** `src/target/aarch64/abi.c:aarch64_special_aggregate_member` 先判断 `type.base_kind` 是否 FLOAT/DOUBLE，再判断 pointer/array/record；`double*` 的 base_kind 仍指向 DOUBLE，故错误报告存在 FP aggregate leaf。`minic_aarch64_indirect_record` 用它排除合法一般整数/pointer Record，preflight 的返回 ABI 拒绝该函数。
- **对照：** `tests/compiler/audit/mc066_aarch64_fp_pointer_aggregate.c`，32 字节 `struct { double *number; unsigned char padding[24]; }` GNU C 编译通过、MiniC `aarch64-linux -S` FAIL；只加 `-DMC066_INTEGER_PTR` 变 `int *number`，对象总长度不变，MiniC PASS。不能因此推断已实现 A64 所有 HFA 参数/返回：本路径只在间接大 Record 分类范围内生效。
- **验收：** 在 target ABI predicate 先判断 type indirection，再做 FP member 归类；回归 float/double 的值字段 vs 各种 pointer/array/typedef、16B/17B/32B/64B Record、GNU AArch64 ABI args/returns 与 GAS/ELF/QEMU。

### MC-067 带 GNU 属性的 Record 前向声明被错判为定义

- **状态：** `OPEN`，P2，`LOCAL-COMPILE-DIFF`；五目标复现的是 Frontend 入口同一个解析错误。
- **位置/失配：** `src/frontend/parser_record.c:minic_parser_parse_record_definition` 使用浅拷贝 lexer lookahead 仅匹配 `struct IDENTIFIER ;` 为 forward，遇 `struct __attribute__((aligned(16))) Forward;` 时错误调度到 `minic_parser_parse_record_definition_specifier`，后者要求 `{`，报 `expected '{' after record specifier`。其余没有属性的 tag forward 路径可用。
- **持久反例：** `tests/compiler/audit/mc067_attributed_record_forward_declaration.c`；GCC `-std=gnu11 -Wall` 正常编译无警告、x86 native exit4；MiniC x86/A64/RV32/RV64/Loong `-S` **0/5 PASS**。`-DMC067_PLAIN_CONTROL` 仅移除属性，同一五目标 **5/5 PASS**。
- **修复与验收：** 将 record declaration-head 的 attribute 和 tag 识别先统一到 tokenizer/parsed-specifier contract，再区分 forward 与 complete definition；覆盖 `struct`/`union`、属性在 tag 前后、重复声明、同名 scope，不要用多个更深的 Parser 浅复制来规避语义状态。

### MC-068 always_inline 未实际展开且外部 Clone 造成 init 段生命周期失配

- **状态：** `DEBT`，**P1 架构正确性债务**，`CI-LOG/SOURCE-TRACE`；尚未修复、未完成 GNU 差分验收，也**没有证据宣称已出现运行时 UAF**。
- **精确证据：** Linux 6.6.143、MiniC exact HEAD `f493adffa6b14d4bd4ac1877c2d6c503f79556aa` 的 [CI #41002539295](https://github.com/yituanxing/minic-toolchain/actions/runs/41002539295)，七路对象编译通过并进入 GNU Kbuild `MODPOST vmlinux.symvers`。GNU modpost 报 `__minic_inline_spec_8859_28+0x104 (.text) -> memblock_alloc_try_nid (.init.text)`、`__minic_inline_spec_6114_72+0xd4 (.text) -> memblock_phys_alloc_range (.init.text)` 等多条 section mismatch；也出现普通 inline wrapper `memblock_alloc_from (.text) -> memblock_alloc_try_nid (.init.text)`。**本次最终 GNU LD 因独立的 `__compiletime_assert_*` 未定义符号失败，不是这些 modpost 警告导致 LD 失败。**
- **源代码链路：** `src/frontend/attribute.c:272–279` 已识别 `always_inline`/`__always_inline__`；`src/frontend/parser_function.c:function_attribute_class_is_parse_only`（约 `:54–68`）将 optimization 属性按 parse-only 处理，函数语义没有持久强制内联标记；`src/compiler/inline_integer.c:minic_add_inline_integer_specialization`（`:36–94`）生成可调用的 `__minic_inline_spec_*` 外部函数，仅在源函数显式有 `section_name` 时继承，其余落入默认 `.text`；`src/core/core_lower_calls.c`（约 `:804`）保留真实 `MINIC_CORE_INSTRUCTION_CALL`，而非将函数体纳入调用方。Linux `include/linux/memblock.h` 中 `memblock_alloc`、`memblock_phys_alloc` 为 `static __always_inline` 包装器，分别调用生命周期有限的 memblock init 函数。**这不是仅某个目标后端的特例；普通 `static inline` 未消除的同类跨段调用也须审计。**
- **架构方向：** 让属性语义从 Frontend 穿过函数实例/声明合并和 Core IR；由统一、目标无关的 Core inliner 实施函数体展开及 CFG/参数/返回值/局部对象映射。`always_inline` 应保证可展开或提供明确不可展开诊断，不应悄然保留 out-of-line 调用；普通 inline 走独立的合法性及代价策略。不能简单把所有 clone 改成 `.init.text` 或关闭 modpost 告警，这会掩盖调用方生命周期边界。
- **验收条件：** 新增持久的 GNU 差分最小用例：正常 `.text` 调用方、`__init` 调用方、`static __always_inline`/普通 `static inline` 多层包装，目标函数在 `.init.text`，以 GCC 和 MiniC→**GNU AS/GNU LD** 分别生成 ELF，严格比较符号、relocation、段间引用和执行结果；mandatory-inline 路径不得遗留可调用的 `__minic_inline_spec_*` 或因未展开而生成 `.text → .init.text` 非法引用。覆盖多出口、局部变量、aggregate、嵌套调用、递归/超预算、asm，在实际支持目标上运行 native/QEMU 差分。以新 exact HEAD 重做 Linux 2064 C-owner 对象 GNU Kbuild 链接、modpost 与同配置 GNU baseline 比较，消除 MiniC 引入的 section mismatch；继而运行 Linux QEMU P0/P1 与同一 Image 连续启动回归。逐项保留日志和构件 SHA；不能以 `-S` 100% 或“编译成功”替代链接/运行验收。关联 [MC-043](#mc-043-inline-整数特化预算耗尽直接拒绝合法-c) 的内联预算语义另行核对。

### MC-069 AArch64 Linux vDSO 在 GNU LD 拒绝动态重定位

- **状态：** `OPEN`，P1，`GNU/MiniC-ELF-AB`。首次非法动态重定位及符号可见性差异已经由原始 GNU `.o` 与 MiniC `.o` 的 `readelf` 同输入差分证实；前端/IR/Target 具体哪个所有权边界遗失 visibility 尚未修复。
- **现有证据：** [CI #41006230438](https://github.com/yituanxing/minic-toolchain/actions/runs/41006230438) AArch64 Linux **4108/4108 项 `MiniC -S`/GNU AS PASS**，原版 GNU QEMU P0/P1 PASS，但候选 GNU Kbuild `LD arch/arm64/kernel/vdso/vdso.so.dbg` 报 `dynamic relocations are not supported`。对同一 `arch/arm64/kernel/vdso/vgettimeofday.o`：原 GNU `.symtab` 的 `_vdso_data`、`_timens_data` 为 `NOTYPE GLOBAL HIDDEN UND`，MiniC 却为 `NOTYPE GLOBAL DEFAULT UND`；GNU 的 `.rela.text` 使用 `R_AARCH64_ADR_PREL_LO21`（共 11 条针对这两个符号的直接 PC 相对引用），MiniC 则对每个符号使用 `R_AARCH64_ADR_GOT_PAGE` 加 `R_AARCH64_LD64_GOT_LO12_NC`，引入 vDSO 不允许的动态 GOT reloc。不可把 4108 汇编覆盖算作链接/启动通过。
- **下一步差分：** 审计 GNU `__attribute__((visibility("hidden")))`、汇编 `.hidden`、声明与实体 visibility 从 Parser→AST/IR→AArch64 符号寻址/ELF 的完整传播；保持 DEFAULT 可抢占符号的 PIC/GOT 语义，HIDDEN 不可抢占符号采用有 GNU ABI 证明的直接 PC 相对寻址。原 Kbuild 命令、链接脚本和 GNU binutils 不变，不靠符号名/vDSO 特例或混入 GCC C 对象掩盖失配。
- **验收条件：** 在确切修复 HEAD 使用全部 MiniC C-owner vDSO 输入完成 GNU LD，无新增非法动态重定位；GNU readelf A/B 及最终 ELF/dynsym/reloc 审计 PASS；内核完整链接后同环境 QEMU P0/P1 PASS，记录 exact-head CI、构件 SHA 和实际 MiniC/GNU 工具边界。

### MC-070 LoongArch64 Linux head.o 对 start_secondary 的 B26 越界

- **状态：** `OPEN`，P1，`GNU/MiniC-ELF-AB`。这是已观测的 GNU LD 相对位移链接阻塞；新差分已证明广泛的 C 机器码体积膨胀，但没有将该溢出的全部地址贡献精确归因给单一生成算法。
- **现有证据：** [CI #41006230438](https://github.com/yituanxing/minic-toolchain/actions/runs/41006230438) 的 LoongArch64 **2723/2723 项 `MiniC -S`/GNU AS PASS**、vDSO GNU LD PASS、主链接在 `head.o .ref.text+0x15c → start_secondary` 的 `R_LARCH_B26` 仍溢出约 **−347.47 MiB**，比旧版 −354.69 MiB 改善约 7.22 MiB。冻结原始 GNU Linux QEMU P0/P1 PASS，MiniC 候选未进入 QEMU。严格同输入 `.o` executable ELF section 汇总：原版 GNU **16,887,804 B**，MiniC **371,965,092 B**，为 **22.03×**；`vmlinux.o .text=365,536,852 B`。增量前三名为 `lib/zstd/compress/zstd_lazy.o` +4.84 MiB、`net/core/filter.o` +4.50 MiB、`kernel/bpf/verifier.o` +3.83 MiB。不能把单纯 `-S`/GNU AS 通过称为链接完成。
- **下一步差分：** 优先利用现有 `src/target/common/core_spill_layout.c` 的 SSA last-use 槽复用替换 LP64 逐 `value_id*8` 独占栈槽，在 AArch64/LoongArch64 共用一个正确性模型；审查 Loong 未知对齐 8B 访存的约 14/20 指令展开（保留 GNU packed 的非对齐语义），以及 Core `OBJECT/FIELD_ADDRESS → LOAD/STORE` 的地址重物化。它们是候选放大器而非已完成的 355MiB 归因。对相同 `.config`、GNU linker script 保留 map/section/relocation/size A/B，不未经证明插 target-only trampoline 或修改 LD 接受范围。
- **验收条件：** 新候选 GNU/MiniC map/readelf/size A/B 定位首个确切偏差并以通用合法规则修复；全部 2723 C-owner 对象由 MiniC 实际生产，GNU LD 完整 PASS 且无越界 relocation；Linux QEMU P0/P1 与 GNU 基线在相同配置中 PASS，保留 exact-head CI、map、ELF 和 SHA。

### MC-071 RV64 Linux 完整 GNU 链接成功但 QEMU 45 秒仅见 OpenSBI

- **状态：** `INVESTIGATING`，P1，`QMP-FAULT + IMAGE-BYTES`；**已观察到 MiniC 候选内核早期实际的 load access fault，但尚未定位造成非法指针的生成指令或 C owner**。运行验收仍 FAIL/INCONCLUSIVE，不能宣称 Linux P0 PASS。
- **已完成：** [CI #41009152382](https://github.com/yituanxing/minic-toolchain/actions/runs/41009152382) 在准确 2064 个 MiniC GNU-E→MiniC-S→GNU-AS 对象、GNU Kbuild link、头文件重协调/第二次完整 GNU LD 后，生成真实候选 Image **150,491,136 字节**（golden GNU **22,033,920 B**）。此前 379 版本也在 [#41004740706](https://github.com/yituanxing/minic-toolchain/actions/runs/41004740706) 完整链接过更大的 232,284,160 B Image；两次都未获得 Linux 运行 P0。
- **QMP 定位：** 当前 Image 的 45 秒 P0 仅见 OpenSBI；对**同一 SHA 镜像、原 2G+DTB+映射 initrd loader**的只读 QMP 在 1/4/8/12/20/30 秒采样完全稳定：`pc=0x802010cc`、`stvec=mepc=0x802010c8`、`satp=0`、`scause=mcause=5`、`stval=mtval=t1=0xffffffff88620a4c`、`sepc=0x883076de`。按同一 Image 物理基址 `0x80200000` 解码，`sepc` 是 **`lbu t0,0(t1)`**，`stvec` 是 `wfi` 及停机回跳。错误地址低 32 位 `0x88620a4c` 落在预期物理映像范围，**疑似不正确的符号扩展**，但当前不能直接认定是 RV64 全局寻址、内联汇编还是 Core 整数→指针转换的错误。`COHORT_QMP_IMAGE_IDENTITY=PASS`，采样前后 Image SHA 不变。P1 没有到达，`COHORT_RESULT=INCONCLUSIVE` 是严格的原测试判定。
- **下一步/验收条件：** 原 artifact 没有同次 `vmlinux/System.map`，物理 PC 直接用虚拟链接符号 `nm` 会得到误导性 `BELOW_LINKED_TEXT`。在下一 exact-head GNU LD 成功时保存 bounded 原链接符号表和异常指令窗口，以正确 load-bias 将 `sepc/ra` 映射到第一个 C 函数及具体 relocation，再用同输入 GCC/MiniC `.i/.s/.o` 证明首个值错误并修正确性；不可硬编码故障地址。保留 2064 strict MiniC C-owner、不回退 GCC 编译，要求 GNU LD、相同 loader 的 QEMU P0/P1 和同 Image 12 次独立 P1 启动全部真实 PASS 后才能标记 VERIFIED。

### MC-072 static inline 的 computed-goto 表指向不存在的 source label

- **状态与边界：** `OPEN` / **P1** / `GCC-DIFF + ASM-ONLY`；**先于本轮 Common reachable-block 过滤就存在**。这是局部标签重定位所有权与 inline 特化克隆的原有缺陷，不能把它归因于新启用的 LP64 CFG 可达块过滤。尚未运行目标 GNU AS/LD/QEMU，不冒称机器级差分已通过。
- **合法 GNU C 复现：** `static inline int dispatch_inline_local(int n) { static void *const jumps[] = {&&alpha, &&beta}; if (n < 0 || n > 1) return 0; goto *jumps[n]; alpha: return 10; beta: return 20; } int caller_one(void) { return dispatch_inline_local(0); } int caller_two(void) { return dispatch_inline_local(1); }`。宿主 GCC `-O0 -c` 与 `-O2 -c` 均 PASS。注意**不能添加 `__attribute__((always_inline))`**：GCC 对静态变量持有局部标签地址的这种函数会合法拒绝强制内联，错误样例不能作为 GNU 兼容性证据。
- **MiniC 最小差分：** 持久样例 `tests/compiler/audit/mc072_inline_computed_label_owner.c`，手工审计脚本 `tests/compiler/audit/probe-mc072-inline-computed-label-owner.sh`（不放入 check-fast，修好后须反转负例预期）。用 `MINIC_CC=... sh tests/compiler/audit/probe-mc072-inline-computed-label-owner.sh` 实测 `MC072_PREEXISTING_NEGATIVE=CONFIRMED`。对同一预处理 `.i` 执行 `minic-cc --target=loongarch64-linux -S input.i -o mini.s`；日志 `MINIC_INLINE_INTEGER_SPECIALIZATION_V0 clones=2`、`CORE_REACHABILITY kept=2`，`.data.rel.ro` 存在 `.8byte .Luser_0` 与 `.8byte .Luser_1`，但整份 `.s` 不存在对应 `.Luser_0:`/`.Luser_1:` 定义；仅有 `__minic_inline_spec_0_0`、`__minic_inline_spec_0_1` 两个函数。AArch64 同样 `.8byte`、RV64 `.dword`、RV32 `.4byte` 复现引用缺定义。x86 的动态 computed goto 当前另有 Core 指令支持缺口，不列为此失败的四目标证据。
- **前后版本确认：** 原 debug 编译器（本轮 LP64 walker 修改前）和新 Release 编译器对相同 LoongArch `.i` 都输出无定义的 `.Luser_0/.Luser_1`，证明不是新裁剪策略引入。Core `ensure_statement_block()` 对 `MINIC_STATEMENT_LABEL` 赋 `source_label_id`，克隆 FunctionBody 的标签身份仍来源共享 AST；`src/core/core_lower.c` 设置 clone 的 `suppress_source_label_aliases=true`，而源函数因专门化后不再被发射。静态局部对象的 ELF 重定位却保留对原 `.Luser_N` 的引用，消费者和被引用函数实例不再属于同一所有权域。
- **修复门槛：** 先明确 `static` 局部跳转表与克隆代码实例的 C/GNU 标签身份、所有者、运行时地址有效期；不能仅全局补一个 `.Luser_N` 别名到任一 clone（不同 caller 常量特化和动态 computed goto 可能需要不同函数体地址）。可选择禁止不合法的克隆并保留一个可调用原函数，或设计每实例一致的 relocation 归属，但必须与 GNU C/ELF 语义逐项验证。覆盖 RV32/RV64/AArch64/LoongArch64 的 `-S`→GNU AS→GNU LD、表项符号定义、两个不同实参 caller、QEMU 差分，且不得退化当前 Linux `__jump_table`/`asm goto` 的标签身份。

### MC-073 Core BLOCK_ADDRESS 目标缺少 reachable root 闭包证明

- **状态与边界：** `INVESTIGATING` / **P2 RISK** / `SOURCE-TRACE + DESIGN-REVIEW`。这是 Core API 级别的潜在不变式缺口，**尚未复现有效 GNU C 生产失败，也没有证据表明现有 Frontend 可触发**；不把该风险当成此次 LP64 reachable-block 优化的已发生回归。
- **已核实的代码条件：** `src/core/core_ir_verify.c` 的 `MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS` 校验只要求目标 `< function->block_count`，并不要求目标 Core block 的 `source_label_id != SIZE_MAX` 或目标在普通 CFG 上可达；`src/core/core_ir.c:minic_core_function_mark_reachable_blocks` 仅从 entry 和 source labels 起步，沿普通 terminator 的 CFG 边扩展，不把存活 block 内的 `BLOCK_ADDRESS` 引用当作附加可达性边。`src/target/aarch64/residual_emit.c`、`src/target/loongarch64/residual_emit.c` 及 RV 发射器会将 `BLOCK_ADDRESS` 生成为目标本地 machine block 符号引用。因此，**如果未来 Core 生产者创建“存活块取地址→无源标签、无 CFG 前驱的块”**，当前筛选理论上会留下符号引用而丢掉定义。
- **为什么现阶段不判实际缺陷：** 现有 GNU C computed goto 的 `&&label` 通过 `src/core/core_lower_expression.c` 调用 `ensure_statement_block`，该函数对 `MINIC_STATEMENT_LABEL` 目标赋予 `source_label_id`；两条 `asm goto` lowering 路径同样对目标调用它。所有 source labels 都被 walker 无条件设为 root，已验证五 ISA `-S`、AArch64/LoongArch64 地址引用/定义闭包和 x86 GNU/MiniC 本机差分。此项记录的是未来手写/重构 Core 生产者绕过当前 lowering 约定的风险。
- **后续门槛：** 如开放其他 Core 生产者，先加入 verifier 或通用机器发射前的 BLOCK_ADDRESS／asm-goto 目标闭包检查，或将存活块取地址关系纳入共享可达性算法；用合法合成 Core 验证目标定义保留，随后检查 CFG 裁剪、GNU AS/LD 与运行语义。不要复制两套 Target 私有补丁。

### MC-074 推测 IF 条件继承未来局部变量事实导致 x86 Linux 拒编

- **状态：** `FIXED-UNVERIFIED`，P0，`LOCAL-COMPILE-DIFF/ELF-ORACLE`；本地已验证真实 GNU 冻结输入的 MiniC `-S` 和 GNU AS，尚待修复提交的 exact-head 三 ISA Linux CI 重新验收，**不宣称已修复全部 917 个 C owner 或已完成 Linux GNU LD/QEMU**。
- **真实原件：** Linux 6.6.143 x86-64 原 GNU `arch/x86/entry/vsyscall/vsyscall_64.o` 的 `original.gnu.i` SHA256 `4a7eec4836d5d769e8518e6a7c4a46a2de3acf07cb6ff6fbf72067236a7949ab`，原 GNU `.o.cmd` SHA256 `b320f29c6bc6937ba092d8aad06961d501678410f72138ee9c9338eb79967e90`，GNU `.o` SHA256 `65f16b4d574e59204fdb6171e666fb786aaf4f4ee189ce3fab08b03feda2d60d`。证据来自 [CI #41009152371](https://github.com/yituanxing/minic-toolchain/actions/runs/41009152371) 的 first8 diagnostic artifact `12957283790`，与早期 a380/85d 失败输入清单的 SHA 一致；不使用重写或缩减 `.i` 冒充原件。
- **严格版本差分：** 同一 `.i`、相同 `--target=x86_64-linux -fshort-wchar -funsigned-char -fno-pie -S`：`a38005e8` PASS、`37944e47` PASS、`85d175b9` FAIL、精确 GitHub `2c7387502d8cb6da80ef167439ff15d543126781` FAIL、本地 `3014fe0` FAIL。旧 2c `trace_emulate_vsyscall` 在保留的 GNU `if (__builtin_expect(!!(__ret_warn_on),0))` 死分支错误尝试降低 UD2 `asm`，`"i" (__flags)` 立即数不可解析，报 `Core IR does not yet support function`。`379→85d` 中 **仅撤销** `src/core/core_lower_if.c` 的 impure-condition `branch_entry_facts` 恢复可使原输入从 FAIL→PASS；仅撤销另一个 `core_lower_local_effects.c` 的 `__builtin_constant_p` 效果扫描变化则仍 FAIL，排除了以 ASM 特判绕过的必要性。
- **通用根因与修复：** `src/core/core_lower_if.c:core_local_constants_prepare_if_branch` 维护所有 FunctionId 作用域的 local-fact 数组（包含源码后续才声明的对象）。此前对 `local->name_span.begin.offset >= condition->span.end.offset` 直接 `continue`，使尚未开始生命周期的 future local 继承 `incoming` 的 known/escaped/program-zero 状态，进而污染 speculative IF 后的表达式条件判断。改为先将该 future local 的整个事实置零，再继续；以后执行到真实初始化器时重新建立事实。无需任何 x86/ASM 特判，保留 RV64 BPF 所需的 `branch_entry_facts`/condition-owned 语义。
- **回归证据：** 使用新改动对同 artifact 的八个原 GNU 冻结 `.i`（vsyscall、AMD iommu/ibs/core、events core、Intel bts/core/ds）执行 MiniC→GNU AS：**8/8 `-S` PASS、8/8 GNU AS PASS**，全部为合法 x86-64 ELF64 `ET_REL`；未为通过测试替换 GNU 来源或重写 C。小型正向回归已增加至 `tests/compiler/c0/gnu_if_condition_local_owner.c` + driver，验证未来局部初始化、nested GNU condition-owner、RV64 不生成 `__compiletime_assert_645` 以及 x86 GNU/MiniC 原生差分；**此小型 positive smoke 旧 2c 亦通过，它不是本项真实历史 red/green 的替代品**。
- **后续验收：** 用修复的 exact-head 重跑原 GNU frozen 2758 C owner 全量 MiniC→GNU AS，定位仍失败的每一个 owner，再运行 GNU Kbuild LD、QEMU P0/P1；并对 RV64 原冻结 `net/core/bpf_sk_storage.i` 的 `diag_get` 重新检查 `__compiletime_assert_645` 不可达。当前尚无该 RV64 原 `.i` 的本地副本，不冒称其已完成 exact-input 差分。

### MC-075 RV64 预MMU从 GOT 取出未重定位的 chosen 链接虚址

- **状态：** `FIXED-UNVERIFIED`，P0，`SAME-IMAGE/QMP/GOT-BYTES`。通用 Target 地址模式修复已在本地代码和 apt-free MiniC `-S` 小样例通过；**尚未在 Linux 6.6.143 完整 GNU AS/LD、QEMU P0/P1 复验，不能标记 Linux 启动 PASS**。与 MC-071 的实际早期 load access fault 是同一事件，MC-075 记录进一步定位的 ELF/GOT 归属与候选修复。
- **同 Image 证据：** [CI #41009152382](https://github.com/yituanxing/minic-toolchain/actions/runs/41009152382) 的原始 `Image` SHA256 `e818626307e0edc35983fba94d90c920221ccaaa669d2c33b5cc66ece8f56d5c`，长度 150491136 字节。RISC-V Image header `text_offset=0x200000`，QEMU 内存基址 `0x80000000`，所以原始 Image 偏移 `0x8620a4c` 的字节 `/chosen\0bootargs\0no4lvl\0no5lvl\0nokaslr\0` 实际物理地址为 **`0x88820a4c`**。出错时 `satp=0`、`sepc=0x883076de` 指向 `lbu t0,0(t1)`，前一条为 `c.mv t1,a0`，`a0=a1=t1=0xffffffff88620a4c`，与源字符串的**链接态高虚址**一致，却不是当前有效物理地址。
- **GOT 实证：** 同一 QMP `gp=0x8917a728`；`gp-0x800=0x89179f28` 对应 Image 偏移 `0x8f79f28`。原始二进制该处前两个 8 字节槽全零，从 `0x8f79f38` 起按 8 字节出现连续链接态符号地址：`0xffffffff8862062a`、**`0xffffffff88620a4c`**、`0xffffffff88620a54`、`0xffffffff88620a5d`……分别落在 `/chosen`、`bootargs`、`no4lvl` 等字符串；高度吻合 RISC-V `.got` 的两零保留槽与 gp 的 +0x800 布局。该高虚址作为一个绝对 GOT 项真实存在于 Image；**还需同试次 `vmlinux` 的 GNU section/relocation、调用者的反汇编追溯 `a0` 的实际来源，才能把具体生产指令闭环证明**。
- **目标层候选根因与通用修复：** `src/target/riscv64/core_codegen.c` 的 `GLOBAL_ADDRESS` / `FUNCTION_ADDRESS` 此前只看 `position_independent`，只要 `-fPIE/-fPIC` 就给**所有符号**发 `.option pic; la`，包括 TU 内定义的静态字符串 `.Lminic_string_*` 和隐藏符号，可能形成启动前不可用的 GOT 入口。改为复用公共 `minic_target_info_elf_object_is_nonpreemptible` / `minic_target_info_elf_function_is_nonpreemptible`：`static`/TU-local 和 `visibility(hidden/internal)` 无论 PIE/PIC 均用 PC-relative `lla`；默认可抢占的外部数据/函数继续 `.option pic; la` 使用 GOT；`-fno-pie` 路径保持 PC-relative，不按 `/chosen` 名字写特例。这与 AArch64 同类 ELF 可见性修复共享判定模型，不复制公共算法。
- **验收门槛：** 持久脚本 `tests/compiler/c0/probe-rv64-pic-glibc-got.sh` 对 `-fPIE`、`-fPIC`、`-fno-pie` 的私有字符串、静态对象/函数、隐藏 extern、默认可抢占 extern 分别断言 `lla` 与 GOT，并接入 `make check-fast`；下轮仍须原 GNU Linux `.i`→MiniC `-S`→GNU AS 验 `R_RISCV_PCREL_HI20`/`R_RISCV_GOT_HI20`，复用同 GNU Kbuild LD 后运行 QEMU P0 和同 Image 12 次独立 P1。保存同试次 `readelf -SW -Wr vmlinux`、相关 `.o.cmd/.i/.s/.o` 与 `nm/objdump`，证明该 `.got` 入口的实际消费者，且不得将缩短/移动/掩盖故障当作通过。

## 已核对并排除的误判（不要在未有新证据时重新打开）

| 项目 | 结论及当前依据 |
| --- | --- |
| `FunctionId` / `ExpressionId` 对应数组下标 | **有意稳定身份**；已发表的 AST ID 准确标识对象，不是两个数组碰巧同序。错误是跨实例写共享 CALL，不是使用整数索引。Core 另有严格尾部事务回退，不能把 AST 的正常 ID 契约机械扩展到所有暂态 Core ID。 |
| RV32 CALL ordinal 的并行数组 | 由同一顺序构造、单调 ID 检查及查找时 `instruction_id/prepared` 校验保证，设计成立。 |
| Core IR append 中的 Block 指针 | 期间增长的是 Instruction/Value/Block 自有 ID 列表，不是 Blocks arena；当前未发现可达 UAF。 |
| Core switch 退出后的 local facts | 唯一调用方在 switch 前后清理已知事实，不会泄漏最后一项 case 的事实。 |
| 静态 Record 初始化器快照 | `MinicRecord record_snapshot` 保存完整的值型头，完成后的字段数组单独分配且保持稳定；实测无对应 UAF。 |
| 词法前瞻的 Parser 浅拷贝 | 38 处中 35 处当前仅做词法/字符串/属性列表前瞻，不修改 owned metadata；不应禁止所有浅拷贝。 |
| x86 Typed/Legacy ASM 并存 | 机器形式资格与实际消费者都存在，属于受控迁移；AArch64 旧全文本 legalizer 已退出当前生产路径。 |
| RV32/RV64 Common GPR cursor | 仅复用八寄存器 word-placement，ILP32/LP64 frame、参数宽度、具体 ABI 仍由目标负责。 |
| RV64 Frame standalone fallback | 生产路径传入完整 ABI plan；standalone 后备有独立消费者，不是两套生产实现。 |
| `continue-on-error`、SQLite status 收集 | 集中 owner outcome 最终裁决，不能视为静默吞错。 |
| AST clone 共用 FunctionBody | Inline clone 按 FunctionId 追加但合法共享源 `body_block` 和 local range；Body ownership 在 clone 前校验。MC-006 是调用实例事实被写进共享 CALL，而非共享树本身有错。 |
| ASM operand binding 双数组 | Backend contract 按序显式设置 `binding.operand_index=i`，Core 发布前检查索引一致；这是生产者/消费者协议。 |
| Core lowering 对失败输出的保护 | `minic_core_lower_function_with_index` 用暂态 lowered 构造并验证，成功才 move 到 output；不能把过程中的 append 当成污染原 Core 对象。 |

## 审计与 CI 口径

- `src/` 当前跟踪 **332 个 C/H 文件**。`src/compiler` 11/11 已全面阅读；Frontend/Core/Target 已全面盘点并深查关键调用链，但仍有未逐行深读文件。Target 账本共 206 项，上轮结果 `Full=7 / Targeted=4 / Pending=195`，`drift=125` 表示旧审查随源码变更失效；账本 `PASS` **不等于代码全审完**。
- GNU Linux 三 ISA × 七路是冻结预处理 `.i` 经 MiniC `-S` 的**发现型编译成功率**；工作流成功退出不等于 `compiled=total`，要看每 shard 的报告，且 campaign 通常需要显式触发。不等同 GNU AS、ELF、QEMU。
- 四架构 runtime workflow **有真实交叉汇编/链接/QEMU 步骤**；不能因某个 source-only native smoke 的假 QEMU 标题就否定这些独立的真实运行证据。
- 任一修复需要精确 HEAD 的目标回归。对运行风险至少区分 `MiniC -S`、GNU AS、ELF/重定位、QEMU/原生执行四种证据层。

## 新条目模板

```markdown
### MC-<NEXT-ID> 一句话描述（按现有最大编号递增）

- 优先级：P0/P1/P2/P3；状态：INVESTIGATING；证据：SUSPICION。
- 最近核对：<commit>；涉及架构：<targets>。
- 位置：`path:line`（必要时附函数名，避免行号漂移）。
- 现象与最小反例：<输入、命令、期望 vs 实际>。
- 生产者/持有者/消费者：<完整路径>。
- 已排除的正常设计：<为什么不是 ABI/模式差异>。
- 下一验证：<GCC/ASan/ELF/QEMU；仍缺什么>。
- 修复/关闭条件：<所需测试、精确 HEAD 记录、commit/PR>。
```

**维护原则：** 缺陷可以先挂起；证据不能凭印象升级；合理设计要明确记为已排除。真正进入修复批次时，把同一根因的条目联动关闭，并在这里记下提交与精确验证结果。
