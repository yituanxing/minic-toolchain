# MiniC RISC-V + LoongArch64 第二轮架构审计与去重

日期：2026-10-29。分工目录：`src/target/{riscv32,riscv64,loongarch64}/`，只涉及 MiniC，不包含 MiniAS。范围内 80 个 C/H 文件已在第一轮 `review-2026-10-29-riscv-loong.md` 的 **92/92** 完整审阅清单内；本轮针对 ABI 调用合法化、CallView、Loong 访存与 RV32 ABI 重新沿调用链检查，**不宣称本轮重新逐字通读 80/80 文件**。第一轮完整逐文件清单保留作可查审计记录。

## 本轮已实施：RV64 direct/indirect CALL ABI 去重

`src/target/riscv64/legalize.c` 的 `minic_riscv64_core_direct_call_supported()` 和 `minic_riscv64_core_indirect_call_supported()` 原来各自实现一整套 ABI 参数定位与合法性检查：重复执行 Core 参数验证、浮点 GPR/FPR fallback、int128 宽标量拆分、0/1/2 槽 record、间接 record 指针传递，合计约 200 行。两份代码只因调用目标来自符号或 SSA 指针便分叉，对未来 psABI 更改极易产生漂移。

调整后的结构：

1. `MinicCoreCallView` 已提供目标无关的已验证调用视图，direct 和 indirect 分别验证 symbol/signature，再交给同一个 `rv64_core_call_return_supported()`，共享返回形态 + RV64 psABI 检验。
2. 共用 `rv64_core_call_arguments_supported()` 以同一个 `MinicRiscv64AbiCursor` 和实参顺序统一判定 scalar FPR/GPR、wide split、聚合 register/stack/indirect；不复制 cursor 算法。
3. `indirect` 的 **unnamed variadic record 仍须 fail-closed**：`call_emit.c` 的机器传递未实现这一分支，helper 的 `direct_call` 入参仅限定此支持边界，不是 ABI 重复实现；`direct` 既有 unnamed record 路径继续允许。
4. `program == NULL` 的独立 legacy scalar-only 路径本轮保持原状，避免改变历史 API 的宽松能力与报错时机；后续可先证明调用者可达性再收敛。

## 带证据的问题台账

| 级别 | 位置与问题 | 本轮动作 / 后续方向 |
|---|---|---|
| P1 已处理 | `riscv64/legalize.c`：direct/indirect 独立遍历全部形参并重复判 record、FP、wide；即使 emitter 已统一参数运输，也有两套 admission | 单一参数+返回 policy；脚本覆盖 direct / indirect record、LP64/LP64D FP/GPR、RV32/Loong 基线 |
| P1 未处理，需根接口 | `src/target/target_info.h` 的 `MinicRiscvIsa riscv_isa` 作为通用目标结构字段；`tools/minic-cc/main.c` 直接 include `riscv/isa_resolve.h` 并检查 `isa_family == RISCV` | 根接口应以 target-owned subtarget option descriptor/parse-resolve 回调承接 `-march/-mabi` 原始值，结果放目标私有/opaque policy；CLI 不需要 `if RISCV`，后端仍可消费已验证 RV32/RV64 ISA 结构。**本轮不改公共结构或 CLI，避免与根任务冲突。** |
| P1 未处理 | `riscv64/legalize.c` 与 `riscv64/frame_requirements.c` 等依然各自通过默认 ABI cursor 对同一次调用分类，不消费唯一 frozen `TargetFunctionCallPlan`；软浮点配置边界可能漂移 | 将合法化、frame requirements、machine emission 改为消费同一持久的 invocation-scoped typed ABI placement；先建立可审计 plan，后删旧 cursor，再严测 LP64/LP64D 多参数 |
| P1 未处理 | `loongarch64/asm_contract.c` 约 2900+ 行的源 qualification 和 3400+ 行的 machine emission 各有 pattern-first-match 解析，form ID 不持久化 | 将 Loong Linux inline ASM 转为 typed `AsmFormDescriptor`/proof，分类与 emit 共用同一个 form key，保留源端资格和目标指令边界两段检查 |
| P1 未处理 | `riscv64/abi.c`、frame、global paths 中 `minic_default_data_layout()` 的隐式 RV64 LP64 假设 | 让 TargetDataLayout 从 invocation 显式进入 ABI、FrameRef、global emit，而不是目标逻辑暗取默认；不应机械地将 RV32 ILP32 与 RV64 LP64 分类合并 |
| P2 已证明暂安全 | `riscv32/rv32_frame.c` 栈对象当前至少 4 字节对齐，RV32 word fallback guard 是未来布局更紧时的安全设施，暂无可达奇数帧错码证据 | 保留 4-byte 对齐边界，不以此宣传现存 P0 |
| P2 建议后续 | `loongarch64/frame_emit.c` 大偏移 helper 的 `$t3` scratch clobber、`riscv/record_copy_bytes.c` scratch 与入参重合时无显式 API 契约 | 给 MachineAddress/FrameRef 与 ScratchSet 写出明确寄存器占用证明，避免后续扩展时隐式别名 |

