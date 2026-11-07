# MiniC RISC-V / LoongArch64 逐文件架构与正确性审计（滚动记录）

审计日期：2026-10-29。取样基线：`3866af2f6`，后续工作树允许并行提交改动。范围仅为 `src/target/{riscv,riscv32,riscv64,loongarch64}/` 的 92 个 `.c/.h`，不审 MiniAS。这里的 `FULL` 表示逐段读过完整文件，`PARTIAL` 表示读过重要调用链而仍有未完整展开的代码，`PENDING` 表示已入清单但尚不能声称通读。初始审计先只读源码；后续经主管分配，RV32 variadic ABI 和 RV64 record ABI 由本审计 agent 实作，仍保持审计发现与实现验证分开。

## 直接发现（证据与可达性分开）

### P0 — RV32 generic structured ASM 容量不一致（主管已本地修复，待验证）

- `src/core/core_ir.h` 的结构化 ASM 上限为 16；`src/target/riscv32/rv32_emit.c:282-290` 的目标物理寄存器数组只有 8 项。原代码 `:~428` 容许 16 个操作数并按 `operand_index` 查表，9–16 个会越界。
- `src/target/backend.c:31` RV32 没有专属 source 资格策略，故 generic 路径存在。应限制到实际 8 个槽并回归 9 操作数失败行为；主管已告知正在做该防护。本审计未声明已跑测试。

### P0 — RV64 3/5/6/7 字节 RECORD_LOAD 无声丢失高位字节（本地已修复，严格编译回归通过）

- `src/target/riscv64/memory_emit.c:77-112`：`record_size <= 8` 的 fast path 只有 8/4/2 映射 `ld/lwu/lhu`，其余任意长度均落入 `lbu t1,0(t0)`；之后 `minic_riscv64_frame_store_chunk(...,record_size)` 会把 `t1` 中的一个有效字节存回 3/5/6/7 个字节。
- `src/target/riscv64/frame_access.c:167-214`：非 1/2/4/8 chunk 按 `t1` 逐次右移 8 位写入；只载入第一个源字节意味着其它字节全部清零。`src/target/riscv64/legalize.c` 仅验记录尺寸与对象形状，未限定必须为 1/2/4/8，故这不是纯理论路径。
- 修复时进一步确认 RECORD_LOAD 的源指针不一定具有其记录类型的自然对齐（详见下一项），因此不是只局限非标准尺寸，**最终统一使用已有 `minic_riscv_emit_record_copy_bytes` 对所有大小的记录逐字节复制**。`tests/compiler/c0/probe-rv64-record-load-irregular.sh` 在 strict `rv64i/lp64 -S` 下通过 3/5/6/7/9/15 字节逐字节汇编断言；此处不声称已跑 RV64 QEMU 运行时。

### P0 — RV64 packed 成员地址与聚合 ABI 帧槽存在未对齐宽访存（本地已修复，严格编译回归通过）

- `struct natural_four{unsigned x;}; struct __attribute__((packed)) outer{char pad;struct natural_four v;}; return p->v;` 中，被返回成员类型本身对齐 4，但真实源地址从 packed 外层偏移 1。原 `RECORD_LOAD` 生成 `addi t0,t0,1; lwu t1,0(t0)`。这证明仅检查 **记录类型** 对齐仍不够，Core 需要独立的源地址对齐证明才能恢复宽加载快路。
- `src/target/riscv64/frame_access.c:101-214` 对 frame 2/4/8-byte partial chunk 原无目标偏移对齐门槛；`codegen_support.c:64-123` 返回聚合 2/4/8-byte chunk 原无源偏移证明。确切可达的 packed 先一字节参数场景 `mix_after_one_four(P1,P4)` 旧汇编为 `addi t3,sp,1; sw a1,0(t3)`，返回有 `addi t0,sp,1; lwu a0,0(t0)`；调用路径 `call_after_one_four` 还从 `sp+6` `lwu a1`、向 `sp+10` `sw a0`。不能依赖环境对未对齐字访存的容错。
- 修复：RECORD_LOAD 统一字节复制；frame chunk 仅当 `offset % width == 0` 用 `lhu/lwu/ld/sh/sw/sd`，否则复用原有 byte gather/scatter；聚合返回 helper 接收 **已验证的具体 frame object offset**，据此保持对齐快路，对来源不明的地址保守字节读取。未新增第二套复制算法。
- 实际证据：`tests/compiler/c0/probe-rv64-record-load-packed.sh` strict `rv64i/lp64 -S` 输出 `PASS packed/natural=2,4,8 nested_source_offset=1 odd_frame_abi=2,4,8 odd_call=4`；独立 debug build 成功、`git diff --check` 成功。**无 QEMU 动态运行证据**。

