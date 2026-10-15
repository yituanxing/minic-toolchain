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
