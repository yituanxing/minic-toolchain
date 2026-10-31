# MiniC 手动审计最小反例（已知失败，非绿灯回归）

本目录保留与 [架构问题台账](../../../docs/architecture/minic-issue-register.md) 中部分 MC 编号绑定的**合法 GNU C 输入**。没有纳入 `make test` 的自动 PASS 集合：在修复前让某些用例失败是预期行为，不能把“仓库含测试文件”写成“问题已修复”。

| 编号 | 源文件 | 当前核对的差异 | 需要的级别 |
| --- | --- | --- | --- |
| MC-023 | `mc023_static_record_backward_uaf.c` | Record arena 扩容后被后向零覆写读取；ASan UAF，不保证可见值错 | GNU11 C + MiniC ASan `-S` |
| MC-049 | `mc049_static_chained_array_uaf.c` | ArrayType arena 扩容后静态链式设计符 UAF | GNU11 C + MiniC ASan `-S` |
| MC-051 | `mc051_long_double_negzero.c` / `_support.c` | GNU x86 native mask `0`；MiniC `-S`→GNU cc native mask `0x10` | GNU native 双 TU 严格差分 |
| MC-056 | `mc056_runtime_record_positional_uaf.c` | 普通局部 Record 顺序初始化器跨 Record arena grow UAF | GNU11 C + 五目标 MiniC ASan `-S` |
| MC-057 | `mc057_gnu_empty_record_abi.c` | GNU empty struct 返回与参数在 RV32/RV64/x86 被不同路径拒绝 | 四种宏 case × 六目标 `-S` |
| MC-058 | `mc058_inline_symbolic_pointer_offset_direct.c` / `_transitive.c` | 同一个 `i` ASM source，RV64 直接静态指针 PASS、经过一层 `key+1` 传递 FAIL | GNU `-O2` 编译 + RV64 `-S` 正反控制 |
| MC-059 | `mc059_rv64_lp64d_union_float_abi.c` | LP64D 具名 FP union 四类 return/param/call FAIL，LP64 soft/control PASS | GNU C + RV64 两 profile `-S` |
| MC-060 | `mc060_lp64_large_record_abi_cap.c` | 2048B Record A64/Loong PASS、2049B FAIL | GNU C + A64/Loong `-S` 阈值对照 |
| MC-061 | `mc061_record_field_pointer_array_uaf.c` | Record 字段递归声明扩容后借用 UAF | GNU C + 五目标 MiniC ASan 容量矩阵 |
| MC-062 | `mc062_x86_alt_reloc_memory_operand.c` | GNU 的实际 ALT relocation 随 m 输入符号变化，MiniC 只允许固定符号 | GNU ASM/ELF relocation + x86 `-S` 正反控制 |
| MC-063 | `mc063_tagged_bare_record_field_layout.c` | tagged bare struct 声明错误产生匿名成员，GNU native exit48，MiniC native exit88 | GNU warning-allowed + x86 native 差分 |
| MC-064 | `mc064_asm_only_static_function_reachability.c` | GNU `i` 静态函数引用被裁掉定义，`.s` 有 `.quad` 无 label | GNU x86 ELF + RV64 `-S`/直接 C call 正控 |
| MC-066 | `mc066_aarch64_fp_pointer_aggregate.c` | 32B Record 的 `double*` 被误认成 FP member；`int*` 正控 | GNU C + A64 `-S` |
| MC-067 | `mc067_attributed_record_forward_declaration.c` | GNU 属性修饰 forward decl 被 MiniC 误认为必须带 `{` | GNU C + 五目标 `-S`/plain 控制 |

在本仓库根目录执行，示意（`build/sanitize/bin/minic-cc` 需先按项目的 sanitize 构建方式建立）：

