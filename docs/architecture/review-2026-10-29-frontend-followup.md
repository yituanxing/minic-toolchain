# MiniC Frontend / Compiler / Driver 二轮跨边界复核（2026-10-29）

## 范围与方法

第一轮 84/84 逐文件精读台账：`src/frontend` 70、`src/compiler` 11、两条 MiniC CLI 入口、`include/minic/compiler.h` 公开 API。第二轮在此基础上沿解析→AST/type→TargetInfo/DataLayout→Core→target emitter 交叉追踪，并阅读相关 `src/target/{target_info,target_catalog,data_layout}.c` 和 x86 long-double emitter 的关键实现。**本轮是纯审计：不改生产代码或测试，不承诺全项目运行正确。** 所有 GCC/MiniC 差分测试仅通过临时预处理输出和 `/tmp` 构建，未写入仓库。

## 二轮发现（新增或补强）

| 编号 | 优先级 / 证据 | 源码根因与边界 | 建议和不能误改的约束 |
| --- | --- | --- | --- |
| F2-01 | **P1 已复现：GNU packed 位域 ABI/layout 错误**。同一源 GCC x86 返回 `31`，MiniC 返回 `44`，即前者 `sizeof=3, alignof=1`，后者 `sizeof=4, alignof=4`。| `src/frontend/parser_record.c:491-505` 命名 bit-field 解析到 `:` 后直接 `minic_c0_record_add_bit_field` 并 `return true`，跳过该字段的声明/声明尾 GNU attribute application；`src/target/data_layout.c:194-248` 位域路径也不看 `field->is_packed`/`explicit_alignment`，非位域路径 `:256-264` 才处理。已被前端收集的 `__attribute__((packed))` 静默丢失。 | 优先在解析层将字段属性解析/校验持久化，再统一改位域布局的有效对齐规则；同时核对 x86/ARM/RV GCC ABI 下 packed/aligned、零宽位域、跨存储边界等复合情况。主 agent 已领修，不在本审计分片改代码。 |
| F2-02 | **P1 已复现：long double L 字面量超 binary64 精度丢失**。GCC x86 运行退出 `0`，MiniC x86 退出 `1`。 | `src/frontend/parser_expression.c:124-170` 的 `minic_binary128_from_binary64`，`:239-258` 把 `L` 字面量先 `strtod` 到 `double` 再升为 binary128；`src/target/x86_64/machine_fp.c:16-45` x87 常量路径明确假设 Core literal 来源仅 binary64，并拒绝宽出此范围的负载；`src/core/core_lower_scalar_values.c:566-698` 以 TF libgcc 命名表示 long double，而 x86 machine_call 另外作 x87 helper 映射。因此不能只把 strtod 换成 strtold 而继续假设所有目标是 binary128。 | `TargetInfo`/DataLayout 单列 long-double **编码格式**（x87-80/binary128）与解析/转换能力，使用无损字面量 parser；保留现有 x86 Core→x87 明确转换/拒绝规则，重新定义 helper 名称映射的边界。需跨五目标差分，不要凭 `sizeof=16` 推断相同编码。 |
| F2-03 | **P1 已复现并已在本分片修复：static 整数/字符数组同名声明合并失败**。原本 GCC 接受 `static int a[2]; static int a[]={11,22};`，MiniC 第二行 `duplicate global object`；合法的 `static int a[];` 第一行报 `expected '=' after static array`。 | `src/frontend/parser_global.c` 原 `parse_static_inferred_integer_array` 与 char branch 无条件新建对象，绕开 pointer-array path 的 tentative/composite merge；同文件 fixed scalar path 又提前把 tentative 当 zero-definition。 | **已把 integer/char/pointer scalar arrays 合并成一个 `parse_static_scalar_array`**，删除两个 inferred 特例及原 generic fixed scalar 重复路径；保留宽/窄字符串 inference、已有 pointer array 事务、完整/不完整 bound 合并和 transient descriptor retire。`parser_global.c` 本轮 diff +55/-294 行。新增 GCC/MiniC 5 ISA 差分覆盖 fixed→inferred、incomplete→inferred、混合 tentative chain、字符 literal、pointer 原回归、冲突 bound 正反例。首轮 FC-19 的 typedef-array 仍是单独未修事项，不得借此声称已修。 |
| F2-04 | P1/P2 架构知识泄漏（不涉及当前已证明 runtime 错） | `src/frontend/type.h:22,48,60-64`、`type.c:223-241` 的 AArch64 opaque vector/x86 va cursor 与 `MS_X64` 进入通用枚举和 constructors；`ast_verifier.c:105-128` 重复目标专属 case；`parser_function.c:291-349` 维护 x86 `safe_scalar_targets` 和 security spelling，`:1125,2011-2020` 硬编码 MS_X64；`parser_record.c`、`parser_typedef.c`、`parser_function_parameters.c`、`parser_postfix.c` 同样硬编码 MS_X64 调用策略。 | `TargetInfo` 提供语义声明/ABI 资格回调、opaque extended type identity 和属性 string policy，前端保留通用字段、限制与诊断；**不能删除 TargetInfo 询问，亦不能把 header-only vector 声明等同机器执行能力**。这是扩新目标需要动前端的结构性债务，先设计 schema 再迁移。 |
| F2-05 | P2 产品 Driver/ISA 边界仍独立，非五目标 end-to-end | `tools/minic-cc/main.c:152-164` 直接判断 RISCV32/64 才给 `-march/-mabi`，依赖 `target/riscv/isa_resolve.h`；`tools/minic/main.c:65-82,649-676,843-853` 仍固定 RV64预定义宏、staged assembly/link ABI。与此同时 `src/target/target_catalog.c` 的注册清单明确只有 RV64 `MINIC_TARGET_STAGE_STAGED_ASSEMBLER_AND_LINKER`，`include/minic/compiler.h` 公开 API 只保证 target-aware `-S`。 | `minic-cc` 改目标-owned option parser 能力；完整 driver 阶段逐目标注册 stage operations，确认外部 sysroot/toolchain 后再扩；不把 `-S` 编译视为完整多目标链接认证。当前限制与文档一致，属待扩展项。 |
| F2-06 | P2 多线程首用竞态候选 | `src/target/target_info.c:297-322:minic_target_info_for_architecture` 静态 `canonical_targets[]` 按 `target->data_layout == NULL` lazy 初始化并无同步；两个宿主线程首次调用同一架构时并发写相同静态对象，C 语言属于数据竞争。`minic_target_invocation_begin` 保护的是 invocation 的后续可变参数，不覆盖此全局发布动作。 | 如果库将开放同进程并行编译，改成编译期完整 `const` descriptor / C11 `call_once` 或显式初始化；CLI 单编译进程下仅低优先级风险，不应阻塞当前 CI。 |
| F2-07 | P2 trace 与历史兼容层仍分散 | `parser_static_initializer.c:370-390,1824-1860`、`parser_expression.c:1502`、`parser_external_object.c:502`、`ast_verifier.c:1808,2006` 用同一 `CORE_FAST_TRACE` 做若干截然不同的 stderr 细节输出。`compiler/inline_boolean.c:124` 的 `_legacy` domain fallback 仍有生产 caller（`:398`），不能见名字就删；`function_body.c:711` 的 fallback 也有独立保守语义。 | trace 改统一诊断 category 或 trace sink；**有引用的保守 fallback 不是死代码**。先反证可达性再删；此项低优先级。 |

