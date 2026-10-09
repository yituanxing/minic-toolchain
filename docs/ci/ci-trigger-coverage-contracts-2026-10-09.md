# CI code → trigger → job coverage review (2026-10-09)

Current source inventory: **28 Runtime / 29 Performance / 32 unique names / 57 branch-path YAMLs**. The existing [57-entry file](active-workflow-inventory-2026-10-09.tsv) records every active entrypoint and its actual job IDs. The new `check_ci_trigger_coverage_v1.py` asserts that inventory and key positive/negative routing contracts on each checked-out branch.

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

**Performance automatic T1 gates:** MiniC RV64, MiniAR and MiniLD focused regressions now include Performance in their push branches; their original complete test bodies are unchanged. Relevant source/test/tool edits trigger them via existing source paths. This does not certify an applied Performance profile or optimized First500. The three expensive Runtime regressions now also exclude the MiniObjcopy ELF selector and the pure `tools/ci/check_ci_*` maintenance checks; positive source paths remain unchanged. Other `tools/**` globs still merit dependency review.

**Limitations:** These are T0 static dispatch checks and selector self-tests. Two controlled ELF-documentation-only pushes have now verified the actual GitHub push → job execution path; the proof uses a benign `elf/CI_ROUTING_SCOPE.md` change, not a production ELF code patch. A green route/M0 is not an ELF regression PASS, a full 3352-TU result, a Linux Image certificate, or a QEMU boot PASS. GitHub's server-side path/commit diff limits are not fully solved by a runner-side git diff. The 57-row ledger is now reconciled from the current owner branch's actual YAML source hashes and includes the most recent verified ELF-only run IDs.

## Machine-readable full job matrix

Each branch M0 now exports **every active job** (not only workflow names) to an artifact named `ci-trigger-matrix-runtime` or `ci-trigger-matrix-performance`. It records the owner, evidence tier, trigger types, declared branches, complete raw push-path list, raw job `if`, route classification and named test steps. The two branch-specific M0 artifacts together cover all 57 entrypoints. The matrix is descriptive: dynamic conditions and indirect calls are not assumed to PASS simply because they appear in a YAML. Artifacts have a 14-day retention and may be regenerated through M0.

## Real GitHub Actions proof: ELF-only changed paths

The controlled probe modified **only** `elf/CI_ROUTING_SCOPE.md` on each ref.
Actual runs on the two separate commits produced T1 SUCCESS:

| T1 owner | Runtime push `dfb6ebe8` | Performance push `b6fd6ae0` |
| --- | --- | --- |
| MiniC RV64 | [#37883301811](https://github.com/yituanxing/minic-toolchain/actions/runs/37883301811) SUCCESS | [#37883500396](https://github.com/yituanxing/minic-toolchain/actions/runs/37883500396) SUCCESS |
| MiniPP A0 | [#37883301718](https://github.com/yituanxing/minic-toolchain/actions/runs/37883301718) SUCCESS | [#37883500383](https://github.com/yituanxing/minic-toolchain/actions/runs/37883500383) SUCCESS |
| MiniAR | [#37883301856](https://github.com/yituanxing/minic-toolchain/actions/runs/37883301856) SUCCESS | [#37883500387](https://github.com/yituanxing/minic-toolchain/actions/runs/37883500387) SUCCESS |
| MiniLD | [#37883301706](https://github.com/yituanxing/minic-toolchain/actions/runs/37883301706) SUCCESS | [#37883500487](https://github.com/yituanxing/minic-toolchain/actions/runs/37883500487) SUCCESS |
| MiniObjcopy/Strip T1 | [#37883301722](https://github.com/yituanxing/minic-toolchain/actions/runs/37883301722) SUCCESS | [#37883500452](https://github.com/yituanxing/minic-toolchain/actions/runs/37883500452) SUCCESS |

Both MiniObjcopy runs included `route=success`, `regressions=success`,
`linux-tool=skipped` and `linux-image=skipped`: no accidental T3/T4
launch. The Performance branch was initially missing automatic MiniAR and
MiniLD eligibility; this was corrected **before** the second Perf ELF-only
push. The original full regression bodies remained source-equivalent as
verified by dual-branch M0.

The latest structural checks for the changes are Runtime M0
[#37883457937](https://github.com/yituanxing/minic-toolchain/actions/runs/37883457937)
and Performance M0
[#37883428996](https://github.com/yituanxing/minic-toolchain/actions/runs/37883428996),
both SUCCESS. Machine-readable per-job matrix artifacts are in Runtime M0
[#37883216458](https://github.com/yituanxing/minic-toolchain/actions/runs/37883216458)
(**88 job declarations across 30 YAMLs**) and Performance M0
[#37883242165](https://github.com/yituanxing/minic-toolchain/actions/runs/37883242165)
(**84 across 31**). These are branch-path declarations, not 172 distinct
executed tests. Source/certification T2–T4 still requires independent proof.