```sh
cc -std=gnu11 -O0 -fsyntax-only tests/compiler/audit/mc023_static_record_backward_uaf.c
cc -std=gnu11 -O0 -fsyntax-only tests/compiler/audit/mc049_static_chained_array_uaf.c
cc -std=gnu11 -E -P -x c tests/compiler/audit/mc023_static_record_backward_uaf.c -o /tmp/mc023.i
cc -std=gnu11 -E -P -x c tests/compiler/audit/mc049_static_chained_array_uaf.c -o /tmp/mc049.i
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 build/sanitize/bin/minic-cc --target=riscv64-linux -S /tmp/mc023.i -o /tmp/mc023.s
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 build/sanitize/bin/minic-cc --target=riscv64-linux -S /tmp/mc049.i -o /tmp/mc049.s
cc -std=gnu11 -O0 -o /tmp/mc051-gcc tests/compiler/audit/mc051_long_double_negzero.c tests/compiler/audit/mc051_long_double_negzero_support.c
/tmp/mc051-gcc
cc -E -P -x c tests/compiler/audit/mc051_long_double_negzero.c -o /tmp/mc051.i
build/debug/bin/minic-cc --target=x86_64-linux -S /tmp/mc051.i -o /tmp/mc051-minic.s
cc -no-pie -o /tmp/mc051-minic /tmp/mc051-minic.s tests/compiler/audit/mc051_long_double_negzero_support.c
/tmp/mc051-minic
```

**容量相关：** MC-049 默认 30 预置 typedef 适合 RV32/RV64/A64/Loong 的已观察相位；x86 在 `cc -E -P` 一步添加 `-DMINIC_DROP30` 移除第 30 个；`-DMINIC_EXTRA31` 可检查未命中相位时无报警并不代表修复。MC-023 的 14 个前置 Record 适合 16 容量相位，其他目标/配置应调整容量条件。`-S` 不能证明目标链接/QEMU，ASan PASS 也不能单独证明所有容量无 UAF。

第六轮 `MC-056/057/058` 复现步骤（先固定 GNU 预处理产物；任何非零退出码都应和精确诊断匹配）：

```sh
cc -std=gnu11 -E -P -x c tests/compiler/audit/mc056_runtime_record_positional_uaf.c -o /tmp/mc056.i
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 build/sanitize/bin/minic-cc --target=riscv64-linux -S /tmp/mc056.i -o /tmp/mc056.s
# x86/AArch64 使用 cc -E -P -DMINIC_SKIP_R15 命中其已观测容量相位。
cc -std=gnu11 -E -P -DTEST_RETURN -x c tests/compiler/audit/mc057_gnu_empty_record_abi.c -o /tmp/mc057-ret.i
build/debug/bin/minic-cc --target=riscv32-linux -S /tmp/mc057-ret.i -o /tmp/mc057-rv32.s
build/debug/bin/minic-cc --target=aarch64-linux -S /tmp/mc057-ret.i -o /tmp/mc057-a64.s
cc -std=gnu11 -O2 -c tests/compiler/audit/mc058_inline_symbolic_pointer_offset_transitive.c -o /tmp/mc058-gnu.o
cc -std=gnu11 -E -P -x c tests/compiler/audit/mc058_inline_symbolic_pointer_offset_direct.c -o /tmp/mc058-dir.i
cc -std=gnu11 -E -P -x c tests/compiler/audit/mc058_inline_symbolic_pointer_offset_transitive.c -o /tmp/mc058-trans.i
build/debug/bin/minic-cc --target=riscv64-linux -S /tmp/mc058-dir.i -o /tmp/mc058-dir.s
build/debug/bin/minic-cc --target=riscv64-linux -S /tmp/mc058-trans.i -o /tmp/mc058-trans.s
```

MC-055 的 Core verifier API ASan 仅针对 malformed 内部形状，尚未归档成自动 compiler regression；它**不是**合法 C 正常编译路径的已证问题。所有这些审计输入在修复前均为预期失败材料，不能混入绿灯通过率统计。

第七轮 `MC-059..067`（`MC-065` 是排除的误报，编号不重用）建议分型执行：

