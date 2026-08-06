# Maintenance checks / 维护检查

This directory contains repository-structure checks that protect explicit project boundaries. These checks validate the project description; they do not silently rewrite it.

本目录包含保护显式项目边界的仓库结构检查。检查负责验证项目声明，不会静默重写声明。

## Production C source inventory / 生产 C 源码清单

`check-production-source-inventory.sh` compares two sources of truth:

`check-production-source-inventory.sh` 比较两份信息：

1. the explicit `MINIC_SOURCES` list in the root `Makefile`;
2. every `.c` translation unit below the active production directories `src/` and `tools/minic/`.

1. 根目录 `Makefile` 中显式维护的 `MINIC_SOURCES`；
2. 当前生产目录 `src/` 和 `tools/minic/` 下的全部 `.c` 翻译单元。

The gate rejects:

门禁会拒绝：

- duplicate `MINIC_SOURCES` entries / 重复条目；
- listed files that no longer exist / 已登记但不存在的文件；
- production `.c` files omitted from `MINIC_SOURCES` / 存在但未登记的生产源码；
- list entries outside `src/` or `tools/minic/` / 越出当前生产目录边界的条目。

The list remains explicit so adding a production translation unit is a deliberate architecture and build-system decision. Automatic wildcard compilation would make accidental files silently become part of the compiler.

清单保持显式，是为了让新增生产翻译单元成为有意的架构与构建决策。自动通配编译会使误放入目录的文件静默成为编译器的一部分。

Run locally with:

本地执行：

```sh
sh tools/maintenance/check-production-source-inventory.sh
```

The complete GitHub Actions clean-checkout gate runs the same command before accepting compiler builds.

GitHub Actions 完整干净检出门禁会在接受编译器构建前运行同一检查。

## First-party C formatting / 第一方 C 格式化

`run-format.sh` applies the repository `.clang-format` configuration with `clang-format-18`. The fixed major version prevents unrelated formatter upgrades from rewriting the tree differently.

`run-format.sh` 使用 `clang-format-18` 应用仓库内 `.clang-format` 配置。固定主版本可以避免格式化器升级无关地重写代码树。

The active scope is every `.c` and `.h` file below:

当前范围是以下目录中的全部 `.c` 与 `.h` 文件：

- `include/`;
- `src/`;
- `tools/minic/`.

Vendored upstream files, generated outputs, and C files used as compiler input fixtures are deliberately excluded. They have separate provenance or test-shape requirements and must not be rewritten by the project formatter.

Vendor 上游文件、生成输出和作为编译器输入夹具的 C 文件被明确排除。它们具有独立来源或测试形态要求，不得由项目格式化器重写。

Run locally with:

本地执行：

```sh
make format-check
make format
```

`make format-check` is non-mutating. `make format` rewrites the declared first-party surface. The complete clean-checkout gate runs `format-check` in parallel with source inventory and host builds.

`make format-check` 不修改文件，`make format` 会重写声明的第一方范围。完整干净检出门禁会将 `format-check` 与源码清单和宿主构建并行运行。
