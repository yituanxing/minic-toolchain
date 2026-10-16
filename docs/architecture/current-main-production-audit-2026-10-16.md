# MiniC 当前主线第一轮生产源码审计（2026-10-16）

> **审计类型**：第一轮、静态与既有性能证据复核；**不是全仓每函数死代码证明，也不是任何优化已生效的宣告**。
>
> **检查源码**：`main@ed77c8d55545a746ed7c913288cff321e5f3ca60`（README/设计文档整理后的 HEAD；自 `24b02474` 到此审计版本只有文档变动）。
>
> **现有生产行为证据**：`24b0247401d1f973c4aa4f3495d7298cc7dd4cc1`：[Linux 七路 2064/2064 + QEMU P1 12/12](https://github.com/yituanxing/minic-toolchain/actions/runs/38870185276)；[工具链聚焦回归](https://github.com/yituanxing/minic-toolchain/actions/runs/38870185297)。这些**不证明**未来源码改动。

## 1. 本轮实际检查了什么

1. 读取当前默认分支、完整 Git tree（`recursive=1`、无截断）、`Makefile` 的 `MINIC_SOURCES`、历史 B0 源码清单和停用的 B1 自举工作流。
2. 逐段核对生产入口 `tools/minic-cc/main.c`、`src/compiler/compiler.c`、`src/frontend/parser_function.c`、`ast.h/ast.c`、`parser_internal.h`、`src/core/core_ir.c`、`core_lower.c/core_lower_bool.c`、`src/target/riscv64/core_codegen.c`、`codegen_function.c` 以及目标布局/ABI 入口。
3. 对照已有完整 Linux 性能文档、AST 体积基线、源代码收敛文档及历史自举里程碑，**不重复声称已经做过的优化是新发现**。
4. 本次未执行本地编译、未运行新 A/B、未以整个仓库全部文件为单位完成可达性分析。因此「未见可安全删除」与「确证无死代码」是两回事。

当前树概况（计数基于本轮 Git tree）：

| 路径 | 跟踪文件/子路径统计 |
|---|---:|
| `src/` | 73 个文件；46 个 C 实现文件 |
| `tools/ci/` | 362 个文件，其中名称匹配 `apply-*.py` 的 184 个 |
| `.github/workflows/` | 26 个启用工作流 |
| `.github/workflows-disabled/` | 322 个历史/停用工作流文件 |
| `tests/` | 1237 个文件 |
| `docs/` | 117 个文件（含近次文档更新） |

**不要把 184 个 apply 脚本或 322 个停用工作流直接当作可删除生产冗余。** 很多仍被验证、回放、历史定位引用，需要调用图、差分 oracle 和保留策略支持清理。大文件本身也不能证明算法不合理。

## 2. 已确认的硬问题：自举输入与当前编译器不一致（P0）

- 当前 `Makefile` 的 `MINIC_SOURCES` 列有 **47 个 C 文件**，实际构建编译器可执行文件 `minic-cc`。
- `tests/compiler/bootstrap/b0-sources.txt` 列有 **46 个 C 文件**，**缺 `src/core/core_lower_bool.c`**；列表末尾是 `tools/minic/main.c`，而当前编译器入口是 `tools/minic-cc/main.c`。两者目前分别表示 **工具链 Driver** 与 **C 编译器入口**，不能互换。
- `.github/workflows-disabled/compiler-bootstrap-b1-sharded.yml` 历史 B1 流程仍要求 `selected == 46`、使用旧源清单来产生 Stage1/Stage2；`make bootstrap` 现在只打印信息，不执行真实 self-host pipeline。
- `docs/milestones/compiler-v1-frozen.md` 的 Stage0→Stage1→Stage2 固定点确有历史 PASS 记录（历史 46 TU、16 replay shards、Stage1/2 loadable image 比较），**但不能证明 2026-10-16 最新编译器已自举**。

**建议动作（自举前必须修复）**：以 `minic-cc` 为当前 self-host 编译器目标，统一源码清单、正确的 `main`、Stage0 的产物链接与 Stage1/Stage2 的输入、清单计数和固定点校验；优先恢复旧 B1 脚本可复用的核心逻辑，暂不创建新的 Actions 工作流。**不能只把 46 写成 47，或只补充一个 C 文件。** 先聚焦编译器自身自举，不将「自举」误等同于「全工具链零 GNU 依赖」。

## 3. 重复、可简化与真实性能候选

### P1-A：同一 Core 函数执行重复结构校验（已证实代码路径，收益未测）

- `src/compiler/compiler.c:2448-2484`：`minic_validate_core_functions` 在 Core lowering 完成后对每个有效函数调用 `minic_core_function_verify`。
- `src/target/riscv64/core_codegen.c:3934+`：`core_function_can_emit` 又调用 `minic_core_function_verify`。
- `src/target/riscv64/core_codegen.c:6773-6801`：正式 `emit_core_function_with_symbol` 调用上述 `core_function_can_emit`；`codegen_function.c:1240+` 已明确避免在这里**第三次**预检。
- 这两次 Core Verifier 调用不是不同 AST 形态的校验；从当前调用链看，其间未发生对 Core 函数的显式修改（全局 layout 处理的是 C Program）。校验本身会迭代指令、值与基本块，并分配临时数组。
- **重要约束**：直接移除 backend 入口的校验会破坏其作为独立可调用 API 的防护。应考虑「正式编译流水线中已认证的 Core 对象」与「外部独立 backend 调用」两种路径的边界，或者测出影响低后明确不动。不能用未经证明的全局 `verified=true` 旗标掩盖对象后续变更。

**下一步证据**：先用现有聚焦样本和真实慢 `.i` 测两次 verifier 调用耗时、临时分配量及占总 MiniC wall 比重。如果可观，再做保持 backend 公共安全边界的最小复用试验；不与其他优化合并。

### P1-B：零值布尔 CFG 证明逐候选全 TU 扫描（已证实复杂度，收益未测）

- `src/core/core_lower_bool.c:74-157` 的 `core_zero_bool_is_immutable` 对 `program->statement_count`、`program->expression_count` 和 `inline_asm_count` 逐项扫描。
- `src/core/core_lower_bool.c:159+` 的 `core_zero_bool_cfg_condition` 遇到局部 `_Bool` 条件时调用它；`core_lower.c:12969+` 在 if lowering 时调用条件证明。
- **潜在**重复成本与候选数相乘，最坏接近 `O(B × (S+E+A))`；目前缺少 B 与实际请求/缓存命中次数的测量。全源扫描是为了证明没有写入、地址逃逸、RMW 或 asm output，**不是无用循环**。
- 遇到不确定条件必须 fail-closed；提前加不完整缓存会产生错误删除分支的风险。

**下一步证据**：按 TU 统计函数数、bool condition 请求/重复 local-id、表达式扫描步数、此证明占 Core lowering 的 CPU 时间。仅当达到热点阈值才考虑 function-scoped immutable facts 或 memoization；需要在验证器语义和控制流重入规则不变条件下比对汇编/运行时。

### P1-C：AST 大 union 导致显著空间负担（已有真实三样本量测；高风险结构变更）

- `src/frontend/ast.h` 的 `MinicExpression` 内联 `arguments[64]`（单参数 ID 在 64-bit host 上为 8 字节）；`src/frontend/ast.c:7-26,393-406` 采用 `realloc` 倍增容量和整对象赋值。
- 现存真实 Kbuild 测量 [#38675483958](https://github.com/yituanxing/minic-toolchain/actions/runs/38675483958)：`sizeof(MinicExpression)=680 B`、`sizeof(MinicFunction)=6536 B`。
- `net/core/dev.o` 的 AST 表达式 `268,691/524,288`（数量/容量）；CALL `14,461`，仅 **5.38%**；表达式容量 **356.52 MB**，测得进程峰值 RSS **510,052 KiB**。
- `lib/crc32.o` CALL 4.25%；`net/ipv4/tcp_input.o` CALL 5.67%。固定数组给大量非 CALL 表达式带来可证明的空间下界浪费。
- 这**确实可能值得日后设计 CALL 参数 side arena**，但 C AST 的复制、函数克隆、所有权和析构均要一起审核，不能直接把 64 定长参数改成拥有权不明的裸指针。
- 已验证 2×→1.5× 增长倍率实验有负收益（[#38676131973](https://github.com/yituanxing/minic-toolchain/actions/runs/38676131973)），**不重复尝试同方案**。

**决定**：本轮只记录为高价值/高风险候选，**不在自举前盲改 AST 物理表示**。如果当前迭代 RAM 压力成为首要瓶颈，另开独立、具保序回退的实验验证。

### P2-A：Parser 的固定 switch 上下界表（结构事实明确，综合价值待测）

- `src/frontend/parser_internal.h:16-17,77-110` 为每层 switch 内置 512 个 lower 与 512 个 upper 64-bit case bound；16 层，固定 case 存储约 **131,072 字节**，`MinicParser` 实测约 133,424 B。
- `src/frontend/parser_function.c:3687+` 将整个 parser 放在栈上并全部清零。
- 对无 switch 的 TU 也承担初始化和栈峰值成本；但这块空间远小于 AST Expression 的数百 MB，**优先级暂低于 Core/Inlining 计时实证**。
- 只有量到深度、case 数分布、实际初始化开销后才考虑延迟分配或紧凑列表。

### P2-B：两次 AST Verifier + 两次 FunctionBody ownership（重复遍历，但不可当死代码）

- `compiler.c:2633-2665` 分别对 **PARSED** 和 **NORMALIZED** 形态检查类型/AST 不变量与 ownership。
- 这两次服务于不同变换边界，不能直接合并或删除。可分别计时并审计分配，发现明显多余部分后才优化。

### P2-C：RV64 Codegen 的重复对象 layout 查询（已有候选，未测当前权重）

- 旧全链路复杂度审计记录 `core_codegen.c` 在 frame storage 计算和 object-offset 填充中对同一类型再次执行 layout 查询；**复核阶段要按当前源码定位，不照抄旧行号作为当前证据**。
- 与上面 Core double-verifier 或 boolean scan 相比优先级由当前真实 Codegen 占比决定，不因「看上去重复」便修改。
- 已有 Core 值 spill slot、CoreObject slot reuse 和 use-index 等正式优化，不重复开发。

### P2-D：迭代反馈的七片负载差异（已实测，不代表编译器单文件算法慢）

- 2026-10-16 七个 job 分别约 **12:39、15:06、13:34、15:24、13:07、7:43、13:47**；5 片比 3 片短将近一半。虽然每片目标数 294/295，单文件大小与复杂度并不均等。
- 历史已存在 `tools/ci/linux-perf-time-balanced-shard-plan-v1.py`；先以当前 per-owner 耗时只读验证是否能缩短 critical path。用户暂不要求改 CI，本轮不改 shard 规则或创建任何工作流。
- 总 wall 与单进程耗时必须分开；不能把历史 2064 对象三索引 A/B `1.3047×` 说成这轮整体加速。

## 4. 需要标为「保留，不能删」的设计

| 代码/资产 | 保留理由 |
|---|---|
| `src/core/core_lower_bool.c` | 已进入当前 `MINIC_SOURCES`，已验证源级重构；不是旧实验脚本 |
| `src/core/core_ir.c` 的 Verifier | IR 语义安全边界；可研究避免生产流重复，但公共接口校验必须保留 |
| Parsed + Normalized AST contracts | 验证不同变换阶段的不变量 |
| `src/target/riscv64/core_codegen.c` 的已有 slot reuse/index | 经过真实 Linux 正确性与性能工作；不能重复实现或当死代码删除 |
| 历史 B1/B2 自举工件说明与停用 workflow | 重建当前自举的回归方法依据；与正式生产可执行对象不同，但仍有研究/诊断价值 |
| `tools/ci/apply-*.py` 和 Python 差分 oracle | 必须通过正式执行入口、SHA 认证与引用关系分别划分「生产 / 实验 / oracle / 档案」，不能名字批量删除 |

目前没有完成编译器全部符号的调用图可达性证明，**不能宣布任何某个生产 C 函数为确定可删的死代码**。对重复逻辑也必须区分 API 防御、语义边界、真正的重复计算。

## 5. 审计判断：优先做什么，哪些暂停

| 等级 | 条目 | 当前证据 | 建议 |
|---|---|---|---|
| **P0** | B0 清单 / 当前 `minic-cc` 目标漂移 | 已核实、确定阻碍最新源码自举 | 最先修复配套清单与 Stage0/1/2 入口，保留旧脚本语义 |
| **P1** | 双重 Core Verifier | 确认两条正式调用路径，耗时未测 | 先计时，若显著则只消除生产流水线冗余，不撤掉 backend 公共保护 |
| **P1** | zero-bool 全 TU 扫描 | 确认源码扫描范围，耗时未测 | 先采样计时和扫描量，谨慎决定缓存方案 |
| **P1** | AST Expression 680B | 真实 RSS、CALL 占比、容量有数据 | 性能压力真实，但所有权风险高，先放在独立实验，不阻断自举 |
| **P2** | Parser switch 预留 | 静态结构与历史 sizeof 有证据 | 先量再改 |
| **P2** | object layout 重复计算 | 既有审计候选，需复核当前源码 | 仅 codegen 占明显比重时优化 |
| **P2** | 七片负载分布 | 最新真实 job wall | 可只读用现有工具估算，不改当前 CI |
| **DEFER** | 多前端/CoreType/full SSA/新后端/MIR/Pass 框架 | 属于长期设计 | 不在自举前实施 |
| **NO DELETE** | 无调用图证明的「旧代码」、诊断脚本、测试资产 | 用途尚未证伪 | 不删除 |

## 6. 实施与验收约束

1. 先保护 `24b02474` 的完整产物与性能证据；用此代码身份做 baseline，工作分支中的每一步都必须明确是纯整理还是行为/性能优化。
2. **第一次实际源码修改优先处理自举材料与入口的一致性**，因为它有明确失败机制、可局部验证、范围小；不是先拆大文件。
3. 实际性能工作按：**明确算法/重复 → 同输入旧值 → 最小改动 → 同输入新值、RSS、语义或 `.s` 等价 → 决定保留/回退**。将时间成本高的样本先限在固定 `.i`，不要每次从 Linux 开始编。
4. 先复用现有 T0/T1/T2、历史 bootstrap、七片真实 owner、QEMU P1，**不新增工作流，不禁用真实失败、不伪造缓存命中**。
5. 真正 Stage0→Stage1→Stage2 fixed-point 的通过和 Stage2 编译真实程序，要使用**同一个最新源码身份**分别认证；未达到不能称「最新版已自举」。

### 审计完成状态

- **已完成**：第一轮目录/构建边界、独立 compiler 主路径、典型重复校验、boolean 证明、AST 空间、历史自举清单和已有性能证据复核；形成具体待办。
- **尚未完成**：全量函数级死代码可达性证明、所有脚本调用者分类、任何新性能 A/B、当前源码自举复测。
- **本提交影响**：仅新增本文档，无任何编译器、工具链、Makefile、CI 或测试代码修改。