## ISA 与 ABI 的架构边界

RV32I/ILP32 的 ABI cursor 不等同于 RV64 LP64/LP64D：尤其 unnamed 2×XLEN variadic 对齐仅在实际 `alignment >= 2*XLEN` 时需要偶数寄存器对，两者不能仅以一个宽度 switch 粗暴合并。**可以共享算法的宿主是带数据布局、ABI policy 的参数位置规划器；不能共享的仍由各目标声明 ABI 规则。**

`riscv64/isa.c` 的 `codegen_features` 表示 MiniC 主动生成的指令能力，GNU inline ASM 与系统内联汇编可合法含有更多目标支持的 ISA 指令。目标选项解析迁移时必须保留这两个独立的 capability set，不能因为 CLI 去掉 RISC-V case 而放宽 `-march=rv64i` 的实际发射门禁。

## 可复验证据与限制

- 独立 debug `-Werror` `minic-cc` 构建、`make check-rv64-abi`。
- `tests/compiler/c0/m151_indirect_call_batch.c` 在 `x86_64-linux`, `riscv32-linux -march=rv32i -mabi=ilp32`, `riscv64-linux -march=rv64i -mabi=lp64`, `aarch64-linux`, `loongarch64-linux` 五目标 `-S` 均通过。只证明 Core 到汇编成功，不代表机器码动态正确。
- `probe-gnu-indirect-record-return-5isa.sh`：**PASS**，五目标分别验证 1-GPR / 2-GPR / hidden-sret 返回与 direct record 实参；额外以 x86_64 GNU/native 与 MiniC/native 可执行文件作实际运行，且拒绝不支持的 SysV FP record。
- `probe-lp64-empty-record-call-abi.sh`：AArch64、LoongArch64 **PASS**，空参数 0 GP/0 stack，嵌套空成员不占槽，空返回 0 字节。`probe-four-backend-record-call-abi.sh` 所需 `aarch64-linux-gnu-gcc` 在本工作环境不可用，因此没有将它算作通过。
- RV64 hardfloat 9th fixed、FP record fail-closed、packed/irregular RECORD_LOAD；RV32 variadic 4/8 字节对齐；Loong packed scalar/wide 保守 guard：现成专项脚本断言 `-S` 均通过。
- 尚无新 QEMU 运行时结果，本轮不把 `-S` 标为 QEMU PASS；后续独立运行完整 Linux 21-shard exact-head CI。

本轮生产文件单独 `git diff --numstat`: `legalize.c` **+128 / -253，净减少 125 行**；未改 `Core`、`common`、`target_info.h`、`main.c` 和任何其他 agent 负责的生产文件。`git diff --check` 无错误。
