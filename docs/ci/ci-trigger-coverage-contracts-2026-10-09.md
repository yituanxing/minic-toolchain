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

**Known gap, not yet certified:** Performance has no guaranteed on-push MiniC/RV64 T1 gate for all compiler changes; opt-in First500 is not an automatic substitute. Enabling one needs an explicit cost/coverage decision and a real Performance run. Other broad `tools/**` globs still merit dependency review.

**Limitations:** These are T0 static dispatch checks and selector self-tests. The newly added ELF-only auto run requires an actual ELF-source-only push to prove the complete GitHub event path. A green route/M0 is not an ELF regression PASS, a full 3352-TU result, a Linux Image certificate, or a QEMU boot PASS. GitHub's server-side path/commit diff limits are not fully solved by a runner-side git diff. The branch-local TSV tracks ownership; all-branch source hashes should be refreshed following the routing change.