### P0 — RV32 varargs 宽度与对齐混同，导致 align4 Pair32 错误跳过 a1（本地已修复，严格编译回归通过）

- [RISC-V 官方 psABI 调用约定](https://docs.riscv.org/reference/abi/v1.0/riscv-cc-procedure-calling-convention.html) 的 even-register-pair 例外限定 **unnamed variadic，2×XLEN 对齐**，不是简单的两个 XLEN 大小。
- 原 `src/target/riscv32/rv32_abi_cursor.c:14-15` 用 `unnamed && words==2` 直接对齐到偶数 GPR；`rv32_abi_plan.c:45-60` 已求出 `shape.alignment` 却 `(void)align`。在 `check_pair(unsigned tag,...)` 先传 `tag=a0`、8-byte **align4** Pair32 时，旧 caller 用 `a2/a3`、tail `a4`，而 callee `va_start` 保存的连续游标从 `a1` 开始；align8 WideAlign 用 `a2/a3` 则正确。
- 修复：RV32 的 ABI shape 将 `DataLayout` 对齐值作为 typed plan metadata 保留；caller/callee 都使用唯一 `minic_riscv32_abi_place`，仅对 `unnamed && words==2 && alignment>=8` 请求 shared GPR cursor 的偶数寄存器配对。named 2-word 参数既有跨 `a7/stack` 规则保持不变。
- `tests/compiler/c0/probe-rv32-variadic-record-abi-alignment.sh` strict `rv32i/ilp32 -S`：自然 Pair32 align4 与 packed Pair32 align1 都用 `a1/a2`、tail `a3`；align8 Pair32 和 64-bit scalar 用 `a2/a3`、tail `a4`。现有 `core_rv32_va_record_cursor.c` 也独立 strict -S 通过，`main -> check_pair` 与 `main -> check_aligned` 差异符合上述映射。独立 `build/review-rv32-abi` 全编译 exit0；**没有 RV32 QEMU 运行时声称**。
- 额外复核 `rv32_frame.c:122-125`：当前所有 CoreObject 槽位即使 packed align1，也强制至少4字节对齐。因此 `rv32_call.c` 新加的 `offset%4` byte gather/scatter gate 只是 **P2 预防性硬化**，不是可达 P0；自然对齐帧仍走原有 `lw/sw` 快路。

### P1 — RV64 固定实参 FP 寄存器耗尽时错误拒绝合法 psABI 栈/GPR 路径

- `src/target/riscv64/abi.c:225-237`：当 named float/double 的 FPR 数已为 8，立即返回 `false`，没有转入后面的 GPR/栈布局。RV64 psABI 中浮点实参无法用 FPR 时应回落整型调用约定；如第 9 个固定 `double` 实参有可表示的位置，当前丢失编译覆盖。
- 下游：`function_abi_plan.c:108-126`（函数定义），`frame_requirements.c`（调用），`legalize.c:384,577`（admission）。修复同时调整 `function_abi_plan.c:54-69` 对 FLOAT 强制 FPR 的不变量；回归 8/9/10 个固定 float/double、整数/浮点穿插、GPR 已满与否。
- 这属于 fail-closed 能力缺口，不声称已编译出错误机器码。

### P1 — RV64 CALL admission 的独立 ABI 再分类没有 soft-float 参数

- `src/target/riscv64/legalize.c:384,577` 两种 CALL 用 `minic_riscv64_abi_cursor_initialize_for_return` 默认 LP64D；`core_admission.c:112-116` 又独立调用二者，而调用预案 `frame_requirements.c` 根据 strict LP64 选 soft-float。即前置检查与最终发射并非消费同一份已备好的 ABI 方案。
- 对严格 `rv64i/lp64`，固定 FP 参数的预检错误套用 `fa0..fa7`，可能因第 9 个固定 FP 参数而拒绝 soft-float 方案原本可表示的调用；与前一问题叠加。
- 目标是 `semantic call index → 目标 ABI plan → machine emit` 单链路，而非在 admission 重走另一套（可能不同配置的）cursor。

### P1 — LoongArch64 的 GNU ASM contract 源端和机器端两套 first-match 分发表

- `src/target/loongarch64/asm_contract.c:2935-2996` source qualifier 顺序尝试二十余个 `*_source`，`asm_contract.c:3442-3544` Core emitter 又按规范化模板选择另一个 `*_emit` 表，并分别再次验证 operand 位置、形状和类型。
- Linux 特种硬件 ASM 的白名单和严格机器验证应保留；问题是同一 form 资格和 emission 之间没有持久 typed proof/form ID，配置增长会出现相互漂移、重复解析与维护债。建议将各 Linux 源 form 的资格和 machine form 选型转化为同一个 Loong-only descriptor/plan，保留独立不变量验证和严格拒绝。

### P2 — 可复用小工具缺少 scratch/输入寄存器不相交契约（当前已查调用安全）

- `src/target/riscv/record_copy_bytes.c` 固定使用 `t2/t3` 作循环 scratch，公开 helper 参数允许任意 `destination_register`/`source_register`。当前调用点均使用 `t0/t1`，因此是未来调用隐患，不是现有可达 bug。应在接口给出不可重叠契约，并 fail-closed。
- `src/target/riscv64/codegen_support.c:64-123` 的 3、5–7 字节 aggregate chunk 路径固定用 `t6`，未防目的/地址寄存器别名。实际唯一生产调用 `core_codegen.c:352-360` 用 `a0/a1` 和 `t0`，当前安全；后续抽通用算法前须防别名。
- `src/target/loongarch64/frame_emit.c:6-32` 的大偏移永久占用 `$t3`；**不是已证实的现成 bug**：已人工追 `asm_contract.c` 的 60+ 处访问，以及 `block_emit.c`/`call_emit.c`/`integer_emit.c`/`codegen.c`/`simd_asm.c` 的显式 helper 调用；常见数据寄存器为 `$t0/$t1/$t2/$t4`，`integer_emit.c:120` 的 int128 high limb 用 `$t3`，但其最后一次 frame load 后才使用。`frame_emit.c:41` 有 `$t3` 目的 **load**，地址重用对 load 本身安全。仍建议让 Frame helper 接口明确 scratch/clobber 契约，以避免今后新增对 `$t3` 的 **store** 丢值。
- `src/target/loongarch64/call_emit.c:45-74` 的旧 builtin-immediate fallback 最多五次循环扫描整函数 instructions 以按 SSA ID 寻定义；`fn->values[value].definition` 已持有直接索引。`has_constant_integer` 更快的路径优先，因此仅标 P2 隐性 O(n²) 热路径，不标为每次 CALL 的常态瓶颈。

### P2 — 目标布局假设没有全部显式传 `TargetInfo`

- RV64 `abi.c:52-58,249`、`legalize.c`、`global_emit.c`、`frame_layout.c:45,78,156` 反复使用 `minic_default_data_layout()`。目前 RV64 LP64 与默认布局一致，暂不能断言具体输出错误；长线应将目标布局从 TargetInfo/已有 Common typed plan 明确传入，避免未来 RV32/非默认数据模型泄漏。

## 已确认成立的架构边界

- `riscv/isa_features.h` 将目标 ISA 能力与 MiniC 主动发出的基础指令子集分开；`riscv/isa_resolve.c` 属于 RISC-V 共同的 ISA/ABI 组合裁决。
- RV32 `rv32_abi_plan`/`rv32_abi_cursor` 是 ILP32，RV64 `function_abi_plan`/`abi` 是 LP64/LP64D。**独立 ABI 策略合理**，不能机械合并成一个 `size_t` 版 ABI。
- RV64 的 `asm_plan.c:216-220` 在使用 `[10]` 数组前检查 `operand_count <= 10`；这是 RV32 [8] 修复应对应的 fail-closed 模式。
- `loongarch64/simd_asm.c:440-449` 根据 `source_inline_asm_id` 精确指向原 AST ASM；已不存在以全 TU 扫描碰运气的旧选源途径。
- LoongArch64 的 LP64D ABI policy 在 `codegen.c:13-33` 独立描述间接结果 GAR、varargs、wide split，不与 AArch64 的实际 ABI 混用；Common 负责 envelope/typed shapes 是合理共享。

## 逐文件读证据及未覆盖清单

每行按目录顺序罗列；审计层级为 **当前** 状态，后续阅读会更新。本记录不因 `rg` 命中或一次 `wc` 就视为 FULL。

| 目录 | FULL（整文件读） | PARTIAL（已看关键调用链） | PENDING |
|---|---|---|---|
| `riscv/`（12） | `frame_address_policy.h`, `function_symbol.h`, `gpr_abi_cursor.h`, `instruction_select.h`, `isa_features.h`, `isa_resolve.c`, `isa_resolve.h`, `record_copy_bytes.c`, `record_copy_bytes.h`, `register_identity.h`, `word_operations.c`, `word_operations.h` | 无 | 无 |
| `riscv32/`（16） | `rv32_abi_cursor.c`, `rv32_abi_cursor.h`, `rv32_abi_plan.c`, `rv32_abi_plan.h`, `rv32_call.c`, `rv32_emit.c`, `rv32_emit.h`, `rv32_fp_emit.c`, `rv32_frame.c`, `rv32_global_emit.c`, `rv32_global_emit.h`, `rv32_integer_emit.c`, `rv32_internal.h`, `rv32_memory_emit.c`, `rv32_scalar.c`, `rv32_wide_emit.c` | 无 | 无 |
| `loongarch64/`（22） | `asm_contract.c`, `asm_contract.h`, `block_emit.c`, `block_emit.h`, `call_emit.c`, `call_emit.h`, `codegen.c`, `codegen.h`, `fp_emit.c`, `fp_emit.h`, `frame_emit.c`, `frame_emit.h`, `integer_emit.c`, `integer_emit.h`, `memory_emit.c`, `memory_emit.h`, `residual_emit.c`, `residual_emit.h`, `scalar_normalize.c`, `scalar_normalize.h`, `simd_asm.c`, `simd_asm.h` | 无 | 无 |
| `riscv64/`（42） | `abi.c`, `abi.h`, `asm_plan.c`, `asm_plan.h`, `call_emit.c`, `call_emit.h`, `codegen.h`, `codegen_function.c`, `codegen_internal.h`, `codegen_support.c`, `core_admission.c`, `core_admission.h`, `core_codegen.c`, `core_codegen.h`, `fp_emit.c`, `fp_emit.h`, `frame_access.c`, `frame_access.h`, `frame_layout.c`, `frame_layout.h`, `frame_requirements.c`, `frame_requirements.h`, `function_abi_plan.c`, `function_abi_plan.h`, `global_emit.c`, `global_emit.h`, `inline_asm_emit.c`, `inline_asm_emit.h`, `isa.c`, `isa.h`, `legalize.c`, `legalize.h`, `memory_emit.c`, `memory_emit.h`, `overflow_emit.c`, `overflow_emit.h`, `record_snapshot.c`, `record_snapshot.h`, `runtime_libcall.c`, `runtime_libcall.h`, `scalar_emit.c`, `scalar_emit.h` | 无 | 无 |

注意：以上 12+16+22+42=92 个生产 C/H 文件已经**逐段通读 92/92**。此标记只代表源代码静态审阅覆盖，不代表每条控制流已得到动态测例，尤其 Loong 大 ASM contract 与 RV64 多重 ABI 分类，仍需对关键跨文件契约实施定向验证。
