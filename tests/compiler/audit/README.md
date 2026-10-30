# MiniC 手动审计最小反例（已知失败，非绿灯回归）

本目录保留与 [架构问题台账](../../../docs/architecture/minic-issue-register.md) 中部分 MC 编号绑定的**合法 GNU C 输入**。没有纳入 `make test` 的自动 PASS 集合：在修复前让某些用例失败是预期行为，不能把“仓库含测试文件”写成“问题已修复”。

| 编号 | 源文件 | 当前核对的差异 | 需要的级别 |
| --- | --- | --- | --- |
| MC-023 | `mc023_static_record_backward_uaf.c` | Record arena 扩容后被后向零覆写读取；ASan UAF，不保证可见值错 | GNU11 C + MiniC ASan `-S` |
| MC-049 | `mc049_static_chained_array_uaf.c` | ArrayType arena 扩容后静态链式设计符 UAF | GNU11 C + MiniC ASan `-S` |
| MC-051 | `mc051_long_double_negzero.c` / `_support.c` | GNU x86 native mask `0`；MiniC `-S`→GNU cc native mask `0x10` | GNU native 双 TU 严格差分 |

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
