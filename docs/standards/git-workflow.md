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

## 7. Branch scope / 分支范围

A branch **MUST** have one independently reviewable and testable engineering outcome. The branch boundary should be wider than a mechanical one-file edit but narrower than a multi-goal umbrella effort.

一个分支**必须**对应一个可以独立审查、独立验证的工程结果。分支范围应比机械式的“一个文件一次修改”更宽，但必须小于混合多个目标的总括性工程。

An external project is a milestone and a source of requirements; it is not automatically one branch. A small project may fit in one branch. A project that exposes several independent compiler capabilities should advance through several bounded branches and pull requests, each with its own acceptance criteria.

外部真实项目是一个里程碑和需求来源，并不必然等同于一条分支。足够小的项目可以使用一条分支；若项目暴露了多个相互独立的编译器能力，则应拆成多条范围受限的分支和 PR，每条都有独立验收条件。

Unrelated maintenance, compiler semantics, test infrastructure, vendored dependencies, formatting, and documentation migrations **SHOULD NOT** be combined merely because they were discovered during the same audit.

即使问题来自同一次审计，也不应把无关的维护、编译器语义、测试设施、第三方依赖冻结、格式化和文档迁移混在同一条分支中。

## 8. Branch base and stacking / 分支起点与堆叠

A new production branch **MUST** normally start from the latest validated `main` after the preceding pull request has merged.

新的生产分支通常**必须**在前一 PR 合并后，从最新且已验证的 `main` 创建。

Feature branches **MUST NOT** be stacked implicitly. If intentional stacking is temporarily necessary, the pull request must state its non-main base and the branch must be rebased or recreated from the updated `main` after an earlier squash merge. Old commits from a squashed predecessor must not leak into the next pull request's review history.

功能分支**不得**默认隐式堆叠。确需临时堆叠时，PR 必须写明其非 main 起点；前序分支 Squash 合并后，后续分支必须基于更新后的 `main` 重新变基或重建，不能让已压缩前序分支的旧提交继续污染下一条 PR 的审查历史。

Prefer one active production branch at a time unless independent workstreams have clearly non-overlapping files and acceptance gates.

除非多个工作流的文件范围和验收门禁明确互不重叠，否则优先只保留一条活动生产分支。

## 9. Branch names and lifecycle / 分支命名与生命周期

Use a stable category and a bounded outcome, for example:

分支名称使用稳定类别和受限结果，例如：

```text
compiler/general-pointer-scaling
tests/vendor-tiny-aes
maintenance/main-integration-safety
docs/validation-toolchain-profiles
```

After a pull request is squash-merged, its ordinary feature branch **SHOULD** be deleted. Long-lived branches are reserved for deliberate archives, release maintenance, or explicitly documented backup points.

PR 完成 Squash 合并后，普通功能分支**应当**删除。长期分支只用于有意保留的历史归档、版本维护或明确记录的备份节点。

Archive and backup branches must use explicit `archive/` or `backup/` prefixes. A merged `compiler/`, `tests/`, `maintenance/`, or `docs/` branch is not an archive.

归档和备份分支必须使用明确的 `archive/` 或 `backup/` 前缀。已经合并的 `compiler/`、`tests/`、`maintenance/` 或 `docs/` 分支不属于归档。

## 10. Integration safety / 集成安全

Pull-request validation protects the proposed head commit. The generated `main` commit must also run the complete clean-checkout gate after integration.

PR 门禁保护的是候选分支提交；集成后实际生成的 `main` 提交也必须重新运行完整的干净检出门禁。

Required status checks should be configured on the repository ruleset. A merge queue may be enabled when concurrent pull requests make the tested head differ from the final integration result.

仓库规则应配置必须通过的状态检查。当并发 PR 可能使测试过的分支头与最终集成结果不一致时，可以启用合并队列。

## 11. Local template / 本地模板

Configure the repository template after cloning:

克隆仓库后配置提交模板：

```sh
git config commit.template .gitmessage
```

The template is guidance and does not replace review.

模板只提供提示，不能替代审查。
