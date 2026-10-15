# Runtime cleanup synchronization contract

This document records the controlled convergence of `agent/ci-runtime-cleanup-v1` with the canonical runtime branch `agent/linux-expanded-kbuild-v0`.

## Divergence before merge

Immediately before synchronization:

- cleanup HEAD: `c56726feb5755fe2cd58b5545ecbf4072dd7bc4e`
- runtime HEAD: `53eccae8c6c638cbbd30f05e9d2143fae96fa110`
- common merge base: `8940b3558ff16e615355eff7e62fc8eda7a085fa`
- cleanup was ahead by 135 commits and behind by 16 commits.

The 16 runtime-only commits changed no compiler, linker, assembler, or test implementation source. Their branch-side delta reduced to nine GitHub Actions workflow files.

Seven of those nine workflow files are byte-identical at both branch tips:

- `linux-runtime-compiler-semantics-v1.yml`
- `linux-runtime-generated-kallsyms-first-die-v0.yml`
- `linux-runtime-link-fixture-cert-v1.yml`
- `linux-runtime-owner-focused-v1.yml`
- `linux-runtime-satp-refresh-v0.yml`
- `linux-runtime-spinlock-first-context-v0.yml`
- `linux-runtime-spinlock-stack-v0.yml`

## Resolved workflow differences

### QEMU watcher contracts

`linux-runtime-qemu-watch-contracts-v1.yml` differs by one non-behavioral step label only. The runtime branch wording:

`Prove unknown same-stage fault does not stop the watcher early`

more accurately describes the certified fault-aware behavior than the cleanup wording. The synchronized tree therefore keeps the runtime blob. No command, assertion, trigger, or artifact path changes in this resolution.

### Historical PI-local-symbol workflow

The runtime branch still carries `linux-runtime-pi-local-symbol-v0.yml` as an active workflow. Cleanup has already retired that standalone workflow after canonical certification of `linux-runtime-compiler-semantics-v1.yml`.

The cleanup archived copy is intentionally retained instead of reintroducing the runtime active file. It is also stricter than the runtime copy: after the watcher completes it reads `frontier-result.json` and explicitly accepts only `SAME_FAULT`, `MOVED_LATER`, or `FRONTIER_PASS`, rejecting other verdicts. The runtime copy exits directly with the watcher return code.

Canonical compiler-semantics certification run `37460113388` already passed both the PI-local-symbol and SATP-micro jobs, so the standalone active workflow remains retired.

## Merge rule

The synchronization commit uses the cleanup tip as its first parent and the runtime tip as its second parent. This makes every runtime certification commit reachable from cleanup history without discarding the cleanup retirement work.

The resulting tree is the validated cleanup tree plus the runtime QEMU-watcher label resolution described above. No retired workflow is reactivated and no compiler/runtime implementation source is changed by the merge.
