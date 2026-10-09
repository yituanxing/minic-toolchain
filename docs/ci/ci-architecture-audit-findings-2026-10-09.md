# CI architecture correctness review — 2026-10-09

This is a **live defect register**, not a workflow-count target. A source-equivalence/T0 PASS **does not** prove correct trigger reachability, good resource policy, artifact availability or Linux runtime boot.

## Verified current topology
- Remote refs: `main`, Runtime `agent/linux-expanded-kbuild-v0`, Performance `agent/linux-perf-boolean-domain-v1`, passive `archive/all-progress-2026-10-04`.
- Development ref inventory: **Runtime 26, Performance 13; 39 branch-local YAMLs, 27 distinct names**. The branch-local machine-maintained TSV is `active-workflow-inventory-2026-10-09.tsv`.
- Each active YAML must have: one responsible owner, a unique contract or explicitly justified shared ownership, event/branch/path or tag routing, an executable input/provenance path, a defined PASS/FAIL/SKIPPED/INCONCLUSIVE verdict, an appropriate concurrency policy, cost class, and evidence for its last real run.
- The `current-workflow-map.md` is the human-readable authoritative overview. Do not append multiple conflicting 'latest snapshot' paragraphs again. Source SHA comparisons are checked by T0, not just row-count or job-ID comparisons.

## Immediate verified defects and actions

| ID | Severity | Finding | Action/status | Exit check |
| --- | --- | --- | --- | --- |
| **CI-01** | **P0** | `miniobjcopy-strip-regressions-v1.yml` had an unscoped push listener and **workflow-wide `cancel-in-progress: true`**. An unrelated docs push could cancel a running 90-minute Linux-tool or 35-minute historical Image test on the same branch. | **Corrected**: workflow-wide cancellation is now `false`, on both development refs. The original entire executable workflow is reconstructed by `check_ci_miniobjcopy_elf_route_v1.py`; no test body or tag was changed. | Two development M0 green; source reconstruction PASS. |
| **CI-02** | **P0** | MiniObjcopy `linux-image` still downloads a fixed artifact from run **33623125809**. The run is SUCCESS (2026-09-02), but GitHub currently reports **zero artifacts**. It is a **known non-reproducible active historical mode**. | **OPEN**. Do not claim it executable or a valid Image oracle. Replace with an owned fresh fixture producer plus exact provenance; otherwise archive the **original full YAML and exact legacy Job** and remove the broken mode from active routing after coverage review. | Actual compatible input artifact generated from a current producer and fresh GNU-vs-MiniObjcopy full Image proof, or retired legacy input explicitly excluded from current acceptance. |
| **CI-03** | High | Development-only `workflow_dispatch` sources are not necessarily runnable from the default branch `main`. | **OPEN**: Runtime verified tag router has a real reusable push-context smoke (#37902327902), but remaining manual-only choices have no end-to-end reachability guarantee. | Test entrypoint reachability for each unique mode or install an approved default-branch dispatcher without copy/pasting executable tests. |
| **CI-04** | High | Inventory `git_blob_sha` column was not previously checked by the trigger coverage validator and carried **two stale MiniAS focused source hashes**. | **Corrected** in both refs: refresh MiniAS SHA to its actual Git Blob `10a4fed7bd5cb1e111d773efc9edba9533e4c9fb`, refresh MiniObjcopy safe-policy SHA, and validate **all live YAML full Git Blob identities** on M0. | Both T0 owners pass live source-to-manifest SHA check. |
| **CI-05** | High | Multiple scripts and narratives each maintain hard-coded `26/13/39/27` topology counts; additions require repeated edits. | **OPEN**: replace parallel counts with one schema-validated manifest and generated exports, retaining an explicit allowlist and fail-closed owner proofs. | Update a benign manifest fixture and prove all reports agree without manual per-script counter edits. |
| **CI-06** | High | GitHub repository ruleset listing is empty; the 4-ref agreement is a **policy, not enforced branch protection**. | **OPEN**: implement read-only governance validation and use repository Settings/Rulesets for server-side creation/deletion restrictions when available. Do not pretend a scheduled/CI script prevents branch creation. | Unapproved permanent refs detected in T0 and appropriate repository protection verified separately. |
| **CI-07** | Medium | MiniObjcopy's all-push `route` job allocates a runner for non-ELF maintenance pushes; current benefit is preserving multi-commit ELF diff correctness, failure-open and legacy opt-in tags. | **OPEN/ACCEPTED TEMPORARILY**. Do not change `on.push.paths` blindly and lose `[miniobjcopy-*]` tags. | Real paired push/route proof for future replacement, with no false negative on multi-commit or forced push. |

## Necessary independent capabilities — do not combine on filename similarity
- T0: workflow structure and source identity; T1: actual compiler/PP/AS/AR/LD/ELF regression tests.
- T2: 3352 Linux TU compile and assembler coverage; T3: current-input strict full Image production/link provenance; T4: GCC/MiniC differential object isolation and explicit QEMU/kernel-init verdict.
- Performance P: paired candidate runs with matching input, toolchain fingerprint, hardware and conditions; **not** an implicit full-Image/Linux boot certificate.
- The standalone Linux image **graph preflight** validates the manifest graph on narrow source paths; the integrated full Image graph → shards → strict receiver has a distinct, expensive trigger. Their path scopes do not currently create duplicate expensive builds from a normal CI docs push.
- Special diagnostic owners (early PI/P1, fault/frontier, FDT/Kallsyms, IRQ/RCU, MiniAS 3536) are distinct from one another unless exact job/test-body and artifact-provenance equivalence proves otherwise.

## Governance/retirement rule
No new permanent `agent/*` branch or `.github/workflows/*.yml` without specifying (1) unique owner/test contract, (2) independent oracle not already covered, (3) documented runtime/perf target, (4) trigger cost and concurrency, (5) source/fixture provenance, and (6) explicit exit/retirement criterion. Prefer modifying an existing canonical owner. Keep historical test source recoverable through original immutable commit/blob SHA and `.github/workflows-disabled/` only when needed for source reconstruction; do not copy temporary experiments into permanently active YAML.

## Closing condition
Both current development-branch M0 successes are **necessary, insufficient**. The cleanup phase closes only after P0 modes are fixed/retired, all active modes are reachable or explicitly marked inactive, and topology/branch governance is tested. A green source-only gate never implies that Linux Image booted.
