# Active workflow source audit — 2026-10-09

This audit complements [CI ownership taxonomy](ci-ownership-regression-taxonomy-2026-10-09.md). The [machine-readable ledger](active-workflow-inventory-2026-10-09.tsv) contains one row per active path per development branch, plus source Git blob SHA, jobs, named steps, triggers, artifact declarations, literal pinned run-ID fields, and cache references. This is **source reading, not execution certification**.

## Coverage

- Runtime active YAML: **51**; performance active YAML: **50**; total branch-path rows **101**.
- Distinct active filenames **55**; distinct content blob SHAs **64**.
- Full content reads **64/64**; unresolved rows **0**.
- Across distinct blobs: **106** job definitions; **35** with at least one `actions/cache` reference. Distinct blobs are not distinct responsibilities.
- Literal `run-id: 33623125809` in `miniobjcopy-strip-regressions-v1.yml` has zero downloadable original artifacts.
- Extracted columns are **syntactic**, not full semantic YAML evaluation. Matrix-expanded jobs, action expressions, scripts and workflow-call chains need a follow-up audit; a blank literal run-ID field does not prove no external pinned dependency.
- Latest real evidence is marked `not-per-workflow-verified`. Do not interpret this source inventory as 101 green runs.

## Domain inventory by branch

| Owner | Runtime | Performance |
| --- | ---: | ---: |
| ci-structure | 1 | 1 |
| compiler-linux-corpus | 3 | 3 |
| linux-image | 3 | 1 |
| linux-runtime | 25 | 25 |
| miniar | 2 | 2 |
| minias | 3 | 3 |
| minic-driver-core | 2 | 2 |
| minild | 4 | 4 |
| miniobjcopy-strip | 1 | 1 |
| minipp | 4 | 4 |
| performance | 3 | 4 |

## Cross-branch asymmetry

Runtime-only names (5): `linux-distributed-full-image-signedness-v2.yml`, `linux-distributed-image-graph-preflight-v1.yml`, `linux-runtime-optin-perf-all3352-v1.yml`, `linux-runtime-optin-perf-constant-p-qemu-v1.yml`, `linux-runtime-optin-perf-first500-ab-v1.yml`.

Performance-only names (4): `linux-core-object-interval-top5-ab-v1.yml`, `linux-gnu-constant-p-ice-regression-v1.yml`, `linux-optimized-first500-verify-v1.yml`, `linux-parser-scope-first500-ab-v1.yml`.

Shared names with different YAML blob SHA (9): `linux-expanded-entry-trace-v0.yml`, `miniar-regressions-v1.yml`, `minias-a0-gate-v1.yml`, `minias-semantic-oracle3536.yml`, `minic-rv64-focused-regressions-v1.yml`, `minild-regressions-v1.yml`, `minipp-linux-exact-v1.yml`, `minipp-linux-focus-v1.yml`, `toolchain-m0-structure.yml`.

A shared YAML SHA does not imply equivalent compiler-source profiles. Any differing YAML must be independently reconciled instead of overwriting a branch wholesale.

## Candidate queue (not yet migrated)

1. **T0/T1:** choose canonical owner entrypoints for driver/RV64/MiniPP A0/MiniAS A0/MiniELF/AR/LD/NM/Objcopy/Strip; keep named verdicts.
2. **Frozen corpora:** preserve Core first500/focused-five/3352 compile/GNU as distinct proof labels; keep MiniAS 3536 exact versus semantic oracle independently. Shared setup is a candidate, not deletion of contracts.
3. **Linux Image:** graph preflight and 7-shard full Image have different costs; require current-HEAD full strict replay before declaring redundancy.
4. **Runtime:** GCC baseline, frozen cert, link fixtures, frontier, watcher and first-fault family need producer/consumer/cache scope review. Consolidate repeated setup/owner selection only with identical identity checks.
5. **Performance:** preserve four performance-only names and three Runtime opt-in performance entries until same-scenario paired benchmarks, source-profile comparison and correctness gates justify merging.
6. **MiniObjcopy:** historical Image fixed source run is expired; regenerate an identified oracle input with immutable checked provenance, not arbitrary substitution.

## Migration gate

Inspect exact job bodies, trigger policy, producer/consumer edges, cache scope and historical result of each proposed move; preserve the original YAML byte-for-byte and record original blob SHA. Run structural M0 **and actual tier-appropriate test** before retiring the old entrypoint. The 20–30 YAML target is aspirational, not acceptance evidence.
