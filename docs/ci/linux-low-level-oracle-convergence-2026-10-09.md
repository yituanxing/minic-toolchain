# Low-level Linux focused and runtime watcher workflow convergence — 2026-10-09

## Scope and exact original sources

Two further canonical owners replace four legacy opt-in workflows on both development branches without changing their executable test bodies.

| New owner | Old archived source | Original Git blob SHA | Preserved jobs |
| --- | --- | --- | --- |
| `linux-efi-vdso-focused-v1.yml` | `linux-efistub-diff-v0.yml` | `ee010b82654c79ad1d9d8da0fb8fa3848ad75fd5` | `efistub-diff` |
| same | `linux-vdso-focused.yml` | `2b3844cd7d19d9b80381068ce7947ece25a239d4` | `vdso` |
| `linux-runtime-contract-oracles-v1.yml` | `linux-runtime-compiler-semantics-v1.yml` | `8601132b3f798dbfb0d57d3c3bb8eb802ea99c1b` | `pi-local-symbol`, `satp-micro` |
| same | `linux-runtime-qemu-watch-contracts-v1.yml` | `c7f1311975ffd5703d216d1973ff6aa206ca6370` | `qemu-watch-cert`, `inconclusive-cert` |

Each original source YAML is preserved byte-identically under `.github/workflows-disabled/`, with its Git blob SHA asserted by M0. Every original job executable body, test invocation, timeout, cache key and artifact upload command is compared independently, ignoring only the new manual selection predicate and moved concurrency block.

## Invocation policy

`linux-efi-vdso-focused-v1.yml`: manual mode `efi` (default), `vdso` or `all`. Both old explicit push tags remain, on both development branches and the same `src/**`, `include/**`, `tests/external/linux/**`, `tools/ci/**` path filters. EFI's original `cancel-in-progress: true` group is preserved at job scope. vDSO had no concurrency override and still does not.

`linux-runtime-contract-oracles-v1.yml`: manual mode `semantics` (default), `watch` or `all`. On push, both original groups of commit tags and the Runtime branch restriction remain. Four independent jobs retain their distinct assertion and artifact contracts. The old *workflow-level* cancel-in-progress groups are moved to **per-job** groups, including `github.job` to prevent simultaneous named jobs from cancelling one another. This is a deliberate improvement to concurrent execution, not a change to the respective test body.

The new shared workflow does **not** reinterpret `QEMU_RC=0` as a Linux boot success, or replace the independently certified GCC baseline, frozen/link fixture producer, watcher pass marker, full Image or QEMU boot path.

## Gates

- `tools/ci/check_linux_efi_vdso_convergence_v1.py`: exact two original YAML SHA, original test steps, tag predicates, trigger/path scope, concurrency policy.
- `tools/ci/check_linux_runtime_contract_oracles_convergence_v1.py`: exact two original YAML SHA, four job bodies, six original tag tokens, branch trigger and per-job concurrency identity.
- Both run under each branch's structural M0. A green M0 is **only T0 source/structural proof**. Real EFI/vDSO, PI/SATP and QEMU Watch runs are still independently required for post-consolidation T2/T4 certification.

Expected active workflow count: Runtime 40 -> **38**, performance 41 -> **39**, distinct names 44 -> **42**. No compiler or test implementation changes.