## 可重复最小 A/B（Linux x86_64，GCC 对照）

**Packed 位域：**

```c
struct S { char c; __attribute__((packed)) unsigned int x:3; char z; };
int main(void) { return (int)(sizeof(struct S)*10 + _Alignof(struct S)); }
```

GCC `cc -std=gnu11 -x c` 原生运行退出 `31`；MiniC `minic-cc --target=x86_64-linux -S`、再 GNU `cc -no-pie` 后运行退出 `44`。以下合法断言 GCC 通过、MiniC 报 `static assertion failed`：

```c
_Static_assert(sizeof(struct S) == 3 && _Alignof(struct S) == 1, "layout");
```

**Long double:**

```c
int main(void) {
    long double a = 1.0L;
    long double b = 1.0000000000000001L;
    return b > a ? 0 : 1;
}
```

GCC 退出 `0`；MiniC x86 汇编、GNU 链接后退出 `1`。`double` 无法区分这两个字面量，x86 80-bit `long double` 可以。

**Static incomplete array 复合声明：**

```c
static int arr[2];
static int arr[] = {11, 22};
int main(void) { return arr[1] == 22 ? 0 : 1; }
```

GCC `-std=gnu11 -Wall -Wextra -Werror -fsyntax-only` 退出 `0`；MiniC x86 `-S` 第二行 `duplicate global object`。

