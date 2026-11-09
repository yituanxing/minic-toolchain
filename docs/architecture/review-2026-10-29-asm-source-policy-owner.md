# MiniC 五目标 GNU ASM 源级准入策略：Target-owned Policy 批次

2026-10-29。范围：`src/target/target_info.c` 原 875–1320 行的 GNU ASM 源词法/语义准入、LoongArch 机器 builtin 声明。**这不是机器 ASM emitters 正确性的宣称**。

## 逐调用链审计

| 来源和消费者 | 入口 | 搬迁后唯一权威 |
| --- | --- | --- |
| `parser_function.c` 文件作用域绑定；`parser_local_declarations.c` 局部绑定；`parser_expression.c` 赋值；`ast_verifier.c` 复验 | fixed-register read/write/local | `MinicTargetAsmSourcePolicy` 各目标 fixed/local callbacks |
| `expression_semantics.c` GNU `+r` 特例 | readwrite hardreg | x86 源准入回调，最终物理机器所有权另验 |
| `parser_gnu_inline_asm.c` 显式 clobber | source clobber | 各目标 source callback；x86 `rbx`/`ebx`/`flags` 为 syntax-only，不扩大 machine clobber |
| Core ASM operand checks、parser GNU `I` 和 `m`/`Q` | immediate/memory constraint | 五 ISA `I` 语义在 owner；通用 `m` 保留统一契约，AArch64 `Q` 单独准入 |
| `parser_builtins.c` 和 LoongArch native call emitter | machine builtin signature | 独立 `MinicTargetMachineBuiltinPolicy`，避免跟 GNU ASM policy 混淆 |

`target_info.[ch]` 保留原公开 API 和 source 参数类型以保证 Frontend/Core 无 ISA 枚举。每个 `MinicTargetInfo` 只持有不可变的 typed policy 指针，拷贝到 invocation 后仍指向相同 descriptor。原 `target_info.c` 9 处按架构分发的多目标逻辑已经完全抽出，相关范围没有 `switch (target->architecture)`；五目标独立 owner：`source_policy/{riscv32,riscv64,x86_64,aarch64,loongarch64}.c`。LoongArch builtin 在同一目标模块但作为**独立 typed policy** 注册。

## 保留的 fail-closed 边界

- RV32 的局部 `a0–a7` 和 clobber `a0–a7/t0–t6` 不意外继承 RV64 的物理 GPR 别名规则；RV64 仍独立验证 `x10–x17` 和 caller-saved。
- x86 `%rax` 是局部声明可用的 AT&T GNU 语法，但 `rbx`/`ebx` 只准 **source** clobber，**machine** 和 local 仍拒绝；`rsp +r` 必须匹配无符号 long，最终机器寄存器状态仍由 x86 owner 判断。
- AArch64 `sp` 全局读取、`x30` 局部 PAC 绑定，局部 `x/r0–17`，拒绝 `x18`/callee-saved；只有该目标将 `Q` 认作 memory operand。
- LoongArch 全局 `$tp/$sp/$r21` 读取，只有 `$tp/$r21` 写入；局部仅有 `t0/a0/a1/a7`；`$s0` 仍是语法 clobber，而不是随意物理破坏许可。
- `I`：RV32/RV64/LoongArch signed imm12；AArch64 imm12 或左移 12；x86 shift count 0–31。opaque/missing policy 一律拒绝（错误文本保留兼容默认）。
- LoongArch CRC C plain char 参数保留 **plain-char identity** 和目标 signedness；不能错误转换为 explicit `signed char`。

## 验收

- `CFLAGS=-Werror make -j8 BUILD_DIR=build/registry-source-policy all`：PASS。
- `make ... check-four-target-model-m0`：PASS，新增 `TARGET_ASM_SOURCE_POLICY=PASS`（五目标 callbacks、语法/机器分离、opaque policy fail closed、Loong CRC C 身份）。
- 五目标 `minic-cc --target=<isa>-linux -S tests/compiler/c0/gnu_function_void_pointer_equality.c`：5/5 PASS；此前 target-selection M0 PASS。
- `make ... check-fast`：PASS（在拆分 Loong builtin 成独立 policy 前，最后这次拆分只更换指针所属结构，需在 exact head 验证）。
- `git diff --check`：PASS。

## 后续观察清单（没有混入本批）

1. `target_info.c` 更早区域仍有 target-specific function security attributes、Loong named builtins/type predicates 和部分 varargs cursor分支。这是**另一批 language/ABI owner 分离**，不应借 GNU ASM policy 抽象顺带改动。
2. Source admission 不等于 ASM operand allocator 的 alias/early-clobber/no-overlap 完整证明。即使语法接受，target backend 必须 fail-closed 不支持的 machine template。
3. `MinicTargetInfo` 初次 canonical 初始化沿用旧的静态懒写逻辑，多线程调用 race 风险属于旧设计债；本批没有改全局初始化。
4. Header/Bootstrap/CI manifest 新增源清单均须登记 5 个 `.c`，避免只在 full build 可链接。主 agent 集成。
