# CI operations / CI 操作入口

This document describes how to assess the **checked-out source**. The actual GitHub Actions definitions in [`.github/workflows/`](../../.github/workflows) and their commit-specific run results are authoritative. Historical successful runs must never be reported as current HEAD certification.

本文只解释如何检查**当前检出源码**。工作流定义与对应提交的 Actions 记录才是当前验收依据；旧版本的 PASS 不代表当前版本通过。

## Maintained entry points / 保留的验证入口

| Scope | Entry / contract |
| --- | --- |
| Build and fast semantic regressions | `make check-fast`; includes unified C0 Native Matrix |
| Compiler native regression | `.github/workflows/minic-native-refactor-gate.yml` and `tests/compiler/c0/run-native-matrix.sh` |
| Strict backend / MachineIR | `.github/workflows/minic-native-refactor-gate.yml` and its typed backend verifiers |
| Toolchain functional integration | `.github/workflows/toolchain-focused-regressions-v1.yml` |
| Four-architecture runtime | `.github/workflows/minic-runtime-fourarch.yml` |
| Linux C-owner qualification | `.github/workflows/linux-runtime-gcc-baseline.yml` |
| Stage2 / Stage4 | `.github/workflows/minic-current-stage2-selfhost.yml`, `.github/workflows/minic-current-stage4-native.yml` |
| Linux heavy performance | `.github/workflows/linux-runtime-optin-perf-suite-v1.yml` and `minic-refactor-paired-perf.yml`; opt-in only |
| MiniAS / MiniLD / MiniPP | Their independent named workflows and executable regression gates |

**Acceptance levels:** source compilation is not assembler/linker acceptance; GNU or MiniC-linked native execution is not a QEMU Linux boot; Stage2 is not Stage4 self-hosting; performance measurements are not correctness certification. For each claim retain source SHA, exact target/ABI, input provenance, runtime/exit evidence, and known unsupported cases.

**Workflow routing:** some historical diagnostic YAMLs intentionally retain `workflow_dispatch` or `workflow_call` even when their old `agent/*` push branches no longer exist. Do not classify them as dead solely from the push filters. Conversely, historical trigger labels, cache receipts and retired overlay scripts are not live feature gates. Check the actual caller and source-specific prerequisites before promoting or retiring any workflow.

**Preserve independent evidence:** the historical distributed graph preflight and full-image producer/shards/receiver have distinct contracts; they are not substitutes for the current compiler unit matrix. Never replace a runtime oracle with a compile-only `grep` or an old artifact-cache success marker.

## Current supporting references

- [Linux runtime isolation / provenance](linux-runtime-isolation.md) — fixture identity and runtime isolation.
- [Four-ISA Linux qualification](minic-four-isa-linux-runtime-qualification-2026-10-30.md) — exact certified scope and limitations.
- [Performance experiment decision index](minic-perf-decision-index-2026-10-17.md) — accepted, rejected and paused experiments.
- [Compiler throughput evidence](minic-compiler-throughput-2026-11-09.md) — historical timing context, not current SHA performance proof.
- [Current architecture and defect register](../architecture/README.md) — owners and unresolved work.

## Immutable historical record / 历史归档

The retired October branch topology, source-rewrite experiments, CI route changes, failure postmortems, artifact availability audits, and test consolidation receipts can be read at the exact [pre-cleanup historical CI README](https://github.com/yituanxing/minic-toolchain/blob/92eb200c61db6fbd64080d9d141cb6ac750bf6ea/docs/ci/README.md). Its [October 9 workflow inventory](https://github.com/yituanxing/minic-toolchain/blob/92eb200c61db6fbd64080d9d141cb6ac750bf6ea/docs/ci/active-workflow-inventory-2026-10-09.tsv) was a dated, two-development-ref snapshot, **not** today's active workflow list. Removed historical files remain in Git history; use matching historical checkouts to reproduce experiments. Do not replay pinned experiments against unrelated current sources.

历史分支映射、失效 CI、旧脚本与测试整合流水账均可从上述固定提交恢复。当前工作树不再复制这些旧版记录；复现旧实验必须使用对应历史源码和输入，不能把历史结果当作当前认证。
