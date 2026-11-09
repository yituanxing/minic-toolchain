# MiniC LoongArch64 Linux ASM 的 source/machine 描述符统一

日期 2026-10-29。只修改 `src/target/loongarch64/asm_contract.c`，不修改 Frontend、Core、backend registry、Makefile 或 MiniAS。以第一轮完整 92/92 RISC-V/Loong 文件审计和本轮 80 文件范围为背景；本轮重点重新确认 Linux GNU ASM 从 source 原始模板到 typed Core 和机器发射的**实际活调用链**，不将过去的整文件通读记录冒充本轮重读。

## 活链证据、分工与问题

1. `src/core/core_lower_asm.c` 先调用 `core_asm_try_target_register_contract`，失败时调用目标声明的 `minic_backend_requires_qualified_input_asm` 阻止原 ISA 模板未经批准落入 generic scalar/opaque Core。
2. Loong 的目标资格入口 `minic_loongarch64_qualify_inline_asm` 位于 `asm_contract.c`；原来的 `:2935+` 是 20 多个 `*_source` first-match 条件。`minic_loongarch64_emit_qualified_structured_asm` 是 `:3440+` 独立的 20 多个 `*_form` 再匹配条件。二者虽然调用了多个相同模板解析器，**优先级和新 form 登记并非同一份所有权**。
3. `minic_loongarch64_requires_qualified_input_asm` 另持一份 reserved opcode prefix 清单。这份清单并非 source forms 的全集；新增 Linux form 时容易遗漏，导致资格拒绝后还有被 generic Core 接走的风险。现有负例没有证明所有 opcode 都会溜过，故归类为 **P1 fail-closed 契约风险**，不冒称已证 P0 错码。
4. Loong `simd_asm.c` 的 `source_inline_asm_id` 指向真实 AST 来源，不存在随意全 TU 扫描复用同一模板的旧行为；本轮保留。`linux_jump_label_asm_goto` 有真实 label/ELF PREL 重定位与专门 Core block，不能并入普通 structured-ASM 描述符。

## 本轮整批消重

新增唯一的目标私有 typed registry：

```c
typedef struct LoongLinuxStructuredForm {
    LoongLinuxTemplateMatch matches;
    LoongLinuxSourceQualify source;
    LoongLinuxStructuredEmit emit;
} LoongLinuxStructuredForm;
```

同一 `loong_linux_form_for_template(text,len)` 提供给三类读者：

- source contract 调用该 form 的 `source` 类型/约束 qualifier；
- machine structured ASM 调用**同一个 form** 的 `emit`，若已匹配但 emitter 因 Core operand 不合格拒绝，立即 fail-closed，不能尝试其它 form 的 fallback；
- `requires_qualified_input_asm` 保留原预防恶意变体的 reserved opcode prefix 快路，另外向此注册表查询所有**精确的有效 Linux 硬件模板**，确保新增正例硬件 form 即使非法 GNU constraint，也不掉进 generic Core。

本次统一的 **14 类活 form**：CACOP、VDSO syscall、frame trace、frame trace CSR、CPUCFG、FPR transfer、futex cmpxchg、FCSR0 write、per-CPU indexed load/store、LL/SC cmpxchg、atomic xchg、small xchg、mask nospec、PTE。

没有机械纳入需要额外 typed 参数的 clock form、futex RMW、IRQ、invtlb、sub-positive、add-unless、uaccess、per-CPU AMO，以及非普通 structured ASM 的 ASM goto / SIMD 模式。这些依然保留原检查器，目的是避免为了合表而丢弃特殊寄存器、CSR immediate、goto 标签和 fixup 语义。

本批代码 `git diff --numstat` 为 **`asm_contract.c +123/-49`，净增 74 行**。这是引入一份三方共享的表驱动模型和单一查找函数的实际成本；删除了源端和机器端各自的 14 个决策分支。没有删掉真正的 target-specific emission 算法，没有新增目标 .c 或依赖。

## 完成的本地验证

