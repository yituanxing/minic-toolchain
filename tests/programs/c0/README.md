# C0 differential programs / C0 差分程序

This directory contains complete executable programs that are compiled and run through two target-side lanes:

本目录包含完整可执行程序，并通过两条目标侧流水线编译和运行：

```text
source.c -> RISC-V GCC -> static ELF -> QEMU
source.c -> RISC-V GCC preprocessing -> MiniC -> RV64 assembly
         -> RISC-V GCC assembly/linking -> static ELF -> QEMU
```

The gate compares exit status, standard output, and standard error. A program belongs to the permanent suite only when its basename is listed in `manifest.txt`.

门禁比较退出码、标准输出和标准错误。只有源码基名登记在 `manifest.txt` 中时，程序才属于永久套件。

## Manifest rules / Manifest 规则

- one lowercase `a-z`, digit, or underscore basename per line / 每行一个只含小写字母、数字或下划线的源码基名；
- blank lines and lines beginning with `#` are ignored / 空行和以 `#` 开头的行会被忽略；
- order is execution order and should remain intentional / 清单顺序就是执行顺序，必须有意维护；
- duplicate entries are errors / 重复条目属于错误；
- every entry must have a matching `<name>.c` file / 每个条目必须有对应 `<name>.c`；
- every top-level `.c` file in this directory must be listed / 本目录每个顶层 `.c` 文件都必须登记。

Adding a program therefore requires adding the source and its manifest entry in the same review. Renaming or removing a program must update both together.

因此，新增程序必须在同一次审查中同时加入源码和 Manifest 条目；重命名或删除也必须同步修改两者。

## Boundary / 边界

The manifest inventories complete GCC/MiniC differential executables only. Focused parser, type, layout, diagnostic, and assembly fixtures remain in their existing dedicated suites and are not listed here.

该 Manifest 只清点完整 GCC/MiniC 差分可执行程序。Parser、类型、布局、诊断和汇编等聚焦夹具继续由各自专用套件管理，不在本清单中登记。
