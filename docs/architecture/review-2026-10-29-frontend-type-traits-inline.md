# MiniC Frontend 第四轮：类型角色、AST 合约与 Symbolic 跨调用者验证（2026-10-29）

本批建立在已完成的 `src/frontend` / `src/compiler` / 两个 C driver / public API **84/84** 全文件首轮审读及后续变更复核上；不是声称第四次重新逐字读完全部 84 文件。第四轮按 `type.h → type.c → target_info.c → parser → ast_verifier.c → compiler.c → inline_{reference,integer,symbolic,boolean}.c → Core` 读调用链，其中 `src/target`、Core、backend 仅只读，未修改。重点确认**语言类型身份**、**目标声明准入**和**可执行 ABI/ISA 能力**必须是不同契约。

## 本轮实际结构性改动

| 部位 | 之前 | 现在 | 保留契约 |
| --- | --- | --- | --- |
| `src/frontend/type.c:221`, `type.h:71` | header-only AArch opaque constructor 通过 enum 连续数值范围判断。 | 引入 `MinicBuiltinDeclarationRole` 统一归类 3 个标准浮点、5 个扩展浮点、5 个不透明 header 类型和 1 个 x86 `va_list` cursor；constructor 按角色判断。 | `MinicTypeBaseKind` 数值、TargetInfo、Core/backend ABI 未变；角色仅描述 declaration shape，**不代表**可 `sizeof`/运算/寄存器调用。 |
| `src/frontend/ast_verifier.c:27-43,122` | 浮点、extended FP、AArch opaque、x86 cursor 三组手写并行验证重复相同的 record/array/function/integer/plaint-char shape 条件；容易改一处漏一处。 | 共同 `builtin_declaration_type_is_valid`，先验证角色，再走 `minic_target_info_language_type_supported` 的目标准入；cursor 仍要求 `pointer_depth >= 1`，非法身份始终 fail closed。 | `VOID/INT/ENUM/FUNCTION/RECORD/ARRAY` 自有验证保持原样；RV32 不默许 extended FP，AArch opaque 仍仅 AArch，x86 cursor 仍仅 x86。 |
| `tests/frontend/type_test.c`, `tests/frontend/ast_contract_test.c` | 缺少覆盖全部 14 类 identity 的 role 合约和五目标 header-only/ABI cursor admission 正负单测。 | 新增完整 role table、非 opaque identity 反例、5 目标单元验证（基础浮点、extended FP、非法 rank、AArch header-only、x86 cursor pointer、裸 cursor 拒绝）。 | 不将声明合法性单测误算为五目标可执行 ABI 验收。 |
| `tests/compiler/c0/symbolic_inline_two_callers.c` 与 `probe-symbolic-inline-two-callers.sh` | 无直接的独立 caller-specific symbolic transitive 语义对照。 | 两个不同 static global，两个上层 caller 给出不同 `condition`、动态 fallback；GCC 编译原生与 MiniC x86 汇编原生运行作值比较，同时五 ISA `-S`。 | 这是**正例 PASS**，不是已经发现 optimizer bug 的负例。`MINIC_INLINE_SYMBOLIC_SPECIALIZATION_V0` 源码可按需要打 trace，当前结果无法证明所有共享 AST 场景安全。 |

## 优先级、残留与明确界限

| ID | 优先级 | 位置与实证 | 处置 |
| --- | --- | --- | --- |
| F4-01 | **P1 已缓解重复逻辑** | `type.c:221`、`ast_verifier.c:27-43,122`，五目标 shape/admission 之前三个 switch 独立复制。 | 已在 frontend 层统一 role predicate；未来若需新增不同 target opaque type，需 target-owned extension descriptor，不能在前端继续枚举 CPU 名称。 |
| F4-02 | **P1 OPEN，已确认错误** | `parser_expression.c` floating-literal path `strtod` 提前把 `long double` 的十进制值量化成双精度，再转 binary128；x86 的 `return !(1.0000000000000001L > 1.0L);` GCC x87 返回 0，MiniC x86 返回 1（此前 GCC/MiniC 对照）。 | 需联合 lexer/token literal 精度、target-owned long-double **format/encoding**、AST 常量、Core 转换与 x87 或 runtime helper。目标 `MinicByteOrder` 已存在，但并不等于 long-double encoding；本批没有错误地硬改。 |
| F4-03 | **P2 OPEN** | `src/frontend/type.h` 中 `MINIC_TYPE_BASE_AARCH64_*`、`MINIC_TYPE_BASE_X86_VA_LIST_CURSOR` 和 `MINIC_CALLING_CONVENTION_MS_X64` 仍是数值型语言类型身份；目标端 `target_info.c:383-439` 有真正 language capability。 | 这些不是可直接删除的死 enum；先 target-owned stable type-descriptor（身份、C 名称、类型等价、声明准入、对象 layout、调用 ABI、运算能力独立），再渐进替换 compatibility adapter，不能改变现有 Core IR 类型值。 |
| F4-04 | **P2 未复现的优化器候选** | `src/compiler/inline_symbolic.c` transitive candidate 调整共享 expression-call AST；调用上下文来自不同 caller，理论上须核对 AST owner/clone 与被修改父节点。 | 已加两个不同 caller+condition+dynamic fallback GCC 对照，全部通过；保持未证实风险，不标 P0 已发现错误。后续针对同源 AST 的多层变体/fuzz，并在 profile 下检查 fast-path array indexes。 |
| F4-05 | **P2 PERF 待基准** | `compiler/compiler.c:279-343` parsed/normalized AST 验证 + reference/integer/symbolic/Boolean 四个 pass；`function_body.c` 已共享 `ast_traversal.c`。 | 两阶段 verifier 是不同 stage contract，优化 pass 对 AST 会修改/克隆；不能仅为减少扫描就合并。先对 500 个 Linux 文件采样各 pass walltime、visited nodes/allocated nodes，再看可缓存的只读拥有者索引。 |

### 验证记录

- 独立目录 `build/review-frontend-traits-werror`：`make MODE=debug ... CFLAGS=-Werror -j8 all check-type check-ast-contract` **PASS**（全部项目对象严格编译，type/AST 合约单测通过）。
- 同一独立 `-Werror` build：`make MODE=debug BUILD_DIR=build/review-frontend-traits-werror CFLAGS=-Werror -j8 check-fast` **PASS**（退出状态 0；日志内 GNU/QEMU 不存在时相关子用例如实 `SKIP`，不能算跨架构 runtime PASS）。
- `MINIC_CC=build/review-frontend-traits-werror/bin/minic-cc BUILD_DIR=build/review-frontend-traits-werror sh tests/compiler/c0/probe-symbolic-inline-two-callers.sh`：**PASS**，GCC native 运行、MiniC x86 native 运行、`riscv32/riscv64/x86_64/aarch64/loongarch64` 五目标 `-S` 均通过。
- 以上是受影响组件的兼容性与针对性编译验证；不是 Linux 全量 7 路或全部 runtime 正确性证明。

文件边界：只修改 `src/frontend/{type.h,type.c,ast_verifier.c}`、两个 `tests/frontend` 单测，以及 symbolic 两 caller 测试和本报告；**未修改 target/Core/backend/Makefile/CLI；未自行 commit/push**。
