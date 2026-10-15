# Runtime CI dead-reference cleanup / 2026-10-08

## Confirmed facts
- Current long-lived remote refs: `main`, `agent/linux-expanded-kbuild-v0`, `agent/linux-perf-boolean-domain-v1`, `archive/all-progress-2026-10-04`.
- Workflows at this operation's source HEAD `e2a9e3304bfe789580a41e2a832fb6a4da25cbd1`: **91 active**, **187 disabled**. This operation archives 7 historical-only workflows verbatim and updates 9 trigger blocks, producing **84 active**, **194 disabled** in this commit (subject to later concurrent work).
- No production compiler, Linux Image producer, runtime verification implementation or cached fixture key is changed. Historical workflows remain retrievable byte-for-byte in the disabled folder and ancestral Git history.

## Retired (stale/deleted branch owners)
- `stage2-linux-kbuild-cc-smoke.yml` — exact blob `d2a2ebda8f0003becb30e56ada4ec2e8c4c1c64a`; tied to vanished `refactor/declaration-sema-v1`, `toolchain/static-readiness`, `toolchain/minias-m0-boundary-freeze`, `toolchain/regression-ledger-v0`, or deleted external ownership. Original specialty contract remains historically available; this does not claim all of those tests were executed on the modern runtime branch.
- `static-readiness-bootstrap-b1.yml` — exact blob `f6ca69c0601b53398db8e0898cfbd3d3776051ad`; tied to vanished `refactor/declaration-sema-v1`, `toolchain/static-readiness`, `toolchain/minias-m0-boundary-freeze`, `toolchain/regression-ledger-v0`, or deleted external ownership. Original specialty contract remains historically available; this does not claim all of those tests were executed on the modern runtime branch.
- `static-readiness-core.yml` — exact blob `74815fa1143471b860646a5f3f8a78980231100f`; tied to vanished `refactor/declaration-sema-v1`, `toolchain/static-readiness`, `toolchain/minias-m0-boundary-freeze`, `toolchain/regression-ledger-v0`, or deleted external ownership. Original specialty contract remains historically available; this does not claim all of those tests were executed on the modern runtime branch.
- `static-readiness-linux-final.yml` — exact blob `1d43ffba257622131f9d99de0669a849afa195d0`; tied to vanished `refactor/declaration-sema-v1`, `toolchain/static-readiness`, `toolchain/minias-m0-boundary-freeze`, `toolchain/regression-ledger-v0`, or deleted external ownership. Original specialty contract remains historically available; this does not claim all of those tests were executed on the modern runtime branch.
- `static-readiness-toolchain-selfhost.yml` — exact blob `428679835b4e4d0e3defc30f80d3e3b4a37de866`; tied to vanished `refactor/declaration-sema-v1`, `toolchain/static-readiness`, `toolchain/minias-m0-boundary-freeze`, `toolchain/regression-ledger-v0`, or deleted external ownership. Original specialty contract remains historically available; this does not claim all of those tests were executed on the modern runtime branch.
- `minic-minias-linux-runtime.yml` — exact blob `7218386bb99997902d604a626ae105f4b7f80be7`; tied to vanished `refactor/declaration-sema-v1`, `toolchain/static-readiness`, `toolchain/minias-m0-boundary-freeze`, `toolchain/regression-ledger-v0`, or deleted external ownership. Original specialty contract remains historically available; this does not claim all of those tests were executed on the modern runtime branch.
- `toolchain-regression-ledger-v0.yml` — exact blob `48cbc0a54e0aa4c40619fd82cc87ea4687a8ffe0`; tied to vanished `refactor/declaration-sema-v1`, `toolchain/static-readiness`, `toolchain/minias-m0-boundary-freeze`, `toolchain/regression-ledger-v0`, or deleted external ownership. Original specialty contract remains historically available; this does not claim all of those tests were executed on the modern runtime branch.

## Trigger fixes without weakening live gates
- Remove non-existent `agent/ci-runtime-cleanup-v1` from push branch filters in `minic-rv64-focused-regressions-v1.yml`, `miniar-regressions-v1.yml`, `minild-regressions-v1.yml`, `minias-a0-gate-v1.yml`, `minipp-linux-exact-v1.yml`, `minipp-linux-focus-v1.yml`. Current `agent/linux-expanded-kbuild-v0` push events and manual dispatch remain intact.
- Retain three unique historical MiniAS diagnostic workflows as **manual-only** by removing their now-impossible two-branch push filters: `minias-a0-window.yml`, `minias-semantic-oracle3536.yml`, `minias-a0-focused-diagnostics-v1.yml`.
- Do **not** retire `core-first500-regression.yml` or `linux-core-{all3352,assemble-all3352,focused-five,shards-v1}.yml`; their frozen-corpus correctness contracts have not been proven redundant. Do not delete full Image/QEMU/runtime cert chains merely to lower YAML count.
- The standalone `toolchain-m0-structure.yml` currently has cleanup-era conditionals and an unused `toolchain/**` push pattern. Kept pending precise re-ownership of its independent M0 gate.

## High-priority correctness discovery: prior all3352 report was falsely green
- Run [37760972924](https://github.com/yituanxing/minic-toolchain/actions/runs/37760972924) returned GitHub SUCCESS but its aggregate log reported **3350 PASS, 2 FAIL**, not 3352 PASS.
- Exact failed corpus indices, retrieved from shard f's artifact `11542721833`: `2768 drivers/input/serio/serio.i` and `2770 drivers/input/serio/libps2.i`. Both MiniC runs reported `expected type name` near EOF; GCC syntax-only check for `serio.i` returned 0.
- Cause of false-green status: Python aggregate exit code was masked by `| tee` without `pipefail`, and the shard's `REPLAY_RC=1` was logged but not enforced. Fixed in runtime commit `e2a9e3304bfe789580a41e2a832fb6a4da25cbd1`; the expensive 7-runner experiment is now manual-only until those two compiler failures are addressed.
- Passing [500 TU A/B 37760522658](https://github.com/yituanxing/minic-toolchain/actions/runs/37760522658) shows 500/500 exact assembly equality between the opt-in profile candidates and 2.4641x paired A/B time ratio (sum 1429.757 vs 580.236 TU-s). This does **not** establish the full 3352 or QEMU Image compatibility.

## Next acceptance checks
1. Resolve two real MiniC TU failures; run them as focused regression before repeating costly all3352.
2. Re-run the fail-closed 3352 aggregate with no failed TUs (or explicit negative-fixture test).
3. Verify actual Legacy Runtime-vs-performance compiler throughput; the 500 paired optimization A/B uses two performance profiles, not the runtime baseline.
4. Re-certify changed Linux objects, Image link and QEMU on the final compiler identity before promoting to the canonical runtime build profile.
5. Continue sweeping dormant workflows by validated invariants/consumers, not file naming alone.
