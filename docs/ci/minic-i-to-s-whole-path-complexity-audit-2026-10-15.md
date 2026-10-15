# MiniC 全链路复杂度、存储与高频操作审计（2026-10-15）

> 审计范围：**预处理好的真实 Linux Kbuild `.i` → MiniC → RISC-V `.s`**。代码来源为 `agent/linux-expanded-kbuild-v0`，没有凭经验假定 MiniC 的实现与 GCC/LLVM 相同。所有“候选”仅是源码可证明的潜在复杂度/空间问题，只有已经有 A/B 数据的条目标为已验证。和 MiniPP 原生预处理 `.c + .h → .i` 分开处理，不能把头文件扫描的成本误算进 `.i → .s`。

## A. 已核对的真实执行路径

```text
GNU GCC -E / MiniPP（独立模块）：C + include + 宏 → .i
    ↓
compiler.c:minic_read_file 一次读入 .i（malloc(size+1)）
    ↓
lexer.c:minic_lexer_next 按需按字符扫描，Token=kind+源文件span
    ↓
parser_function.c:minic_parse_c0_program
    ↓    各作用域/typedef/tag/函数/全局对象名查找
    ↓    ast.c 全局动态 ID 数组（Expression/Statement/Block/Function/Record...）
    ↓
AST PARSED verifier + FunctionBody ownership validator
    ↓
cast normalization
    ↓
AST NORMALIZED verifier + FunctionBody ownership validator
    ↓
Runtime 扩展：整数/符号/Boolean inline specialization、变体克隆
    ↓
inline emission references 重算
    ↓
compiler.c:minic_prepare_core_function_set：逐已定义且可达函数 Lower 成 Core IR
    ↓
Core IR verifier
    ↓
RV64 layout
    ↓
core_codegen + assembly writer → .s
    ↓
按序释放 Core IR、AST、源 .i buffer
```

证据：`src/compiler/compiler.c:234-281,327-449`；`src/frontend/parser_function.c:3687-3766`；`src/frontend/lexer.h:10-25`；`src/frontend/ast.h:495+`；`src/target/riscv64/core_codegen.c`。Linux 联合验收复用 GNU Kbuild `.i` / `.o` / QEMU，不能用微基准代替全内核运行时。

## B. **优先级 S：非常显著的空间/带宽放大结构，过去性能循环没有覆盖**

**B1. 所有 AST Expression 内嵌 64 个 CALL 参数槽。**

`src/frontend/ast.h:11,34,132-166`：`MinicExpressionId` 是 `size_t`，`MINIC_MAX_FUNCTION_PARAMETERS=64U`；`MinicExpression.value` 是 union，其中最大候选 `value.call.arguments[64]`。在 64 位构建中单这个数组**固定占 64×8=512 字节**；即使表达式是常量、变量、算术表达式、取地址而非 CALL，union 对象也需要为 CALL 的最大形态预留这块空间。整个 Expression 还有 kind、span、type、category，实际 sizeof 更大。无需任何运行时假设即可断言这是所有 Expression 的**静态布局下界**，不是每次都 malloc 512B。所有 `program->expressions` 以一整块变长数组按值存储，`ast.c:393-406` 和指数扩容 `ast.c:7-26` 将复制与缓存流量放大。当前记录的表达式数、CALL 比例、真实 RSS、realloc 次数尚无量测，故**不能直接承诺改这里能让编译速度提升多少**。

**备选方向：** 对 `MinicExpression` 使用定长小主体，CALL 参数单独通过 arena/side table 存储；保持 ExpressionId 稳定，参数数量上限 64 和求值次序不变。谨防 AST 浅复制、函数克隆、临时对象和释放所有权问题。**绝不**直接把 `arguments[64]` 改成一个裸 `malloc` 指针而遗漏所有复制/析构路径。先量：`sizeof(MinicExpression)`、`expression_count/capacity`、CALL 数量、参数数分布、`maxRSS`、malloc/realloc、parse/core wall。只有当真实 CALL 比例和 RSS 说明该布局浪费严重才着手高风险重构；验证首批样本、500/500 汇编和七路 QEMU。

