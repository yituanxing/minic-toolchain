# Audit: GitHub Actions skipped Workflow noise, trigger reachability and duplicate jobs — 2026-10-09

## Evidence and precise scope

Audited both current development refs, including **25 active Runtime YAMLs / 87 declared jobs** and **23 active Performance YAMLs / 84 declared jobs**; **26 distinct names / 48 branch-local YAML copies**. The M0 command `python3 tools/ci/audit_active_workflow_routing_v1.py` exports a per-file branch-aware TSV (name, push declared, push actually eligible for that branch, path scope, manual eligibility, declared job IDs, literal commit-message tags, unguarded jobs and classification).

**Observed real push**, `docs/ci/**` plus T0 audit updates at Runtime `52647a104448c8ebe4a992eac4b9531d9cb7c810` and Performance `669d4f849a1971164b80910bfa463c700107468a`:

| Branch | Workflow-run cards | SUCCESS | SKIPPED | Example exact run IDs |
| --- | ---: | ---: | ---: | --- |
| Runtime | **14** | 2 | **12** | [M0 #37893765548](https://github.com/yituanxing/minic-toolchain/actions/runs/37893765548), [MiniObjcopy #37893765390](https://github.com/yituanxing/minic-toolchain/actions/runs/37893765390) |
| Performance | **7** | 2 | **5** | [M0 #37893779981](https://github.com/yituanxing/minic-toolchain/actions/runs/37893779981), [MiniObjcopy #37893779955](https://github.com/yituanxing/minic-toolchain/actions/runs/37893779955) |

Both MiniObjcopy/Strip **route** jobs occupied approximately **6s** each on this non-ELF push, and all of their substantial test jobs skipped. The M0 structural source check was needed; MiniObjcopy's unscoped push route was not useful on that commit. The remaining skipped workflows generally allocated no runner. **21 UI workflow cards are not 21 runner jobs**; the observed 17 SKIPPED outcomes are principally status/UI clutter rather than meaningful compute billing.

## High-priority findings

### A. Unscoped `push` + commit-message-only Job guards

These **seven** Runtime-eligible Workflow entrypoints declare `on.push` *without any paths filter*, and their actual expensive jobs normally require a special `[tag]` in the head commit message (or an explicit manual dispatch):

| Current workflow | Job declarations | Representative opt-in |
| --- | ---: | --- |
| `linux-expanded-kbuild-v0.yml` | 1 | `[linux-expanded-kbuild-v0]` |
| `linux-expanded-pi-p1-runtime-v1.yml` | 7 | `[linux-expanded-runtime-p1]` |
| `linux-runtime-fixture-producers-v1.yml` | 2 | `[linux-runtime-frozen-cert-v1]` |
| `linux-runtime-focused-faults-v1.yml` | 7 | `[linux-runtime-fault-context-v1]` |
| `miniar-linux-kbuild.yml` | 1 | `[miniar-linux-kbuild]` |
| `minias-a0-gate-v1.yml` | 5 | `[minias-gate-v1]` |
| `minild-integration-v1.yml` | 3 | `[minild-linux-rel-boundaries]` |

The Performance branch remains eligible for **`minild-integration-v1.yml`** among these; the others' push branch scope is Runtime-only even though some YAML copies are present on both refs. **We must not mistake presence of a YAML on a branch for eligibility to run automatically on that branch.**

A push to README/docs/CI housekeeping with no special tag can create empty workflow-run cards for each unscoped eligible owner. Because GitHub evaluates `on.push.paths` at the event level, **it cannot express “run for any changed path, but only if the head commit message has `[linux-...]`”**. Job-level `if` conditions must run after the Workflow has been created. Adding `paths-ignore: ['docs/**']` would eliminate empty cards **but also block intentional tagged documentation-only triggers**. Changing push to workflow_dispatch-only would break existing `[tag]` contracts. Neither is a zero-behavior-change fix. [GitHub official path/event rules](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax) and [job condition semantics](https://docs.github.com/en/actions/how-tos/write-workflows/choose-when-workflows-run/control-jobs-with-conditions).

**Recommended migration, not yet applied:** formally retire *operator-message tags* once their callers have been converted to explicit `workflow_dispatch` mode or a single canonical routing workflow. Then remove broad push from retired diagnostic YAMLs. Preserve archived source SHA + old-tag mapping in docs. Do **not** silently break tags or shift an expensive Linux Image/QEMU test to normal push.

### B. Universal MiniObjcopy/Strip router remains real recurring runner cost

`miniobjcopy-strip-regressions-v1.yml` has **no push path filter** on either branch. Its `route` job always runs on a push to obtain an accurate multi-commit changed-file diff; this is necessary to trigger shared `elf/**` changes while keeping `[miniobjcopy-strip-regressions]`, `[miniobjcopy-linux-tool]` and `[miniobjcopy-linux-image]` legacy opt-in tags. The real docs-only run consumed 6s of runner time on each branch. It is the next small **real runner** optimization target.

A cheap event-payload-only `if` could skip it for some pushes, **but an incomplete GitHub push payload, force push, or multi-commit event must fail open** or source regression could be lost. A naive string search in the commit message is not acceptable evidence that no `elf/**` files changed. A redesign must be tested with multi-commit, deletions/renames and missing ancestry, and preserve every legacy tag/manual mode.

### C. Broad, tag-gated path filters generate SKIPPED cards on CI maintenance

The following paths often match ordinary `tools/ci/**` edits but the expensive jobs remain opt-in:

- `linux-core-all3352.yml` (eight declared jobs, full 3352 TU + assembly oracles);
- `linux-efi-vdso-focused-v1.yml` (two focused jobs);
- `linux-runtime-gcc-baseline.yml` (one job);
- `linux-core-shards-v1.yml` (three declared jobs);
- `minic-driver-v0.yml` (one job and `tools/**` broad filter, commit-tag gated);
- `minipp-linux-frozen-v1.yml` (five jobs, tag-gated).

Do not remove these Workflows just because an unrelated source push records SKIPPED. Narrow path globs **only after** verifying the exact files the corresponding tests execute and retaining commit-tag/manual accessibility. The recent MiniC RV64/AR/LD T1 refactor has already excluded the entire CI-only `tools/ci/**` path safely because its unpatched `make all` and eight focused assertions have no dependency on those scripts. That proof does **not** automatically transfer to Linux test workflows.

### D. Dead or misleading tag contracts and legacy branch filters

- `minias-a0-focused-diagnostics-v1.yml` is **workflow_dispatch-only**, but some historical Job `if` expressions still mention `github.event.head_commit.message` and `[minias-*]` tags. Those tag paths are **unreachable** because this YAML does not declare `on.push`. Manual semantic/oracle modes are real and must be kept.
- `linux-runtime-optin-perf-suite-v1.yml` has a Runtime push filter matching **only its own YAML filename**, but expensive Job `if` conditions request `[linux-perf3352]`, `[linux-perf500]` etc. Those tags cannot start the workflow on an ordinary `src/**` change; they require manual dispatch or a coincident self-YAML edit. Audit documentation should not present them as fully automatic source regression gates.
- `linux-core-shards-v1.yml` still includes the historical branch `agent/ci-runtime-cleanup-v1` in its push filter. Confirm ref deletion/remaining callers before removing the obsolete branch literal; do not add Performance auto-trigger without verifying the original frozen/shard corpus and cache identity.

### E. Definition of done / tests still missing

- **T0 route audit:** every Workflow categorized by actual branch-eligible event filters and job `if` gates, old tags explicitly enumerated, existing count inventory unchanged.
- **T1 baseline:** the one-runner eight-check focused suite has already passed on both branches. Do not retrigger it on CI-only audit edits.
- **T4 Linux diagnosis:** IRQ-only combined Runtime owner diagnostic has run to `FRONTIER_FAULT=store_page_fault:calc_global_load+0x2a`. A successful watcher is not a successful kernel boot. Full Linux Image/QEMU certification remains open.
- **Reduce UI cards safely:** either migrate historical tags to explicit operator dispatch (after changing documentation/callers) or consolidate owners with exact archived body/SHA checks and real before/after trigger proof. Avoid a file-count target that loses a unique correctness oracle.

The report is intentionally **auditable evidence and a migration plan**, not a claim that 17 skipped cards can be eliminated without any compatibility trade-off. The automated audit is **source-only T0**, not runner/tier certification.
