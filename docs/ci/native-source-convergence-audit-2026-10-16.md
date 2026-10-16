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


## 6. MiniLD ELF32 独立编译与缓存身份隔离：第一项纯整理已执行（2026-10-16）

**正式源码/构建修改：**

- `Makefile` 的既有 `MINILD_SOURCES` 显式增加 `linker/src/elf32_compat_vdso.c`，由既有 `linker/%.c` 规则编译成独立 `.o` 并链接到 `minic-ld`；没有第二套编译入口。
- `tools/minic-ld/main.c` 删除临时 `#include "../../linker/src/elf32_compat_vdso.c"`；原 ELF32 实现、函数 API、动态符号、重定位处理和回退策略都不改。
- `tools/ci/linux-runtime-profile-cache-id-v1.py` 的 narrowly scoped 兼容校验：仅当当前 Makefile 除了**这一行 MiniLD .c 输入**外恢复出的 Git Blob SHA 恰为 `22d2b8332e1a310c3ba806af2df8527bddf7ae0e`，且校验器本身恢复出的旧 Blob 恰为 `27dfe30688e4ccaaad21b0fd01a1bd602d3196e7`，才沿用原 2064 编译器缓存身份。任何额外 Makefile/校验器字节改动走新哈希，不做不受控的全局忽略。无需依赖浅克隆中不存在的历史 git 对象。
- 新的 `--self-test` 由**原有** focused T1 Job 调用；未新增 workflow/job、未删除历史证据。

**真实 CI 验收：**

- [#38868741617](https://github.com/yituanxing/minic-toolchain/actions/runs/38868741617) **SUCCESS**：实际 host `cc` 输出独立 `build/.../obj/linker/src/elf32_compat_vdso.o`，随后链接命令显式包含该独立对象；MiniLD 静态/动态 A0–A6、section GC、MiniAR、RV64 一组既有聚焦测试全部 PASS。
- 同次测试 `MINIC_CACHE_COMPONENT_ISOLATION=PASS legacy_2064_cache_key_preserved=true linker_source_only=true other_changes_invalidate=true no_network_no_git_history=true`。
- [#38868865311](https://github.com/yituanxing/minic-toolchain/actions/runs/38868865311) **SUCCESS**：现有缓存四对象真实 Linux ELF32 链接回放，GNU 汇编对象参考与 MiniAS 生成的四对象均 `MINILD_VDSO32=PASS`；GNU/MiniLD 相同输入的 `.text` 字节一致，原生 ELF32 ET_DYN/动态版本符号得到核验，`C_recompiled=0`、无 GNU 生产链接回退。仅使用已存在的一个 CI receiver。
- 上一次 **完整** Linux Kbuild + QEMU P1 12/12 认证仍是重构前的 [#38862398059](https://github.com/yituanxing/minic-toolchain/actions/runs/38862398059)。本次独立组件纯重构后，已证明静态/动态链接和真实四对象差分等价，**没有重新运行完整 2064 Linux QEMU**；不把短测重新标成完整 P1。

**后续退出条件：** 当前把旧脚本和 Makefile 的确切 Blob 固定为单次兼容窗口；将来若正式把 C 编译器与链接器的构建图分成稳定的组件 Makefile/缓存身份，必须先实证新身份命中规则和旧证书映射后再删除兼容层；任何变更不得使旧 Linux 对象在编译器源变更后静默命中。


## 7. Core Lowering zero-only boolean proof extraction（第二项纯整理）

**不改变算法和选择条件。** 原 `src/core/core_lower.c` 顶部从
`CORE_ZERO_ONLY_BOOL_CFG_V1` 开始的完整连续约 188 行搬入
`src/core/core_lower_bool.c`。仅 `core_zero_bool_cfg_condition`
由 `static` 改为供内部编译单元调用的函数，内部
`core_zero_bool_literal / is_local / has_entry_initializer / is_immutable`
仍为 `static`。函数体与原源码逐字核对，无表达式语义修改。

- `src/core/core_lower_internal.h` 声明唯一内部入口，`lower_if`
  仍用原函数原参数调用；外部 `src/core/core_lower.h` API 不变。
- `Makefile` 原 `MINIC_SOURCES` 增加
  `src/core/core_lower_bool.c`，使用已有 C 编译规则。
- 正式源码 manifest 更新的 **仅两处**：
  `src/core/core_lower.c = ce262d93f8c77348019a4f6fd94a0a8ccdc22d7d`；
  `src/core/core_lower_internal.h = 7997978a9b10a6d01e33dbc00dd11cb3f668f69f`。
  其余原生 Runtime 7 个校验项没有变。
- 当前 `linux-runtime-profile-cache-id-v1.py` 的兼容层仅豁免
  **旧的一行 MiniLD 构建调整**，发现 `MINIC_SOURCES` 实际变化时
  **严格错过旧的 2064 MiniC 缓存**，不能沿用旧 Image 作新代码
  的运行时证书。聚焦 CI 初次失败来自此前缓存自检误假定永远是
  MiniLD-only 修改；已修复该自检，将来源区分成
  `unchanged_exact_linker_only` 与
  `changed_compiler_cache_invalidated`，不需要修改编译器代码。
- 现有 [focused T1 #38869982837](https://github.com/yituanxing/minic-toolchain/actions/runs/38869982837)
  实际结果 **SUCCESS**，`TOOLCHAIN_FOCUSED_T1=PASS`，
  `MINIC_CACHE_COMPONENT_ISOLATION=PASS scope=changed_compiler_cache_invalidated`。
  RV64/驱动常量 if、MiniAR、MiniLD 8 独立检查仍被复用。
- **待证**：这个 C 文件迁移改变了 MiniC 二进制及构建缓存身份，
  必须用新的七路 2064/2064 MiniPP→MiniC→MiniAS 生产对象，
  运行实际 Linux MiniAR/MiniLD/MiniObjcopy/QEMU P1 12/12。
  T1 PASS 不是完整 Linux P1 认证。

**保留的算法疑点**：`core_zero_bool_is_immutable` 为每次候选
检查遍历 TU 的 statement/expression 数组。是否在大 .i 中重复付出
显著 CPU 成本，需要真实 phase/profile 数据；本次只是让这类
全程序级语义证明拥有明确边界，并没有引入缓存或改变 fail-closed
路径。