**B2. 函数实体也大量预留参数类型。** `src/frontend/ast.h:333-356` 每个 `MinicFunction` 嵌入 `MinicType parameter_types[64]`；`src/frontend/ast.h:394-399` 每个 FunctionType 也有 64 项；`src/frontend/type.h:52-66` Type 含多个 ID、限定符、alignment，不是 1B 紧凑类型。因此**无论函数实际 0、2 还是 64 参数，都携带整个预留数组**。相较 Expression 实体数通常较少，初始优先级次之；应实测 `function_count`、实际参数数分布、单位实体大小及因 inlining 克隆带来的增量。

**B3. 固定 Switch stack 在 parser 内联分配。** `src/frontend/parser_internal.h:69-101` 的 switch context 各有上下界 512×uint64 两数组，`switch_contexts[16]` 静态预留至少 **16×2×512×8=131072B**，再加 parser 成员。每 TU 在 `parser_function.c:3692-3695` 的栈上 `MinicParser parser` 被 `memset` 清零。这不代表时间主瓶颈，但影响栈峰值、页触碰与空 Switch TU；将真实 switch 深度、每个 caseCount、触发 cache miss 数量测出，再考虑“只为进入 switch 的 scope 分配 case 数组”或更紧凑记录。必须保留支持上限及嵌套语义。

## C. **优先级 A：可直接证实的潜在重复扫描/二次复杂度**

**C1. 局部变量绑定线性扫描。** `src/frontend/parser_core.c:592-632`：find_local、find_scoped_global_object、name_bound 均从 `local_binding_count` 最后一个逆扫，直到名字匹配。单查找 O(L)，同一作用域大量标识符引用可达 O(R·L)。作用域规则“最近绑定优先”，不能像函数名那样沿用 first-declaration-wins。候选：增量 hash + scope shadow chain + pop 回退、分配失败仍旧逆扫。**先读取现成 `apply-perf-parser-lookup-counters-v0.py` 所记录的 calls/steps，不凭调用点多就认定瓶颈。**

**C2. Typedef、Global、RecordTag 查找。** `parser_typedef.c:8-35` 先查局部绑定后顺序查 `type_alias_count`；`parser_global.c:32-50` 顺序遍历 `global_object_count`，其他函数多次使用；`parser_core.c:652-686` RecordTag 逆扫。全局对象查找的旧独立 `apply-perf-parser-global-hash-v0.py` 已试过：BTF ~1.039×、verifier ~1.027×、filter ~0.990×，**总体边际，曾决定停止**。不能只是看到 O(G) 就立刻投入优化，尤其要测“已启用函数哈希与 inline 专门化”情况下的热点是否变化。Typedef 和 local name 需要新的频率/步数证据；保留语义的 first/last/shadowing 和特殊 block extern 行为。

**C3. 函数名 O(F) 已解决，不要重复研究同一项。** `parser_core.c:635-650` 原 `find_function` 正序线性，earliest-declaration-wins。此前保序增量函数 hash 在独立六个 Linux 文件（和 symbolic-owner + integer CALL 叠加）仍有 1.20～1.44×**局部**边际，三算法联合已获得 500/500 byte-identical `.s`。应按保序哈希计入联合候选，而不是继续重设计 parser hash。

