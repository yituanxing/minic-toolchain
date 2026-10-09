# CI code → trigger → job coverage review (2026-10-09)

Current source inventory: **30 Runtime / 31 Performance / 34 unique names / 61 branch-path YAMLs**. The existing [61-entry file](active-workflow-inventory-2026-10-09.tsv) records every active entrypoint and its actual job IDs. The new `check_ci_trigger_coverage_v1.py` asserts that inventory and key positive/negative routing contracts on each checked-out branch.

## Verified source-level routing contracts

| Changed input | Intended T1 owner / selected job | Policy |
| --- | --- | --- |
| `elf/**` | Runtime MiniC RV64, MiniAR, MiniLD, both-branch MiniPP A0; MiniObjcopy/Strip `regressions` | Positive dependency. Objcopy now uses a cheap path router; its original regression body remains byte-identical. |
| `src/frontend/**` | Runtime MiniC RV64, MiniAR/MiniLD broad historical suites | Existing automatic push coverage, subject to GitHub diff limitations. |
| `tools/ci/runtime-timekeeping-trigger.txt` | Runtime Owner `timekeeping` | Must not run unrelated Runtime MiniC/MiniAR/MiniLD or MiniObjcopy ELF T1 tests. |
| `tools/ci/select_miniobjcopy_elf_route_v1.py` or its workflow YAML | MiniObjcopy/Strip T1 `regressions` | Conservative self-test/owner edit behavior. |
| Incomplete or inaccessible push history | MiniObjcopy/Strip T1 `regressions` | Fail open, to avoid false negatives. |
| Commit tag `[miniobjcopy-strip-regressions]` | MiniObjcopy/Strip T1 `regressions` | Preserved. |
| Commit tags `[miniobjcopy-linux-tool]`, `[miniobjcopy-linux-image]` | Corresponding T3 job | Preserved; automatic ELF route MUST NOT run costly Linux jobs. |
| Manual MiniObjcopy mode | Original selected job(s) | Preserved, independent of path router. |

**Performance minimum T1 gate:** the existing MiniC RV64 focused regression now includes the Performance branch in its push branches, preserving the original entire regression job body. It covers normal source/test/tool changes matched by its existing paths. This is not an optimized-profile First500 certificate, and real CI verification is required. The three expensive Runtime regressions now also exclude the MiniObjcopy ELF selector and the pure `tools/ci/check_ci_*` maintenance checks; positive source paths remain unchanged. Other `tools/**` globs still merit dependency review.

**Limitations:** These are T0 static dispatch checks and selector self-tests. The newly added ELF-only auto run requires an actual ELF-source-only push to prove the complete GitHub event path. A green route/M0 is not an ELF regression PASS, a full 3352-TU result, a Linux Image certificate, or a QEMU boot PASS. GitHub's server-side path/commit diff limits are not fully solved by a runner-side git diff. The branch-local TSV tracks ownership; all-branch source hashes should be refreshed following the routing change.

## Machine-readable full job matrix

Each branch M0 now exports **every active job** (not only workflow names) to an artifact named `ci-trigger-matrix-runtime` or `ci-trigger-matrix-performance`. It records the owner, evidence tier, trigger types, declared branches, complete raw push-path list, raw job `if`, route classification and named test steps. The two branch-specific M0 artifacts together cover all 61 entrypoints. The matrix is descriptive: dynamic conditions and indirect calls are not assumed to PASS simply because they appear in a YAML. Artifacts have a 14-day retention and may be regenerated through M0.
