# MC-064｜RV64 真正 ELF 链接负例、Core ASM 依赖身份接缝

基线 `b0846c7`；审计结果：`OPEN / P1 / RV64-ELF-LINK-DIFF`，**无生产修复**。该轮只增加合法 GNU RISC-V C 反例、GNU ELF/native 对照及修复合同边界；不得把成功复现记为已修复。

## 同源编译、汇编、链接与 native 结果

独立审计探针：

```sh
make MODE=release CFLAGS=-Werror -j7 build/release/bin/minic-cc
MINIC_CC="$PWD/build/release/bin/minic-cc" \
  sh tests/compiler/audit/probe-mc064-rv64-elf-link.sh
```

探针先以 `riscv64-linux-gnu-gcc -march=rv64i -mabi=lp64 -E -P` 对**同一份** `tests/compiler/audit/mc064_rv64_asm_static_symbol.c` 生成 `.i`；再分别由 GNU GCC 和 MiniC 编译该 `.i`。GNU 使用同一 `-march=rv64i -mabi=lp64 -O0 -ffreestanding -fno-builtin`，MiniC 使用 `--target=riscv64-linux -march=rv64i -mabi=lp64 -S`；MiniC `.s` 交目标 GNU AS。两侧 `.o` 均加同一手写 `mc064_rv64_start.S`，由目标 GNU LD 静态链接；链接成功者由 `qemu-riscv64` 运行并核查进程退出值。启动桩只调用 `anchor()` 并发 Linux exit syscall 93，不依赖目标 libc。

| `anchor` 条件 | GNU RV64 GCC→LD→QEMU | MiniC→GNU AS→LD→QEMU | 结论 |
| --- | --- | --- | --- |
| 默认：`"i"(asm_only_target)`；`anchor()` 返回 11 | PASS，exit 11 | AS PASS；LD FAIL：`.rodata+0x0` undefined `asm_only_target` | **已证真实链接错误** |
| `-DMC064_DIRECT_CALL`；返回 `asm_only_target(4)=23` | PASS，exit 23 | PASS，exit 23 | 普通 Core CALL 保活正控 |
| `-DMC064_STATIC_OBJECT`：`"i"(&asm_only_object)` | PASS，exit 11 | PASS，exit 11 | static object 符号根正控 |

默认形态的 MiniC `.o` 有 undefined `asm_only_target`，而 GCC `.o` 定义该本地函数；证明差异已经超出汇编文本、落到 GNU ELF 的可解析性。负例**没有可运行 ELF**，因此不能声称“MiniC QEMU 运行失败”；QEMU 证据仅来自 GNU 及两个 MiniC 正控。现有老文件的 `%c0` 是 x86 GNU operand modifier；**GNU RV64 GCC 拒绝该用法**，新对照改用 RV64 有效的 `%0`。

脚本目前明确接受“已知负例被重现”；不能放进 `check-fast` 当绿色正确性用例。修复 MC-064 时须将默认形态改成 MiniC LD PASS + QEMU exit 11，并升级脚本断言。

## 精确 owner 链，不可盲扫文本

1. `src/frontend/parser_expression.c` 建立 `MINIC_EXPRESSION_FUNCTION` + `MinicFunctionId`，`src/frontend/parser_static_constants.c:minic_parser_static_symbol_function_relocation` 已能在直接函数/取地址表达式中读出 typed ID。
2. `src/core/core_lower_asm_operands.c:core_inline_asm_immediate_text` 经 `core_inline_asm_symbolic_immediate_name` 把表达式 typed 函数身份折成 assembler name；`core_inline_asm_specialize_immediates` 只返回已替换好 `%0` 的字符串及长度。transitive inline-specialization 另可通过 `core_inline_asm_specialized_symbolic_text/name` 折出名字，不应被普通 direct case 的探针误当完整覆盖。
3. `src/core/core_lower_asm_scalar_forms.c:core_asm_try_atomic_and_immediate_forms` 调 `minic_core_function_add_opaque_inline_asm`，Core 的 `OPAQUE_INLINE_ASM` 只关联 `inline_asm_id`，没有 function relocation edge。
4. `src/compiler/core_reachability.c:minic_walk_reachable_core_edges` 在实际 reachable Core block 里已有 `CALL` 和 `FUNCTION_ADDRESS` 的 named-function enqueue，也有 `GLOBAL_ADDRESS` 的 static-object graph；但是 `OPAQUE_INLINE_ASM` 没有可追踪的 typed function edge。`minic_prune_inline_specializations_from_core` 把 frontend `is_referenced` 当 discovery hint，函数无其他根时被剪。
5. static-object 正控是另一份合同：`core_reachability.c:minic_static_asm_operand_visit` 当前从 source ASM operand AST 保守保留 object；不能据此宣称所有 function ASM root 已有 Core 精确支持，也不应直接搬其**全程序原始 ASM 扫描**去保留所有函数。那会让不可达 ASM 也保活函数，绕开 Core CFG 的发射事实。