**C4. 验证/归属全 AST 多次全图遍历与分配。** `compiler.c:360-402` 在 PARSED、NORMALIZED 各跑一次 `minic_c0_program_verify_target_detailed` 和 `minic_c0_program_validate_function_body_ownership`。Verifier `ast_verifier.c:1286-1844` 遍历 enums、types、records、functions、globals、**全部 expressions、statements、blocks**；ownership `function_body.c:155-205,637-659` 每次临时分配多张长度为 block_count、statement_count、expression_count 的表、初始化、遍历、释放。粗看这些为 O(E+S+B+F)×2，再加内部邻接边/验证工作；分配与全内存读写可能可观。**强制要求安全性：** 两次分别对应不同 AST 形态，不能凭重复就删除第二次或关掉全部校验。先单独增加计时（以及计数每张工作表分配字节）对最慢真实 `.i` 归因；仅经等价证明才尝试缓存“未变化部分”的验证结果。第一次可能主要用于及时捕捉 parser bug，丢失它会损害调试。

**C5. AST allocation 的含义**：`ast.c:7-26` 用 16 起始的倍增 `realloc`，每次新增表达式 `ast.c:393-406` 对巨型 Expression 做整结构体赋值。一方面确保 ID 稳定，无树节点 per-expression heap 指针；另一方面 giant union × 多次动态增长可能引入内存带宽和 cache 压力。对大表达式数组如量测到容量浪费明显，可考虑分块 arena/稳定 ID + 占用比、flat narrower Expression 先后，**但不要无理由将已 O(1) amortized append 改成链表**。

## D. **优先级 A/B：Inline → Core → Codegen 的另一轮结构性收益**

**D1. 已有经过真实实验的 inlining 收益不可丢**：Symbolic transitive “每 clone 全 TU expression 扫描”改为 FunctionBody 源归属过滤，在六个不同 `.i` 上各加速 1.24～1.42×，独立 first500 以及三算法合并 500/500 汇编一致；integer-transitive 大于 99% 的 integer specialization 耗时，CALL-ID 索引与源归属过滤可叠加，后者独立三文件 A/B 分别 1.5435×(crc32)、1.1579×(dev)、1.1514×(tcp_input)，目前只限三文件，要扩样与新 500 才可推广。

**D2. 源归属扫描之外还可能有 O(clones²) 变体匹配。** `apply-inline-specialization-transitive-integer-v0.py` 中针对每次 inline variant 在从 original_function_count 到 `program->function_count` 之间找已有等价 clone，暗含 clone×candidate 的最坏二重循环。**该点目前仍是假说**：测 variant lookup 次数与 candidates inspected 数量，再决定用 sourceID+常量事实哈希、保留原匹配/容量上限/回退。优先基于已优化后真实阶段计时，而非最初 Boolean-only 的旧阶段数据。

**D3. Core IR 为全部函数预分配函数对象与状态，再选择性 Lower。** `compiler.c:104-168` `calloc(function_count × MinicCoreFunction)`、`malloc(function_count × status)`，逐函数初始化；只有 defined 且 internal/referenced 才 Lower。与 AST function clone 增长有关，可能造成未发射 function 的额外初始化/容器开销。可先量“函数总数、定义数、发射数、Core IR 总 instr/block/value/object”，以及 per-function Core lower 时间；考虑仅对真的要 Lower 的函数懒分配，同时需保持 indexed `statuses`/functionId 映射。

**D4. RV64 Codegen 多趟对象遍历与重复布局。** `src/target/riscv64/core_codegen.c:375-412` 第一次遍历 `object_count` 计算 storage_size，对每个 object 运行 `minic_data_layout_type`；`core_codegen.c:569-608` 第二次遍历相同对象再次调用 type layout，填入 `object_offsets`。这确定是二次对同一不可变对象类型做布局查询，理论上可缓存 size/align 或一次派生。但是此前 Codegen P14 使用索引在 filter 上有 1.178×，另两文件收益有限，而 crc32 现阶段 Codegen 毫秒量级；需要**先量候选实际 Codegen 时间、layout 调用次数**，再决定是否值得优化。Core value lifetime/slot 代码已实现每基本块复用（见 `core_codegen.c:417-450,619-675`），不能再声称“所有 SSA value 都有一个独立永久 16-byte slot”。

## E. 不建议浪费时间的方向（已从源码排除）

