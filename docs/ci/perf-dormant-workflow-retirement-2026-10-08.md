# Perf branch dormant workflow retirement (2026-10-08)

This operation **does not merge or modify compiler/performance sources**. From perf branch `agent/linux-perf-boolean-domain-v1` at parent `104ccf9b00e0061105c3b98dd879c41931aa3d48`, it moves exactly 28 inactive historical workflow definitions from `.github/workflows/` into `.github/workflows-disabled/`, by reusing each **unchanged Git blob SHA**. All 28 were already retired with **exactly the same SHA** in canonical Runtime `agent/linux-expanded-kbuild-v0`; no file is dropped.

Original perf inventory: 91 active workflows. Expected after this operation: 63 active workflows. Archive counts increase by 28. The following **4** perf-specific active workflows are deliberately preserved: `linux-core-object-interval-top5-ab-v1.yml`, `linux-gnu-constant-p-ice-regression-v1.yml`, `linux-optimized-first500-verify-v1.yml`, `linux-parser-scope-first500-ab-v1.yml`. Existing shared Runtime/mini-toolchain gates are untouched.

Original SHA manifest, one line per retained historical blob:
- `.github/workflows-disabled/busybox-failure-pool-v0.yml` — `67901fb0c3a7d50e29e96fae833c7ca7d83f5ca8`
- `.github/workflows-disabled/busybox-full-driver-v0.yml` — `d1f95066a0f00a5dbd05a7e93793d69ef177bc0f`
- `.github/workflows-disabled/busybox-full-toolchain-v0.yml` — `fee0680d5d114776340e29d240297bff1357f114`
- `.github/workflows-disabled/busybox-mini-aggregation-v0.yml` — `fe7476fa9ae0c52dc710e0a9aea70ef54993b512`
- `.github/workflows-disabled/compiler-bootstrap-b0.yml` — `2e4eec0fa6e1d8fa922791b12c3ceb71316d54bf`
- `.github/workflows-disabled/compiler-bootstrap-b1-sharded.yml` — `c2d4883ddb504e56994e00735946bd5896818994`
- `.github/workflows-disabled/compiler-bootstrap-b2-runtime.yml` — `d8b1fe54a1b1b6b7e4eb1c4ce2717bca6de6be89`
- `.github/workflows-disabled/compiler-bootstrap-b4-linux-all3352.yml` — `4bc8f24617ffed59b1858c4acb74402b41a84614`
- `.github/workflows-disabled/compiler-runtime-r0.yml` — `7c09d7399f82ea76986379320b1fc6d4a8cf04aa`
- `.github/workflows-disabled/compiler-runtime-r1-lua.yml` — `4054bc7540f650a0e72621ac8a1e763e7179fddb`
- `.github/workflows-disabled/lua-full-driver-v0.yml` — `21ffff1f5af775dddefe25ef60c53f22beef217c`
- `.github/workflows-disabled/minias-linux-ground-truth-inventory.yml` — `2e4236380c5c673ea9527e1bb542dc41c78360e5`
- `.github/workflows-disabled/minias-linux-runtime-gcc-single-variable.yml` — `9e2d0a7fa5e1d8cc853c1df33c49cc4030b6a133`
- `.github/workflows-disabled/minias-linux-sidecars.yml` — `fc37e277e5abd7744a511d377b494c62e6316869`
- `.github/workflows-disabled/minias-semantic-oracle-smoke.yml` — `c94da101d4c0cd119bbb5cbc71fd830aed8014a6`
- `.github/workflows-disabled/minic-driver-musl-headers-v0.yml` — `df98fe50789839d7333fa80dddcfe0c4da0ca40e`
- `.github/workflows-disabled/minic-minias-linux-runtime.yml` — `7218386bb99997902d604a626ae105f4b7f80be7`
- `.github/workflows-disabled/minild-busybox-static.yml` — `0f0848aeace781daacb731ec1ad379b28d5f5ad4`
- `.github/workflows-disabled/minild-musl-shared-rebase.yml` — `e500df6d4ca60ab031ac691e0a42cd062da03938`
- `.github/workflows-disabled/minild-sqlite-static.yml` — `dd38308adaed581f5ea7cbd853e66d8c8e2ab5c9`
- `.github/workflows-disabled/sqlite-full-driver-v0.yml` — `a9de08c2e7adeeb0c7023c0f083ee0ad5685e1e4`
- `.github/workflows-disabled/stage2-linux-kbuild-cc-smoke.yml` — `d2a2ebda8f0003becb30e56ada4ec2e8c4c1c64a`
- `.github/workflows-disabled/static-readiness-bootstrap-b1.yml` — `f6ca69c0601b53398db8e0898cfbd3d3776051ad`
- `.github/workflows-disabled/static-readiness-core.yml` — `74815fa1143471b860646a5f3f8a78980231100f`
- `.github/workflows-disabled/static-readiness-linux-final.yml` — `1d43ffba257622131f9d99de0669a849afa195d0`
- `.github/workflows-disabled/static-readiness-toolchain-selfhost.yml` — `428679835b4e4d0e3defc30f80d3e3b4a37de866`
- `.github/workflows-disabled/tinycc-full-driver-v0.yml` — `63955e9a8dde6d8aa661039c261d4d164e9200ef`
- `.github/workflows-disabled/toolchain-regression-ledger-v0.yml` — `48cbc0a54e0aa4c40619fd82cc87ea4687a8ffe0`

Why: these 28 old jobs are tied to deleted `external/*`, `toolchain/*`, `refactor/*` or old `agent/*` experiment branches, with only manual fallback. Earlier Runtime cleanup already decided they do not own the present certified Linux or performance gates. Archived source plus Git history preserve every unique old diagnostic. Do not reactivate just to fix a filename-count regression; restore intentionally with current branch/fixtures and run the desired specialty contract.

No new build, Linux Image or QEMU certification is claimed. To resume runtime investigation, the canonical self-generating Image workflow must still be run and QEMU boot remains failing after OpenSBI.
