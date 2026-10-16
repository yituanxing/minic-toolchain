# MiniC 已认证主线：正式源码与历史补丁收敛审计（2026-10-16）

## 0. 当前不可回退的基线

- 主分支：`main`，提交 `6da7e580bdec7f82c2e984e1a2a8e5d28bbffe87`。
- 真实 Linux 6.6.143 RV64、原生 RV32 compat-vDSO 联合验收：
  [Actions #38862398059](https://github.com/yituanxing/minic-toolchain/actions/runs/38862398059) **SUCCESS**。
- 七路 `MiniPP -> MiniC -> MiniAS`：2064/2064 C 对象及 34 个原生汇编对象，通过真实合并。
- `COHORT_CANDIDATE_CACHE=HIT objects=2064`，复用的是这一轮已校验的七路对象，不能理解为未运行编译器。
- `COHORT_P1=PASS name=full_all_verified syscalls=12 python_era_oracle=PASS`。
- `MINILD_VDSO32_KBUILD=PASS native_calls=1 gnu_product_calls=0 ELF32_RISCV_ET_DYN=true versioned_exports=3 QEMU_P1=12/12`。
- `MINILD_VDSO64=PASS ... real_Kbuild_link=true ... QEMU_P1=12/12`。
- `NATIVE_UNION_RESULT=PASS`，实际 MiniAR、MiniLD 已认证边界、MiniObjcopy、MiniPP/MiniC/MiniAS。
- 这**不是**“所有 GNU 调用为零”：被替换的目标链接与现存探测/参考调用需分别计数。
- **禁止**为了整理文件而把以上已认证的产物标记为新提交的 P1 认证；改变编译器可执行文件身份后，先局部、再七路、最后 QEMU。

## 1. 直接检查的当前实际生产链

`tools/ci/linux-runtime-build-minic-profile-v1.sh` 有 **44 个不重复的历史 Runtime apply-*.py 名称**。

| 源码路径 / 条件 | 当前实际行为 | 收敛结论 |
| --- | --- | --- |
| 默认的 44 个历史 Runtime 补丁，`native_manifest` 存在 | 校验 7 个正式源文件 Git Blob SHA；全部通过后只往 `patch.log` 写 `ALREADY_NATIVE_VALIDATED`，**不执行**这 44 个脚本 | 历史回放器保留用于旧分支/取证，**不是当前 main 的生产补丁依赖** |
| `MINIC_LINUX_FULL_CROSSBLOCK=1` | 额外执行 `apply-riscv64-p1-defined-before-use-crossblock-v1.py` | 已核对脚本检查 `P1_DEFINED_BEFORE_USE_CROSSBLOCK_V1` 的 7 组精确标记；当前 `core_codegen.c` 各标记数量全部吻合，故该 Python **只验证并返回 `ALREADY_CANONICAL`**，不会重新写 C 文件 |
| `MINIC_OPT_PURE_DISCARDED_ADDRESS_EQ=1` | 与 full-crossblock 模式的显式门禁绑定 | **不要**将此运行时配置开关误写成 Python 源码补丁；追踪实际二进制编译参数/等价规则 |
| `native_manifest` 缺失的历史恢复路径 | 顺序执行 44 个历史补丁 | 是遗留兼容分支，不可未证明可达性就删除，也不可允许它悄悄代替主分支固定源码 |
| `[linux-perf-*]` 显式触发的 A/B | 仍通过 `apply-perf-*.py` 生成独立候选并作配对验证 | 是**实验候选**而非认证后的默认主线。不能因历史测试通过就把所有实验变体一起合入 |
| ELF32 兼容链接 | `tools/minic-ld/main.c` 末尾 `#include "../../linker/src/elf32_compat_vdso.c"` | 行为已认证，但这是一个为保持编译器缓存键而作的 **跨 .c 包含临时构建依赖**，应当在专门的构建图改造中退出；不宜同 AST/Lowering 重构一起改 |

### 7 个正式源码 SHA：GitHub Blob 与 manifest 逐一实核

`docs/ci/native-runtime44-perf4-validated-sha1.txt` 的以下七项与当前 `main` **全部严格一致**：

| 文件 | 当前 Git Blob SHA |
| --- | --- |
| `src/compiler/compiler.c` | `1424ae500d9662068612b63091691ce8f322b371` |
| `src/core/core_lower.c` | `434da35dcba2d2fdd7f8c7bc5a74f8355612e21f` |
| `src/core/core_lower_asm.c` | `a8418e45608310687e3d6a1e96da79ca49135870` |
| `src/core/core_lower_internal.h` | `668f08f9f2de8e4fa207000cf2504017c3ab9b57` |
| `src/frontend/ast.h` | `bfca6a1861104f768b3b0ae93a72fa28bf0eded0` |
| `src/target/riscv64/core_codegen.c` | `3abecc04f9cec7523963b5368667ceb8ccb9f905` |
| `src/target/riscv64/function_symbol.h` | `d55e435041f4e34432a7870b6a2886e9bce70156` |

CI 七路日志可交叉核对：`MINIC_NATIVE_PROFILE=PASS runtime44=checked_in integrated_perf4=checked_in`；`LINUX_FULL_CROSSBLOCK=COMPILER_READY` 的各分片 compiler SHA 相同。

**关键澄清：** 此时应优先清理“历史补丁究竟是否仍被生产使用”的误解，而不是重新把已合入正式 C 源码的 44 个补丁执行一遍。正式源码已有这些结果，且得到整体 Linux P1 认证。

## 2. 分支和测试资产审计

`main` 之外的四个可见分支：

- `agent/linux-expanded-kbuild-v0`
- `agent/linux-perf-boolean-domain-v1`
- `agent/linux-rv64-only-parallel-v1`
- `archive/all-progress-2026-10-04`

逐个 `compare_commits(base=分支, head=main)`：全部 `behind_by=0`，说明截至本次检查分支顶端均为 `main` 祖先，**没有尚未合入 main 的分支提交**。这不授权直接删除保存中的归档 refs / 历史测试资料。

生产验收和历史实验必须分层：`T0` 静态/路由、`T1` 单元、`T2` 冻结输入等价、`T3` Kbuild/QEMU。尤其不可把某个旧 A/B 优化的临时产物当作已在当前源码上全量认证。

## 3. 实际源码结构和整理优先级

| 模块 | 实际观察 | 建议 |
| --- | --- | --- |
| `src/core/core_lower.c` | 约 16,479 行；包含 CFG、局部事实、Core Lowering 和相关表扫描 | 拆分前先以完整函数/数据拥有者为单位审查职责及头文件依赖，**先纯重构再单独做算法更改** |
| `src/target/riscv64/core_codegen.c` | 约 7,019 行；已经实现多种值/对象槽复用和生产者索引 | 不重复开发现有优化；对高频扫描与临时数组按真实 Codegen 占比排序 |
| `src/frontend/parser_statement.c` | 约 5,928 行；复杂控制流/大小固定的 switch 栈 | 先审查边界、生命周期和局部栈上数据量；不要机械分割 |
| `src/frontend/ast.h` / `ast.c` | `sizeof(MinicExpression)=680B`，`sizeof(MinicFunction)=6536B`（历史实测）；CALL 仅约 4～6%，却使所有表达式共享 64 参数的巨大 union | **空间高潜力但高风险**；先审查所有按值复制、function clone、arena 的所有权和析构，再设计 side table，而不是直接替换裸指针 |
| `tools/minic-ld/main.c` / `Makefile` | 直接包含 `elf32_compat_vdso.c` 避免改变 Makefile SHA 和 2064 C 缓存身份 | 推荐作为**首个低语义风险的构建图整理候选**，但单独证明 MiniLD 产物一致和缓存键只依赖真实编译器输入，再执行源码变更 |

以前固定 Linux 样本的 AST 增长策略 2× → 1.5× 已有负结果
([#38676131973](https://github.com/yituanxing/minic-toolchain/actions/runs/38676131973))，**不再重复这条弱收益实验**。

旧全量 2064 owner 双臂实测显示三索引合并 18,606.071s → 14,261.185s、`1.3047×`、**331 个单文件逆向回退**，这是特定版本、特定 .i→.s 累计耗时而非当前七路 end-to-end 实测。保留旧配对的源输入和编译器身份，不重新编数字。

## 4. 从这里继续的具体执行顺序（每步都要闭环）

1. **固定当前已成功版本**：保存本 Run/Job/artifact、二进制 SHA、输入 SHA 与 pass 范围，作为后续差分基线。不增加新的冗余 Workflow。
2. **生产/实验依赖矩阵**：以现存 profile、`Makefile`、workflow 的脚本调用为唯一有效入口，补齐“运行场景→是否改正式源码→是否更改可执行文件→认证 Run”四列。当前已证实默认 44 补丁跳过、crossblock 只校验。
3. **低风险整理先行**：只选择一个明确边界做纯重构，例如独立编译 MiniLD ELF32 .c 并调整组件级缓存身份；先 focused native linker regression + 原生四对象 ELF32/Kbuild，*构建缓存不能随与 MiniC 无关的 Makefile 修改失效*；运行时认证不得沿用旧编译器哈希。
4. **纯重构之后测量**：复用 `native-performance.json`、2064 owner old/new perf artifacts、真实慢文件与对照组。先分析当前最慢 Core/Parser/Inlining，而非依据行数盲猜。
5. **高收益数据结构优化**：独立试 AST CALL arguments 旁路存储/Function 参数存储，先限定不可变 ID/所有权证明和单文件 A/B；不能跳过 `.s` 字节对照、first500 与 Linux QEMU P1。

## 5. 本轮的严格边界

- 本审计**只读生产 C/Makefile/CI**；没有修改已通过 Linux 的源码、构建脚本、工具 SHA、测试命令或已有缓存。
- 记录已观察事实，不把待做的源码整理描述成已经完成，也不把历史 patch 名称当作当前运行时仍在应用。
- 用户暂缓的 `.eh_frame` / CFI 完整实现**不在本轮计划内**。