- **“一次性把 .i 全部切词存一个 Token 数组”**：当前实现并无这样的全 Token 数组。Lexer 按需返回 kind+borrowed span，`parser_core.c:43-45` 只保持一个 current token；引入全量 Token 缓存反而会多占内存（至少 span 6×size_t + kind）且回填语义复杂。
- **“MiniC 每次打开很多头文件”**：`.i` 是已经预处理展开的输入，`compiler.c:minic_read_file` 仅打开一份 `.i`，`lexer.c:69-95` 跳过 GNU #line markers；头文件搜索/宏展开放在 GNU E / MiniPP 独立测量，不能混入 MiniC compile-to-S 的计时。
- **“所有大文件都主要慢在 Codegen”**：已优化阶段数据显示 crc32 仍以 integer transitive 为主，网络同时受 Parser/Core/Integer 影响；应每轮因算法变化重新量测。
- **“一次微调收益就直接推广为 Linux 速度”**：局部 P50 不是 Kbuild wall，多 worker 下的 CPU/Mem/IO 并发行为不同；只以相同 Kbuild/GNU 构建条件下七路构建墙钟与 QEMU 作为整体结论。

## F. 具体测量与下一轮决策（可复用旧 CI，而非创建新 Workflow）

先在**相同两批 SHA-frozen .i**（BTF/verifier/filter + crc32/dev/tcp_input）建立一张统一表，每个 input 记录：input_bytes、peakRSS、`sizeof(MinicExpression)`、`expression_count/capacity`、kind==CALL 的数量及 arg_count 分布、`sizeof(MinicFunction)`、`function_count/capacity`、`typedef/global/local-binding` 数量、两轮 verifier/ownership wall、normalization、各 specialization、Core lower/validate、layout/codegen wall、Parser 全 lookup counters 的 calls/steps；对最慢的三个 owner 重复测同 runner 2～4 轮。对 `MINIC_PARSER_PERF_TRACE` 复用既有计数补丁；必要时用只读时钟/尺寸诊断插桩，不修改编译结果，所有输入和输出 SHA 固定。

**排序规则**：
1. 已验证且足够大（符号 owner / integer CALL / parser hash）的 **三算法联合 first500 通过，先七路 2064+GNU link+QEMU**；本次单独打开 `[linux-perf-integrated-full]`，缓存身份绑定全部 patch 脚本 SHA。GNU Kbuild preflight 先被 init/version.o 和 vmlinux.o **时间戳重建**阻塞，在精确七路、精确 .config、仅三项 volatile allowlist 下修复并回到同一 GNU 认证快照；不能把这解释为 MiniC 输出错误。七路真实结果以 CI 结论判定。
2. 新的第四项 integer source-owner 有 3/3 正收益，但**不属于三算法的 500/500 证书**，候选晋级必须另做独立 cohort 和 first500，不混入本次七路运行。
3. 数据结构空间探索优先 B1（Expression 巨型 union）与 B2；这是高潜在 ROI、**高改造风险**，在真实输入 RSS + Call Ratio 证明影响前仅设计原型，不先大改生产 AST。
4. Parser 新候选优先比较 C1（local name 查找）与 C2（typedef/record tags）的实际 lookup steps；Global hash 已曾低收益，不再无休止打磨。
5. AST verifier/ownership 计时后若占现有 wall >10%，尝试**结果复用/分配复用**的可证明局部改动；保证 PARSED 与 NORMALIZED 双形态完整合同。
6. Codegen 若确实成为新第一热点，再沿 Core value/object use-index、layout memoization 看算法收益；先不调毫秒级热点。

验收要求每条改动都保留原 Linux/GNU/ABI 的 pinned provenance；新候选采用单变量 A/B、`.s` SHA 全等或等价语义 oracle，再 first500 → 七路真实 GNU link → QEMU P0/shell/P1。时间/内存的相对改善必须给同-run 数值，回归退化要如实记账，收益很小就停换瓶颈。