**本轮修复后的复验**：`cc` 严格 `-Werror` 编译 `src/frontend/parser_global.c` PASS；`make MODE=debug BUILD_DIR=build/review-root build/review-root/bin/minic-cc -j 6` PASS；`MINIC_CC=build/review-root/bin/minic-cc sh tests/compiler/c0/probe-static-array-redeclaration.sh` 输出 `STATIC_ARRAY_REDECLARATION=PASS gcc_runtime=1 minic_x86_runtime=1 targets=5 negative_cases=2`。新 regression 的错误分支进一步检查 MiniC 日志必须出现 `conflicting static scalar array`，排除后端无关错误制造的假绿。

## 二轮排除/保留的合理设计

- `target_info.c` 中三套 signed/unsigned-char/ILP32 整数模型由多个目标共享仅表示 **C integer model** 等价，不意味着相同指令/ABI；相反这是合理复用。
- `target_catalog.c` 的注册与 `target_info.c` 的语言 descriptor 是不同职责；仅目标清单有两个位置并不自动构成错误，须确认 runtime registration 的一致性。
- `compiler/inline_integer.c`、`inline_boolean.c` 不直接改写 shared AST CALL 是有意将 caller facts 留给 Core lowering；首轮 FC-11 针对 symbolic 全局 AST rewrite 的疑点需要差分，不把三者一概归类为死分支。
- 解析器 `float < float` 的 `ast_verifier.c` 初疑点已由主 agent x86 A/B 编译/运行排除；普通比较经过类型归一化，无新 bug 实证。
- `declaration_sema.c:328-368` 当前共享树已经把可能 OOM 的 section attr commit 排在 composite array descriptor mutation 之前，修正首轮 FC-09 的主要非原子失败路径；仍需独立故障注入才能把 rollback 视作完全验证。
- **首轮 FC-17 调用链更正**：`parser_external_object.c:parse_external_integer_array_definition` 只从 `[]` declarator 入口进入，但遇先前完整 `[N]` 的外部实体会把 `inferred_bound` 改回 `false`，所以原手写指针/整数 initializer 并非完全死代码。删除它属于迁移到已存在通用初始化器的语义重构；此前所述「全部不可达」不准确。已有固定/推断 initializer 的正反 A/B，但应另补 `[N]`→`[]` 外部重声明 fixture 才能证明迁移覆盖曾可达分支。

## 尚待重点复核

1. record alignment 和 bit-field 存储规则跨 x86/RV/AArch/GCC 差分；修复不能只改 `sizeof` 而漏 bit access/ABI 参数布局。
2. long-double 字面量、Core constant、TF helper、x87 emitter/ABI 数据表示的一次性完整目标策略。
3. typedef-array、anonymous/record 和外部数组初始化/redeclaration 分支的统一事务；保留作用域和旧 `initializer_plan` 的重叠覆盖顺序。直接 scalar `static int/char/pointer` 数组路径本轮已统一，**不代表 typedef-array 问题修复**。