```sh
# 当前构建程序以 build/debug/bin/minic-cc 为例；所有输入先由系统 GCC 预处理。
cc -std=gnu11 -E -P -DPROBE_RETURN -x c tests/compiler/audit/mc059_rv64_lp64d_union_float_abi.c -o /tmp/mc059.i
build/debug/bin/minic-cc --target=riscv64-linux -march=rv64imafd -mabi=lp64d -S /tmp/mc059.i -o /tmp/mc059-hard.s
build/debug/bin/minic-cc --target=riscv64-linux -march=rv64i -mabi=lp64 -S /tmp/mc059.i -o /tmp/mc059-soft.s

cc -std=gnu11 -E -P -x c tests/compiler/audit/mc060_lp64_large_record_abi_cap.c -o /tmp/mc060-big.i
cc -std=gnu11 -E -P -DMC060_SMALL -x c tests/compiler/audit/mc060_lp64_large_record_abi_cap.c -o /tmp/mc060-small.i
build/debug/bin/minic-cc --target=aarch64-linux -S /tmp/mc060-big.i -o /tmp/mc060-big.s
build/debug/bin/minic-cc --target=aarch64-linux -S /tmp/mc060-small.i -o /tmp/mc060-small.s

cc -std=gnu11 -E -P -DMINIC_SKIP_R15 -x c tests/compiler/audit/mc061_record_field_pointer_array_uaf.c -o /tmp/mc061.i
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 build/sanitize/bin/minic-cc --target=x86_64-linux -S /tmp/mc061.i -o /tmp/mc061.s

cc -std=gnu11 -Wall -Wextra -Werror -c tests/compiler/audit/mc062_x86_alt_reloc_memory_operand.c -o /tmp/mc062-bad.o
readelf -r /tmp/mc062-bad.o
cc -std=gnu11 -E -P -DALT_GOOD -x c tests/compiler/audit/mc062_x86_alt_reloc_memory_operand.c -o /tmp/mc062-good.i
build/debug/bin/minic-cc --target=x86_64-linux -S /tmp/mc062-good.i -o /tmp/mc062-good.s

cc -std=gnu11 -Wall tests/compiler/audit/mc063_tagged_bare_record_field_layout.c -o /tmp/mc063-gnu
/tmp/mc063-gnu
cc -std=gnu11 -E -P -x c tests/compiler/audit/mc063_tagged_bare_record_field_layout.c -o /tmp/mc063.i
build/debug/bin/minic-cc --target=x86_64-linux -S /tmp/mc063.i -o /tmp/mc063-minic.s
cc -no-pie -o /tmp/mc063-minic /tmp/mc063-minic.s
/tmp/mc063-minic

cc -std=gnu11 -E -P -x c tests/compiler/audit/mc064_asm_only_static_function_reachability.c -o /tmp/mc064.i
build/debug/bin/minic-cc --target=riscv64-linux -march=rv64i -mabi=lp64 -S /tmp/mc064.i -o /tmp/mc064.s
# 手动比对 `.quad asm_only_target` 与缺失的 `asm_only_target:` label。

cc -std=gnu11 -E -P -x c tests/compiler/audit/mc066_aarch64_fp_pointer_aggregate.c -o /tmp/mc066.i
build/debug/bin/minic-cc --target=aarch64-linux -S /tmp/mc066.i -o /tmp/mc066.s
cc -std=gnu11 -E -P -DMC066_INTEGER_PTR -x c tests/compiler/audit/mc066_aarch64_fp_pointer_aggregate.c -o /tmp/mc066-control.i
build/debug/bin/minic-cc --target=aarch64-linux -S /tmp/mc066-control.i -o /tmp/mc066-control.s

cc -std=gnu11 -E -P -x c tests/compiler/audit/mc067_attributed_record_forward_declaration.c -o /tmp/mc067.i
build/debug/bin/minic-cc --target=x86_64-linux -S /tmp/mc067.i -o /tmp/mc067.s
cc -std=gnu11 -E -P -DMC067_PLAIN_CONTROL -x c tests/compiler/audit/mc067_attributed_record_forward_declaration.c -o /tmp/mc067-control.i
build/debug/bin/minic-cc --target=x86_64-linux -S /tmp/mc067-control.i -o /tmp/mc067-control.s
```

各阶段非零退出码**需核对准确诊断或退出值**：尤其 MC-063 的 native exit48/88 本身是观测值，不能用 shell 的“非零表示测试失败”把合法对照吞掉；MC-064 目前只有 RV 汇编证据，不能写成目标 GNU 链接已经失败。没有目标 QEMU 证据的条目保持当前证据等级。
