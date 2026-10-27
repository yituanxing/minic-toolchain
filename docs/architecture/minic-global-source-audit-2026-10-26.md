# MiniC 全局源码审计（2026-10-26）

> 范围：**只审计 MiniC 源码和入口及关联的 CI 路由**，不是 MiniAS、MiniLD、MiniPP 的全量源代码审计。审查当前 main 基准 `ea7f2b3`。本文件区分静态阅读、CI 执行、真实 ELF/内核运行证据；完整的文件扫查不等于所有 C 功能已实现或形式化验证。

## 覆盖实况

此次读取了 `src/` 下面全部 **177 个 C 实现文件**（Frontend 45、Core 33、Compiler 7、Target Common 18、Target 专属/配置 74），并阅读 `tools/minic/main.c`、`tools/minic-cc/main.c`、各目标 Backend 接口及主要 ABI/Frame/TargetInfo 头文件。大文件先做模式和模块边界的全文件筛查，对主入口、四目标共享路径、可疑重复处再进行上下文精读。**不是逐行人工证明了全部实现与测试**；测试/文档/历史脚本也不是全量读完。这些限制不得从本审计标题中省略。

| 层级 | 文件入口与职责 | 审计结果/风险 |
| --- | --- | --- |
| Frontend | `src/frontend/{parser_*,ast*,type*,const_eval*}` | 没找到直接 `MINIC_TARGET_ARCH_` 机器分支；通过 TargetInfo/DataLayout 查询 C 语言类型。大型静态初始化器和 builtins 仍是回归风险区。 |
| Core | `src/core/{core_ir*,core_lower*,local_facts*,reentry_index*}` | Core 语义和目标机器注册层分离；保留必要的 `target/data_layout`、Backend inline-asm 资格接口。需防止再次增加 CPU 枚举分支。 |
| Compiler | `src/compiler/{compiler,core_function_set,core_reachability,inline_*}.c` | `compiler.c` 依次执行 Parse→AST verify→specialize→Core verify→Target emit；编译器不应维护第二张架构跳转表。 |
| TargetInfo | `src/target/{target_catalog,target_info,data_layout,backend}` | **发现并删除**无生产调用者的 `minic_riscv64_argument_registers`：物理寄存器名字不属于通用 TargetInfo 的语言 ABI/数据布局接口。 |
| x86-64 | `src/target/x86_64` | 保留 SysV caller/callee、x87/SSE 和本地机器发射器；外部 global alias 等尚有明确 fail-closed 限制，不能因为四架构测试绿灯称为全 C 语法完成。 |
| RV32/RV64 | `src/target/riscv32`、`riscv64`、`riscv` | 共享 Base-I 操作选择、位操作/record byte copy；ILP32/LP64 ABI、帧和 call lowering 必须继续分离。当前 RV32 GNU Linux **真实** Image+QEMU P1 已重建验收。 |
| AArch64/LoongArch64 | `src/target/{aarch64,loongarch64}`、`common/direct_*` | 复用单个 DirectLP64 预检、call/frame planner 和 residual semantic plan；寄存器分配、帧重定位、指令语法仍为各 target 独立所有。AArch64 large-SP 文本合法化及 LoongArch frame helper 是重点差分测试路径，不强行统一。 |
| 两个 CLI | `tools/minic*` | 使用同一 Target Catalog；五目标 `-S` 不代表五目标都能经过内置 assembler/linker 完成完整工具链。 |

## 全局 CI owner 路由中的确认缺陷及修复

`.github/workflows/minic-runtime-fourarch.yml` 原来的 `pull_request.paths` **只有 RISC-V 源码**，所以 AArch64、LoongArch64、x86、Frontend、Core 等 PR 不一定执行四架构运行时；`push.paths` 也只列了部分具体后端文件（例如未包含完整 AArch64 `fp_emit.c`、`asm_legalize.c` 及 LoongArch `frame_emit.c`），可能在改动时错误地沿用旧 CI 的成功结果。修复：PR 与 main Push 都将 `src/**`、`include/minic/**`、`tools/minic/**`、`tools/minic-cc/**`、`Makefile` 作为 source-owner 广泛覆盖，同时保留外部真实软件专门脚本的既有路径；边界检查验证这些全局触发项在两种事件下均存在。**没有为此引入新的构建流水线。**

## R12 最新内核证据（按确切版本记录）

- SHA：`ea7f2b35894dc4195d403e93ecd24dae50ac9b49`
- GitHub Actions：`40268099875`
- RV32I/ILP32：Linux 6.6.143 原版 GNU Kbuild 七分片，MiniC C `1741/1741`，无 GCC C owner 后备。
- Image SHA256：`8282e0499576936ef89075bea875ec5eb7e5fab06365d6a7906eb113780ca638`
- 真实 QEMU：ELF32 PID1 P0、ELF32 shell、P1 12 种 syscall × 12 次独立启动、同 Image SHA 均 PASS。
- RV64 Linux P1：历史上通过，但未在本次 SHA 完成重新生产/真实 P1 验收；不以 3352 个 `.i -> .s` 替代 ELF/QEMU。

## 后续优先级