## 最窄安全修复合同（本轮只设计，不实现）

建议添加**与 `source_inline_asm_id` 分开的 Core opaque-ASM typed relocation 引用**；由 Lower 在 `%0` operand 解析发生时产出，由 reachability 根据 `OPAQUE_INLINE_ASM` 所在的**实际 reachable Core block**遍历。最窄对象可以是 `MinicCoreInlineAsm` 每实例 0..N 条边：`FUNCTION -> MinicCoreFunctionSymbolId`（重用 Core 已有 `function_symbols` / `minic_core_function_add_function_symbol` 名字池）；确有 object 即 `OBJECT -> MinicCoreGlobalId`，让对象根通过现有图处理。没有 typed ID 的任意 ASM 原始文本**不合成边**，不能靠 substring、正则或任何 target `.s` token 反解析身份。

实施时需同时保证：

- 边来自**真实被替换的 GNU `i` symbol operand**，而非碰巧在模板里出现某字符串；按重写 `immediate_text` 的同一事实判定，排除纯数字、`I/rK` 约束误判、未被模板引用的输入；兼容 cast/address-of 和经 specialization 传递的 symbol+addend。
- Core 表持有并释放该 typed edge、Core verifier 严格验证索引；copy/clone、CFG 清理不会把过期边附着到另一条 ASM。先用限定形态（本反例）确认，再逐个扩展 register-output immediate/structured ASM，而非承诺所有 GNU ASM 形式。
- Reachability 遍历只消费已验证的 Core 边，延续当前 named-function enqueue、inline clone 与 static object roots 的语义；一个 dead block 的 ASM 不能凭 source-level 扫描保活无关函数。
- **不改变 opaque ASM 文本、寄存器、clobber、machine eligibility 或 deferred immediate emission**。任何新字段对现有目标后端是只读不可见的 provenance，直至单独批准它们的消费迁移。

尤其**绝不可把普通 opaque ASM 的 `source_inline_asm_id` 从 `SIZE_MAX` 改成 source id 当作修复**：`src/core/core_ir.c:minic_core_function_add_opaque_inline_asm` 将 `SIZE_MAX` 设为无 provenance；`src/target/riscv64/inline_asm_emit.c`、`riscv32/rv32_emit.c`、`aarch64/asm_contract.c` 对 `is_goto && id!=SIZE_MAX` 的路径生成 `__minic_deferred_asm_immediate_*` relocation；`loongarch64/simd_asm.c` 还对 structured SIMD 用该 id 精确回读 C0 并复验 effects。这是现役 Source→Core→Machine 合同，不是空闲通用引用槽。

## 修复后必须新增的验收

先把本脚本翻成**全部绿色**的 RV64 GNU AS/ELF/QEMU 差分，再补：合法 x86 `%c0` 正反控制、RV32/AArch64/Loong ABI 和相关 ASM operand modifier 的 GNU AS/LD 对照；`asm("name")`、weak/alias、`used/unused`、跨 TU/function pointer、纯数字和对象 relocation；不可达 Core block / dead inline clone 的不保活负控；所有既有 deferred asm-goto 及 Loong SIMD 资格/效果测试。仍不能凭只通过 `-S` 结案。
