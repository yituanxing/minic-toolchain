# Compiler C0/C1 Draft PR Scope / 编译器 C0/C1 草稿 PR 范围

This draft integration scope contains the compiler-only path from preprocessed C to RV64 assembly, external assembly/linking, and GCC/MiniC differential execution. It intentionally excludes a native preprocessor, assembler, linker, libc, and bootstrap takeover.

本草稿集成范围只包含“预处理后的 C → MiniC → RV64 汇编”，以及外部汇编、链接和 GCC/MiniC 双轨执行。原生预处理器、汇编器、链接器、libc 和自举接管均不在本 PR 范围内。

The draft PR is opened before merge readiness so GitHub Actions can validate a clean checkout. Merge must remain blocked until the test matrix is green and the branch history is reviewed.

本草稿 PR 在达到合并条件前创建，目的是让 GitHub Actions 验证干净检出环境。测试矩阵全部通过并完成分支历史审查前，不得合并。
