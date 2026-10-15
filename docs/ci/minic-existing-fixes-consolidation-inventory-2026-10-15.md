# MiniC 已验证实现的正式源码整合清单（2026-10-15）

## 依据与唯一目标

- **历史正确版**：`eb583f9dd9`，[#38675862094](https://github.com/yituanxing/minic-toolchain/actions/runs/38675862094)，2064 个 Linux C 对象及 QEMU P1 12/12 已通过；曾开启 full-crossblock 源码补丁。
- **新近整合版**：`59ad3648637eba4991c0fb8ce27f121cc852b4fa`，[#38731782825](https://github.com/yituanxing/minic-toolchain/actions/runs/38731782825)，七路、2064、P1 12/12 全通过；**仍使用构建期 Python 补丁，不是无补丁源码验收**。
- 使用现有两份构建脚本：Runtime 44 项、Performance 53 项，共用 37，去重 60。现有分支和 CI 原样保留至最终验收。

## 状态标签（均不是完成百分比）

- **C**：原脚本已有 `ALREADY_CANONICAL` 检查，只能说明已建立源码整合识别；须继续证实无补丁源码、完整依赖和原测试一致。
- **U**：尚未建立该识别；可能已部分整合，逐项核源，不准凭名称说未合并。
- **T**：疑似调试/追踪，用实际脚本确认是否影响结果，不能当功能改造。
- **B**：构建模式差异；尤其 `.text` 与独立 Section-GC 的冲突，不能强行无条件迁入。

## 两份原始补丁数组的完整 60 项

| 序号 | 原历史补丁 | 适用构建 | 初筛 | 下一步 |
|---:|---|---|:---:|---|
| 1 | [apply-local-integer-assignment-v2.py](../../tools/ci/apply-local-integer-assignment-v2.py) | Runtime + Perf | U | 查清脚本依赖与真正新增源码 |
| 2 | [apply-local-integer-logical-conditions-v0.py](../../tools/ci/apply-local-integer-logical-conditions-v0.py) | Runtime + Perf | U | 查清脚本依赖与真正新增源码 |
| 3 | [apply-local-null-pointer-facts-v0.py](../../tools/ci/apply-local-null-pointer-facts-v0.py) | Runtime + Perf | U | 查清脚本依赖与真正新增源码 |
| 4 | [apply-builtin-constant-p-local-facts-v0.py](../../tools/ci/apply-builtin-constant-p-local-facts-v0.py) | Runtime + Perf | U | 查清脚本依赖与真正新增源码 |
| 5 | [apply-inline-integer-specialization-v0.py](../../tools/ci/apply-inline-integer-specialization-v0.py) | Runtime + Perf | C | 核对旧补丁运行后源码/原生 C 完全等价 |
| 6 | [apply-inline-specialization-local-facts-v0.py](../../tools/ci/apply-inline-specialization-local-facts-v0.py) | Runtime + Perf | U | 查清脚本依赖与真正新增源码 |
| 7 | [apply-inline-specialization-stable-parameter-facts-v0.py](../../tools/ci/apply-inline-specialization-stable-parameter-facts-v0.py) | Runtime + Perf | U | 查清脚本依赖与真正新增源码 |
| 8 | [apply-inline-specialization-transitive-integer-v0.py](../../tools/ci/apply-inline-specialization-transitive-integer-v0.py) | Runtime + Perf | U | 查清脚本依赖与真正新增源码 |
| 9 | [apply-inline-specialization-base-callee-integer-v0.py](../../tools/ci/apply-inline-specialization-base-callee-integer-v0.py) | Runtime + Perf | U | 查清脚本依赖与真正新增源码 |
| 10 | [apply-inline-specialization-capacity-v0.py](../../tools/ci/apply-inline-specialization-capacity-v0.py) | Runtime + Perf | C | 核对旧补丁运行后源码/原生 C 完全等价 |
| 11 | [apply-inline-specialization-symbolic-address-v0.py](../../tools/ci/apply-inline-specialization-symbolic-address-v0.py) | Runtime + Perf | U | 查清脚本依赖与真正新增源码 |
| 12 | [apply-inline-specialization-symbolic-nested-array-v0.py](../../tools/ci/apply-inline-specialization-symbolic-nested-array-v0.py) | Runtime + Perf | U | 查清脚本依赖与真正新增源码 |
| 13 | [apply-inline-specialization-symbolic-integer-closure-v0.py](../../tools/ci/apply-inline-specialization-symbolic-integer-closure-v0.py) | Runtime + Perf | U | 查清脚本依赖与真正新增源码 |
| 14 | [apply-inline-specialization-symbolic-transitive-rewrite-v0.py](../../tools/ci/apply-inline-specialization-symbolic-transitive-rewrite-v0.py) | Runtime + Perf | U | 查清脚本依赖与真正新增源码 |
| 15 | [apply-inline-specialization-symbolic-array-lvalue-v0.py](../../tools/ci/apply-inline-specialization-symbolic-array-lvalue-v0.py) | Runtime + Perf | U | 查清脚本依赖与真正新增源码 |
| 16 | [apply-inline-specialization-symbolic-closed-integers-v0.py](../../tools/ci/apply-inline-specialization-symbolic-closed-integers-v0.py) | Runtime + Perf | U | 查清脚本依赖与真正新增源码 |
| 17 | [apply-inline-specialization-base-callee-symbolic-v0.py](../../tools/ci/apply-inline-specialization-base-callee-symbolic-v0.py) | Runtime + Perf | U | 查清脚本依赖与真正新增源码 |
| 18 | [apply-inline-specialization-resolved-asm-goto-v0.py](../../tools/ci/apply-inline-specialization-resolved-asm-goto-v0.py) | Runtime + Perf | U | 查清脚本依赖与真正新增源码 |
| 19 | [apply-inline-specialization-core-reachability-v0.py](../../tools/ci/apply-inline-specialization-core-reachability-v0.py) | Runtime + Perf | C | 核对旧补丁运行后源码/原生 C 完全等价 |
| 20 | [apply-inline-specialization-original-reachability-v0.py](../../tools/ci/apply-inline-specialization-original-reachability-v0.py) | Runtime + Perf | C | 核对旧补丁运行后源码/原生 C 完全等价 |
| 21 | [apply-inline-asm-local-integer-facts-v0.py](../../tools/ci/apply-inline-asm-local-integer-facts-v0.py) | Runtime + Perf | U | 查清脚本依赖与真正新增源码 |
| 22 | [apply-inline-asm-rk-early-v0.py](../../tools/ci/apply-inline-asm-rk-early-v0.py) | Runtime + Perf | U | 查清脚本依赖与真正新增源码 |
| 23 | [apply-inline-asm-rk-early-hotfix-v0.py](../../tools/ci/apply-inline-asm-rk-early-hotfix-v0.py) | Runtime + Perf | U | 查清脚本依赖与真正新增源码 |
| 24 | [apply-inline-asm-rk-tail-rollback-v0.py](../../tools/ci/apply-inline-asm-rk-tail-rollback-v0.py) | Runtime + Perf | U | 查清脚本依赖与真正新增源码 |
| 25 | [apply-inline-asm-rk-cast-tail-v0.py](../../tools/ci/apply-inline-asm-rk-cast-tail-v0.py) | Runtime + Perf | U | 查清脚本依赖与真正新增源码 |
| 26 | [apply-inline-asm-immediate-batch-trace-v0.py](../../tools/ci/apply-inline-asm-immediate-batch-trace-v0.py) | Runtime + Perf | T | 确认是追踪还是语义行为 |
| 27 | [apply-inline-asm-write-output-trace-v0.py](../../tools/ci/apply-inline-asm-write-output-trace-v0.py) | Runtime + Perf | T | 确认是追踪还是语义行为 |
| 28 | [apply-inline-specialization-core-empty-function-v0.py](../../tools/ci/apply-inline-specialization-core-empty-function-v0.py) | Runtime + Perf | C | 核对旧补丁运行后源码/原生 C 完全等价 |
| 29 | [apply-inline-specialization-label-alias-v0.py](../../tools/ci/apply-inline-specialization-label-alias-v0.py) | Runtime + Perf | C | 核对旧补丁运行后源码/原生 C 完全等价 |
| 30 | [apply-riscv64-default-text-section-v0.py](../../tools/ci/apply-riscv64-default-text-section-v0.py) | Runtime + Perf | B | 维持不同构建模式正确行为 |
| 31 | [apply-inline-asm-symbolic-specialization-v0.py](../../tools/ci/apply-inline-asm-symbolic-specialization-v0.py) | Runtime + Perf | U | 查清脚本依赖与真正新增源码 |
| 32 | [apply-riscv64-core-value-slot-pack-v0.py](../../tools/ci/apply-riscv64-core-value-slot-pack-v0.py) | Runtime + Perf | C | 核对旧补丁运行后源码/原生 C 完全等价 |
| 33 | [apply-riscv64-core-value-slot-reuse-v0.py](../../tools/ci/apply-riscv64-core-value-slot-reuse-v0.py) | Runtime + Perf | C | 核对旧补丁运行后源码/原生 C 完全等价 |
| 34 | [apply-riscv64-record-call-result-snapshot-reuse-v0.py](../../tools/ci/apply-riscv64-record-call-result-snapshot-reuse-v0.py) | Runtime + Perf | C | 核对旧补丁运行后源码/原生 C 完全等价 |
| 35 | [apply-riscv64-core-object-slot-reuse-v0.py](../../tools/ci/apply-riscv64-core-object-slot-reuse-v0.py) | Runtime + Perf | C | 核对旧补丁运行后源码/原生 C 完全等价 |
| 36 | [apply-riscv64-core-object-slot-reuse-cfg-hotfix-v0.py](../../tools/ci/apply-riscv64-core-object-slot-reuse-cfg-hotfix-v0.py) | Runtime + Perf | C | 核对旧补丁运行后源码/原生 C 完全等价 |
| 37 | [apply-riscv64-structured-asm-used-callee-save-v0.py](../../tools/ci/apply-riscv64-structured-asm-used-callee-save-v0.py) | Runtime + Perf | U | 查清脚本依赖与真正新增源码 |
| 38 | [apply-loop-mutated-specialization-fact-barrier-v0.py](../../tools/ci/apply-loop-mutated-specialization-fact-barrier-v0.py) | Runtime | U | 查清脚本依赖与真正新增源码 |
| 39 | [apply-local-asm-if-fact-barrier-v0.py](../../tools/ci/apply-local-asm-if-fact-barrier-v0.py) | Runtime | U | 查清脚本依赖与真正新增源码 |
| 40 | [apply-core-if-condition-dominance-barrier-v0.py](../../tools/ci/apply-core-if-condition-dominance-barrier-v0.py) | Runtime | U | 查清脚本依赖与真正新增源码 |
| 41 | [apply-netlink-if-control-trace-v0.py](../../tools/ci/apply-netlink-if-control-trace-v0.py) | Runtime | T | 确认是追踪还是语义行为 |
| 42 | [apply-core-goto-label-fact-barrier-v0.py](../../tools/ci/apply-core-goto-label-fact-barrier-v0.py) | Runtime | U | 查清脚本依赖与真正新增源码 |
| 43 | [apply-runtime-if-fallthrough-facts-v1.py](../../tools/ci/apply-runtime-if-fallthrough-facts-v1.py) | Runtime | U | 查清脚本依赖与真正新增源码 |
| 44 | [apply-runtime-stable-zero-bools-v0.py](../../tools/ci/apply-runtime-stable-zero-bools-v0.py) | Runtime | U | 查清脚本依赖与真正新增源码 |
| 45 | [apply-perf-symbolic-function-body-owner-filter-v0.py](../../tools/ci/apply-perf-symbolic-function-body-owner-filter-v0.py) | Perf | C | 核对旧补丁运行后源码/原生 C 完全等价 |
| 46 | [apply-perf-parser-lookup-counters-v0.py](../../tools/ci/apply-perf-parser-lookup-counters-v0.py) | Perf | C | 核对旧补丁运行后源码/原生 C 完全等价 |
| 47 | [apply-perf-parser-function-hash-v0.py](../../tools/ci/apply-perf-parser-function-hash-v0.py) | Perf | C | 核对旧补丁运行后源码/原生 C 完全等价 |
| 48 | [apply-perf-parser-global-hash-v0.py](../../tools/ci/apply-perf-parser-global-hash-v0.py) | Perf | C | 核对旧补丁运行后源码/原生 C 完全等价 |
| 49 | [apply-perf-parser-enum-hash-v0.py](../../tools/ci/apply-perf-parser-enum-hash-v0.py) | Perf | C | 核对旧补丁运行后源码/原生 C 完全等价 |
| 50 | [apply-perf-parser-hash-fallback-v1.py](../../tools/ci/apply-perf-parser-hash-fallback-v1.py) | Perf | C | 核对旧补丁运行后源码/原生 C 完全等价 |
| 51 | [apply-perf-symbolic-loop-trace-v0.py](../../tools/ci/apply-perf-symbolic-loop-trace-v0.py) | Perf | T | 确认是追踪还是语义行为 |
| 52 | [apply-perf-specialization-phase-trace-v0.py](../../tools/ci/apply-perf-specialization-phase-trace-v0.py) | Perf | T | 确认是追踪还是语义行为 |
| 53 | [apply-perf-core-lower-statement-workspace-v1.py](../../tools/ci/apply-perf-core-lower-statement-workspace-v1.py) | Perf | U | 查清脚本依赖与真正新增源码 |
| 54 | [apply-perf-core-object-address-use-index-v1.py](../../tools/ci/apply-perf-core-object-address-use-index-v1.py) | Perf | U | 查清脚本依赖与真正新增源码 |
| 55 | [apply-perf-core-object-interval-onepass-v1.py](../../tools/ci/apply-perf-core-object-interval-onepass-v1.py) | Perf | U | 查清脚本依赖与真正新增源码 |
| 56 | [apply-inline-specialization-local-boolean-domain-scoped-v1.py](../../tools/ci/apply-inline-specialization-local-boolean-domain-scoped-v1.py) | Perf | U | 查清脚本依赖与真正新增源码 |
| 57 | [apply-perf-parser-typedef-index-v1.py](../../tools/ci/apply-perf-parser-typedef-index-v1.py) | Perf | C | 核对旧补丁运行后源码/原生 C 完全等价 |
| 58 | [apply-perf-parser-local-shadow-fix-v1.py](../../tools/ci/apply-perf-parser-local-shadow-fix-v1.py) | Perf | C | 核对旧补丁运行后源码/原生 C 完全等价 |
| 59 | [apply-perf-parser-nearest-ordinary-scope-v1.py](../../tools/ci/apply-perf-parser-nearest-ordinary-scope-v1.py) | Perf | C | 核对旧补丁运行后源码/原生 C 完全等价 |
| 60 | [apply-correctness-constant-p-ice-v1.py](../../tools/ci/apply-correctness-constant-p-ice-v1.py) | Perf | U | 查清脚本依赖与真正新增源码 |

## 容易被漏掉的数组外入口

- `apply-riscv64-p1-defined-before-use-crossblock-v1.py`：仅 Runtime 可选 full-crossblock 使用，已整合到原生 C；不能忽视历史 P1 成功时的这一前提。
- `apply-perf-parser-source-deltas-v1.py`：Performance 数组外先执行；已经具备 `ALREADY_CANONICAL` 判断。
- `apply-perf-integer-call-expression-index-v1.py`：Integrated Full 在 Runtime 工具编译后额外执行；还没有已整合识别器。
- Integrated Full 还调用 symbolic-owner / parser-lookup-counters / function-hash 三项，均已计入上述 60 项，不能重复统计。

## 强制执行流程与每轮结束检查

1. 查对应**已经通过的历史实现**，读脚本、调用顺序、目标源码、以前测试与 SHA。
2. 对比真实 C 源码和历史补丁生成的源码，标出已落地 / 部分落地 / 真正缺失；依赖未迁入时不能跳步。
3. 只迁入已验证实现，**不根据新 CI 报错设计新算法**；保留原脚本作为兼容和防重复的严格检查。
4. 复用原专项回归、七路 2064 Linux 和 QEMU P1；性能必须是**同一输入真计时 A/B**，缓存命中不算性能提升。
5. 合并成果验证前保留五个分支；不新增或删除 CI 工作流，不扩大本阶段目标。
6. **每轮自问**：我是否依据历史正确实现？是否修改实际正式源码？有没有擅自修新 Bug？是否复用旧测试和旧计时方法？若有偏离，立即停止扩展。

此表为首轮静态盘点。将每个 U 和 T 项在核对源码/历史依据后逐渐转换为明确的原生实现状态，**不要机械地把 60 项当 60 个未完成任务**。

## Canonical main 验收后对账（2026-10-16）

**已实际验证的目标构建**：`b46822f`，Actions [#38764970582](https://github.com/yituanxing/minic-toolchain/actions/runs/38764970582)，七路 2064/2064、GNU 最终链接、QEMU P1 12/12 全部 PASS，构建期 Python 动态注入跳过。主分支合并提交 `4f927180` 的 Git Tree 与该验证版本字节相同；其他三条开发线与历史归档通过合并提交父节点保留，**不是**说所有实验性代码均进入 canonical `src/`。

**布局参数化**：`38e26d65` 在 main 实现默认共享 `.text`、`-ffunction-sections` 独立段、`-fno-function-sections` 恢复共享段，并保留显式 section 属性优先级；源头是编译器写 `.s`，MiniAS 负责依伪指令形成 ELF `.o` 的 Section。`9126345` 修正原有 Section-GC 测试的显式 opt-in；Actions [#38770201214](https://github.com/yituanxing/minic-toolchain/actions/runs/38770201214) 的集中 MiniC RV64、MiniAR、MiniLD 回归 SUCCESS。该回归不代替参数化后的全量 Linux P1 检查。

**源码级优化保留**：主分支 `src/core/core_lower.c`、`src/core/core_lower_asm.c`、`src/target/riscv64/core_codegen.c`、`src/frontend/parser_core.c`、`src/frontend/parser_typedef.c` 的 Git Blob 与 `b46822f` 完全一致。`src/compiler/compiler.c` 增加入口参数传递与兼容旧 API wrapper，未修改 inline-specialization 主算法。Runtime 原生 44 项及联合验证的 4 项 perf 补丁仍在正式源代码中。但目前未做参数化版本对历史构建的配对编译时间 A/B，**绝不能把源代码未丢失等同于实测速度不回退**。

**单独 Performance profile 不等于生产 Runtime profile**：`linux-perf-build-minic-profile-v1.sh` 包含 53 项；其中与原生 Runtime44+Perf4 的合集相比有 13 个额外脚本。逐项初查：parser global/enum/hash fallback、Core object address/interval index、parser typedef hash、local shadow、nearest ordinary scope 8 项已有 `ALREADY_CANONICAL` 检查，并可找到对应 C 实现；symbolic-loop 和 specialization-phase 2 项是追踪仪表；Core lower workspace、Boolean Domain scoped-cache 和 `__builtin_constant_p` ICE 3 项需单独做补丁后对比和全量正确性验证，不能凭 opt-in 使用就并入 Runtime。另有 perf-only 前置 `apply-perf-parser-source-deltas-v1.py`，需另行判定。

**性能实测引用的适用边界**：Actions [#38571430622](https://github.com/yituanxing/minic-toolchain/actions/runs/38571430622) 的第一批 500 输入配对 A/B，在 opt-in 性能配置下「Scoped Boolean Domain + typedef index」组合对其禁用两者的配置达约 2.7492x；这**不是** canonical Linux 全 2064 对象或者当前主分支对历史版本的加速倍数。正式版本已有 typedef index 代码，仍需独立验证剩余 scoped-cache 的可迁移性、速度与 QEMU 正确性。

**main 环境隔离**：GitHub Actions 缓存按分支隔离；原 agent 分支的 GNU golden 对象和 Linux source 缓存不能在新 main 上直接 restore，Actions [#38769158242](https://github.com/yituanxing/minic-toolchain/actions/runs/38769158242) 因此在 cache-miss 处失败，不代表 MiniC 编译回归。已沿用原 `gcc-runtime` job 在 main 上产生并保存完全相同、带配置认证的 GNU golden/source 缓存；此准备动作先跑，不用缓存命中代替新的源码编译 A/B 或 P1 结果。

**验收后续顺序**：完成 main 的 GNU golden/source cache 授权迁移后，先在 main 以现有七路真实 2064 编译及 P1 判定布局参数默认模式；然后在 SHA 身份固定的同一套 GNU-E 输入上，对比核心编译耗时和 CPU/Wall 指标（剔除对象 cache-hit）；最后只考虑经证实有增益且全量正确的性能候选。


## Main 真实编译时间观察（2026-10-16，2064 TU）

这次不依赖缓存命中充当性能数据，直接读取两次 **真实 2064 C 对象编译** 的七路日志：

- 迁移与双布局前/后第一轮成功版本 `b46822f`：Actions [#38764970582](https://github.com/yituanxing/minic-toolchain/actions/runs/38764970582)，7 个 producer 均显示 `COHORT_CANDIDATE_CACHE=MISS`，并报告 `COHORT_COMPILE=PASS`。
- 主分支 GCC 参数化版本 `d7e0ec9`：Actions [#38771304613](https://github.com/yituanxing/minic-toolchain/actions/runs/38771304613)，7 个 producer 也全部 `MISS` 后真实编译，每路校验 `COHORT_ABI=PASS`；receiver 的 2064 全量链接 + QEMU P1 12 项系统调用 **PASS**。接收端出现 `COHORT_CANDIDATE_CACHE=HIT objects=2064` 是对这次刚生成的对象执行链接复用，不是生产者跳过编译。

| 分片 | C 对象数 | `b46822f` 编译 wall ms | `d7e0ec9` 编译 wall ms |
|---|---:|---:|---:|
| 0 | 295 | 407223 | 525903 |
| 1 | 295 | 379232 | 382639 |
| 2 | 295 | 553903 | 409560 |
| 3 | 295 | 292150 | 388623 |
| 4 | 295 | 396937 | 393394 |
| 5 | 295 | 398855 | 411967 |
| 6 | 294 | 493135 | 368719 |
| **7 路计时求和** | **2064** | **2921435** | **2880805** |
| **最慢单路** | — | **553903** | **525903** |

观察：同一 2064 目标规模下七路 wall 求和下降约 **1.39%**，最慢单路耗时下降约 **5.05%**。这仅是**两次不同 GitHub Runner 调度/负载/时间的观测值，不是同一环境交替测量的严格配对 A/B**，也不是累计 CPU time；每路有明显双向波动（如 shard 0 变慢、shard 2 变快），不能把 1.39% 宣布为实际优化收益或「确定没有任何性能回归」。

代码证明：`src/core/core_lower.c`、`src/core/core_lower_asm.c`、`src/target/riscv64/core_codegen.c`、`src/frontend/parser_core.c`、`src/frontend/parser_typedef.c` 的 Git Blob SHA 在 `b46822f` 与 `main` 完全相同；`compiler.c` 仅增加编译选项参数与保留原 API wrapper 的布局传递，并未重写 inline-specialization 主算法。这证明**已经验收的源码级优化没有被合并操作删除**，但不能替代严格 A/B。

后续严格 A/B：同机、同一 GNU 预处理后 .i 输入集、固定编译器版本与参数、交替 ABBA 顺序、至少重复 3 轮；只计 MiniC 自身 CPU 与 wall，不包含预处理、汇编、缓存复制、链接；逐 TU 匹配且报告中位数 / P95 / 总 CPU 时间。同时要求目标代码/ABI、Linux 全量链接与 P1 不退化。历史 perf-only scoped Boolean Domain 先走单独的受控候选试验，不直接并进生产分支。
