# Git Workflow / Git 工作流

## 1. Purpose / 目的

Git history is part of MiniC's technical documentation. Commits should explain not only what changed, but why the boundary, ownership model, algorithm, or validation strategy changed.

Git 历史是 MiniC 技术文档的一部分。提交不仅应说明修改了什么，还应解释为什么改变边界、所有权模型、算法或验证策略。

## 2. Commit scope / 提交范围

A commit **SHOULD** represent one reviewable change. Pure refactoring, behavior changes, production-path switches, formatting, generated files, and large evidence updates should remain separate whenever practical.

一个提交**应当**只包含一项可审查的修改。纯重构、行为变化、生产路径切换、格式化、生成文件和大型验证证据应尽量分开提交。

Temporary architectural deviations **MUST** reference an entry in `docs/DEVIATIONS.md`.

临时架构偏离**必须**引用 `docs/DEVIATIONS.md` 中的登记项。

## 3. Commit title / 提交标题

Use an English imperative or concise action title with a subsystem prefix:

提交标题使用英文祈使或简洁动作描述，并带子系统前缀：

```text
frontend: intern identifier spellings
sema: separate target ABI planning
riscv: add aggregate return classification
build: add sanitizer gate
docs: record allocator ownership rules
```

Avoid vague titles such as `fix`, `update`, `try again`, or `linux changes`.

避免使用 `fix`、`update`、`try again`、`linux changes` 等含义不明确的标题。

## 4. Commit body / 提交正文

Production and architectural commits **MUST** contain:

生产和架构提交**必须**包含：

1. an English explanation;
2. a `中文说明：` section;
3. a `Validation / 验证：` section;
4. a deviation reference when applicable.

1. 英文说明；
2. `中文说明：` 段落；
3. `Validation / 验证：` 段落；
4. 如有临时偏离，必须注明对应编号。

The body should describe observable behavior, ownership, invariants, architectural boundaries, or the reason for the chosen algorithm. It should not repeat the diff line by line.

正文应说明可观察行为、所有权、不变量、架构边界或算法选择理由，不应逐行复述差异。

## 5. Validation / 验证

Record the commands or gates actually executed. Do not claim tests that were not run.

只记录实际执行的命令或门禁，不得声称通过未运行的测试。

Examples:

```text
Validation / 验证：
- make check-fast: PASS
- focused lexer suite: 12/12 PASS
- Linux-18 token differential: mismatch 0
```

Use `NOT RUN` with a reason when a relevant gate is intentionally deferred.

相关门禁暂缓时，使用 `NOT RUN` 并注明原因。

## 6. Exceptions / 例外

Very small metadata-only commits may use a shorter body, but repository initialization, policy, build, migration, architecture, and production-code commits still require bilingual context.

极小的纯元数据提交可以缩短正文，但仓库初始化、规范、构建、迁移、架构和生产代码提交仍必须包含双语背景。

Emergency or experimental commits may temporarily use an abbreviated message on a non-main branch. They **MUST** be rewritten or squashed into compliant commits before entering `main`.

紧急或实验提交可在非主分支暂时使用简化信息，但进入 `main` 前**必须**重写或压缩为符合规范的提交。

## 7. Local template / 本地模板

Configure the repository template after cloning:

克隆仓库后配置提交模板：

```sh
git config commit.template .gitmessage
```

The template is guidance and does not replace review.

模板只提供提示，不能替代审查。
