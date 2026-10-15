# Orphaned bootstrap/runtime workflow archival — 2026-10-08

Base `9aabd56675c8fb79c8eb277bb538870d43fedf6c`, active workflow count 86 and disabled count 194 before the change.

Seven **historical-only** YAML files below were moved *verbatim* into `.github/workflows-disabled/`. These were tied to branches that no longer exist (notably `refactor/declaration-sema-v1` and `agent/busybox-full-mini-v0`). They do not currently own the Linux canonical compiler profile, fixture, Image or QEMU certificate. The underlying compiler tests and Git history were not removed.

- `compiler-bootstrap-b0.yml`: Git blob `2e4eec0fa6e1d8fa922791b12c3ceb71316d54bf`
- `compiler-bootstrap-b1-sharded.yml`: Git blob `c2d4883ddb504e56994e00735946bd5896818994`
- `compiler-bootstrap-b2-runtime.yml`: Git blob `d8b1fe54a1b1b6b7e4eb1c4ce2717bca6de6be89`
- `compiler-bootstrap-b4-linux-all3352.yml`: Git blob `4bc8f24617ffed59b1858c4acb74402b41a84614`
- `compiler-runtime-r0.yml`: Git blob `7c09d7399f82ea76986379320b1fc6d4a8cf04aa`
- `compiler-runtime-r1-lua.yml`: Git blob `4054bc7540f650a0e72621ac8a1e763e7179fddb`
- `busybox-mini-aggregation-v0.yml`: Git blob `fe7476fa9ae0c52dc710e0a9aea70ef54993b512`

Historical Stage2 bootstrap scripts and the separate current MiniC/MinIPP/MinIAS regression owners have different verification contracts: do not infer that a current green test subsumes these old historical contracts. If a historical contract becomes necessary again, restore its exact YAML from this archive, update input/cert/branch ownership, and run it explicitly on the current code.

Expected after archival: 79 active / 201 disabled (subject to intervening independent commits). No production source, toolchain profile, cache key, runtime producer, CI evidence, or branch reference was touched.
