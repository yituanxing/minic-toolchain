# CI execution routing audit and repairs — 2026-10-09

## Why the CI structure alone was insufficient

M0 proved historical shell/QEMU test steps were preserved, but **did not establish that each intended job actually runs** when a user selects a manual mode or pushes a single owner-specific trigger. Two specific faults were identified before making further workflow-count reductions.

### A. Early Runtime modes silently skipped on the Performance development ref

Merged `linux-expanded-pi-p1-runtime-v1.yml` exposed `mode=semantics` on both development branches, but the original `pi-local-symbol` and `satp-micro` predicates retained `github.ref_name == 'agent/linux-expanded-kbuild-v0'` *outside* the manual/automatic routing split. On the Performance branch, an explicit manual Semantics dispatch therefore produced a skipped job rather than an actual test verdict.

The corrected guards now express:

`manual && (mode == semantics || mode == all)` **OR** `push && Runtime branch && old named tag`.

The same explicit formulation is applied to both QEMU Watch contract jobs, preserving their two historical push tags. All four executable job bodies and success/failure assertions are unchanged. `tools/ci/check_early_runtime_dispatch_v1.py` pins the four expressions and checks a truth table: manual on either branch, manual mode isolation, old push tags on Runtime only, no automatic Performance jobs.

This enables manual Performance-branch semantic diagnostics, **not** a declaration that Runtime-owned branch-scoped certified cache input will be available there. Cache miss and QEMU INCONCLUSIVE must still fail according to the historical contract.

### B. Owner Focused trigger paths previously ran all four costly matrix cases

`linux-runtime-owner-focused-v1.yml` had four matrix entries (`timekeeping`, `vsyscall`, `notifier`, `build-policy`) and four distinct push trigger files. Modifying **one** file scheduled **all four** compiler/QEMU owners (each with its own ~15-minute job timeout).

Now a cheap `route` job performs a full-history `git diff` from `github.event.before` to `github.sha`, and `owner` uses `fromJSON(needs.route.outputs.modes)` to schedule **only the selected owner mode(s)**. When explicitly manually dispatched, users can select a single mode or `all` (default, the previous manual behavior). A workflow or selector-source edit, an unknown changed path, a zero/missing before SHA, or any Git diff problem conservatively selects **all four**. Routing must never fail closed on missing provenance. Job executable steps, GCC/MiniC rebuild, QEMU assertions, cache keys and evidence uploads are unmodified.

`tools/ci/select_runtime_owner_modes_v1.py --self-test` tests the path/manual truth table. `tools/ci/check_runtime_owner_routing_v1.py` reverses exactly the approved YAML-level routing edits and proves byte-exact equality to the immediately previous complete Owner Focused workflow Git SHA `c9b8fff60a2a805ceff68e1d028da69f7485dceb`. Existing shared-cache restore M0 still reverses its earlier cache step extraction against its older whole-workflow SHA, so this improvement has **two independent source-equivalence layers**.

## Actual live routing evidence

- [Timekeeping-only routing run #37879420999](https://github.com/yituanxing/minic-toolchain/actions/runs/37879420999): exactly `route` + `owner (timekeeping)`; both SUCCESS. No other owner matrix cases scheduled.
- [Post-exclusion timekeeping push 7bf11473](https://github.com/yituanxing/minic-toolchain/commit/7bf11473d97757375de4cdfd7d5789276561ec46): [Owner run #37879734755](https://github.com/yituanxing/minic-toolchain/actions/runs/37879734755) again scheduled one owner job. The 3 unrelated heavyweight MiniC RV64/MiniAR/MiniLD workflows did **not** appear among this HEAD's workflow runs. Both the route job and the timekeeping job (including its QEMU frontier assertion) completed SUCCESS.
- Both [Runtime M0 #37879660134](https://github.com/yituanxing/minic-toolchain/actions/runs/37879660134) and [Performance M0 #37879677311](https://github.com/yituanxing/minic-toolchain/actions/runs/37879677311): SUCCESS with restored-whole-YAML SHA and both route tests. The manual Performance semantics mode is structurally accepted but has **not** had a new actual manual run; it is not a Runtime PASS.

## Verification tiers and exit conditions

- **T0**: Runtime/Performance M0 YAML parse + full source equivalence + route selftests green.
- **T1/route**: Execute at least one actual *single-owner* push and verify only its one expensive job is scheduled, plus one explicit manual mode invocation as needed. This is a separate operational proof; static truth tables are not enough.
- **T3**: Existing certified frozen and linked fixture keys and consumer cache scopes must resolve from the selected branch. Never treat stale or missing cache as proof of compiler failure or green build.
- **T4**: Real Linux QEMU progress PASS markers required; `QEMU_RC=0` for a controlled watcher or `INCONCLUSIVE` is not boot success.

This iteration deliberately **does not reduce the number of active workflows**. It fixes trigger semantics first.