`MODE=debug`, `CFLAGS=-Werror`, 独立 `build/review-riscv-loong-strict/bin/minic-cc` 全构建通过。`m151_indirect_call_batch.c` 分别使用 `x86_64-linux`、`riscv32-linux -march=rv32i -mabi=ilp32`、`riscv64-linux -march=rv64i -mabi=lp64`、`aarch64-linux`、`loongarch64-linux` 的 `-S` 全 PASS（5/5）。

Loong 原 GNU 源模板专项 `-S` 与恶意变体 fail-closed：

| 测试脚本 | 本地结果与边界 |
|---|---|
| `probe-loongarch-linux-percpu-fpu.sh` | 15 真实 form、4 负例 PASS，`SOURCE_ONLY=1` |
| `probe-loongarch-linux-nospec.sh` | 原 nospec form、6 负例 PASS，`NATIVE_ONLY=1`（本环境不跑 Loong QEMU） |
| `probe-loongarch-linux-csr-constants.sh` | 4 CSR 形态、3 负例 PASS，`SOURCE_ONLY=1` |
| `probe-loong-linux-xchg-qemu.sh` | 32/64 位模板、2 负例 PASS，`SOURCE_ONLY=1` |
| `probe-loong-linux-small-xchg-qemu.sh` | 8/16 位模板、15 负例 PASS，`SOURCE_ONLY=1` |
| `probe-loong-linux-cmpxchg-qemu.sh` | 3 LL/SC 模板、2 负例 PASS，`SOURCE_ONLY=1` |
| `probe-loong-linux-cache-ops.sh` | 6 CACOP+IBAR、6 负例 PASS，`SOURCE_ONLY=1` |
| `probe-loong-linux-named-store-mb-qemu.sh` | 32/64 位模板、10 负例 PASS，`SOURCE_ONLY=1` |
| `probe-loongarch64-linux-final-three.sh` | opaque、bit-arith、原 GNU 本机 x86 比较、unsupported ASM PASS |
| `probe-loongarch64-linux-cpucfg.sh` | constant/runtime、未知指令负例 PASS；Loong GNU assembler 不存在，显式 SKIP |
| `probe-loongarch64-linux-jump-label.sh` | PREL32/PREL64、goto、mutated form 负例 PASS；Loong GNU assembler 显式 SKIP |
| `probe-loongarch64-linux-simd-provenance.sh` | 128 重复模板、128 decoy、late source PASS |

部分历史脚本的成功输出文本包含固定的 `GNU_QEMU` 字样，但这轮明确设置 `SOURCE_ONLY=1`，因此**不应把它们报告为 GNU/Loong QEMU 实际运行**。此环境 `loongarch64-linux-gnu-gcc-13` 不存在，完整 GNU GAS+QEMU 对照必须由远端 exact-head CI 补证。

## 剩余 P1 / P2

- **P1：持久 typed `InlineAsmFormId` / operand payload**。当前 registry 只在 source 与 emit 两阶段用同一个模板重新查找；Core 无 form ID，所以不能证明跨阶段身份保持不变。后续如允许改目标 Core ASM metadata，可让 target-specific form payload 归入 `TargetAsmPlan` 并消除二次模板扫描；不能直接把 GNU 原文随意放进机器生成器。
- **P1：复杂异形契约**。Clock、AMO、invtlb、uaccess 和 futex 特型含需解析的操作数值/枚举；先做 typed parser `recognize + validate + emit` 的分层模型，再迁入描述符。切忌创建四十个带 `void*` 的不安全 callback。
- **P2：性能**。精确 form registry 带来单次最多 14 个短模板匹配，目标内联汇编不是标量 hot loop；对超大 Linux Linux shard 的构建性能仍应做 A/B，不能宣称零开销。
- **P2：source reserved prefix 列表仍留用于拒绝非法/变体硬件模板，与正例描述符是不同职责，不建议直接删除。未来可让 descriptor 提供自己的 negative source prefix 以减少维护两份列表。
