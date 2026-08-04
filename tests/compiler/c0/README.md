# C0 Compiler Gate / C0 编译器门禁

C0 is the first executable vertical slice of the C rewrite.

C0 是 C 重写的第一条可执行垂直切片。

The active boundary is:

当前边界为：

```text
external C preprocessor
→ MiniC .i-to-.s compiler
→ external RISC-V assembler and linker
→ QEMU runtime
```

`run.sh` is the fast host gate. It uses the external host compiler only as a preprocessor, then compares MiniC's emitted RISC-V assembly with frozen expected output.

`run.sh` 是快速宿主门禁。它只使用外部宿主编译器进行预处理，然后将 MiniC 生成的 RISC-V 汇编与冻结结果比较。

`run-runtime.sh` performs the complete external-toolchain path when `RISCV_CC` and `QEMU_RISCV64` are available. It reports `SKIP` rather than pretending the runtime gate passed when those tools are absent.

当 `RISCV_CC` 和 `QEMU_RISCV64` 可用时，`run-runtime.sh` 执行完整外围工具链流程。缺少工具时明确报告 `SKIP`，不会假称运行门禁已经通过。

C0 deliberately accepts only:

C0 有意只接受：

```c
int main() {}
int main(void) { return <decimal-int>; }
```

The next milestone extends the same vertical path with expressions and local variables; it does not start the native preprocessor or linker.

下一里程碑沿同一条垂直路径增加表达式和局部变量，不启动原生预处理器或链接器。