1. P0：所有 C/IR/目标架构变更必须有四架构运行时 gate（本批修），确认不把某一个 ISA 的局部绿灯当全局通过。
2. P1：RV64 2064/2064 新 HEAD 完整 GNU Linux Image/P1 对照；与 RV32 的已经确认证据并列保存。
3. P1：AArch64/LoongArch 的动态栈、long double/int128、large frame、varargs 与真实 GNU/QEMU 做专项差分；这些并非只看源代码就能宣称完整正确。
4. P2：R10 只继续抽离确有同一机器中立算法的重复计算；不要重写 x86、AArch64、LoongArch 机器发射器。R11 过时文档/未使用的目标名字按编译与 CI 证据逐步删除。

## 2026-10-27 incremental follow-up (base main a2c88e13)

Scope: cross-checked the 2026-10-26 source-wide audit, merged PRs #665/#666/#668/#669, and the fingerprinted review ledger. The unchanged Frontend, Core CFG, LP64 function dispatcher and Common object planner are not re-reviewed without source drift. Full source was read for RV32 physical frame and call ABI planners and RV64 physical frame. Parts of x86/RV64/AArch64/LoongArch CALL emitters were inspected selectively, not certified fully read.

**P1 confirmed: RV32 prototype-vs-actual ABI classification.** rv32_plan_call previously used the expression's actual Core value type for fixed prototype arguments, unlike RV64 and x86 which use the declared signature. This can reserve the wrong GP/stack word count for width-mismatched fixed arguments. The correction selects formal for fixed and actual for variadics, and rejects unconverted fixed scalar mismatches rather than silently emitting the wrong ABI. This may expose incorrect upstream Core conversions; exact-head upstream/QEMU evidence is required before merging.

**P2 confirmed: malformed RV32 machine frame accesses.** Slot helpers admitted a valid Core ID with the SIZE_MAX unallocated offset sentinel and wide access could wrap offset +4. A large-offset store using t6 as both scratch address register and source would silently corrupt that source. Corrective machine guards reject absent/unallocated/out-of-frame 4- and 8-byte slots, preserve all existing valid calls, and refuse self-aliasing large stores; a native negative probe exercises actual RV32 frame helpers.

**Reviewed without change:** RV64 physical frame size and second offset placement walk still implement separate target-specific psABI geometry, not a proven redundant Common semantic scan. Keep RV32 ILP32, RV64 LP64, x86 SysV, AAPCS64 and LoongArch64 LP64D physical call mechanics independent. Priority for remaining deep inspection moves to five call emitters; ledger tracks source revisions and review depth without claiming inventory means full review.

**Open:** RV64 latest-head original Linux Image/QEMU P1, versus the narrower seven frozen C shards; AArch64/LoongArch dynamic frame, varargs and FP QEMU differential; potential quadratic first-fit spill search, which needs a profile before algorithm replacement. None should be marked resolved by a broad PASS badge alone.

## 2026-10-27 RV64 CALL continuation (base main 5fea20f9)

**Concrete wide-return defect (P1):** the RV64 direct-call machine writer
already stored `long double` (binary128) and `__int128` from the two
psABI GPR return words `a0/a1`, but the indirect-call writer used the
single-word `emit_core_call_scalar_result` after `jalr`. With a function
pointer returning 16 bytes, the high half of the result was lost. Both
paths now call the same checked target-owned wide-return helper. This is
machine ABI transport, **not** a new Common Core instruction family.

The existing GNU/MiniC RV64 client/provider/QEMU differential
`tests/compiler/c0/run-rv64-core-call-transfer.sh` is extended with
*actual function-pointer returns* of 128-bit integer and binary128.
The preexisting RV64 origin smoke CI triggers it at the exact PR head;
the 7 expensive Linux shards remain opt-in by the existing PR title
gate. A C compiler build or static grep is not accepted as execution
proof; record the GNU/QEMU result before merge.

AArch64/LoongArch64 already have separate native handling for wide
returns. x86 handles `__int128` via RAX/RDX and x87 `long double`
independently. No cross-ISA "universal return register" was introduced.

### Failure-driven admission audit (same batch; original GNU/QEMU test)

The first exact-PR-head differential failed *before assembly*:
`cannot emit RISC-V function 'rv64_transfer_entry'`. This was not a QEMU
failure and not evidence that the new two-register return transfer was
wrong. Global inspection of `riscv64/legalize.c` and `riscv64/abi.c`
found two additional **independent structural gates** rejecting the
previously intended capability:

1. The direct CALL legality check only accepted long double as a
   WIDE_SCALAR return, while indirect CALL legality disallowed
   WIDE_SCALAR return/arguments entirely. Machine code already had
   two-word arguments and now has two-word returns. One checked
   `rv64_wide_call_argument_supported` function is shared across direct
   and indirect legality, and both accept int128/binary128 returns with
   full 16-byte ABI qualification.
2. ABI value classification mistakenly treated `__int128` as 2*XLEN
   **only with LP64 soft-float**. Both LP64 and LP64D pass 128-bit
   integer in the same two GPR/stack words; the FP ABI changes scalar
   float registers, not integer storage width. The classifier and
   native ABI cursor assertions now reflect this.

This is a concrete example of why reading **admission + ABI planning +
machine emission together** exposed a missing capability that code
emitter-only review missed. Exact-head GNU/QEMU remains the acceptance
gate; do not mark complete before its successful rerun.
