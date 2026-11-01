# MiniC 缺陷、风险与架构债台账

> **长期维护文档，不是立即修复清单。** 发现问题先记录、核实、分类；是否进入某一批重构另行决定。
>
> 首次建档：2026-10-30；第五轮审计起点：`b0870345b`（`refactor/minic-target-language-boundary`）；之前第四轮基线为 `400f4a611`。
> 范围：MiniC 的 Frontend、Compiler、Core、Target、Driver 和验证流程；不包含 MiniAS、MiniPP、MiniLD、NaSeed。

配套阅读：[全局架构与所有权图谱](minic-global-contract-map.md) · [函数契约审计台账](minic-function-contract-register.md)。

> **第八轮候选线索（2026-10-31，初审 `f6e9eb870d80`、续审 `3d504e7ee4a7`）：**
> [架构总入口的 A8-01～A8-25](README.md#3-本轮新增的待补证据和清理接缝)
> 保存本轮新增的源码接缝、尚缺动态证据和旧实现退役条件。
> 未确认的线索仍为审查索引；其中 x86 mixed-use 已升级 **MC-076**，
> Frontend `va_start` 跨 arena 扩容的真实 ASan UAF 已升级 **MC-077**；
> 现有 MC-001～MC-075 的优先级、证据和已排除项不因清理顺序而改变。

> **第九轮函数级追查：** [A9-01～A9-13](README.md#第九轮函数深挖旧路径消费者与新待证线索)
> 保留新的 `va_start` 递归借用、hidden undefined weak PIC 选路、target ASM typed 迁移和具体旧 API caller 证据；
> 其中 Parser 10 个无调用入口已补到 **MC-031**，Compiler 失败退出遗漏清理关联 **MC-043**，
> 不借新索引重复立永久问题编号或关闭旧项。

> **第十一轮全目录复扫（2026-10-31，基线 `eddd49c`）：**
> [A11 大批清理候选与实证](README.md#第十一轮全编译器复扫实证问题与大量清理候选)
> 在原 77 个不丢失的前提下追加 **MC-078～MC-087**，区分 Native wrongcode、GNU LD、
> GNU/MiniC 编译差分、ASM-only 与已测量的代码体积债；无代码修复、无全量 CI。
> 2026-11-01 的后续独立 RV32 ELF/QEMU 差分补充了 **MC-088**；
> 不将新 Bug 混计为纯清理删行。

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
| [MC-001](#mc-001-符号闭包读取失效的-expression-指针) | P0 | FIXED-UNVERIFIED | ASAN | Compiler | 符号闭包递归后读取扩容前的 Expression 指针；定点修复已通过五目标容量边界测试 |
| [MC-002](#mc-002-record-字段声明中的悬空指针) | P0 | FIXED-UNVERIFIED | ASAN | Frontend | 旧函数指针参数 Record UAF 由 `449eab2` 的稳定 RecordId 重取覆盖；新增独立函数指针容量回归 |
| [MC-003](#mc-003-运行时-record-初始化器中的悬空指针) | P0 | FIXED-UNVERIFIED | ASAN | Frontend | 原 tagged Record 表达式复现已被 MC-056 同根修复覆盖；五目标容量 ASan 与 x86 GNU native 回归通过 |
| [MC-004](#mc-004-属性探测使-parser-tags-失效) | P0 | FIXED-UNVERIFIED | ASAN | Frontend | 移除 attribute 语义浅拷贝；tags 容量边界五目标 SAN/Release `-S` 与 GNU/MiniC x86 原生通过 |
| [MC-005](#mc-005-属性探测使-parser-enum_constants-失效) | P0 | FIXED-UNVERIFIED | ASAN | Frontend | 同源 enum_constants owner 修复；独立枚举容量回归与五目标 SAN/Release `-S` 通过 |
| [MC-006](#mc-006-传递特化污染共享-call) | P0 | FIXED-UNVERIFIED | ELF-ORACLE | Compiler/Core | 传递符号特化保留 source CALL，Core 按 caller 实际事实择 clone；正反 caller 顺序均经双 key ELF reloc 验证 |
| [MC-007](#mc-007-整数与符号特化无法组合) | P1 | OPEN | GCC-DIFF | Compiler/Core | 特化阶段间丢失调用实例的整数事实 |
| [MC-008](#mc-008-符号特化过度要求整数参数常量化) | P1 | OPEN | GCC-DIFF | Compiler | 符号立即量与运行时整数无法共存 |
| [MC-009](#mc-009-表达式探测回滚遗漏-owned-call-参数) | P1 | FIXED-UNVERIFIED | SOURCE-TRACE/SAN | Frontend | `expression_count` 回滚前清理 owned CALL tail；真实 12 参数 probe 五 ISA `-S` 与 GNU/MiniC native 通过 |
| [MC-010](#mc-010-loongarch-特殊-asm-写回绕过对齐保护) | P1 | OPEN | ASM-ONLY | LoongArch64 | 13 个特殊结果写回槽未走安全 store helper |
| [MC-011](#mc-011-source-only-的-qemu-结果报告不实) | P1 | OPEN | CI-LOG | CI | Source-only 仍报告 `GNU_QEMU=2` |
| [MC-012](#mc-012-source-only-的-elf-结果报告不实) | P1 | OPEN | CI-LOG | CI | `.s` 字符串检查被表述为 ELF reloc 验证 |
| [MC-013](#mc-013-native-gate-的-测试触发白名单缺口) | P2 | OPEN | SOURCE-TRACE | CI | 修改 review-batch 编排未必触发 native gate |
| [MC-014](#mc-014-rv32-变参与过对齐栈帧交界) | P0 | OPEN | ASM-ONLY | RV32 | 合法 aligned(64) 变参函数：保存区与 caller stack 不连续；零剩余 GPR 的首址也错位 |
| [MC-015](#mc-015-rv64-变参与过对齐栈帧交界) | P0 | OPEN | ASM-ONLY | RV64 | 合法 aligned(64) 变参函数：保存区跨向 caller stack 的物理地址错位 |
| [MC-028](#mc-028-rv64-的-int128-移位计数类型限制) | P2 | OPEN | LOCAL-COMPILE-DIFF | RV64 | 合法 GNU C 的宽移位计数导致 MiniC `-S` 拒绝，窄计数可过 |
| [MC-033](#mc-033-core-独立自增自减语句保留过时常量事实) | P0 | FIXED-UNVERIFIED | GNU-NATIVE-DIFF | Core | 原语句捷径过时 fact 错码已被现行 invalidation 覆盖；新增五目标与原生运行回归 |
| [MC-034](#mc-034-静态-record-设计符查找对无关对象做指针相减) | P1 | FIXED-UNVERIFIED | ASAN-POINTER-PAIR | Frontend | 静态设计符用稳定 RecordId 重取 Program-owned 描述符；六相位五目标 SAN/Release 及 GNU/MiniC native PASS |
| [MC-035](#mc-035-运行时范围初始化器借用-expression-跨扩容) | P0 | FIXED-UNVERIFIED | ASAN | Frontend | GNU range initializer 的 Expression arena UAF 已用源 span 按值快照修复；五目标容量门禁通过 |
| [MC-036](#mc-036-record-条件表达式的-local-facts-分支合流缺口) | P1 | FIXED-UNVERIFIED | GNU-NATIVE-DIFF | Core | Record 条件式现有分支 fact snapshot/restore/meet，十路径 GNU/MiniC x86 native 及五目标 `-S` 通过 |
| [MC-037](#mc-037-loong-特殊-asm-源端与机器端限定符不一致) | P2 | OPEN | LOCAL-COMPILE-DIFF | LoongArch64 | 合法 volatile cpucfg 输出仅因限定符在机器层拒绝 |
| [MC-038](#mc-038-匿名字段设计符后的位置初始化游标错误) | P1 | FIXED-UNVERIFIED | GNU-NATIVE-DIFF/RV64-QEMU | Frontend | 匿名/嵌套设计符后按真实 Record 成员路径续接游标；13 例双编译器 x86 native 与 RV64 QEMU 通过 |
| [MC-039](#mc-039-运行时嵌套数组递归跨-array-type-扩容借用) | P0 | FIXED-UNVERIFIED | ASAN | Frontend | ArrayType 元数据按值快照；五目标容量相位 ASan 与 x86 GNU native 对照通过，完整目标验收待做 |
| [MC-041](#mc-041-long-double-字面量提前按-binary64-舍入) | P0 | OPEN | GNU-NATIVE-DIFF | Frontend/Target | `1.0000000000000001L` 提前按 double 舍入；x86 执行得到错误比较结果 |
| [MC-042](#mc-042-packed-零宽位域丢失对齐栅栏) | P1 | FIXED-UNVERIFIED | GNU-NATIVE-DIFF | DataLayout | 保留匿名零宽位域自然对齐栅栏；9 组 GNU layout 断言、五 ISA `-S` 与 x86 原生通过 |
| [MC-043](#mc-043-inline-整数特化预算耗尽直接拒绝合法-c) | P1 | OPEN | LOCAL-COMPILE-DIFF | Compiler | 2048 个 clone 预算由优化上限变成对合法 C 的硬编译失败 |
| [MC-044](#mc-044-函数参数列表中的-record-enum-tag-作用域外泄) | P1 | FIXED-UNVERIFIED | GNU-NATIVE-DIFF | Frontend | prototype/definition/nested callback 形参 tag 改由独立 scope 持有，14 例五 ISA `-S` 与 x86 native 通过 |
| [MC-045](#mc-045-loong-futex-cmpxchg-覆盖-gnu-可读写输出初始值) | P1 | OPEN | ASM-ONLY | LoongArch64 | GNU `+r` 输入原值未被消费，机器特化无条件写回 0 |
| [MC-048](#mc-048-rv32-指针到-64-位整数的合法转换被拒绝) | P2 | FIXED-UNVERIFIED | GNU-RV32-ELF-QEMU | RV32 | 直接 pointer→u64/s64 已按 GNU RV32 **符号扩展**发射；目标 QEMU 对照通过 |
| [MC-023](#mc-023-静态聚合后向覆写跨递归借用-record) | P0 | FIXED-UNVERIFIED | ASAN | Frontend | `a4b8b71` 后向静态 Record 覆写跨递归重新取 RecordId；五目标容量与 GNU/native 定点 PASS |
| [MC-049](#mc-049-静态链式数组设计符跨-arraytype-扩容借用) | P0 | FIXED-UNVERIFIED | ASAN | Frontend | `f53f9a9` 链式设计符跨递归按值缓存 ArrayType 信息；五目标三相位、GNU/native PASS |
| [MC-050](#mc-050-x86-六组-asm-限定符源端与机器端不一致) | P2 | OPEN | LOCAL-COMPILE-DIFF | x86-64 | 六种合法 GNU ASM 的 volatile 结果被机器端不一致准入拒绝 |
| [MC-051](#mc-051-long-double-一元取负丢失有符号零) | P1 | FIXED-UNVERIFIED | GNU-NATIVE-DIFF/RV64-QEMU | Core | `long double` 一元符号翻转改为 x87 `fchs` 或 TF `__negtf2`，x86/native 与 RV64/QEMU 定点通过 |
| [MC-052](#mc-052-aarch64-uaccess-窄输出按八字节写回) | P1 | FIXED-UNVERIFIED | A64-SOURCE | AArch64 | 窄 C 输出的写回宽度已按对象大小选择；GNU A64 ELF/QEMU 尚未验收 |
| [MC-053](#mc-053-rv64i-lp64-软浮点只覆盖四则) | P2 | OPEN | LOCAL-COMPILE-DIFF | RV64 | LP64 已实现软四则，但比较、取负、转换的 10 个合法函数均无法 `-S` |
| [MC-056](#mc-056-运行时-record-顺序初始化器跨扩容借用) | P0 | FIXED-UNVERIFIED | ASAN | Frontend | RecordId 重新获取、字段按值快照；五目标容量相位 ASan 与 x86 GNU native 对照通过，完整目标验收待做 |
| [MC-057](#mc-057-gnu-空-record-返回及实参-abi-缺口) | P2 | OPEN（RV32 子项 FIXED-UNVERIFIED） | GNU-NATIVE/5ISA-S | RV32/RV64/x86 | RV32 空 Record 定义/返回已过多路径回归；RV64、x86 独立拒编仍 OPEN，不能整项结案 |
| [MC-058](#mc-058-符号指针加常量偏移的传递特化丢失) | P1 | OPEN | LOCAL-COMPILE-DIFF | Compiler | 唯一 caller 的 static pointer+offset 经 inline wrapper 后无法解析 GNU ASM `i` |
| [MC-059](#mc-059-rv64-lp64d-含浮点成员-union-被误当-fp-aggregate) | P2 | OPEN | LOCAL-COMPILE-DIFF | RV64 ABI | LP64D union 中浮点字段不能触发 FP flatten；具名参数/返回四种 GNU 用例错拒 |
| [MC-060](#mc-060-lp64-两个目标的大聚合-2048-字节硬上限) | P2 | OPEN | LOCAL-COMPILE-DIFF | Common LP64/A64/Loong | 合法 2049B Record 的按值参数/返回在 A64/Loong preflight 错拒，2048B 通过 |
| [MC-061](#mc-061-record-字段声明中-pointer-to-array-跨扩容借用) | P0 | FIXED-UNVERIFIED | ASAN | Frontend | `449eab2` 递归 field declarator 后按 RecordId 重新取指针；五目标×三容量 ASan、GNU/MiniC native 通过，完整 Linux 未验 |
| [MC-062](#mc-062-x86-alt-reloc-的-memory-输入源机约束分裂) | P2 | OPEN | LOCAL-COMPILE-DIFF | x86-64 ASM | Source 允许任意 GNU m lvalue，Machine 硬编码唯一符号，合法替代重定位遭拒 |
| [MC-063](#mc-063-带标签的裸-record-声明被误作匿名成员) | P1 | FIXED-UNVERIFIED | GNU-NATIVE-DIFF | Frontend/DataLayout | `6aac3c1` 区分 direct tagless anonymous 与 tagged/typedef bare declaration；GNU/MiniC x86 native exit48 同值，完整目标 ABI 未验 |
| [MC-064](#mc-064-inline-asm-仅符号引用的静态函数被错误裁剪) | P1 | OPEN | RV64-ELF-LINK-DIFF | Core Reachability | GNU ASM-only static 函数符号被降成文本后丢失可达性边；同源 GNU ELF/QEMU PASS，MiniC GNU AS PASS、GNU LD undefined，等待 typed Core 引用方案 |
| [MC-066](#mc-066-aarch64-浮点指针字段被误判为浮点聚合) | P2 | FIXED-UNVERIFIED | GNU-NATIVE/5ISA-S | AArch64 ABI | 现基线 pointer/array 在 FP base_kind 之前分类；134 项五目标 -S 与四种尺寸 GNU native PASS，A64 FP HFA 仍 fail-closed |
| [MC-067](#mc-067-带-gnu-属性的-record-前向声明被错判为定义) | P2 | FIXED-UNVERIFIED | GNU-NATIVE-DIFF/SOURCE | Frontend | `072ca74` 识别前/后置 GNU 属性的文件/块作用域 struct/union forward；GNU/MiniC x86 native 0/4、四目标 -S PASS，完整矩阵未验 |
| [MC-069](#mc-069-aarch64-linux-vdso-在-gnu-ld-拒绝动态重定位) | P1 | OPEN | GNU/MiniC-ELF-AB | AArch64 Linux vDSO ELF visibility | 4108 项 MiniC `-S`/GNU AS 通过；GNU 的 HIDDEN 未定义数据符号经 ADR_PREL，而 MiniC 错为 DEFAULT+GOT，GNU LD 拒绝动态重定位 |
| [MC-070](#mc-070-loongarch64-linux-heado-对-start_secondary-的-b26-越界) | P1 | OPEN | GNU/MiniC-ELF-AB | LoongArch64 Linux 链接/体积 | 2723 项 MiniC `-S`/GNU AS 通过，主 GNU LD B26 越界；同输入 C executable ELF 代码约 16.89MB(GNU)→371.97MB(MiniC)，膨胀 22.03× |
| [MC-072](#mc-072-static-inline-的-computed-goto-表指向不存在的-source-label) | P1 | OPEN | GCC-DIFF/ASM-ONLY | Compiler/Core/Target | 有效 GNU `static inline` 的静态 `&&label` 表引用 `.Luser_N`，整数特化克隆发射但原函数被裁剪；源标签别名没有任何定义 |
| [MC-076](#mc-076-x86-early-head-同一全局符号混合物理访问与链接-vma-地址错编) | P0 | OPEN | GNU-NATIVE-DIFF/ELF-ORACLE | x86-64 early head | 同一 global 用作 pre-MMU 物理存储与链接 VMA 取址时，函数级 per-global 布尔选路把两种地址都发成 RIP-relative；GNU 异址执行 2/2 PASS、MiniC 2/2 FAIL |
| [MC-077](#mc-077-va_start-首参递归使-function-arena-借用失效) | P0 | FIXED-UNVERIFIED | ASAN | Frontend/Loong builtin | `parse_builtin_va_start` 跨递归解析借用失效的真实 UAF；缓存 `parameter_count` 后原 sanitizer 复现通过，已增持久 probe，尚缺全目标回归 |
| [MC-078](#mc-078-x86-pat-clflushopt-源端接受却被机器端拒绝的-unused-立即数) | P2 | FIXED-UNVERIFIED | GNU-NATIVE-DIFF | x86-64 GNU ASM | 历史 `"i"(1)` 错拒已修；GNU/MiniC `0/1/-1` 原生及负例 PASS，全矩阵待验 |
| [MC-079](#mc-079-core-取址解引用抵消绕过-local-事实逃逸造成静默错编) | P0 | FIXED-UNVERIFIED | GNU-NATIVE-DIFF | Core local facts | 历史错编 GNU exit22 / MiniC exit11；当前 HEAD 的持久 GNU/MiniC native 回归通过，完整矩阵待验 |
| [MC-080](#mc-080-函数指针间接变参调用拒绝合法的-aggregate-实参) | P2 | OPEN | LOCAL-COMPILE-DIFF | Frontend/Core/Target call ABI | 函数指针的 GNU `(...)` 非具名参数为 Record 时，五目标 `-S` 0/5；同一 Record 的 direct variadic 和 indirect fixed 各 5/5，通过对照隔离门槛 |
| [MC-081](#mc-081-rv32-接受-gnu-constructor-却漏发初始化注册) | P1 | FIXED-UNVERIFIED | GNU-QEMU-DIFF/ELF32 | RV32 global/function emission | RV32 缺 `.init_array` 已修，GNU/MiniC 真 ELF32/QEMU 双 constructor PASS，扩展属性/完整 Linux 待验 |
| [MC-082](#mc-082-x86-sysv-三字节-record-按值参数与返回被拒绝) | P2 | FIXED-UNVERIFIED | GNU-ELF-NATIVE | x86 SysV ABI | 1..16B 自然对齐 INTEGER-class Record 参数/返回完成双向 GNU ELF native 对照 |
| [MC-083](#mc-083-已完整声明的-char-数组再以省略边界字符串定义被错拒) | P2 | FIXED-UNVERIFIED | GNU-NATIVE-DIFF | Frontend external array declarations | `char a[4]; char a[]="abc";` 经 `15b08f4` 修复，GNU/MiniC 原生差分及超长负例 PASS，完整矩阵待验 |
| [MC-084](#mc-084-x86-record-copy-逐字节展开带来可观机器码膨胀) | P2 | DEBT | GNU-NATIVE-ABI+SIZE | x86-64 aggregate machine emission | 合法 4096B 赋值双方 GNU native 执行均正确，但 MiniC 源汇编 8223 行、`.text` 65264B，GNU 仅 65 行、`.text` 5485B（含相同 driver） |
| [MC-085](#mc-085-gnu-errorwarning-函数属性被保留但没有可达调用诊断消费者) | P1 | OPEN | LOCAL-COMPILE-DIFF | Frontend/Compiler/Core diagnostics | GNU `__attribute__((error))` 活 CALL 应拒编，MiniC 发出 CALL 且成功；`warning` 活 CALL 的 GNU 诊断被 MiniC 静默丢弃 |
| [MC-086](#mc-086-x86-pic-隐藏的未定义-weak-对象与函数错用-pc32-重定位) | P1 | FIXED-UNVERIFIED | GNU-LD/ELF-ORACLE | x86 ELF/PIC symbol binding | `866fa28` 用 TargetInfo 已有 undefined-weak 事实修复 PIC GOT 选路；GNU/MiniC GNU AS/LD PIE 2×2、ELF relocation/native PASS，完整 Linux 矩阵未验 |
| [MC-087](#mc-087-common-lp64-单函数-257-条合法-inline-asm-硬拒) | P2 | FIXED-UNVERIFIED | LOCAL-COMPILE-DIFF | Common Direct LP64 preflight | 257 人为上限已移除，A64/Loong 255/256/257/512 源汇编与伪造 Core 负例 PASS；目标 GNU ELF 未测 |
| [MC-088](#mc-088-rv32-宽整数条件分支只检查低-32-位造成静默错编) | P1 | FIXED-UNVERIFIED | GNU-QEMU-DIFF/ELF32 | RV32 Core terminator | 64 位条件仅高 32 位非零时被当作假；修复前 GNU ELF32 QEMU PASS、MiniC exit11；修复后 8 组双 ELF32/QEMU PASS |
| [MC-089](#mc-089-x87-long-double-大于比较方向错误) | P0 | OPEN | GNU-NATIVE-DIFF | Core/x86 x87 | 普通 `2.0L > 1.0L` GNU 原生返回 42、MiniC x86 GNU AS/LD 原生返回 1；独立于字面量精度 MC-041 |

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
| [MC-031](#mc-031-parser-name-bound-无消费者的旧-api) | P3 | FIXED-UNVERIFIED | SOURCE-TRACE/TARGETED-BUILD | Parser 10 个无消费者入口和 2 个级联 helper 已在当前工作树退役，含整个 `parser_type_query.c` 与 Makefile 项；尚待跨目标回归 |
| [MC-040](#mc-040-嵌套-switch-labels-重复全量扫描) | P3 | DEBT | SOURCE-TRACE | 同一 nested switch 先检测再构造，两次收集标签 |
| [MC-046](#mc-046-aarch64-两个特殊-asm-路径引用默认-target-layout) | P2 | DEBT | SOURCE-TRACE | A64 特例借用默认 RV64 DataLayout，目前字段相同但接口错误耦合 |
| [MC-054](#mc-054-inline-symbolic-恒假表达式残留) | P3 | DEBT | SOURCE-TRACE | Inline 静态表达式深度函数保留无消费者意义的 `&& false` 分支，周边 fallback 仍活跃 |
| [MC-068](#mc-068-always_inline-未实际展开且外部-clone-造成-init-段生命周期失配) | P1 | OPEN/DEBT | CI-LOG/GNU-NATIVE-DIFF | `always_inline` 只解析未落实强制展开；另有 return_address 原生差分和 modpost section mismatch |

## 条目详情与验收门槛

### MC-001 符号闭包读取失效的 Expression 指针

- **位置：** `src/compiler/inline_symbolic.c:135–333`，第一次明确读取在 `:250`；扩容点 `src/frontend/ast.c:516`。
- **证据：** ASan `heap-use-after-free`，`READ of size 208`。递归闭包保存 `program->expressions[id]` 的地址，递归追加表达式引发 `realloc`，返回后仍执行 `closed = *expression`。单、双符号参数和多个 `PAD_COUNT` 扩容边界均可复现。
- **第十一轮新独立正例：** `/tmp/minic-f11-symbolic-recursion.c` 的嵌套 inline helper `f(1, &g[i])` 由整数 caller 特化进入 symbolic closure，普通 Debug x86/RV64 编译器均 SIGSEGV 139；现有 ASan 编译器报告相同 `inline_symbolic.c:250` use-after-free（由 `ast.c:524` 的 Expression arena 扩容释放）；GNU 编译并执行返回测试预期 7，三个去除闭包条件的对照源码可编译。**属于本 MC-001 的新复现，不再新开一项。**
- **关联风险：** `inline_symbolic.c:748–825` 在实参循环外借用 `nested` 指针，同样可能跨表达式追加，但此位置尚未独立动态复现（内部 UAF 会先触发）。
- **修复门槛：** 使用稳定 `ExpressionId` 保存子节点身份，并在任何可能追加之后重新获取地址；双参数场景另补外层 `nested` 复测。避免递归层层拷贝 208B Expression，顾及 RV64 自举栈预算。
- **2026-11-01 独立定点修复，状态 `FIXED-UNVERIFIED`：**
  - `src/compiler/inline_symbolic.c` 的 unary/subscript/member/binary 递归分支均在递归前保存稳定的子节点 `ExpressionId`，递归后按父 `ExpressionId` 重新取得当前地址，再复制 `MinicExpression` 并改写子 ID；无需逐层 240B 的 AST 栈快照。transitive CALL 的实参循环也改用稳定 `nested_expression_index` 每次重新取 CALL，以覆盖首参导致 Expression arena 扩容、后续实参再读取的场景。
  - 永久回归 `tests/compiler/c0/mc001_symbolic_closure_arena.c` / `probe-mc001-symbolic-closure-arena.sh`，接入 `check-mc001-symbolic-closure-arena` 与 `check-fast`，包含单符号参数及双参数嵌套闭包。**修复前**在此文件 `PAD_COUNT=13` 单参数、x86_64 `-S` 的独立 ASan 编译器明确报 `inline_symbolic.c:250` **READ 240B heap-use-after-free**，释放栈由 `ast.c:524` arena `realloc`；不只是源代码推测。
  - **修复后 SAN+UBSan Release `-Werror`：** `PAD_COUNT={0,12,13,14,29,30,31,62,63}` × 单/双参数 × x86_64/RV32/RV64/AArch64/LoongArch64 **90/90** 通过 `-S`；GNU/MiniC x86_64 原生对照 **6/6** exit0，transitive clone 日志确认真正运行闭包路径。另对 x86_64 使用 SAN 扫描 padding **0..255 × 单/双参数 = 512/512** 通过。邻近 `probe-inline-symbolic-shared-body.sh`、`probe-symbolic-inline-two-callers.sh` 通过。
  - **未达全面 VERIFIED：** 其他四目标尚未做 GNU/本工具链 ELF、链接/QEMU 运行；完整 Linux corpus 和 MiniAS/MiniLD 联合验收未做。`run-inline-functions.sh` 仍断言 `add_one/add_two` 不可达 inline body 保留 emitted symbol（当前产物只保留 `main`）；用未修改 `508622d` 的 Release `minic` 对照重跑同样 exit1，属于已有脚本文本锚点漂移，本次未将其计为通过或回归。

### MC-002 Record 字段声明中的悬空指针

- **位置：** `src/frontend/parser_record.c:385–443`，`record_has_field()` 在 `:443` 读旧地址；`src/frontend/ast.c:1136` 使 Record arena 扩容。
- **证据：** 带函数指针字段、嵌套 `struct Inner { ... }` 参数类型的有效 GNU C 输入；ASan 在 RV32、RV64、LoongArch64 复现 UAF。x86/AArch64 同一容量边界未触发，**不能据此宣称安全**。
- **状态：** `FIXED-UNVERIFIED`（尚缺完整五架构 ELF/QEMU/Linux 与 MiniAS/MiniLD 联合验收）。
- **与 MC-061 的关系：** 并非新的一处待修生产指针：同样是 `parse_record_field_declarator` 在递归 declarator 之前借用 `Record*`，后续 `record_has_field` 读旧地址。MC-061 修复 `449eab2`（原隔离提交 `33ac095`）已按稳定 `record_id` 在递归返回后重取 Record，**覆盖 MC-002 的函数指针参数写法**；没有再为同一 bug 叠加生产代码修改。
- **独立复现与回归：** `tests/compiler/audit/mc002_record_field_function_pointer_uaf.c` 保留 `int (*callback)(struct Inner { int value; } *inner)` 原形。修复前 `72e301d` 的 ASan/UBSan `-Werror` 编译器以容量 14 的 x86 触发 `heap-use-after-free`，`record_has_field` READ8；释放栈从 `minic_parser_parse_parameter_list`→`minic_parser_parse_record_definition_specifier`→`minic_c0_program_add_record`→`realloc`，与已报告 MC-002 同根。当前修复版五目标×14/15/16 三种容量相位 ASan/UBSan **15/15 PASS**，GNU/MiniC x86 原生 exit7，重复字段负例双方拒绝；`check-record-field-function-pointer-realloc` 已纳入 `check-fast`。GCC 接受该 GNU11 定义，但会提示 prototype-scope tag 不对外可见，所以单独运行对照时关闭宿主编译警告，不改变 MiniC `-Werror` 构建。该测试与 MC-061 的 pointer-to-array 测试分别固定两条递归入口。

### MC-003 运行时 Record 初始化器中的悬空指针

- **状态：** `FIXED-UNVERIFIED`，P0，`ASAN`。旧版真实 `heap-use-after-free`，**与 MC-056 是同一运行时顺序 Record 字段循环的同一根因，不是另一处需要重复打补丁的生产代码**。
- **位置/复现：** 原 `src/frontend/parser_runtime_initializer.c:1409:parse_positional_runtime_record_initializer_from` 持有旧 `Record*` 跨 `sizeof(struct Inner { int z; })` 的嵌套解析；`src/frontend/ast.c:1144` 重新分配 Record arena，下一字段的 `minic_c0_record_field` 读取已释放内存（`ast.c:2185`）。GNU GNU11 接受 `struct Outer outer = { sizeof(struct Inner { int z; }), 22 };`。
- **原版强证据：** 独立旧 worktree `b8b3e55`、sanitize `-Werror`，原 tagged-Record fixture 在 15 个前置类型、RV64 `-S` 的 ASan 报告 `heap-use-after-free`，READ 8B；栈从 `parse_sizeof` 到 `minic_c0_program_add_record` → `realloc`，随后在下一字段读取旧 Record。此前 RV32/RV64/Loong 的原始记录保持有效。
- **已有生产修复：** `e36794d` 已为 MC-056 将同一循环改为每次按稳定 `RecordId` 重新获取 Record 并在递归前按值保存 `MinicRecordField`；对 MC-003 原写法自然生效。**本 MC-003 专项没有再改生产代码**。
- **新增永久回归：** `tests/compiler/audit/mc003_runtime_record_tagged_uaf.c`、`tests/compiler/c0/probe-runtime-record-tagged-arena-growth.sh`，`check-runtime-record-tagged-arena-growth` 已加入 `check-fast`。当前 sanitize `-Werror` 与 Release `-Werror`：五目标 × 14/15/16 arena 相位均 **15/15 PASS**；GNU/MiniC x86_64 原生三个相位均 exit **26**。GNU 报错/正例约束没有放宽。
- **未覆盖：** 尚未运行全部五 ISA GNU ELF/QEMU/Linux 执行矩阵、与 MiniAS/MiniLD 的联合验收；因此不标记 `VERIFIED`。

### MC-004 属性探测使 Parser tags 失效

- **状态：** `FIXED-UNVERIFIED`（2026-11-01 独立定点修复；非纯清理工作量）。
- **根因与修复前证据：** `src/frontend/parser_attribute.c` 的 `MinicParser probe = *parser` 在 `aligned(sizeof(struct AttributeProbeTag { int field; }))` 完整语义解析时拷贝动态 `tags` arena 所有权；扩容 `realloc` 更新副本但使原 Parser 的 tag 指针悬空。基线 `d4026e3` 的 sanitize/UBSan `-Werror` 编译器在 x86_64 `-S`、前置 16 个 tag 的有效 GNU11 输入触发明确的 ASan **heap-use-after-free READ 48B**，读取栈从 `minic_parser_find_tag` → `resolve_tag` → `parse_type_specifiers`，释放栈从 `bind_tag` → `minic_parser_grow_array`。不以新版本不崩溃代替旧版复现。
- **生产修复：** `src/frontend/parser_attribute.c:minic_parser_apply_alignment_attribute` 不再按值复制 Parser；只保存/恢复 `MinicLexer`、`MinicToken`，在唯一拥有语义容器的原 Parser 上解析属性整数表达式，恢复词法游标后检查精确参数结尾与常量值。不重构 Parser。GNU C 允许该属性表达式定义的具名 tag 在同一作用域后续使用；修复保留这些语义绑定而不盲目回滚。
- **永久回归：** `tests/compiler/audit/mc004_attribute_tag_probe_uaf.c`，由 `tests/compiler/c0/probe-alignment-attribute-semantic-arena.sh` 覆盖前置 15/16/17 个 tag 的扩容边界，接入 `check-alignment-attribute-semantic-arena` 和 `check-fast`。当前 ASan/UBSan 与 Release **均 `-Werror`**，三个容量相位 × x86_64/RV32/RV64/AArch64/LoongArch64 **各 15/15 次 `-S` PASS**；GNU 与 MiniC x86_64 原生 **各 3/3 次 exit0**，验证后续具名 tag 仍可见。宿主 GNU11 语法、编译器警告检查通过。
- **验收边界：** 尚未运行其他四目标 ELF/QEMU 的行为对照、完整 Linux corpus 或 MiniAS/MiniLD 联合验收；不升 `VERIFIED`。

### MC-005 属性探测使 Parser enum_constants 失效

- **状态：** `FIXED-UNVERIFIED`（与 MC-004 同一 Parser 所有权根因、同一最小生产修复；保留独立证据）。
- **根因与修复前证据：** `aligned(sizeof(enum AttributeProbeEnum { AttributeProbeConstant = 4 }))` 在浅拷贝 Parser 中触发 `enum_constants` arena 扩容。基线 `d4026e3` 的 sanitize/UBSan `-Werror` 编译器在 x86_64 `-S`、预置 16 个枚举常量时触发 ASan **attempting double-free**：新指针仅写入副本，原 Parser 析构时在 `src/frontend/parser_enum.c` 释放旧地址，而该地址此前已经被 `realloc` 释放。此为独立的真实 old-version ASan 崩溃，不只与 MC-004 类比。
- **修复与作用域约束：** 同 MC-004，属性完整语义解析只借用/恢复词法状态，动态语义表始终由真实 Parser 唯一持有。GNU C 接受并允许 `sizeof(enum {...})` 中定义的枚举 tag/常量在同作用域后续使用；修复保留该可见性。
- **永久回归：** `tests/compiler/audit/mc005_attribute_enum_probe_uaf.c` 测前置 15/16/17 个枚举常量容量相位，沿用 `check-alignment-attribute-semantic-arena`/`check-fast`。当前 ASan/UBSan 与 Release **均 `-Werror`**，三个相位 × 五目标 **各 15/15 次 `-S` PASS**；GNU/MiniC x86_64 原生 **各 3/3 次 exit0**，校验 `AttributeProbeConstant` 和具名 enum 在属性之后仍有效。
- **验收边界：** 没有执行全部五 ISA ELF/QEMU/Linux 执行矩阵，也没有 MiniAS/MiniLD 联合验收；仍为 `FIXED-UNVERIFIED`。

### MC-006 传递特化污染共享 CALL

- **位置：** `src/compiler/inline_symbolic.c:874–878` 与 `src/core/core_lower_calls.c` 的候选选择。
- **证据：** 两个 caller 通过共享的 `mid` 函数体传入不同静态 key。GCC x86 目标文件的 `__jump_table` 有两种 key 重定位（`keys+2`、`keys+0xa`）；MiniC 只有一种。反转 caller 顺序，MiniC 保留的地址也反转，说明共享 CALL 被后写者覆盖。
- **根因：** `(caller FunctionId, CALL ExpressionId)` 的实例专属 callee 被降为单个共享 `ExpressionId → callee`。
- **修复门槛（行为合同）：** 共享 AST 保留源码静态关系；实例化 callee 必须随 `(caller_id, call_id)` 的实际调用上下文确定，不能覆盖一个全局 CALL。此合同可由显式实例事实表或 Core 当前具备的 caller-local selector 履行，无须为了修此 Bug 强制新增表。正反顺序均应保留两种 ELF 重定位；不能依靠“生成 clone 数量大于零”作为正确性证据。
- **2026-11-01 最小修复（`FIXED-UNVERIFIED`）：** `inline_symbolic.c` 传递规划仍按 caller 创建各自的符号 clone，**不再把 clone FunctionId 写回共享 source AST 的 CALL**。现有 `core_select_transitive_symbolic_specialization` 已根据 `context->source_function` 的符号参数事实，在对应 caller 的实际 Core lowering 中选择具体 clone，故无须新增全局可变映射表；原始 CallId/FunctionBody identity 保持不变。先在独立 worktree 证明此闭包对两个 caller 分别解析正确，未扩成重构。
- **永久 ELF 差分：** `tests/compiler/audit/mc006_shared_call_relocation.c` 和 `tests/compiler/c0/probe-mc006-shared-call-relocations.sh` 对正反两个 source caller 次序，分别用 GNU `-O2 -c` 与 MiniC `-S`→GNU AS 生成 x86 ELF。修复前同一 fixture 的 GNU `.rela__jump_table` 含 `R_X86_64_PC64 .bss + 2` 和 `.bss + a`，MiniC 仅一条 `.bss + a`（单向次序）/反向仍仅一条；MiniC x86 原生 **exit0** 但 **ELF 语义错**，所以不能靠运行退出码宣称通过。修复后正反两种次序的 GNU/MiniC **4/4 ELF** 均明确包含这两个不同 key addend，GNU/MiniC x86 原生 **4/4 exit0**；`check-mc006-shared-call-relocations` 纳入 `check-fast`。
- **邻近回归：** Release `-Werror` 构建；MC-001 单/双符号闭包五目标 90 次 `-S`、原生六次；symbolic shared body、two callers、GNU Linux ioremap inline guard、x86 Linux static-key ASM-goto 的专项均 PASS。**保留范围限制：** 本 fixture 是 x86 Linux GNU ASM-goto 语法，其他 ISA 源汇编 preflight 不支持该 x86 指令模板，不应将其写成“五 ISA PASS”；本轮未完成 Linux corpus、五目标 GNU ELF/QEMU 或 MiniAS/MiniLD 联合验收。MC-007（整数＋符号组合）的独立事实缺口仍 `OPEN`，不能由 MC-006 的双 key 通过反推其已修复。

### MC-007 整数与符号特化无法组合

- **位置：** `src/compiler/inline_integer.c` 传递特化、`inline_symbolic.c:780–814`、`src/core/core_lower_calls.c`。
- **证据：** `mid(key, k) → leaf(key, k)`，两 caller 分别提供不同静态 key 且 `k=1`。整数阶段建出 leaf 的整数 clone，却没有将事实归到具体 CALL 实例；符号阶段读共享原始 CALL，`transitive=0`，最终 Core 报不支持的 leaf clone。修正探针中的 x86 GNU ASM 为合法 `.balign 8` 后，GCC `-O2 -c` 通过而 MiniC `-S` 失败。
- **修复门槛：** 与 MC-006 共用实例事实模型；联合整数＋符号特化不丢任何一方事实。

### MC-008 符号特化过度要求整数参数常量化

- **位置：** `src/compiler/inline_symbolic.c` 的 `minic_inline_symbolic_integer_inputs_are_closed` 等检查。
- **证据：** `leaf(int *key, int k)` 中 GNU ASM 只要求 `key` 为立即量，`k` 正常运行时使用；GCC `-O2 -c` 成功，MiniC `-S` 拒绝 `leaf`。当前算法要求**所有**整数参数已知才准符号特化。
- **修复门槛：** 按实际立即量操作数和已知事实需求判定，不强制无关整数形参常量化；与 MC-006/007 共同回归。

### MC-009 表达式探测回滚遗漏 owned CALL 参数

- **现状：** `FIXED-UNVERIFIED`，修复合入提交 `cbc439a`（独立原提交 `68e2ef9`）。`ast.c` 在回滚表达式尾部前统一释放/清零 owned CALL overflow；两个 semantic snapshot 事务路径调用同一释放入口，析构亦复用。新增真实 12 参数 CALL 单元断言和 `probe-owned-call-designator-rollback.sh`，合入后 Release `-Werror`、semantic snapshot 单测、五 ISA `-S`、GNU/MiniC x86 原生 exit7 一致；独立分支 ASan/UBSan 单测亦通过。LSan 仍因运行环境不可用，故未升 `VERIFIED`。
- **位置：** `src/frontend/parser_static_initializer.c:3165–3178`、`semantic_snapshot.h:167–194`、`ast.c:499–528,107–111`。
- **证据：** 对数组 designator 的 `sizeof(func(12 个实参))` 试探解析会给 CALL 分配 `overflow_arguments`；恢复较小的 `expression_count` 后，析构只遍历当前节点计数，旧分配失去所有者。GCC 与 MiniC 均接受相应合法输入。
- **证据边界：** 源码所有权链确认；此环境 LeakSanitizer 无有效动态泄漏报告。普通值型 ArrayType/FunctionType 计数回滚并没有同类堆所有权，不应一概否定。
- **旧测试缺口已补：** 历史 `semantic_snapshot_test.c` 不构造真实 CALL；新回归已补真正的 12 实参 overflow ownership 和表达式 probe，故不能再以旧的薄测试断言修复缺席。
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

- **第五轮升级与现状态：** `FIXED-UNVERIFIED`、P0、**ASAN**，原调查已被 GNU C 有效源码及真实 heap-use-after-free 堆栈证实。之前那条单点无报警的输入仅说明当时未命中容量边界，**不能再当成反证**。局部修复提交 `a4b8b71`。
- **位置与消费者：** `src/frontend/parser_static_initializer.c:2364–2528`，`overwrite_static_zero_record_value` → `overwrite_static_zero_record_constant`。`parse_static_record_constant` 后向覆写零字段时持有 `program->records[record_id]` 借来的 `MinicRecord *record`，`overwrite_static_zero_field_value` 解析 `sizeof(struct { int z; })` 可追加 Record，`realloc` 释放旧 arena，随后本函数 `:2496` 读取 `record->fields`（ASan READ8）。
- **独立复现：** 持久用例 `tests/compiler/audit/mc023_static_record_backward_uaf.c`（源自 `/tmp/minic-front-static-record-backward-minimal-20261030.c`），GNU `-std=gnu11` 编译成功。预置 14 个 Record 在 16 容量界、126 个 Record 在 256 容量界均得到 MiniC ASan UAF；RV64 sanitize `-S` 堆栈指向 `overwrite_static_zero_record_constant:2496`。GCC 原生与普通 MiniC x86 生成程序在该反例上都 exit **7**，即目前证明**内存不安全**，不声称已独立观察到输出错编。
- **其他链路的正当设计：** 正向 `parse_static_record_constant` / `parse_static_union_constant` 使用值型 Record header snapshot，fields heap 独立存在，可跨 Record arena 扩容；不可为了修后向路径删除它们。
- **已修与定点验证：** 后向路径在解析复合字面量类型后重取 Record，取得稳定 RecordId，在字段迭代及设计符/union 递归返回后再次按 ID 取新 Record；不碰正向路径。`check-static-record-backward-arena` 作为 `check-fast` 门禁，grow/no-grow 两类相位、五目标 ASan `-S`、GNU/MiniC x86 原生均 exit7，Release `-Werror` 与邻近 static 回归 PASS。**未做** 256 容量以上多次增长、全目标 GNU ELF/QEMU、匿名字段/重定位全集与联合验收。

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

- **状态：** `FIXED-UNVERIFIED`，P0；历史 `GNU-NATIVE-DIFF` 是已在 x86-64 GNU 工具链完整链接、本机执行中复现的**真实控制流错编**；并非仅靠源码猜测。旧版 RV32/RV64/AArch64/Loong 的相同输入都成功 `-S`，汇编里仅保留错误的返回 99 分支，**未在这些目标上执行**。
- **位置：** `src/core/core_lower_expression.c:1237–1246` 对独立 `i++/--i` 表达式语句直接走 `minic_core_lower_scalar_update`；同文件 `:423–435` 的通用 `lower_expression` 入口先执行 `minic_core_local_fact_invalidate`。`src/core/core_lower_assignments.c:233–435` 的更新器只生成 load/算术/store，不自身处理 fact 失效。
- **独立最小复现（2026-10-30，基线 `4f516707d`）：** `int main(void) { int x=0; x++; if (x==0) return 99; return 7; }`。`build/debug/bin/minic-cc --target=x86_64-linux -S input.i -o minic.s` 成功，`cc -no-pie minic.s -o minic` 成功，运行 `minic` 返回 **99**；GNU `cc -std=gnu11 -O0 -x c input.i -o gnu` 运行返回 **7**。MiniC 汇编确实先把递增值写回，又无条件选择旧事实 `x==0` 的分支。负向例 `--` 的相同错判也在联合探针里观察到。
- **根因判断：** statement-only 优化捷径曾与普通表达式事实失效策略分叉；当前两处路径都在更新前失效化直接 local fact，仍需审计其他有副作用的快捷路径和合流事实。
- **2026-11-01 复核（当前基线 `d4026e3`）：** `core_lower_expression.c` 的 statement-only 四种更新捷径现已在调用 `minic_core_lower_scalar_update` 前，针对 `MINIC_EXPRESSION_LOCAL` 执行 `minic_core_local_fact_invalidate`（与普通 value-expression 路径同义）。未取得这一修复的原始引入提交，不冒称本批新修生产代码。新增永久 `mc033_statement_updates_local_facts.c` 和 `check-mc033-statement-update-facts`：GNU C `-Wall -Wextra -Werror` 对照，四种更新语句、嵌套更新值、`goto`、`continue`，MiniC 五目标 `-S` PASS；GNU/MiniC x86_64 GNU AS/LD 本机 exit 0 一致；邻近 `run-gnu-runtime-if-effect-facts.sh` PASS。
- **验证缺口：** RV64 既有 `run-core-postfix-update-m24.sh` 受环境缺失交叉 `crt1.o`/`-lc` 阻断；`run-core-for-postincrement-m20.sh` 受缺失 RISC-V libc headers 阻断；`run-prefix-update-expressions.sh` 的固定汇编文本断言 exit 1（当前产物已成功 `-S`，不作为新增语义回归证明）。本次**不新增生产修复**，仅补永久回归与台账；尚未做五目标 ELF/QEMU、完整 Core CFG/所有 goto reentry 验收，因此保持 `FIXED-UNVERIFIED`。

### MC-034 静态 Record 设计符查找对无关对象做指针相减

- **状态：** `FIXED-UNVERIFIED`，P1，`ASAN-POINTER-PAIR`；在有效源码上已观察到 ASan `invalid-pointer-pair`，**未证明输出错编**。原 UB 已定点消除，完整目标 ELF/QEMU/联合验收仍待执行。
- **位置与链路：** `src/frontend/parser_static_initializer.c:2698–2699` 把 `Program.records[id]` 的 Record 值型头复制到栈上 `record_snapshot`；`:2747` 在 `.field` 设计符下调用 `parse_static_record_designator_path`（约 `:1438–1480`），后者调用 `minic_parser_find_record_field_path` → `src/frontend/parser_member.c:28:search_record_field_path`。该 helper 无条件计算 `(MinicRecordId)(record - parser->program->records)`，但 `record` 此时位于**不相关的栈对象**。
- **触发输入与检测：** `struct Q { int x; }; struct Q q = { .x = 3 };`；GNU C 合法，MiniC 普通 sanitize `-S` 通过。独立构建 `MODE=sanitize BUILD_DIR=/tmp/minic-audit-20261030/pointer-pairs CFLAGS=-fsanitize=pointer-subtract LDFLAGS=-fsanitize=pointer-subtract` 后设置 `ASAN_OPTIONS=detect_leaks=0:detect_invalid_pointer_pairs=2:halt_on_error=1`，RV64 `-S` 真实报 `AddressSanitizer: invalid-pointer-pair`，栈指向 `search_record_field_path:27` ← `parse_static_record_designator_path:1459` ← `parse_static_record_constant:2747`，地址分别来自 `record_snapshot` 栈与 Program.records heap arena。栈 Record 快照本身是为避免 arena 扩容悬空而设计的，不能因这里有 UB 直接删除快照。
- **语义边界：** 静态初始化路径目前只使用返回的 field indices，不使用该处生成的 RecordId；其余成员/offsetof 消费者传入 Program-owned Record。修复要保证仍使用稳定 RecordId，或让不需要 RecordId 的消费者避免该运算；再以 ASan pointer-pair 和多架构目标回归验证，不能把“编译结果看似正常”当作 UB 修复。
- **本轮独立重现（2026-11-01，`5942dcf` 基线）：** 新持久 `mc034_static_designator_pointer_pair.c` phase 0 的 GNU 合法静态 `.x`、匿名提升成员、union `.low` 设计符，在 `MODE=sanitize CFLAGS='-Werror -fsanitize=pointer-subtract' LDFLAGS=-fsanitize=pointer-subtract` 的未修复 `minic-cc --target=x86_64-linux -S`、`ASAN_OPTIONS=detect_leaks=0:detect_invalid_pointer_pairs=2:halt_on_error=1` 下报 `invalid-pointer-pair`，堆地址 `Program.records` 与栈 `record_snapshot`，栈为 `search_record_field_path:30` ← `parse_static_record_designator_path:1467` ← `parse_static_record_constant:2779`，exit 1。该再现独立于源码形式和目标汇编文本锚点。
- **定点修复：** `parse_static_record_designator_path` 从调用者接收已有稳定 `MinicRecordId`，遇嵌套 member 由 `field->type.record_id` 递进，调用通用 `minic_parser_find_record_field_path` 前以 ID 从 `Program.records` 重取 **实际归属 Program 的** `MinicRecord*`。对静态初始化仍保留必要的栈 Record 快照用于后续 arena 扩容安全；其他普通 member/offsetof 消费者及共用 helper 的 O(1) RecordId 映射均未改变。
- **修后专项：** 新 `check-mc034-static-designator-pointer-pair` 接入 `check-fast`，六个 Record-arena 占位相位 × 五目标 `-S`：ASan/UBSan+pointer-pair **30/30 PASS**、Release `-Werror` **30/30 PASS**，GNU/MiniC x86 GNU AS/LD 原生各 **6/6 exit 0**。邻近 `run-static-nested-record-designator.sh`、`run-static-union-zero-overlay.sh` Release PASS，MC-023 静态 Record 反向设计符扩容 SAN 五目标和原生差分 PASS。完整多目标 GNU ELF/QEMU 与 MiniAS/MiniLD 联合验收未运行，保持 `FIXED-UNVERIFIED`。

### MC-035 运行时范围初始化器借用 Expression 跨扩容

- **状态：** `FIXED-UNVERIFIED`，P0，`ASAN`；合法 GNU C 输入在旧版真实触发 `heap-use-after-free`，现有定点修复通过 ASan/UBSan 与本机差分，完整跨 ISA ELF/QEMU 尚未验收。
- **位置：** `src/frontend/parser_runtime_initializer.c:457–514:add_runtime_initializer_once_read` 先缓存 `const MinicExpression *value = minic_c0_program_expression(program,value_id)`；`:489` 的 `minic_parser_statement_local_lvalue` 经 `src/frontend/parser_statement.c:15–35` 追加表达式，可能 `realloc program->expressions`，而 `:496` 与 `:510` 仍读旧 `value->span`。
- **真实 ASan 反例（2026-10-30，基线 `4f516707d`）：** `int testcase(int seed) { seed + 0; seed + 1; int target[16] = {0, [1 ... 15] = seed + 1}; return target[1] + target[15]; }`，由 GNU C 接受。`ASAN_OPTIONS=detect_leaks=0:halt_on_error=1:abort_on_error=1 build/sanitize/bin/minic-cc --target=x86_64-linux -S /tmp/minic-runtime-init-uaf-probe/add_expression_statement_002.i -o ...` 报 `heap-use-after-free`，`READ size 48` 指向本函数 `:496 assignment.span=value->span`；先前 `parser_statement_local_lvalue:33` → `minic_c0_program_add_expression:519` → `minic_grow_array:20` 释放旧 arena。容量探针前置 0..128 个无效效果式时大部分 PASS、特定两个 `seed+i;` 使增长边界准确命中，**PASS 不是安全证明**。
- **2026-11-01 当前基线再现：** 独立可持久 fixture `tests/compiler/c0/mc035_runtime_range_once_arena.c` 的 `MC035_PREFIX_PHASE=2` 在修复前 `d4026e3` x86-64 `MODE=sanitize CFLAGS=-Werror` 上再次触发 READ48 UAF，栈位于 `parser_runtime_initializer.c:503`；前缀 0、1、3、4、5 都 PASS，确认容量边界偶发性而非所有输入必崩。
- **修复：** `5d76e53` 仅在 `add_runtime_initializer_once_read` 借用合法时复制 `MinicSourceSpan value_span`，之后构建临时 Local、目标 lvalue、赋值 Statement 和 LVALUE_READ 全部使用快照，避免 `minic_parser_statement_local_lvalue` 增长 Program.expressions 后读取旧指针；不改对象归属或 GNU range 的求值一次合同。新增 `check-mc035-runtime-range-once-arena` 到 `check-fast`。
- **修后专项验证：** ASan/UBSan 与 Release 均以 `-Werror` 编译，前缀阶段 0～5 × 五目标 `-S` 各 **30/30 PASS**，GNU/MiniC x86 原生各 6/6 exit 0，核对 range expression 只求值一次。邻近 MC-039/056/003 capacity gates 在 ASan 与 Release 均 PASS，`check-ast-contract`、`check-expression-statements` Release PASS。
- **尚未验收：** 全五目标 GNU AS/LD、ELF relocation、QEMU/原生运行矩阵及 MiniAS/MiniLD 联合均未做；保留 `FIXED-UNVERIFIED`，同模块其他 borrow/rollback 风险不以这一处修复自动关闭。

### MC-036 Record 条件表达式的 local facts 分支合流缺口

- **状态：** `FIXED-UNVERIFIED`，P1，`GNU-NATIVE-DIFF`；x86-64 已有旧版错编实证，定点修复后完成专项对照，尚未通过五目标 GNU ELF/QEMU/联合验收。
- **位置：** `src/core/core_lower_record.c:582–713:minic_core_lower_record_conditional_object` 构建 true/false/merge 三块，两臂依次操作同一个 `context->local_integer_constants`，当前看不到进入 true 臂前的快照、false 臂恢复和 merge 交集。对照 `src/core/core_lower_expr_flow.c` 的标量/void 条件表达式使用 `CoreExpressionFactFork` 进行 fork/meet。
- **最小差分（2026-10-30，基线 `4f516707d`）：** GNU C：`struct S{int v;}; int f(int c){int x=0;struct S a={1},b={2};struct S r=c?({x=1;a;}):({x=2;b;});if(x==2)return r.v+10;return r.v;}`，独立 harness 调用 `f(1)` 与 `f(0)`。`cc -std=gnu11 -O0` 执行得 `f(1)=1, f(0)=12`，返回成功；`cc -E -P` 冻结 `.i` → `build/debug/bin/minic-cc --target=x86_64-linux -S` → GNU `cc -no-pie` 链接均成功，执行得 **`f(1)=11, f(0)=12`**，harness 返回 1。说明假臂 `x=2` 的 fact 漏回收后污染 merge 处 IF 剪枝。
- **修复边界与实现：** `core_lower_record.c:minic_core_lower_record_conditional_object` 在 condition 求值之后保存 entry facts，true 臂结束后快照 true facts 并恢复 entry；false 臂从原入口事实开始，merge 调用 `minic_core_local_facts_meet`。与 `core_lower_expr_flow.c` 的标量/void 条件式使用同一 snapshot/restore/meet 语义，保留私有局部实现，**未抽象新的事实层或调整 CFG/ABI**。两个快照在所有下游错误出口均释放；若非空 local facts 无法分配则 fail-closed。带 side-effect condition、嵌套 Record、循环 `continue/break` 等覆盖见持久回归。
- **本轮旧版实证（`5942dcf` 基线）：** `mc036_record_conditional_fact_join.c` 在 GNU `cc -std=gnu11 -O0` 本机执行 exit 0；同源 `.i` 经 MiniC `--target=x86_64-linux -S`、GNU `cc -no-pie` 链接后本机执行 exit 1（第一条 `f(1)` 等价断言失败）。
- **本轮修复后专项结果：** 新 `probe-mc036-record-conditional-facts.sh` 严格 GNU/MiniC x86 原生对照（十路径）exit 0/0，五 ISA `-S` 5/5；Release `-Werror` 的 `minic-cc`/`minic` 构建通过。邻近 `run-record-conditional-materialization.sh`（Record 赋值、CALL、字段投影、汇编与 native）、`run-gnu-runtime-if-effect-facts.sh`（GNU/MiniC native + RV64 `-S`）、`run-core-reentry-query-contract.sh`（索引与 fallback、五 ISA `-S`、GNU/MiniC native）、MC-033 fact 更新探针均 PASS。新增 Makefile `check-mc036-record-conditional-facts` 进入 `check-fast`，但此处没有声称整套 `check-fast` 全过。

### MC-037 Loong 特殊 ASM 源端与机器端限定符不一致

- **状态：** `OPEN`，P2，`LOCAL-COMPILE-DIFF`，限 LoongArch64 特殊 GNU ASM 的**编译接受缺口**，尚无 GNU LoongArch GAS/QEMU 目标运行结果。
- **位置：** `src/target/loongarch64/asm_contract.c:758–823` 的 `cpucfg` source gate 对结果类型先 `minic_type_unqualified`，因此接受 `volatile unsigned int`；机器 emitter 检查 Core 输出地址指针 pointee 时直接与无 cv unsigned int `minic_type_equal`，丢失 qualifier 一致性。类似源/机器配对还存在于 `frametrace` 与 `frametrace_csr`（约 `:2826–2990`），暂以**同类根因一条**跟踪，未独立证明这两个形式的动态失败。
- **复现（2026-10-30，基线 `4f516707d`）：** GNU host `cc -std=gnu11 -Wall -Wextra -Werror -fsyntax-only -x c` 接受 `unsigned int volatile_cpucfg(unsigned int index) { volatile unsigned int cfg; __asm__ volatile("cpucfg %0, %1\\n\\t" : "=&r"(cfg) : "r"(index)); return cfg; }`。MiniC `--target=loongarch64-linux -S` 对 `unsigned int cfg` 通过，只改成 `volatile unsigned int cfg` 就报 `LoongArch64: Core instruction/ABI not yet supported by M1 emitter`。`unsigned long v; __asm__ volatile("csrrd %0, 0x7":"=r"(v));` 与 `volatile unsigned long v` 的对应 GNU CSR source 也测得同一组 MiniC PASS/FAIL；按同类 qualifier 根因归入一项。
- **第四轮独立子型：** `asm_contract.c:loong_linux_fpr_transfer_source` 对 `movfr2gr.d %0, $f0` 的输出 pointee 使用 `minic_type_unqualified`，而 `loong_linux_fpr_transfer_emit` 直接以完整带 cv 类型比较 `unsigned long`。同一 GNU C fixture（`unsigned long out` / `volatile unsigned long out`）前者 MiniC `-S` PASS、后者 FAIL，追踪 `stage=qualified_hardware_asm`；见工作区 `loong4_fpr_{plain,cv}.i`。这是独立 ASM 家族复现、同一根因，不另分配 MC 编号；目标 Loong GNU AS/QEMU 仍未运行。
- **验收门槛：** 对源端 qualifier、Core pointer pointee、机器输出类型定义一个共用类型比对契约；补 cpucfg 和两组 frametrace 的 volatile 与非 volatile 组合，目标 GNU AS/ELF/QEMU 执行后才宣称运行时正确。

### MC-038 匿名字段设计符后的位置初始化游标错误

- **状态：** `FIXED-UNVERIFIED`，P1，`GNU-NATIVE-DIFF/RV64-QEMU`；主线 `2045467`（独立原提交 `9be159e`）。修复在 runtime designator 中沿 MEMBER/ADDRESS_OF 祖先路径保存 Record 游标，优先续接仍有成员的最内层，耗尽后才逐层向外；只修 Record 路径，保留数组下标的原有 fallback。新 13 个 GNU 合法案例/非法 overflow、x86 GNU/MiniC native mask0、RV64 freestanding QEMU exit0、五 ISA `-S`、相邻匿名提升/聚合初始化和 Release `-Werror` 全 PASS。跨 ISA 完整 ABI/ELF 联合验收仍未做。
- **最小反例（2026-10-30，基线 `4f516707d`）：** `struct Outer{struct{int a;int b;};int c;}; int f(void){struct Outer x={.b=2,3};return x.b*10+x.c;}`。GNU `cc -std=gnu11` 接受、执行返回 `23`，MiniC `--target=x86_64-linux -S` 报 `too many positional record initializers`。若只初始化 `{.b=2}` 或显式写 `{.b=2,.c=3}`，MiniC `-S` 和 GNU 链接/执行均通过，说明主要缺口是 `.b` 后的**隐式位置续接**，不是匿名成员查找能力。
- **源码候选根因：** `src/frontend/parser_runtime_initializer.c:1513–1579:parse_runtime_record_designator_target` 从提升的最终 `MEMBER` expression 读取 `field_index` 并用作 `top_level_field_index`；但 `src/frontend/parser_member.c:113–195` 对匿名成员返回的最内层 `field_index` 是内层 Record 的索引。`parser_runtime_initializer.c:1658–1679` 将该值 `+1` 作为下一个顶层位置游标，可能跳错位置。
- **后续验收：** 旧记录中的“只按顶层路径第一段”不足以覆盖多层匿名成员——真实 GNU 语义优先继续最内层未耗尽 Record。持久定点已覆盖此分支；完整五 ISA GNU ELF/QEMU 和更深层数组混合场景待验后才升 `VERIFIED`。

### MC-039 运行时嵌套数组递归跨 ArrayType 扩容借用

- **状态：** `FIXED-UNVERIFIED`，P0，`ASAN`；原版已由合法 GNU C 的真实 Parser 路径和精准容量边界测得 UAF。修复后的聚焦回归通过，未完成全目标运行时验收。
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
- **修复与新证据（基于 `ede361e`）：** `parse_fixed_runtime_nested_array_initializer` 仅在安全取址后复制 `MinicArrayType` 纯值描述符，整个递归过程不再跨 `Program.array_types` 扩容持有 pointer；同文件 zero-element 分支也在建立 lvalue 前保存 `element_count`。持久回归 `tests/compiler/c0/runtime_nested_array_arena_growth.c` 与 `probe-runtime-nested-array-arena-growth.sh` 按前置不同数组类型数 13/14/15/16 相位执行，`MODE=sanitize CFLAGS=-Werror` 下五目标 `-S` **20/20 PASS**，同一 GNU-preprocessed 输入的 GNU native 与 MiniC x86 GNU AS/LD native 均 **4/4 exit 0**。修复前当前基线在 14 相位 x86 ASan 读已释放 ArrayType header（`READ size 88`），修复后未再复现。
- **保留门槛：** 全目标 GNU ELF/QEMU 运行、更多深度/字符子数组的递归组合、其它 ArrayType 借用 owner 分别验收；本修复不宣称 MC-049 的静态路径或全部 parser 借用问题已解决。

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
- **第十轮定向运行的新观察：** `run-alignof-type-query.sh`/`run-enum-constant-expressions.sh` 假定 RV 汇编结果必为 `li a0,<constant>`；当前机器发射 `li t0,<constant>` 后经栈槽 `ld a0`，字符串匹配失败。对合法 `alignof` type 和 expression 已独立执行 MiniC x86 `.s`→GNU AS/LD→native 均退出 0，**只证明这些小样本有正确运行结果**，不表示所有失败脚本都是假阳性。`run-static-prefix-object-attributes.sh` 仍期待一个未引用 static 符号被发射，而当前 Compiler 的可达性裁剪将它去除；`run-array-bound-constant-expressions.sh` 还有未定位的失败；Loong 旧 `probe-loongarch64-linux-va-fixed-overflow.sh` 在未修改的 Debug 编译器上也失败，暂不能归因于新 `va_start` 修复。这些需要与真实 ABI/ELF oracle 一一核对，**保留红灯**。
- **第十一次跨 CI/source-文本活性核验：** `src/frontend/ast_global.c` 有 8 个无生产 caller 的外露 relocation/extern 包装 API，但 `tools/dev/pr75*.py`、`tools/dev/materialize-linux-first500-aggregate-array-v1.py` 等历史源码重写脚本仍按这些符号名插补 C 文本；`pr75-stage.sh` / `pr76-stage.sh` 仅手动串联，`.github/Makefile/tests` 当前没有自动调用这些历史 stage，属于**源模板引用而非编译器执行调用**。**相反** `src/frontend/ast.c:minic_c0_program_set_function_parameter_count` 当前 `src/tests` 无 caller，却被 `tools/ci/apply-perf-function-parameter-exact-allocation-v1.py:90,112` 作为精确源文本锚点，且 `.github/workflows/linux-runtime-optin-perf-suite-v1.yml:2263,2321` **确实调用该脚本**。因此“删死 API”需要同时做 `.github`→脚本→源码文本锚点的活性图，不仅检索 C 符号 caller；此函数现在不能按零 C caller 直接删除。
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

- **状态：** `FIXED-UNVERIFIED`（第十轮本地清理，待跨五目标/full GNU 差分），P3，`SOURCE-TRACE/TARGETED-BUILD`。`src/frontend/parser_core.c:minic_parser_name_bound` 的实际同 scope 替代是 `_in_current_scope`，普通表达式仍通过 `minic_parser_find_nearest_ordinary_binding` 实现遮蔽关系。
- **第三轮新例：** `src/frontend/parser_type_query.c:minic_parser_parse_alignof_type_value` 也只有定义与 `parser_internal.h` 声明，暂无内部生产/测试调用；实际 `src/frontend/parser_expression.c:parse_alignof` 同时支持类型名和表达式这两种 source 形式。两处都是可能的迁移残留，**属于同类“无消费者私有 helper”清理债，不是两项语义 Bug**。
- **第九轮全仓 caller 清点（`3d504e7`）：** 此类无 `src/` 生产调用者的 Parser 函数共 **10 个**（包括已登记的 name_bound 与 alignof），其余八个是 `parser_function.c:minic_parser_parse_gnu_section_attribute`（约 706–792）、`parser_attribute.c:minic_parser_apply_object_attribute_list`（约 572–597）、`parser_constant.c:minic_parser_parse_integer_value64`（约 269–315）、`parser_core.c:minic_parser_parse_integer_constant_expression_value`（约 101–103）和 `minic_parser_current_attribute_is`（约 231–239）、`parser_expression.c:minic_parser_parse_null_pointer_constant_expression`（约 2974–2985）、`parser_global.c:minic_parser_parse_extern_global`（约 603–637）与 `minic_parser_parse_static_global`（约 1202–1259）。另有两个**级联专属 helper**：`parser_function.c:section_attribute_token_is` 只供已死 section wrapper，`parser_core.c:minic_parser_current_attribute` 只供已死 attribute-is 包装。**仅按调用图认定退役候选**，没有本轮全目标构建/链接回归。
- **真实替代消费者：** GNU section 现由 `parser_attribute.c` 的统一 attribute lists 处理；`parse_alignof` 已支持 type/expression；static/extern global 的 `*_after_head` 入口仍被当前 parser 调用；object attribute 的 `with_used/with_symbol_metadata` 变体仍活。先删旧入口＋专属 helper、`parser_internal.h` 声明；若移除整个旧 `parser_type_query.c`，同时移走 `Makefile` 编译清单，不得误删这些活跃后继路径。
- **第十轮清账结果：** 这 10 个无生产/测试 caller 的入口、两个仅供它们的 helper 已从原代码及 `parser_internal.h` 删除，`parser_type_query.c` 整个退役且从 `Makefile` 移出，**未添加并行的第二套实现**。最新 `minic-cc` 定向构建 PASS；GNU section/symbol attribute、static global section、prefix function attribute、整数 bitwise、GNU function attribute 的正反例脚本 PASS；对 `alignof` 类型/表达式做了 MiniC x86→GNU AS/LD→native，均 exit 0。原 `run-alignof-type-query.sh` 与 `run-enum-constant-expressions.sh` 仍以 `li a0,...` 断言 RV 汇编寄存器，实际当前发 `li t0,...; ld a0,...`，故**原脚本红灯不能当作语义回归，也不能被忽略**，需单独迁为语义/ELF oracle。其他测试仍未完成五目标验收，不标 `VERIFIED`。
- **历史脚本的受影响范围：** `rg` 在 `src/`/两个 CLI 已找不到已退役的声明或调用，但 `tools/dev/pr75-*.py`、`tools/dev/pr77-*.py` 等**历史补丁生成脚本**仍按旧函数名查找或插入文本；它们不是当前 compiler 的生产消费者，重放这些旧 PR 脚本到新 HEAD 可能失败。原文件保留供历史审计，不当作必须复活死 API 的理由；若还需重放，应先将这些脚本转为基于新 Parser 入口的显式迁移版本，关联 MC-021。

### MC-040 嵌套 switch labels 重复全量扫描

- **状态：** `DEBT`，P3，`SOURCE-TRACE`，不是已证语义错编。
- **位置：** `src/core/core_lower_switch.c:707–725:minic_core_lower_switch` 为识别 nested cases 调用 `core_collect_nested_switch_labels`，随后在 `lower_nested_case_switch`（约 `:431`）再次进行同一类别的收集；两个真实生产消费者均在同一 lowering 时序。
- **建议：** 后续性能/结构整理可把第一次收集结果交给 nested 路径，减少额外遍历，但需保留 nested switch 的 reentry、scope 和合法 fallthrough 规则，不能机械把 nested case lowering 并入普通 switch 分支。

### MC-041 long double 字面量提前按 binary64 舍入

- **状态：** `FIXED-UNVERIFIED`，P0，`GNU-NATIVE-DIFF / RV64-QEMU-DIFF`。旧主线的 `parse_floating` 把 `L` 字面量先过 `strtod` binary64，再扩成内部 binary128，已丢失超 53 位尾数；x87 80-bit 常量端也只接受 binary64 payload。这一真实源→目标跨层缺陷已由独立 MC 修复，不计入 C17 纯清理删行。
- **历史同源证据：** `int main(void){ long double a=1.0000000000000001L,b=1.0L; return a>b?7:99; }` 在原 x86 GNU C native exit **7**、MiniC `-S`→GNU AS/LD native exit **99**。本仓库 x86 `long double` 为 x87 extended/16B slot，非 ABI 差异。另有独立 MC-089 比较 helper 的有符号扩展错误，已分开修复。
- **修复所有权：** 新 `src/frontend/binary128_literal.c/.h` 用 ISO C11 整数大数实现 decimal/hex 的准确就近偶数舍入，依据 `TargetInfo` 的 x87 **64** 位或其他目标 binary128 **113** 位有效精度直接形成内部 binary128；Lexer 补合法十六进制浮点；x87 常量发射现完整处理尾数及次正规数。不存在先经过宿主 `double` 的不可逆截断，Target 仍拥有机器精度解释。
- **验收证据：** `check-binary128-literal-oracle` 与 glibc `strtof128`/`strtold` 对照 **286 组逐位一致**，含 near-tie、极值、随机；独立 ASan+UBSan 定向回归（运行器下 LSan 关闭）PASS。`check-long-double-literal-precision` 的 GNU/MiniC x86 本机程序均 exit **42**，decimal/hex/tie/subnormal/float+double 路径覆盖；RV64 GNU 与 MiniC `-nostdlib`+GNU libgcc+QEMU 均 exit **42**；x86/RV64/A64/Loong 四目标 `-S` PASS，Release `-Werror`、lexer/type/layout、x86 M1 定向测试 PASS。RV32 的既有 long-double Core frame 局限仍在；完整五 ISA ELF/QEMU、MiniAS/MiniLD 联合验收不在本轮，故不升 `VERIFIED`。

### MC-042 packed 零宽位域丢失对齐栅栏

- **状态：** `FIXED-UNVERIFIED`，P1，`GNU-NATIVE-DIFF`；合入提交 `3f6cd4f`（独立原提交 `2111b87`）。`data_layout.c` 让匿名 `:0` 以其声明类型自然对齐充当边界，即使 Record/field/pragma 为 packed，也不提高 Record 自身对齐。9 组 GNU 对照静态断言、五 ISA `-S`、x86 native exit42、Release `-Werror`/layout/record/packed 相关回归 PASS。目标 ABI 联合验收仍未执行。
- **位置/根因：** `src/target/data_layout.c:minic_data_layout_record_depth` 在所有位域上先调用 `minic_data_layout_effective_field_alignment`；`record->is_packed` 或 `#pragma pack(1)` 将 `int : 0` 的 `alignment_bits` 压成 8，再用它对齐下一个字段。GNU packed 规则下的**未命名零宽位域是对齐栅栏例外**，不能等同普通非零位域缩窄。
- **复现（基线 `400f4a611`）：** `struct __attribute__((packed)) P { char a; int : 0; char b; };`；同源 `__builtin_offsetof(struct P,b)` 与 `sizeof(struct P)`，GCC x86 目标得到 **4 / 5**，MiniC 得到 **1 / 2**；两套汇编均成功链接，按 `(offset==4 && size==5)` 构造的 harness，GCC 原生退出 **0**、MiniC→GNU AS/链接原生退出 **1**。`#pragma pack(push,1)`/`pop` 的等价 fixture 同样差分，已排除只由 `__attribute__((packed))` 语法触发；不含 packed 的同形结构，两端均为 4/5。
- **独立 ELF/跨 TU 反例：** `struct P packed_instance = {.a=1,.b=2};` 由 GCC 或 MiniC 分别发射并用宿主 GNU GAS 组成 `.o`。`readelf -sW` 显示 `packed_instance` 的 `OBJECT Size` 为 **5 / 2**；用 GCC 另行编译的 consumer 链接此对象并访问 `.b`，GNU 定义对象程序退出 0，替换 MiniC 定义对象程序退出 1。证明错误已越过编译期常量，实际破坏 x86 跨 TU 对象布局 ABI。
- **影响与修复门槛：** `offsetof`、`sizeof`、字段读写及全局初始化皆可能受到布局语义污染；复核 zero-width 与非零 bit-field、packed struct/field/pragma 及显式 aligned 的不同优先级。当前只对 x86 真实执行建立证据；共用 DataLayout 逻辑影响其他目标的风险高，但不能据此宣称其他 ISA 的 GNU/QEMU 结果。

### MC-043 Inline 整数特化预算耗尽直接拒绝合法 C

- **状态：** `OPEN`，P1，`LOCAL-COMPILE-DIFF`，不是已发射错码。
- **位置/根因：** `src/compiler/inline_integer.c:minic_specialize_inline_integer_calls` 的两处 `MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT`（2048）检查，在新 clone 超限时打印 `_LIMIT_HIT` 并 `return false`；`compiler.c` 将其升级为整个 TU `cannot specialize inline integer call sites`。这使特化预算变成了通用源程序的硬接受门槛。
- **有效输入差分（基线 `400f4a611`）：** `/tmp/minic_inline_budget_probe.c` 中同一 `static inline __attribute__((always_inline)) int identity(int x){return x;}` 在一个函数里接收 **2050 个互不相同常量**作为 callsite；GNU `cc -std=gnu11 -O2 -c` 通过，MiniC x86 `-S` 明确报 `MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT_HIT count=2048 limit=2048 functions=2050 source=0` 并失败。同一探针删去尾部两个 distinct callsite、保留 **2048** 项，MiniC `-S` 成功（隔离预算边界）。第二个独立 fixture `/tmp/minic_inline_bool_budget_probe.c` 使用 `range_sum((_Bool)(x != 0), n)` 的 1025 种整数常量，GNU `-O2 -c` PASS、MiniC `-S` 因 **布尔范围特化预算 1024** 失败；删去第 1025 项即成功。两个 pass 不同，但都把优化预算升级为 TU 硬失败，归同一类维护问题，不按每个配额另开编号。
- **第十轮局部资源清账：** 在 `src/compiler/inline_integer.c` clone quota 与 clone-append 两个失败早退处补齐 `static_blocked_sources` 释放。**这只是资源生命周期清理，不改变 2048/1024 边界对合法 C 的错拒，故 MC-043 继续 `OPEN`**；尚需针对失败路径做专用 LSan 验收。
- **2026-11-01 不可盲目软退化的真实反例：** `tests/compiler/c0/generate-mc043-budget-hostile.py` 产生同一 `static inline __attribute__((always_inline))` helper 的精确预算输入，`audit-mc043-budget-hostile.sh` 进行 GNU `-O2`、MiniC `-S`、GNU AS/link/native 差分。纯整数 2048/2049 与布尔范围 1024/1025，GNU 均接受而原 MiniC 仅在超限一侧拒绝。改为无条件保留 generic CALL 的临时实验虽然让 2049 项 `-S` PASS，但 `return __builtin_constant_p(x) ? x+1 : x` 的第 2049 次调用因无法再见到 caller constant，从 GNU native exit0 变成 MiniC native exit1。即使排除 `constant_p`、`attribute(error)` 与 section，还有同源 `__builtin_return_address(0)` 的独立 GNU native exit0 / MiniC native exit1 反例：**单调用已错，2049 调用时不安全的通用退化也会悄然放行**。因此实验性软退化 **不合主线**，保持 `OPEN`，不能把 clone 配额直接改成无条件 `continue`。
- **修复门槛：** 审计整数、符号、布尔三阶段对 clone budget、`always_inline` 与后续 Core 的真实要求；能安全使用原调用/其他保守语义路径时应受控退化，真正不能表示的源语义应给明确目标能力错误。不要盲目无限扩大预算导致 Linux 规模灾难；须同时测内存、栈、自举和回归。
- **与 [MC-068](#mc-068-always_inline-未实际展开且外部-clone-造成-init-段生命周期失配) 的关系：** 当前 `always_inline` 属性未形成必须展开的 Core 合约，clone 仍为真实函数调用。`__builtin_constant_p` 的 caller-fact 丢失可由 MC-043 超限触发；`return_address` 的调用帧身份失真甚至在未触及预算时已存在。两项都须保持未关闭，最终解决须以真正目标无关的函数体展开和 caller-context 语义验收为准。

### MC-044 函数参数列表中的 Record/Enum tag 作用域外泄

- **状态：** `FIXED-UNVERIFIED`，P1，`GNU-NATIVE-DIFF`；主线 `1921744`（独立原提交 `570ab2f`）。分别为原型、定义、内层函数指针/typedef/block-scope 形参建立隔离的 tag scope；函数定义的形参 scope 必须延续到 body，区分 `R (*fn(P))(Q)` 的 P/Q 两层绑定。14 个 GNU 合法样本五 ISA 70/70 `-S`，x86 GNU/MiniC native exit7/7，两个独立 prototype tag 的非法声明 GNU/MiniC 均拒；Release `-Werror` 与相关 7 项回归 PASS。联合 ABI/ELF 验收仍待后续阶段。
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

- **状态：** `FIXED-UNVERIFIED`，P2，`GNU-RV32-ELF-QEMU`；已完成定点 GNU RV32 ELF32/QEMU 真运行双编译器对照，项目级五架构联合验收仍按计划推迟。
- **输入与差分：** `typedef unsigned long long u64; u64 ptr_to_u64(const void *p){ return (u64)p; }` 是有效 GNU C；宿主 GCC 语法检查成功，同源 `cc -E -P` 后，MiniC `--target=riscv32-linux -S` 失败，具体记录 `<rv32-core>: ... unqualified Core instruction function=ptr_to_u64 ... kind=19 (SCALAR_BITCAST)`。x86/AArch64/Loong/RV64 对照 `-S` 全部通过，**只有 RV32 失败**。同一 RV32 中反向 `void *u64_to_ptr(u64 x){return (void *)x;}` 也可通过；多加一个中间转换 `(u64)(unsigned long)p`，RV32 `-S` 成功并发出低 32 位值、高 32 位零的成对返回，进一步锁定直接 1→2 word 的 BITCAST 路径。没有因此宣称 GNU RV32 目标运行已通过。
- **源码契约矛盾：** `src/core/core_lower_scalar_values.c` 对合法 pointer→integer cast 形成 Core `SCALAR_BITCAST`；`core_ir_verify.c` 认可这种形状；`src/target/riscv32/rv32_integer_emit.c` 看到 `from_words=1, to_words=2` 时却只准整数扩展而拒绝 BITCAST。ILP32 下 pointer 1 word、`unsigned long long` 2 words，此差异无需假定 RV64 的物理 ABI。
- **目标语义实证（基线 `8a48099`，GNU RV32）：** `riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 -O0 -S` 对 **直接** `(unsigned long long)p` 和 `(signed long long)p` 都发出 `srai high,low,31`，即将 32 位指针表示**符号扩展**到 64 位；但 `(unsigned long long)(unsigned long)p` 则将高 32 位写为零。C 的 pointer→integer 是实现定义转换，不能因为最终目标 `u64` 为无符号就盲目零扩展，也不能套用经过 `unsigned long` 的不同转换路径。故意用 `0xf1234567`（bit31=1）区分二者。
- **最小修复：** `src/target/riscv32/rv32_integer_emit.c` 仅对合法 Core `SCALAR_BITCAST` 的 1→2 word、source pointer→target integer 路径允许发射，并以 `srai t1,t0,31` 构造高字；原 `INTEGER_CONVERSION` 的有符号/无符号扩展逻辑原样保留，2→1 word 整数→指针低字截取逻辑不变，Core IR 不修改。
- **持久回归与真运行：** `tests/compiler/audit/mc048_rv32_pointer_widening.c` 覆盖直接 pointer→u64/s64、经 ulong 的零扩展区别、bit31=1/0、64 位转回指针以及 signed/unsigned 往返。`check-rv32-pointer-widening-source` 将同源 GNU 预处理 C 验证五目标 `-S` **5/5 PASS**；严格 `check-rv32-pointer-widening-qemu` 用 GNU RV32 C 编译与 MiniC RV32 编译分别生成对象，均由 GNU RV32 AS/LD 链成 ELF32、在真实 `qemu-riscv32` 上运行，结果 **GNU exit7 / MiniC exit7**。Release `CFLAGS=-Werror` 构建，邻近 `check-target-selection-m0`、`check-long-types`、`check-pointer-arithmetic`、`check-cast-expressions` 均 PASS。未运行其他四架构 ELF/QEMU 或 MiniAS/MiniLD 全联合验收。

### MC-049 静态链式数组设计符跨 ArrayType 扩容借用

- **状态：** `FIXED-UNVERIFIED`，P0，`ASAN`；已证生产源码上的 `heap-use-after-free`，区别于 MC-039 的**运行时嵌套数组**路径。局部修复提交 `f53f9a9`。
- **位置/链路：** `src/frontend/parser_static_initializer.c:1120–1197:append_static_chained_array_designator_value` 借 `MinicArrayType *array`；递归调用 `minic_parser_parse_array_designator_component` 可解析 `sizeof(int[3])` 并通过 `src/frontend/ast.c:20` 的 arena grow/realloc 移动 `Program.array_types`；`:1156` 再读取先前的 `array->element_type` 引发 ASan `READ size 72`。
- **反例：** 持久用例 `tests/compiler/audit/mc049_static_chained_array_uaf.c`（源自 `/tmp/minic-front-static-chained-array-borrow-20261030.c`），合法 GNU C 静态前向链式设计符；x86 预置 29 个、RV32/RV64/AArch64/Loong 预置 30 个 typedef 数组类型时可命中容量相位，31 个前置元素可能不触发，**无告警不是安全证明**。ASan 指向上述 read/free 栈。
- **已修与定点验证：** 在设计符递归前保存 `element_type` 和 `element_count` 两项纯值元数据，后续前缀/嵌套/后缀填充均不再依赖会被 `realloc` 移动的 `ArrayType*`。持久 `check-static-chained-array-arena` 已加入 `check-fast`；五目标×前置 29/30/31 三容量 `-S` ASan 定点 PASS，GNU/MiniC x86 原生均 exit7，Release `-Werror` 及邻近回归 PASS。**保留** 全目标 GNU ELF/QEMU、复杂多层重定位与联动验收为未覆盖。

### MC-050 x86 六组 ASM 限定符源端与机器端不一致

- **状态：** `OPEN`，P2，`LOCAL-COMPILE-DIFF`；是合法 GNU C 的 `MiniC -S` 准入缺口，**不是机器执行 wrong-code 证据**。
- **共同根因：** `src/target/x86_64/asm_contract.c` 对 source 的 GNU output carrier 用 `minic_type_unqualified()` 接受 `volatile`，而 `src/target/x86_64/machine_asm.c` 的配对 `emit_*` 对 Core output pointer pointee 直接用 raw `minic_type_equal()`，从而同一 source contract 后端拒绝。Core 的 `is_volatile=true` 是目标 ASM 副作用建模的有意转换，不属于此 bug。
- **六组独立有效模板：** `UNWIND_CONTEXT`（`x86_linux_unwind_context_source`/`emit_linux_unwind_context`）；`PKRU`（`x86_linux_pkru_source`/`emit_linux_pkru`）；`RDRAND/RDSEED`（`x86_linux_random_seed_source`/`emit_linux_random_seed`）；`FSGSBASE`（`x86_linux_fsgsbase_source`/`emit_linux_fsgsbase`）；`ATOMIC_CC`（`x86_linux_atomic_cc_source`/`emit_linux_atomic_cc`）；`BITOPS`（`x86_linux_bitops_source`/`emit_linux_bitops`）。
- **差分：** 同一 GNU11 C 每组 `cc -O0 -Wall -Wextra -Werror -c` plain/cv 均通过，MiniC `--target=x86_64-linux -S` plain PASS、cv FAIL `kind=43`。工作区复现 `x86_5_{unwind,pkru,rdrand,fsgsbase,atomiccc,bitops}_*.c`；UNWIND、ATOMIC_CC、BITOPS 的 GNU 本机与混合 MiniC plain/native 程序可执行并 exit 0；另外三组因 CPU 特性/特权限制**未执行**。
- **修复/验收：** 沿 source→Core→emitter 汇总所有输出类型与 `const/volatile` 规范化接口，成批消除六组不一致，覆盖 plain/cv/packed/读写组合。别机械替换每个 type_equal：例如 MMIO/MXCSR 已正确去 cv、某些 ASM 故意要求 exact identity。按同输入 GNU 严格 C、MiniC `-S`、GNU AS/link/native 可用项复核。

### MC-051 long double 一元取负丢失有符号零

- **状态：** `FIXED-UNVERIFIED`，P1，`GNU-NATIVE-DIFF/RV64-QEMU`；合入提交 `a0232f0`（独立原提交 `08a026f`）。x87 用 `fchs`，binary128 目标用 `__negtf2` 做真正符号翻转；扩展 17 个跨 TU 正负零、Infinity、qNaN、有限值/字面量断言。x86 GNU 参照与 MiniC 修后 `failure_mask=0x0`（旧版 `0x2010`），RV64 QEMU 旧 exit1、修后 exit0，四个 64 位目标的 `-S` 亦成功；A64/Loong 运行矩阵未执行。
- **位置与根因：** `src/core/core_lower_arithmetic.c:12:minic_core_lower_try_arithmetic_unary` 对 `long double -x` 采用 `__subtf3(+0L,x)`。IEEE 浮点有符号零下 `+0L - +0L == +0L`，但 `-(+0L)` 应产生负零；对 `float` 和 `double` 的独立 NEGATE opcode 不受此处影响。
- **可执行差分：** 持久用例 `tests/compiler/audit/mc051_long_double_negzero.c` + `_support.c`，保留 `/tmp/minic_long_double_negzero_probe.c` 的 11 项双 TU 检查。GNU x86 `failure_mask=0x0`、exit0；MiniC x86 `-S`→ GNU `cc` 链接→本机 `failure_mask=0x10`、exit1。只有 `-(+0L)` 不一致；其他正负值、`-(-0L)`、double/float、跨 TU ABI 基线符合预期。此处不能用浮点普通数值比较验证符号位。
- **修复验收：** 实现语义上的 unary sign flip（位符号/有正确负零的 TF helper），检查目标 x87/RV64 binary128 与 softfloat，补 +0/-0、NaN、Infinity、有限值及跨 TU 严格差分；仅将 `__subtf3` 换常量不保证 NaN 语义。

### MC-052 AArch64 uaccess 窄输出按八字节写回

- **状态：** `FIXED-UNVERIFIED`，P1，`A64-SOURCE`。旧问题有真实的 C lvalue 边界越界风险，但当前主线实现已消除静态错误；**没有在本环境运行 AArch64 GNU GAS/ELF/QEMU**，不能宣称目标运行验收。
- **原故障：** `src/target/aarch64/asm_contract.c:aarch64_linux_uaccess_source` 准入 `ldtrb/ldtrh/ldtr` 的 1/2/4 字节 C 输出及 8 字节 pointer 输出；旧 `aarch64_linux_uaccess_emit` 无条件使用 `str x10,[x12]` 将 8B 写到窄对象，并在 pointer 输出处额外错拒。
- **当前代码复核（基线 `1921744`）：** emit 按 `minic_data_layout_type` 得到的**输出对象大小**选择 `strb w10`、`strh w10`、`str w10` 或 `str x10`，不以 LDTR 指令传输宽度误代 C 对象大小；8B pointer output 在源端与机端均放行。错误寄存器写回仍为 `str w9,[x12]`；`1:` 故障 PC、`2:` 续执行 PC 与 `__ex_table` `.short ((9 << 0) | (10 << 5))` 的物理寄存器索引不变。
- **回归复核：** `tests/compiler/audit/mc052_aarch64_uaccess_narrow_output.c` 保留 packed guard、1/2/4/8B 输出、pointer 和 16B negative；`tests/compiler/c0/probe-aarch64-abi-audit-mc052-mc066.sh` 的 A64 `-S` 宽度检查强化为**每函数唯一 C 输出写回**、异常表 1/2 PC 偏移与 9/10 寄存器索引精确匹配。Release `CFLAGS=-Werror` 构建、宿主 GNU `-fsyntax-only -Werror`、A64 源/汇编检查及 16B negative 均 PASS。GNU GAS/readelf/objcopy/cross GCC 在此容器缺失，虽有 `qemu-aarch64`，本轮 `GNU_AS_ELF=SKIP GNU_QEMU=SKIP`。
- **关闭门槛：** 按用户延期决定，待全架构联合验收阶段执行 A64 GNU AS/ELF relocation、GNU/MiniC 双二进制 QEMU 差分（packed 邻接 guard 应保持 `0x31`），确认 fault fixup 与 ABI 行为后再由 `FIXED-UNVERIFIED` 转 `CLOSED`。

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

- **状态：** `FIXED-UNVERIFIED`，P0，`ASAN`；原版在**五目标**合法 GNU C 上按容量相位重现 `heap-use-after-free`。本修复已通过聚焦 ASan 与原生对照，尚未全目标运行验收。
- **位置/链路：** `src/frontend/parser_runtime_initializer.c:1362–1496:parse_positional_runtime_record_initializer_from` 开头借 `program->records[id]` 的 `MinicRecord *record`，解析首字段 `sizeof(struct { int z; })` 在 `:1432` 经 `add_anonymous_record`→`minic_grow_array` 扩容 Record arena，下一字段循环 `:1398 minic_c0_record_field(record,field_index)` 读取旧 header（`ast.c:2194` ASan READ8）。
- **持久反例与容量矩阵：** `tests/compiler/audit/mc056_runtime_record_positional_uaf.c` 具有 `R1..R15` 前置 Record 和普通 `struct Pair pair={sizeof(struct{int z;}),3}`。GCC GNU11 syntax PASS；三种前置数量 14/15/16 的五目标 sanitize `-S`：x86/AArch64 为 **UAF/PASS/PASS**；RV32/RV64/Loong 为 **PASS/UAF/PASS**。相位依赖 `records` capacity，非命中 PASS 不代表安全。默认 15 数量下 GCC native 与 MiniC x86 native 都退出 7，尚未独立观察到输出值差异。
- **不同于 MC-023：** 本项是**运行时局部自动对象、普通顺序字段初始化**，不依赖 static backward zero overwrite、重复 designator 或 GNU diagnostic。两处根因可归为借用指针跨递归扩容，必须在修复批次一起检查，但应分别保留回归。
- **修复与新证据（基于 `ede361e`）：** `parse_positional_runtime_record_initializer_from` 保留稳定 `RecordId`，**每个字段迭代**重新获取 `MinicRecord`，并在递归解析前复制本字段 `MinicRecordField` 元数据；无更改 union/field_limit/rollback 语义。持久回归 `tests/compiler/c0/probe-runtime-record-positional-arena-growth.sh` 使用原 `tests/compiler/audit/mc056_runtime_record_positional_uaf.c`，前置 Record 14/15/16 相位，`MODE=sanitize CFLAGS=-Werror` 下五目标 `-S` **15/15 PASS**；同一 GNU-preprocessed 输入 GNU native 与 MiniC x86 GNU AS/LD native 各相位均 **exit 7**。当前基线修复前在 RV64 15 相位 ASan `READ size 8` 已复现；修复后未再复现。
- **保留门槛：** 更多扩容周期、runtime record 设计符续接/异常出口、GNU RV32/RV64/A64/Loong ELF/QEMU 执行及其它 Record 借用 owner 独立审阅；本修复不视为 MC-023 或 MC-061 已解决。

### MC-057 GNU 空 Record 返回及实参 ABI 缺口

- **状态：** `OPEN`，P2，`LOCAL-COMPILE-DIFF`；是 GNU C extension 的目标 ABI **显式编译拒绝**，非已证静默 wrong-code。
- **有效输入：** `tests/compiler/audit/mc057_gnu_empty_record_abi.c`，分别 `-DTEST_RETURN`（定义空结构体返回函数）、`-DTEST_PARAM`（定义空结构体参数函数）、`-DTEST_CALL_RETURN`（调用空 Record 返回者）、`-DTEST_CALL_PARAM`（调用空 Record 参数者）；四个 case 均通过 `cc -std=gnu11 -fsyntax-only` 和预处理。RISC-V GNU psABI 对支持此扩展的 C 编译器明确规定 empty struct/union 参数和返回在物理 ABI 中被忽略（[psABI 规范](https://riscv-non-isa.github.io/riscv-elf-psabi-doc/)），不是需要凭空分配一个物理返回寄存器。
- **历史 `-S` 六目标×四 case：** RV32I = FAIL/FAIL/PASS/PASS；RV64I/LP64、RV64IMAFD/LP64D、x86-64 均 FAIL×4；AArch64、LoongArch64 均 PASS×4。RV32 的调用侧两项能通过，不能概括为 RV32 全部不支持。
- **历史结构原因：** `riscv32/rv32_abi_plan.c:rv32_plan_shape` 正确识别 `words=0`，但 `rv32_emit.c:rv32_term` 的返回路径仅在 `record_return_size!=0` 时接管 Record，否则误进入 scalar-only 检查而失败。RV64 `abi.c` 将 empty Record 归为 `MINIC_RISCV64_ABI_VALUE_IGNORE`，而 `core_codegen.c:emit_terminator` 只处理 aggregate/indirect，IGNORE 未实现；x86 的独立拒绝路径仍需逐函数关闭定位。
- **RV32 子项修复与定点证据：** 基于 `3a876a290c53f476` 单独复现：原四项 `TEST_RETURN/TEST_PARAM/TEST_CALL_RETURN/TEST_CALL_PARAM` 的 GNU C 均 PASS，RV32 `-S` 原为 FAIL/FAIL/PASS/PASS，拒编点 `rv32i: unsupported terminator or CFG edge`。`riscv32/rv32_emit.c:rv32_term` 现在先按**真实 Record 返回类型**分流；非空 Record 仍走原有 `rv32_emit_record_return`，零字节 Record 必须持有合法 Core object、无 hidden sret，直接跳既有函数返回块，**不凭空读写 a0/a1**，scalar/void 路径未变。修后原四项 RV32 4/4 PASS。
- **持续回归：** `make MODE=release CFLAGS=-Werror check-rv32-empty-record-return` 在 Release -Werror 下 PASS。新增 GNU-valid struct/union+typedef fixture，覆盖三个 `if` 出口、直接返回、入参原样转发、函数指针间接返回调用、空 Record 穿插三个整数实参；宿主 GNU native 的 empty 与 one-byte nonempty sibling 均 exit42。RV32 empty `-S` PASS，静态检查空返回无 a0/a1 payload，穿插的整数参数仍在 a0/a1/a2；union typedef 另有 RV32/A64/Loong `-S` 3/3 PASS。nonempty sibling 五 ISA `-S` 5/5 PASS，MiniC x86 GNU AS/ELF native exit42；A64、Loong empty `-S` PASS。x86 与 RV64 empty 当前仍明确拒编，本次不触碰其独立 ABI 消费者。
- **未验边界：** 环境没有 RV32 GNU cross assembler/compiler 与 `qemu-riscv32`，所以真正 RV32 GNU GAS/ELF/QEMU 运行证据标为 `SKIP`；宿主 GNU native 和 RV32 静态汇编检查不能替代该验收。其它目标 empty Record 以及更多变参/间接外部函数 ABI 留在 MC-057 的整体 OPEN 范围内。
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

- **状态：** `FIXED-UNVERIFIED`，P0，`ASAN`；合法 GNU C **Record 定义阶段**即能触发 `heap-use-after-free`，完全不需局部或静态 initializer，与 MC-023/MC-056 不同入口。修复提交 `449eab2`（原隔离提交 `33ac095`）。
- **失效链：** `src/frontend/parser_record.c:parse_record_field_declarator` 在开头保存借用 `MinicRecord *record`；字段 `int (*field)[sizeof(struct {int z;})]` 经 `parse_pointer_to_array_field_declarator`→`parse_array_declarator_suffix`→`parse_sizeof`→`minic_c0_program_add_anonymous_record` 增长 `Program.records`；返回后 `parser_record.c:443:record_has_field(parser, record, name_span)` 读失效 header（ASan READ8）。
- **持久反例：** `tests/compiler/audit/mc061_record_field_pointer_array_uaf.c`；GNU11 `-fsyntax-only` 与原生可执行控制通过；五目标 ASan `-S` 的前置类型数量 14/15/16：x86/AArch64 = **UAF/PASS/PASS**，RV32/RV64/Loong = **PASS/UAF/PASS**，证明与 arena capacity 相位相关。未触发相位上的 PASS 不代表该借用安全。
- **已实施：** `parse_record_field_declarator` 在递归 field declarator 和尺寸/完整性检查之后，以稳定的 `record_id` 重新从 `Program.records` 获取 `Record*`，再执行重复字段判定；没有改变 AST 类型或 ABI。持久门禁 `check-record-field-declarator-realloc` 已接 `check-fast`。修复前可见 ASan READ8→`realloc` 栈；修复后五目标×14/15/16 三种容量相位 ASan/UBSan `-Werror -S` **15/15 PASS**，GNU/MiniC x86 原生均 exit12，重复字段负例双方拒绝；Release `-Werror` 与原 MC-063/067 正控亦 PASS。
- **未覆盖：** 仍未跑完整五架构 GNU ELF/QEMU/Linux 与 MiniAS/MiniLD 联合验收。此项是局部已修，不能将整个链条标为全验收完成；MC-002 已确认共用本修复并有单独入口回归，MC-003/023/056 各有独立生命周期路径。

### MC-062 x86 ALT Reloc 的 Memory 输入源机约束分裂

- **状态：** `OPEN`，P2，`LOCAL-COMPILE-DIFF`（另有 GNU ELF relocation 实证）；**不是**内核 alternative 的运行时执行差异。
- **Source 与 Machine 不一致：** `src/target/x86_64/asm_contract.c:x86_linux_alt_reloc_selftest_source` 仅检查 GNU `"m"` memory input 是可寻址 lvalue；`src/target/x86_64/machine_asm.c:emit_linux_alt_reloc_selftest` 却额外要求 Core 定义为 `GLOBAL_ADDRESS` 且符号名固定 `__alt_reloc_selftest_addr`，机器替换段硬编码 `leaq __alt_reloc_selftest_addr(%rip),%rdi`。因此 source contract 接受的合法其他符号无法兑现。
- **差分：** `tests/compiler/audit/mc062_x86_alt_reloc_memory_operand.c` 默认 `other_alt_reloc_addr` 和 `-DALT_GOOD` 固定符号正控均通过 `cc -std=gnu11 -Wall -Wextra -Werror -c`；GNU `readelf -r` 的 `.rela.altinstr_replacement` 对应 `R_X86_64_PC32` 从 `__alt_reloc_selftest_addr` 变为 `other_alt_reloc_addr`，是实际 operand 重定位语义。MiniC GOOD `-S` PASS，BAD `-S` FAIL `kind=43`，没有坏例可执行文件。
- **后续设计：** 明确该特化仅承诺 Linux 固定符号还是泛化 GNU memory-input lvalue：前者应在 Source gate 精准限制且交给符合 ABI 的其他路径，后者应从 Core operand 解析动态 symbol+offset 且保留 replacement PC-relative relocation。不能只在 Machine 中硬编码后把 Source 接收范围当正确。

### MC-063 带标签的裸 Record 声明被误作匿名成员

- **状态：** `FIXED-UNVERIFIED`，P1，`GNU-NATIVE-DIFF`；主线修复 `6aac3c1` 仅在 `parse_record_field` 判断 direct `struct/union` + tagless record 才放入匿名成员。裸 tagged 声明和 typedef-name-only 不更改布局，现有真正匿名成员仍保留。
- **位置与语义：** `src/frontend/parser_record.c:parse_record_field` 在 `minic_type_is_record(base_type) && current==SEMICOLON` 时统一向外层 Record 添加匿名字段；但 GNU C `struct Named {int x;}; struct Wrapped {struct Named; int y;};` 的带标签裸类型声明**不声明匿名成员**，GCC `-Wall` 提示 `declaration does not declare anything`。相对地 `struct {int x;};` 是可用的 GNU 匿名 Record 成员，二者必须区分。
- **持久反例：** `tests/compiler/audit/mc063_tagged_bare_record_field_layout.c` 同时检查两种语法。GCC GNU11 x86 native 得到 `sizeof(Wrapped)=4`、匿名 Record 组合大小8，整程序 exit **48**；同一输入 MiniC x86 `-S`→GNU AS/cc→native exit **88**，两个结构都被算作8；五目标 `-S` 都生成错误的 tagged 结构大小8。GCC tagged case 有警告而非错误，不能称为 ISO C 保证。
- **合入后验收：** `check-tagged-bare-record-members` 接 `check-fast`，历史反例 GNU/MiniC x86 native 现均 exit **48**；新正例对 bare tagged、typedef-name-only、真正匿名 struct/union 的布局分别做 GNU 差分，非法 incomplete named field 拒绝。主线 Release `-Werror` 和上述定点门禁 PASS，其他目标完整 GNU ABI/运行矩阵仍待验。

### MC-064 Inline ASM 仅符号引用的静态函数被错误裁剪

- **状态：** `OPEN`，P1，`RV64-ELF-LINK-DIFF`；已由仅生成汇编证据升级为**真实目标 GNU LD undefined static function**。负例无法链接，故不能宣称负例已 QEMU 执行；GNU 同源正例和两个 MiniC 控制例已 QEMU native 运行。
- **链路：** `src/core/core_lower_asm_operands.c:core_inline_asm_immediate_text` / `core_inline_asm_specialize_immediates` 将 GNU `"i"(asm_only_target)` 的静态函数引用转成裸字符串；`src/core/core_lower_asm_scalar_forms.c` 形成 `OPAQUE_INLINE_ASM`。`src/compiler/core_reachability.c:minic_walk_reachable_core_edges` 只跟 `CALL`、`FUNCTION_ADDRESS`，`minic_prune_inline_specializations_from_core` 把缺少普通 C 引用的静态函数标记不发射。前端/Lower 已通过，缺失发生于可达性身份转换。
- **持久反例：** 原 x86 专属 `%c0` 文件 `tests/compiler/audit/mc064_asm_only_static_function_reachability.c`（GNU x86 `-O0/-O2` 保留 LOCAL FUNC）；新目标合法 `%0` 文件 `tests/compiler/audit/mc064_rv64_asm_static_symbol.c` 和 `probe-mc064-rv64-elf-link.sh`。同一个 GNU RV64 预处理 `.i`：GNU GCC→GNU LD→QEMU exit11 PASS；MiniC `-S`→GNU AS PASS，但 GNU LD `.rodata` 报 `undefined reference to asm_only_target`。改为 `MC064_DIRECT_CALL` 时双方 QEMU exit23；ASM-only static object `&asm_only_object` 时双方 exit11。不能泛化为所有 ASM 符号缺失。
- **修复与验收：** 将 ASM 中 function/object relocation source identity 保持在 Core **独立 typed 符号引用**中并加入 reachable Core block 的 reachability roots，不能用全文本符号子串推测身份，也不能重载现有 `source_inline_asm_id`（RV32/RV64/A64 将其视作 deferred ASM owner，Loong 还会据此回读原 C0）。当前**未修复**，无需碰触 opaque ASM 的文本输出合同；最窄接缝和证据见 `docs/architecture/review-2026-11-01-mc064-rv64-elf-reachability.md`。后续需验有/无普通 C 引用、别名、used、死 Core block、多个目标 GNU AS/ELF relocation 与链接。

### MC-065 x86 R11 跨调用快照误报排除

- **状态：** `CLOSED-NOT-A-BUG`；编号永久占用，不计入 OPEN 缺陷数量。
- **候选：** 一个 x86 原生对照让内联汇编写 R11，另一次内联汇编无显式输入依赖而读取 R11，出现 GCC/MiniC 数值差异。
- **排除依据：** 两段 ASM 间包含 C 函数调用，x86-64 ABI 的 R11 是 caller-saved；读取它的 ASM 没有声明相应输入或保活依赖。此测试不能对 R11 的原值建立有效语义约束，因而不能判为 MiniC wrong-code。只有构造出遵守寄存器约束和调用约定的独立反例才能以**新证据**重开调查。

### MC-066 AArch64 浮点指针字段被误判为浮点聚合

- **状态：** `FIXED-UNVERIFIED`，P2，`GNU-NATIVE/5ISA-S`；历史合法 GNU C 的显式拒编已在所指定基线 `6b1c45cf` 中消失。尚无 AArch64 GNU ELF/QEMU 双向运行验收，不推断完整 HFA/packed ABI 已实现。
- **历史根因与当前状态：** 原 `src/target/aarch64/abi.c:aarch64_special_aggregate_member` 在 pointer/array 层级消解前检查 `type.base_kind`，将 `double*` 的 DOUBLE pointee 错认 FP aggregate leaf，继而由 `minic_aarch64_indirect_record` 错拒大 Record。当前基线源码**已**将 `minic_type_is_pointer` 和数组递归判定置于 FP base_kind switch 之前，且旧 `tests/compiler/c0/probe-aarch64-abi-audit-mc052-mc066.sh` 的 double*/int*/double*[]、真实四 double HFA 控制全部通过；本任务不重复制造无效源码 diff。
- **扩展持续回归：** `make MODE=release CFLAGS=-Werror check-aarch64-fp-pointer-record-abi` 以同一合法 GNU C 生成 16/17/32/64 字节精确对象形状，覆盖 double*、int*、double* 数组、double* typedef、pointer-to-double-array、pointer-to-FP-record，以及 17/32/64 字节 `packed` 非自然对齐 double*。五目标 `-S` **134 项全部 PASS**；宿主 GNU C 的独立 caller/callee ELF 原生四种尺寸均 exit42。32B 实际四 double HFA、packed 四 double HFA 以及尚不支持的小尺寸 16B packed 非对齐 pointer Record 均为可核对的 A64 ABI fail-closed（3/3），没有用整数/间接 GP 错误放行。
- **剩余边界：** 本环境有 `qemu-aarch64`，但缺 `aarch64-linux-gnu-gcc/as/readelf`，脚本明确报告 `A64_GNU_ELF=SKIP A64_GNU_QEMU=SKIP`，故无真正 A64 ABI Caller/Callee 原生或 QEMU 证明。AAPCS64 HFA/HVA 全面传递、`<=16B` 非对齐 packed Record 的合法 ABI marshalling、含 FP value 的非 HFA 大复合 Record 仍属于独立未支持/未验区域，不能因本项通过而放宽准入。

### MC-067 带 GNU 属性的 Record 前向声明被错判为定义

- **状态：** `FIXED-UNVERIFIED`，P2，`GNU-NATIVE-DIFF/SOURCE`；主线修复 `072ca74` 在 record、file-scope、block-scope 三个分派 lookahead 中识别 tag 前/后 GNU 属性，实际属性校验仍由 record 解析 owner 完成。没有扩大 Target ABI 支持。
- **位置/失配：** `src/frontend/parser_record.c:minic_parser_parse_record_definition` 使用浅拷贝 lexer lookahead 仅匹配 `struct IDENTIFIER ;` 为 forward，遇 `struct __attribute__((aligned(16))) Forward;` 时错误调度到 `minic_parser_parse_record_definition_specifier`，后者要求 `{`，报 `expected '{' after record specifier`。其余没有属性的 tag forward 路径可用。
- **持久反例：** `tests/compiler/audit/mc067_attributed_record_forward_declaration.c`；GCC `-std=gnu11 -Wall` 正常编译无警告、x86 native exit4；MiniC x86/A64/RV32/RV64/Loong `-S` **0/5 PASS**。`-DMC067_PLAIN_CONTROL` 仅移除属性，同一五目标 **5/5 PASS**。
- **合入后验收：** `check-attributed-record-forward-declarations` 接 `check-fast`；GNU/MiniC 对相同 `.i` 的 x86 native 新正例 exit0、历史 audit exit4 一致，另四目标 `-S` PASS；非法同名 struct/union kind 冲突两者均拒。完整 Linux、不同目标 GNU AS/LD/QEMU 及属性布局 ABI 联合验收未运行。

### MC-068 always_inline 未实际展开且外部 Clone 造成 init 段生命周期失配

- **状态：** `OPEN/DEBT`，**P1 架构正确性债务**，`CI-LOG/GNU-NATIVE-DIFF`；仍未修复、未完成 init 段 GNU LD/QEMU 全链验收，**没有证据宣称已出现运行时 UAF**。
- **精确证据：** Linux 6.6.143、MiniC exact HEAD `f493adffa6b14d4bd4ac1877c2d6c503f79556aa` 的 [CI #41002539295](https://github.com/yituanxing/minic-toolchain/actions/runs/41002539295)，七路对象编译通过并进入 GNU Kbuild `MODPOST vmlinux.symvers`。GNU modpost 报 `__minic_inline_spec_8859_28+0x104 (.text) -> memblock_alloc_try_nid (.init.text)`、`__minic_inline_spec_6114_72+0xd4 (.text) -> memblock_phys_alloc_range (.init.text)` 等多条 section mismatch；也出现普通 inline wrapper `memblock_alloc_from (.text) -> memblock_alloc_try_nid (.init.text)`。**本次最终 GNU LD 因独立的 `__compiletime_assert_*` 未定义符号失败，不是这些 modpost 警告导致 LD 失败。**
- **源代码链路：** `src/frontend/attribute.c:272–279` 已识别 `always_inline`/`__always_inline__`；`src/frontend/parser_function.c:function_attribute_class_is_parse_only`（约 `:54–68`）将 optimization 属性按 parse-only 处理，函数语义没有持久强制内联标记；`src/compiler/inline_integer.c:minic_add_inline_integer_specialization`（`:36–94`）生成可调用的 `__minic_inline_spec_*` 外部函数，仅在源函数显式有 `section_name` 时继承，其余落入默认 `.text`；`src/core/core_lower_calls.c`（约 `:804`）保留真实 `MINIC_CORE_INSTRUCTION_CALL`，而非将函数体纳入调用方。Linux `include/linux/memblock.h` 中 `memblock_alloc`、`memblock_phys_alloc` 为 `static __always_inline` 包装器，分别调用生命周期有限的 memblock init 函数。**这不是仅某个目标后端的特例；普通 `static inline` 未消除的同类跨段调用也须审计。**
- **新增独立 GNU native caller-context 反例：** `tests/compiler/c0/audit-mc043-budget-hostile.sh` 的 `frame1` fixture 为 `static inline __attribute__((always_inline)) void *probe(int x){(void)x;return __builtin_return_address(0);}`，其调用方比较 `probe(0)` 与自己的 `__builtin_return_address(0)`。GNU `-O2` 本机 exit0；MiniC x86 `-S`、GNU AS/链接均成功但本机 **exit1**，即**一个调用点就可 wrong-code**，不需要耗尽 MC-043 预算。2049 个常量调用时若保留 generic CALL，仍会把合法 GNU C 错误放行并在本机运行失配。该证据只证明 caller-frame 语义失真，**不代表已重现 Linux init 段 UAF**；详见 [MC-043](#mc-043-inline-整数特化预算耗尽直接拒绝合法-c)。
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

### MC-076 x86 early head 同一全局符号混合物理访问与链接 VMA 地址错编

- **状态与范围：** `OPEN`，P0，`GNU-NATIVE-DIFF/ELF-ORACLE`，`x86_64`、`-fno-pie`、`.head.text`。原始 `3d504e7` 的错误已经在 x86 主机通过 GNU GCC 对照、GNU AS ELF relocation 和**异址真实运行**确认；本地正在做按 SSA/use-role 修复及更广的地址逃逸验证，不能凭已修的两个 case 声称全部 early-boot 场景安全。
- **原始机制：** `src/target/x86_64/codegen.c:x86_head_find_direct_data_globals` 由 Core LOAD/STORE 找到某个 `global_id`，以每函数布尔位 `direct[global_id]` 传给 `machine_emit.c`。机器层因此将该符号的**每一次** `GLOBAL_ADDRESS` 都发成 RIP-relative 地址；同一 C global 的实际物理内存解引用需 RIP-relative，但 `(unsigned long)&global` 的链接地址算术需要绝对 VMA。`global_id` 只代表符号身份，不代表 use role；不同 SSA 地址生产者仍共享它。
- **最小反例：** 同一 `volatile unsigned long global`，两个 `__attribute__((section(".head.text"),noinline))` 函数分别执行 `global=value; return (unsigned long)&global;` 与 `vma=(unsigned long)&global; global=value; return vma;`。对同一 C 源，GCC `.rela.head.text` 具有数据 store 的 `R_X86_64_PC32` 与返回 VMA 的 `R_X86_64_32/32S`；原 MiniC 四次全为 `R_X86_64_PC32`。将代码与 global 同偏移复制到新的可执行内存段，要求**写入复制后的物理位置且返回原链接 VMA**：GNU **2/2 PASS**，原 MiniC **2/2 FAIL**。这不能被普通链接地址执行的 smoke 检出。
- **最小修复方向与限制：** 仅针对真正证明的角色，在 `GLOBAL_ADDRESS`/SSA consumer 边界区分链接 VMA 和物理访存；直接/原子 LOAD/STORE 的 use-site 可做 VMA→RIP rebasing，必须保留动态 field offset。地址经局部指针变量、PHI、未知 helper 或 inline ASM 逃逸后，若没有跨对象 provenance/别名证明，**不能声称安全**；其他目标、PIE 或普通 `.text` 必须保持原有选路。新增永久回归在 `tests/compiler/c0/x86_kernel_head_link_address.c`、同名 driver 与 `probe-x86-kernel-head-link-address.sh`。
- **本地定向验证与剩余错误：** 第二版实现保持已有 direct-global pointer SSA 的 RIP 物理地址默认，只在 `SCALAR_BITCAST(pointer→integer)` **结果使用点**换成 VMA；若数值再作为内存地址，LOAD/STORE 再重基址，避免“同一 SSA 同时传 helper”在第一版修法中被 VMA 污染。编译与永久 probe 通过；新 probe 用同一 `.i` 额外执行 GNU GCC→GNU AS/ELF→异址运行，`gnu_same_source=PASS`，MiniC mixed case 两个方向 PASS。**但** `volatile unsigned long *local=&global; *local=value; return (unsigned long)local;` 经 local pointer 对象 STORE/LOAD 运输，Core 回溯无法穿越内存别名，仍有预映射物理地址不安全的路径。另用 `.head.text` caller + 普通 `.text` out-of-line helper 在 mmap 异址运行：MiniC 的启发式物理传参 PASS，而**同源 GNU `-O0` FAIL**，说明这是当前 MiniC 特有的 early-head 物理访问策略，不能把该 helper 正控说成 GNU 语义等价。因此整个 MC-076 **继续 `OPEN`**，不是仅凭两个正例标 `FIXED-UNVERIFIED`。
- **完整验收：** 完成 direct mixed-use、共享 SSA、helper pointer、内存间接、动态 field offset 和高半区链接地址的 GNU `.o` relocation/异址执行差分；使用真正 GNU AS/LD；必要时将地址角色在 Core IR 中显式化，不扩散 x86-only 字符串启发式。最后以 exact-head Linux x86 pre-MMU 启动真实 QEMU 验证，未达到前保留 `FIXED-UNVERIFIED` 或 `OPEN`。

### MC-077 va_start 首参递归使 Function arena 借用失效

- **状态与范围：** `FIXED-UNVERIFIED`，P0，`ASAN`，Frontend `src/frontend/parser_builtins.c:parse_builtin_va_start`；LoongArch64 target machine builtin 提供一条可以构造 Function arena 扩容的有效递归路径。最小修复和独立 sanitizer 同输入回归已通过，尚未做五目标全量/整编译器 ASan。暂不推断所有目标均可触发相同序列。
- **实证触发：** 预置 15 个 `extern` 函数占据 Program 的函数槽，当前 variadic 函数进入第 16 槽；`__builtin_va_start` 首参包含 `0U * __builtin_loongarch_cpucfg(0U)` 的可求值 lvalue 索引，递归 target builtin 在 `parse_target_builtin` 添加第 17 个 Function，`src/frontend/ast.c:minic_grow_array` 发生 `realloc`。独立 /tmp sanitizer 构建的 ASan 报 `heap-use-after-free`：读端 `parser_builtins.c:1558`，释放链 `ast.c:20`→`:989 minic_c0_program_add_function`→`parser_builtins.c:2093 parse_target_builtin`；普通 Debug 编译器同输入 `-S` 通过且实际发出 `cpucfg`，排除短路分支造成的假复现。
- **所有权不变式：** `FunctionId` 可在容器扩容后继续使用，旧 `MinicFunction*` 则不可；递归 parse 之前复制标量 `parameter_count` 或在递归后用 ID 重新取得，且不得改变 GNU variadic 参数验证、va_list lvalue 或机器 builtin 的既有行为。
- **实际修复与当前验收：** 在第一次递归 `minic_parser_parse_expression` 前缓存 `size_t parameter_count`，递归后不再访问原始 `function` 指针。新增 `tests/compiler/c0/loongarch64_va_start_nested_builtin_arena.c` 与 `probe-loongarch64-va-start-nested-builtin-arena.sh`；修复前独立 sanitizer 报 UAF，修复后对原 `/tmp` `.i` **EXIT0**，新增脚本用独立 sanitizer 与普通 Debug 编译器均 **PASS**，发出的 Loong `.s` 包含实际 `cpucfg $a0, $a0`，不是死代码空转。保留普通变参正反例及跨目标回归待验；其他 `Function*`、`Expression*`、`Record*` 的递归借用独立审计，不能因此自动关闭 MC-001～005 等原有 ASan 问题。

### MC-078 x86 PAT CLFLUSHOPT 源端接受却被机器端拒绝的 unused 立即数

- **当前状态（2026-11-01）：** `FIXED-UNVERIFIED`，P2；`7d32d43` 消除 Machine PAT 对未使用 `"i"` 输入的 `==0` 误设门槛，保留类型、Core INTEGER_CONSTANT、SSA 结果身份及 ALT 版本防伪。合并后 Release `-Werror` 编译和 `check-x86-pat-clflushopt-alt` PASS：GNU/MiniC→GNU AS/LD x86 native 的 `i(0)`、`i(1)`、`i(-1)` 均 exit42，非常量负例双方拒绝，未知 ALT feature 位仍拒绝。未跑完整 Linux；原 `eddd49c` 的 `OPEN`/错拒是历史发现状态。
- **真实接缝：** `src/target/x86_64/asm_contract.c:x86_linux_pat_clflushopt_source`（约 `:8365–8395`）要求完整 Linux PAT ALT 模板、一个 `+m` 输出及一个 `"i"` 输入，却仅验证该输入属于整数类型；它不参与模板寻址 `%P0`，因此有效输入数值不受 C/GNU ASM 模板约束。`src/target/x86_64/machine_asm.c:emit_linux_pat_clflushopt`（约 `:5176–5210`）却要求此输入的 Core SSA 定义**必须为 INTEGER_CONSTANT 且 `integer_value==0`**，不满足就让后端将整个 structured ASM 判为 unsupported。源资格、Core 传递和机器执行的判定合同没有统一。
- **最小有效差分：** 从现有 `tests/compiler/c0/x86_pat_clflushopt_alternative.c` 只把 `: "i" (0)` 换成 `: "i" (1)`，保存 `/tmp/minic-f11-x86-pat-alt-input1.c/.i`。`gcc -std=gnu11 -O0 -no-pie ... -o ...-gnu` **成功**；原生程序 exit **42** 是这个 fixture 自身规定的成功返回码，不是两编译器运行结果对比。MiniC `build/debug/bin/minic-cc --target=x86_64-linux -S ...i` **exit 1**，诊断 `x86-64 Core instruction unsupported: function clflushopt, kind 43`。本轮负责人已再次执行 GNU 编译与 MiniC `-S` 并确认 0/1 差分。
- **清理/修复门槛：** 先核 template 绑定的 GNU 真实作用及其他 unused immediate 值域，不允许机器端额外私设不可见的 `==0` 条件；需在 Source qualifier 或完整 Machine contract 中表达同一个合规值域，不能只删掉机器端防伪。补永久同源 `i(0)`/`i(1)`、真正引用该输入的反例、GNU AS/ELF 与原生执行差分，随后逐族清掉 Source→Machine 的二次字符串分类（另见 A8-10/A9-10），不能把 MC-050/062 的不同旧问题当作本根因。

### MC-079 Core 取址解引用抵消绕过 Local 事实逃逸造成静默错编

- **当前状态与证据（2026-11-01）：** `FIXED-UNVERIFIED`，P0。该问题在 C1 之前的 `6934c9e` 已有修复及仓内持久测试 `tests/compiler/c0/probe-core-indirect-local-fact-escape.sh`；在含 C1 及 MC-083 的 `15b08f4` 上，以 `-Werror` 构建的 MiniC 运行 `make ... check-core-indirect-local-fact-escape`，得到 `CORE_INDIRECT_LOCAL_FACT_ESCAPE=PASS GNU_C_and_MiniC_GNU_AS_LD_runtime`。**这是 x86 同源 GNU C/MiniC→GNU AS/LD 原生定向回归，不是五目标 Linux 验收。** 原 `eddd49c` F11 的 GNU exit22 / MiniC exit11 是历史发现证据，不再代表当前 HEAD。
- **最小有效 GNU C：** `int probe(void) { int x=1; *(&x)=2; if (x==1) return 11; return 22; } int main(void){ return probe(); }`。`cc -std=gnu11 -O0 -no-pie -x c ...i -o gnu.bin` 成功、原生退出 **22**；相同预处理 `.i` 经 `build/debug/bin/minic-cc --target=x86_64-linux -S`、宿主 GNU AS/link 成功，运行却退出 **11**。证据临时文件 `/tmp/minic-f11-address-jyoZgd/{indirect.i,gnu.bin,minic.s,minic.bin}`；MiniC `.s` 能看见 `*(&x)=2` 真实写入内存，随后的条件已被 Core 折叠为直接返回常量 11，故不是机器 store 丢失或不同优化级的合理差异。
- **源码所有权链：** `parser_expression.c` 保留 `ADDRESS_OF`/`DEREFERENCE` 的合法 lvalue；`core_lower_address.c`（约 `:341–353`）将 `*&object` 直接递归降低成底层 `LOCAL` 地址。普通 `ADDRESS_OF LOCAL` 在 `core_lower_expression.c:383–389` 会先调用 `minic_core_local_fact_escape`，但该优化抵消路径完全绕过 escape 记录；`core_lower_assignments.c:174–179` 只对语法上直写 `MINIC_EXPRESSION_LOCAL` 的赋值清旧事实，间接 `*(&x)` 仍被认为 `x==1`；`core_lower.c:674–685` 将 `core_const_eval_integer_with_locals` 的过时结果用来选择单分支。相关 A8-05、MC-033 是不同的 CFG/更新缺口，不能靠另一个局部补丁静默合并编号。
- **修复门槛：** 明确表达式 canonical lvalue 身份和语义 local write/escape effect，在 `*&` 抵消之前或之后保证 fact invalidation 一致；不要机械关闭地址消除优化以牺牲性能，也不能仅针对 `x` 名称做特例。最少覆盖 `*(&x)`、`&*(&x)`、字段/数组 lvalue、volatile 与嵌套赋值、同一 local 经指针变量/控制流分支重写，逐项 GNU-native diff 后在 Core effect/facts 层收敛，避免每一种 source 表达式各造一种失效处理。

### MC-080 函数指针间接变参调用拒绝合法的 Aggregate 实参

- **状态与证据：** `OPEN`，P2，`LOCAL-COMPILE-DIFF`，范围 Frontend→Core→五 Target 的 GNU C 间接调用 ABI 准入；这是一项有效源码的编译覆盖缺口，**没有**执行时 wrongcode/ELF 差分。既有 MC-057 空 Record、MC-059 RV64 LP64D union FP aggregate 是不同根因。
- **同源控制矩阵：** `struct Pair { long x; }; typedef int (*V)(int,...); int via_ptr(V fn){ struct Pair p={7}; return fn(1,p); }`；GNU `-std=gnu11 -Wall -Wextra -Werror -fsyntax-only` PASS。同一 fixture 在 `/tmp/minic-rv64-weak-Nfhwou/rv_variadic_aggregate.c` 由三个宏隔离：`CASE_INDIRECT_VARIADIC`：MiniC x86/RV32/RV64/AArch64/Loong 五目标 **0/5**，一致报 `unsupported variadic call argument type`；`CASE_DIRECT_VARIADIC`：**5/5**；`CASE_INDIRECT_FIXED`：**5/5**；三个 GNU C case **3/3 PASS**。负责人独立再次确认 x86 indirect FAIL、RV64 direct PASS。
- **源→机器链：** `src/frontend/parser_postfix.c:259–264` 在间接变参 `...` 后直接筛选 `integer/pointer/double`，抛弃合法 Record，即便 `minic_parser_apply_default_argument_promotion` 对非标量已经原样成功返回。直接调用走 `parser_expression.c` 的独立参数处理，允许 Record。**Core 也有独立阻断：** `core_lower_calls.c:833–851`、`core_ir_verify.c:849–867` 的 direct variadic 允许 OBJECT，间接路径的 `core_lower_calls.c:1095–1117`、`core_ir_verify.c:955–973` 仍只接受 scalar。RV64 `src/target/riscv64/legalize.c:357–444`、`call_emit.c:405–414` 对非 direct 的 unnamed Record 还有机器端二次拒绝，故仅放开 Parser 不构成实现完毕。需要核查其他四目标真 psABI 能力，不能凭 source 0/5 推断所有后端已经准备好了。
- **设计与验收门槛：** 将 direct/indirect 的 CALL 签名、变参默认提升、aggregate snapshot 在统一 Core CallView 合同内表达，随后各目标按真实 ABI 放置，机器防伪仍保留。测试 indirect variadic/fixed、direct variadic、空/packed/多寄存器 Record、不同 ABI 和返回值；GNU C→MiniC -S→目标 GNU AS/ELF→GNU caller/provider/QEMU 或原生运行。不得用拒绝合法调用作为规避后端设计的默认行为。

### MC-081 RV32 接受 GNU constructor 却漏发初始化注册

- **当前状态（2026-11-01）：** `FIXED-UNVERIFIED`，P1；提交 `2652738` 在 RV32 函数正文输出后用既有 Function symbol 发射 `.init_array` 的 4 字节注册项。合并后 Release `-Werror` 构建、`check-rv32-constructor-init-array` PASS：同一 `.i` 的 GNU/MiniC 两种生成物经目标 GNU AS/LD ELF32、readelf INIT_ARRAY 检查和真实 `qemu-riscv32` 都 PASS，两 constructor（internal/external）运行且普通函数不误执行。完整 Linux/priority 与复杂 ABI 场景尚未验收；原 `eddd49c` 的 ASM-only 是历史发现证据，当时尚无 RV32 运行环境，现已补齐定向 ELF/QEMU。
- **同源探针：** `/tmp/minic-rv64-weak-Nfhwou/rv_constructor.c/.i`：`static int seen; __attribute__((constructor)) static void initialize(void){ seen=73; } int main(void){return seen!=73;}`。RV32 `constructor_riscv32-linux.s` 存在 `initialize:`，整份 `.s` 无 `.init_array`；RV64 `constructor_riscv64-linux.s` 同时有 `initialize:` 与 `.section .init_array,"aw",@init_array`，x86/A64/Loong 同样保留注册段。不是把未发射函数误报为未注册。
- **生产者与消费者：** Frontend `parser_function.c`（约 `:571–575,2193–2195`）已将 GNU constructor 属性保存到 Function 声明。RV32 `rv32_emit.c:908–918` 的函数 visitor 只发函数正文，不输出 init_array 注册；对照 RV64 `codegen_function.c:88–93`、Common Direct A64/Loong `direct_function_emit.h:105–108`、x86 `codegen.c:563–566` 均有真实输出。当前问题是目标发射职责**没消费现成属性**，不是需要发明新 Source AST 或新 Core opcode。
- **修复门槛：** RV32 使用已有函数声明属性生成符合 GNU ELF32/ILP32 的 `.init_array` 入口，处理 `static`、priority、section/alignment、多个 constructor 的保活/次序，不复制整个 Common 全局分类算法。加入目标 GNU GAS/readelf `-SW -Wr -Ws` 与可运行启动/初始化顺序差分，再升级验证级别。

### MC-082 x86 SysV 三字节 Record 按值参数与返回被拒绝

- **状态：** `FIXED-UNVERIFIED`，P2，`GNU-ELF-NATIVE`；1..16B 自然对齐全 INTEGER-class Record 的参数与返回已完成 GNU/MiniC ABI 双向原生 ELF 差分。完整 Linux/目标矩阵与其他 SysV 类别仍未验收，不将本次扩展泛化为全部 Record 已支持。
- **历史证据：** 同一有效 GNU C 的按值 `struct Three { unsigned char a[3]; }` 参数和返回，宿主 `gcc -std=gnu11 -O0 -no-pie` 编译 PASS、原生 exit42，旧 MiniC `--target=x86_64-linux -S` exit1，提示 incoming `cannot allocate or verify Core function frame` 或 RETURN terminator unsupported。修复前用 1..16B 统一 fixture 独立复现：GNU native exit42，MiniC 首个 3B `make` 的返回即拒编。
- **具体门控：** `src/target/x86_64/abi.c:151–168:minic_x86_64_one_gp_record` 仅将恰好 1/2/4/8 字节 Record 放入一个 GP class；`call_plan.c:25–53` 对合法 3B INTEGER-class Record 走到 `bytes<=16` 的拒绝路径。`MINIC_M1_TRACE=1` 确认 `MINIC_X86_ARG_RECORD_CLASS_REJECT record=1 bytes=3 align=1`，`function=sum_three stage=incoming-abi`。证据 `/tmp/minic-f11-x86-abi-record3.c`，负责人再次执行 GCC PASS、MiniC -S FAIL。返回值和 outgoing 调用也须按 SysV class/寄存器合法性审查，不能断言当前三者全都独立错拒。
- **独立返回门控已补证：** `/tmp/minic-f11-x86-abi-return3.c` 只定义/调用返回 `struct Three` 的 `return_three`，GNU native 编译 PASS；MiniC x86 -S 仍 FAIL，但不同于 incoming，其 trace 为 `Core terminator unsupported: function return_three block=0 kind=0 has_terminator=1 return_object=1`。因此 **incoming 和 return 两个实际消费者** 均已独立拒编；outgoing-only 及跨编译器对象 ABI 仍待核，不把未测分支写成实证。
- **最小修复：** `x86_64/abi.c` 保留递归标量叶子/自然对齐/位域 eightbyte 资格门控：对象有效大小 1..8B 使用一个 INTEGER GP、9..16B 使用两个 INTEGER GP，仍拒 SSE、混合类、非自然对齐字段。`x86_64/machine_support.c` 的 GP 装入/写回以真实 `shape.bytes` 为界：1/2/4/8B 保留专用指令，3/5/6/7B 等尾段逐字节低位打包/拆包，高字节显式归零，不读取/写入对象之后的字节，也不覆盖已装好的 RDI..R9 参数。参数 planner、incoming、outgoing、返回以及调用返回的两 GP 尾段都用相同精确 1..8B 转运；stack fallback 保证双 GP record 的不可拆分分配与 8B slot padding。
- **真实差分：** 新增 `tests/compiler/audit/mc082_sysv_record_abi_{functions,driver}.c` 和头文件，尺寸 1..16B 全覆盖、packed 但字段自然对齐的 5B、显式 align8、含内部 padding 的 8B/16B、寄存器耗尽 one/two-GP 栈回退、间接函数调用。`make MODE=release CFLAGS=-Werror check-x86_64-sysv-odd-record-abi` 对同源 GNU 预处理 C 分别编出 GCC 与 MiniC `.o`，以 GNU AS/LD 形成四组 ELF；**GNU→GNU、GNU caller→MiniC callee、MiniC caller→GNU callee、MiniC→MiniC 均 native exit42**。原有 `check-x86_64-one-gp-sysv`、`check-x86_64-abi-elf`、`check-x86-linux-uaccess-and-abi`、`check-packed-bitfield-field-layout` 也 PASS。
- **保留边界：** `struct __attribute__((packed)){ unsigned char prefix; unsigned int word; }` 的 5B **非对齐** scalar leaf 在 GNU SysV 是 MEMORY，当前 MiniC 仍明确 fail-closed（永久负控）；不能通过本次 all-INTEGER 分类偷偷放入 GP。小型 SSE/混合类型、特殊 bitfield、显式 16B 过对齐的独立内存分类与 sret 路径仍需单独设计/验收。与 MC-057 空 Record（大小为零）不同，联合验收仍按用户决定推迟。

### MC-083 已完整声明的 char 数组再以省略边界字符串定义被错拒

- **当前状态与证据（2026-11-01）：** `FIXED-UNVERIFIED`，P2。独立提交 `15b08f4`（来自子任务 `18df05e`）让已有 complete bound 的 char array 走现成 bounded string initializer；`make MODE=release CFLAGS=-Werror BUILD_DIR=build/cleanup-werror check-external-char-array-redeclaration` **PASS**（GNU/MiniC→GNU AS/LD native 一致、超长字符串负例拒绝）。三个永久 fixture/probe 已入库；完整目标矩阵仍未运行。原 `eddd49c` 的 GNU `-c` PASS / MiniC `-S` FAIL 属历史发现证据，不再代表当前 HEAD。
- **完整分支原因：** `src/frontend/parser_external_object.c:115–126:parse_external_integer_array_definition` 重用早前的完整 `[4]` GlobalObject 时，合法地把 `element_count` 沿用为 4、`inferred_bound=false`；但后续 `:174–185` 的无花括号字符串专用入口只允许 `if (inferred_bound && char && STRING_LITERAL)`，使“先 incomplete 再推断”与“早已有 complete bound”在词法准入上错误耦合。`char a[]="abc"` 和 `char a[4]="abc"` 两个独立正控当前 MiniC 均 PASS，这个混合 redeclaration 才触发。
- **修复门槛：** 将“允许 char string initializer”与“是否应推断/完成 ArrayType”分为两个事实；重用已有完整 bound 时验证字节数、结尾 NUL、超限/截断、tentative/extern 多次声明与显式 GNU section 属性，不能只把 `&& inferred_bound` 删除后再次 complete 既有类型。补 GNU C→MiniC -S→GNU AS/ELF/native 和多 TU redeclaration 正反例；Common GlobalObject/initializer 所有权不可复制一份。

### MC-084 x86 Record copy 逐字节展开带来可观机器码膨胀

- **状态与证据：** `DEBT`，P2，`GNU-NATIVE-ABI+SIZE`，目前**未证语义错码**。对同一 4096B Record `*dst=*src`，MiniC 和 GNU GCC 分别生成 x86 `.s`，交给同一个 GNU C driver 和 GNU AS/LD，双方 native 都返回 fixture 预期 exit42。不是依赖“代码多所以一定有 bug”的主观评价。
- **可重复测量：** `/tmp/minic-f11-x86-record-copy4096-lib.c`、`...-driver.c`。`gcc -std=gnu11 -O0 -no-pie -S` 的 `.s` 为 **65 行 / 1,079B**；MiniC `--target=x86_64-linux -S` 为 **8,223 行 / 196,877B**。两程序包含相同 driver 的 `.text`：GNU **5,485B**，MiniC **65,264B**，二者均 native PASS。这是特定 unoptimized x86 4KiB copy 的**实测代码体积差**，不等同全 Linux 程序均同比膨胀。
- **真实实现重叠：** `src/target/x86_64/machine_emit.c:873–916` 的 RECORD_COPY 按每字节展开 `movzbq`+`movb`；`:837–871` 的 RECORD_LOAD 也逐字节。`machine_call.c:75–90` 的 outgoing aggregate copy 和 `machine_emit.c:637–651` 的 parameter-object copy 各有字/尾字节手写循环。Common `direct_instruction_semantics.c:85–117:minic_multi_direct_record_copy_extent` 已验证地址、类型和 DataLayout 范围，AArch64/Loong 已在生产消费它；x86 目前重新实现同一形状判定，**但 Common 当前有 16MiB cap 而 x86 本地无同等 cap**，不能未核接受域就机械替换。
- **清理与验收：** 先将机器拷贝策略封装成可复用 exact-byte emitter/shape contract（SSA/ABI 与 overlap/volatile/unaligned 边界明确），再让大对象走 counted loop，小对象按安全成本展开；同步删旧逐字节大循环和重复形状判定。测试 1、3、7、8、9、2048、4096、16384B、packed、volatile、src/dst 别名、record-return/vararg，并用同源 GNU `.text`、ABI caller/provider 和 native 结果差分。不能直接套用 Loong/A64 的指令或强加其 cap。

### MC-085 GNU error/warning 函数属性被保留但没有可达调用诊断消费者

- **状态与证据：** `OPEN`，P1，`LOCAL-COMPILE-DIFF`；不是 GNU C 合法性问题的猜想，而是源端属性已保存、真实调用的编译诊断和 GCC `-O0/-O2` 存在确定差异。尚未与 Linux 原冻结 `.i` 全集对照具体 `__compiletime_error` 名单，不能声称已定位 Linux 链接故障唯一根因。
- **生产者/消费者：** Frontend `src/frontend/ast_function.c:208–241` 保存 `error_message/warning_message`，`parser_function.c:764–788` 将 GNU 函数属性持久化。全 `src/` 对 `warning_message` 除 set/free 外没有读取，对 `error_message` 的唯一读取在 `src/compiler/inline_integer.c:192` 仅用于特化准入，**没有**可达 CALL 的 error/warning 发诊断者。此处语义丢失不是源端拒绝不了 attribute，而是存活 CALL 被 Compiler/Core 准入时忘记消费属性。
- **差分矩阵：** `/tmp/minic-f11-gnu-error-attr.c` 声明 `extern void fail(void) __attribute__((error("f11 diagnostic must fire"))); int main(){fail();return 0;}`：GNU `-O0/-O2 -S` 均 EXIT1 并输出精确 error，MiniC x86 `-S` EXIT0 且保留 CALL；`if(0) fail()` 死调用两编译器在两级 GCC 对照下均 EXIT0；`/tmp/minic-f11-gnu-warning-attr.c` 的 `warning("f11 warning must fire")` 活调用 GNU EXIT0 但输出 warning，MiniC EXIT0 且**无诊断**。负责人本轮独立复验 GCC-O0 vs MiniC error/warning。
- **修复门槛：** 在 Core 的合法可达调用集合建立后查源函数 diagnostic-attribute facts，未到达或被常量 CFG 删除的调用不得误报；不可在 Parser 原始 CALL 一律拒绝，也不能因为无常规优化而要求模拟 GCC -O2 全部消除。保证直接/间接 CALL、static clone、符号别名、GNU `__compiletime_error` 语义，必须分别处理函数属性应用于声明与经指针的可见性。将这类 diagnostic-only source policy 留在编译期分析合同，不泄漏为机器指令/SSA pseudo-call。新增原始 `.i` 的 GNU diagnostic 差分正反例，不混淆 `warning` 诊断级别与 error 退出码。
- **2026-11-01 独立复核（原始 `a1ae70f`、恢复 `6b1c45c` 基线，仍 `OPEN/BLOCKED-DESIGN`）：** 持久 [16 例 GNU O0/O2 与 MiniC 差分](mc085-compiletime-call-diagnostic-design.md) 及 `tests/compiler/audit/audit-mc085-diagnostic-attributes.sh` 复现 live error/warning 丢失、间接调用 O2 去虚拟化、always-inline/unused static O0/O2 差异；GNU oracle 32/32 PASS、MiniC 16/16 仍成功且 warning 静默。已证实两条禁止走的捷径：`dangerous() __asm__("same")` 有 error 与 `safe() __asm__("same")` 无属性，共用 Core callee linker name，GCC 对 safe PASS / dangerous FAIL，因此按 Core symbol 反查属性会误拒；`goto after; cold: stop(); after: return 0;` GNU O0/O2 均 PASS，MiniC 因 `source_label_id` 的保守 Core emission roots **仍发出 `call stop@PLT`**，扫描 Core 当前“可达” CALL 也会误拒。需要 compiler-owned source CALL provenance sidecar、与 ELF emission roots 分离的诊断 CFG、clone/indirect 精确策略与非致命 warning 收集器；本轮**只有证据/设计，无生产修复，不进入 check-fast 正向门禁**。

### MC-086 x86 PIC 隐藏的未定义 Weak 对象与函数错用 PC32 重定位

- **状态与证据：** `FIXED-UNVERIFIED`，P1，`GNU-LD/ELF-ORACLE`。主线提交 `866fa28`（隔离验证原提交 `ff5d1a7`）仅更改 x86 machine emitter：PIC 的 hidden undefined weak 对象/函数不再走 direct PCREL，复用已有 TargetInfo undefined-weak facts 改走 GOTPCREL；hidden strong 仍 PCREL、default-visible 仍 GOT、非 PIC 不变。**不是**五架构联合验收或 RV64 同问题已解决。
- **最小源与实际 ELF：** `/tmp/minic-f11-x86-pic-hidden-undef-weak-data.c` 中 `extern int hidden_data __attribute__((weak,visibility("hidden"))); int probe_weak_data(void){return (&hidden_data==0)?42:1;}`；同源 GNU `-fPIC -S` 发 `hidden_data@GOTPCREL(%rip)`，MiniC `--target=x86_64-linux -fPIC -S` 发 `leaq hidden_data(%rip),%rax`。GNU `-fPIC -pie` 链接 PASS，运行 exit42，MiniC `.s` 经 GNU AS 产生 `R_X86_64_PC32 hidden_data -4`，GNU LD 报 `relocation R_X86_64_PC32 against undefined hidden symbol` FAIL。负责人重跑 GNU/MiniC `-S`、GNU LD 差分 4 步确认。
- **已有仓库 fixture 补强：** `tests/compiler/c0/gnu_weak_undefined_live.c` 原已覆盖 hidden missing **object 和 function**，但 `run-gnu-weak-undefined-live.sh` 只覆盖 `-fno-pie`。将同 fixture+`gnu_weak_undefined_provider.c` 的 `-fPIC -pie` 交 GNU C 或 MiniC `.s`：GNU LINK/RUN exit0，MiniC 因 `hidden_missing_function` 的 PC32 GNU LD FAIL；两个符号的 GNU 均走 GOTPCREL、MiniC 均 direct `leaq`。所以这是**测试矩阵缺 PIC 而非需要发明新样例**。
- **修复后持久门禁：** `check-x86-pic-hidden-undefined-weak` 已接 `check-fast`；GNU/MiniC `-fPIC`、MiniC `-fPIE` 产物经 GNU AS/LD PIE/native 正反控制 **PASS**，`readelf -Wr/-Ws` 确认 weak hidden 2 符号 GOTPCREL、保留绑定及不再含 forbidden PC32；strong hidden 2 符号仍 PCREL、default-visible 2 符号仍 GOT。主线额外重跑 `check-elf-hidden-undefined-pic-source` 五 ISA source **PASS**（ELF/QEMU NOT_RUN）。保留完整 Linux 与所有 PIC 合同的后续验收。
- **根因及未验合同：** `src/target/target_info.c` 对 hidden symbol 给 `nonpreemptible=true` 且独立给 `undefined_weak=true`；旧 x86 machine emitter 只问前者便 direct PCREL，忽略 weak 零地址合法性。AArch64 `residual_emit.c` 已有显式排除 weak 的对照策略。本次在 x86 使用两项现有事实决定发射，未统一 ELF reference fact 或跨 ISA relocation；后续仍须验证有/无 provider、共享库等 PIC/PIE 合同，不能靠强行放宽 GNU LD 掩盖问题。

### MC-087 Common LP64 单函数 257 条合法 Inline ASM 硬拒

- **当前状态（2026-11-01）：** `FIXED-UNVERIFIED`，P2；提交 `fffef59` 将 `bool used[256]` 换为按 `inline_asm_count` checked-`calloc` 的临时位图，成功或任何拒绝路径均释放。合并后 Release `-Werror` 及 `check-direct-lp64-asm-capacity check-direct-lp64-asm-preflight-unit check-direct-lp64-reachable-source-labels` PASS；A64/Loong 两目标 255/256/257/512 个合法 `nop` 的 `-S` 数量相等，伪造 Core 的重复、孤儿、越界 ID、NULL 模板与非法 opcode 仍拒绝。**目标 GNU AS 不在本地，ELF/QEMU=`NOT_TESTED`，没有完整 Linux 证明。** 原 `eddd49c` 257 条错拒是历史发现证据。
- **根因：** `src/target/common/direct_preflight.c:15–23` 在栈上固定 `bool used[256]` 并把 `fn->inline_asm_count > 256` 拒绝，生产由 `direct_function_plan.c:41–44` 同时供 A64/Loong 使用。`src/core/core_ir.c:822–853` 的 InlineAsm arena 已按实际数量动态扩容，语义和 ISA 没有 256 指令的上限，属于旧实现容量上限泄漏为语言能力。
- **修复门槛：** 用以 `fn->inline_asm_count` 为界、checked allocation 的位图执行 exactly-once Core ASM ID coverage，保留孤儿/重复引用、越界、内存分配失败时 fail-closed；先测 255/256/257/512 条 valid nop 与 deliberately malformed synthetic Core ASM ID，再做 A64/Loong 真 GNU GAS/readelf、必要时运行 `nop` 正控。测试输入 `/tmp/f11_lp64_asm_{255,256,257}.i`（已移至 /tmp，不在仓库）。

### MC-088 RV32 宽整数条件分支只检查低 32 位造成静默错编

- **当前状态与证据：** `FIXED-UNVERIFIED`，P1，`GNU-QEMU-DIFF/ELF32`。原 MiniC RV32 `rv32_emit.c:rv32_term` 在 `MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH` 使用 `rv32_slot`，仅将 64 位 Core 整数条件的低字加载为 `t0` 再执行 `beqz`；当低 32 位零、高 32 位非零时，错误走 false block。无关 C 指针/ABI 的歧义，属于明确的 RV32 ILP32 静默错编。
- **同源反证与修复：** 持久 `tests/compiler/c0/run-rv32-wide-branch-condition.sh` 为 GNU C 与 MiniC 共享预处理 `.i`，均用 GNU GAS/LD 生成 RV32 `ELF32`，真实 `qemu-riscv32` 执行。修复前、未含 RV32 补丁的 MiniC 编译器：GNU mode **PASS**，MiniC mode **exit 11**（高字非零的分支测试）；修复后包含提交 `ae9937d`、持久探针提交 `fa73ff1` 的当前工作分支以 Release `-Werror` 重新构建，GNU/MiniC 两种 mode 均 **PASS**，8 组覆盖 high-only、low-only、mask 和 ternary 条件。
- **生产者与消费者：** Core 合法携带宽整数值作为 conditional terminator；RV32 `rv32_emit.c:rv32_term` 现经现成 `rv32_scalar_storage_words` 区分 1/2 word，2-word 经 `rv32_pair_slot` 取两半后 `or t0,t0,t1`，再走既有 `beqz`／目标标签输出。未修改 Core 类型合同、其他 ISA 发射器或分支目标布局。
- **未验证边界：** 测试只覆盖 GNU 32 位目标工具链、RV32 GNU 链接产物与 QEMU 定向样例；完整 RV32 Linux 大程序集成、所有 ABI/优化模式、其他目标矩阵、MiniAS/MiniLD 联合验收未跑。因此不升为 `VERIFIED`。

### MC-089 x87 long double 大于比较方向错误

- **状态：** `FIXED-UNVERIFIED`，P0，`GNU-NATIVE-DIFF`。2026-11-01 从 MC-041 字面量精度探针拆出的独立**静默错编**，修复后保留持久回归 `check-mc089-x87-ordered-comparisons`，不把 GNU native 定点验收冒充全目标联合验收。
- **同源复现与精确根因：** `int main(void){long double a=2.0L,b=1.0L;return a>b?42:1;}` 原 GNU x86 native exit **42**，旧 MiniC exit **1**。Core `__lttf2` / `__letf2` helper 把负的有符号比较结果先写入 32 位 `EAX`；x86-64 的 `movl $-1,%eax` 自动将 RAX **零扩展**为 `0x00000000ffffffff`，下游 `x86_value_store` 放进 Core 64 位 SSA 槽，再由有符号 `INTEGER_LESS` 判断时误视为正数。原怀疑的 x87 `fucomip` 比较次序并非根因。
- **最小修复与回归：** `src/target/x86_64/machine_call.c` 的 helper 终点在 `x86_value_store` 前加 `movslq %eax,%rax`，保持 `-1/0/+1` 的有符号含义。两个持久测试 TU `mc089_x87_ordered_comparisons{,_support}.c` 用 GNU/MiniC x86 GNU AS/LD 原生差分六运算符 × 十二组正负有限值、`±0`、无穷及 NaN，另含直接 `2.0L > 1.0L`；旧 MiniC **13 个 mismatch**，修后 GNU/MiniC 都 **0**。合入主线后 Release `-Werror`、`check-mc089-x87-ordered-comparisons` 及 x86/RV64/A64/Loong 四目标 `-S` PASS；RV32 的既有 long-double frame 限制不计通过。
- **验收边界：** MC-051 负零原生独立回归保持通过；不混入 MC-041 的精度变更。未运行完整 Linux 五目标 ELF/QEMU、MiniAS/MiniLD 联合验收，故继续保留 `FIXED-UNVERIFIED`。

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

- **旧审计基线** `src/` 跟踪 **332 个 C/H 文件**、Target 206 项，原账本 `Full=7 / Targeted=4 / Pending=195`、`drift=125` 仅代表当时状态。`3d504e7` 第八轮现有 **337 个 C/H**（Frontend70/Compiler11/Core48/Target208）；另两个 CLI 入口、注册定义及 Makefile 见[逐文件清单](minic-source-coverage-f8-2026-10-31.md)。新覆盖并不使旧审查 `PASS` 自动升级为所有函数全部 U2。
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
